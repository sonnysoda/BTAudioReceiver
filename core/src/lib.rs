//! btcore — A2DP Sink 业务内核
//!
//! 通过 C ABI 暴露给 C++/WinRT 外壳。所有 WinRT 调用都发生在这里，
//! 上层完全不感知 WinRT 的存在。
//!
//! 注意：WinRT 公寓模型是**线程级**的。任何调用本库的连接/扫描接口的线程，
//! 都必须先调用 `btc_init()`（幂等）。外壳侧每个工作线程入口都要调用一次。

use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::{Mutex, OnceLock};

use windows::core::{IInspectable, Ref, HSTRING};
use windows::Devices::Enumeration::DeviceInformation;
use windows::Foundation::TypedEventHandler;
use windows::Media::Audio::{AudioPlaybackConnection, AudioPlaybackConnectionState};
use windows::Win32::System::WinRT::{RoInitialize, RO_INIT_MULTITHREADED};

type EventCb =
    unsafe extern "C" fn(code: i32, state: i32, payload: *const c_char, user: *mut c_void);

struct Inner {
    conn: Option<AudioPlaybackConnection>,
    cb: Option<EventCb>,
    user: usize,
}

static INNER: OnceLock<Mutex<Inner>> = OnceLock::new();

fn inner() -> &'static Mutex<Inner> {
    INNER.get_or_init(|| {
        Mutex::new(Inner {
            conn: None,
            cb: None,
            user: 0,
        })
    })
}

/// 触发事件。先取出回调再释放锁，避免回调重入本库造成死锁。
fn emit(code: i32, state: i32, msg: &str) {
    let (cb, user) = {
        let g = inner().lock().unwrap();
        (g.cb, g.user)
    };
    if let Some(cb) = cb {
        if let Ok(c) = CString::new(msg) {
            unsafe { cb(code, state, c.as_ptr(), user as *mut c_void) };
        }
    }
}

fn close_internal() {
    let old = {
        let mut g = inner().lock().unwrap();
        g.conn.take()
    };
    if let Some(c) = old {
        let _ = c.Close();
    }
}

/// 读取当前连接状态，映射为 BtcState。
fn current_state() -> i32 {
    let g = inner().lock().unwrap();
    match &g.conn {
        None => -1,
        Some(c) => match c.State() {
            Ok(AudioPlaybackConnectionState::Opened) => 1,
            _ => 0,
        },
    }
}

// ---------------------------------------------------------------- C ABI

#[no_mangle]
pub extern "C" fn btc_init() -> i32 {
    // 线程可能已被初始化为 STA（如 XAML UI 线程），此时请求 MTA 会返回
    // RPC_E_CHANGED_MODE —— 这表示“已初始化”，不是失败。
    const RPC_E_CHANGED_MODE: i32 = 0x8001_0106u32 as i32;

    match unsafe { RoInitialize(RO_INIT_MULTITHREADED) } {
        Ok(()) => 0,
        Err(e) if e.code().0 == RPC_E_CHANGED_MODE => 0,
        Err(e) => {
            emit(3, -1, &format!("RoInitialize failed: {e}"));
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn btc_shutdown() {
    close_internal();
}

#[no_mangle]
pub extern "C" fn btc_scan_devices_json() -> *mut c_char {
    match scan() {
        Ok(json) => match CString::new(json) {
            Ok(c) => c.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(e) => {
            emit(3, -1, &format!("scan failed: {e}"));
            std::ptr::null_mut()
        }
    }
}

fn scan() -> windows::core::Result<String> {
    let selector = AudioPlaybackConnection::GetDeviceSelector()?;
    let devices = DeviceInformation::FindAllAsyncAqsFilter(&selector)?.join()?;
    let count = devices.Size()?;
    let mut arr = Vec::with_capacity(count as usize);
    for i in 0..count {
        let d = devices.GetAt(i)?;
        arr.push(serde_json::json!({
            "name": d.Name().map(|s| s.to_string()).unwrap_or_default(),
            "id": d.Id().map(|s| s.to_string()).unwrap_or_default(),
            "enabled": d.IsEnabled().unwrap_or(false),
        }));
    }
    Ok(serde_json::Value::Array(arr).to_string())
}

/// 把字段里的分隔符转义，保证一行一条记录可被简单解析。
fn escape_field(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '\t' => out.push_str("\\t"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            _ => out.push(c),
        }
    }
    out
}

/// 扫描设备并返回「每行一条：id<TAB>名称」的 UTF-8 文本。
/// 给不引入 JSON 库的调用方（C++ Win32 外壳）使用。
#[no_mangle]
pub extern "C" fn btc_scan_devices_text() -> *mut c_char {
    match scan_lines() {
        Ok(text) => match CString::new(text) {
            Ok(c) => c.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(e) => {
            emit(3, -1, &format!("scan failed: {e}"));
            std::ptr::null_mut()
        }
    }
}

fn scan_lines() -> windows::core::Result<String> {
    let selector = AudioPlaybackConnection::GetDeviceSelector()?;
    let devices = DeviceInformation::FindAllAsyncAqsFilter(&selector)?.join()?;
    let count = devices.Size()?;
    let mut out = String::new();
    for i in 0..count {
        let d = devices.GetAt(i)?;
        let id = d.Id().map(|s| s.to_string()).unwrap_or_default();
        let name = d.Name().map(|s| s.to_string()).unwrap_or_default();
        out.push_str(&escape_field(&id));
        out.push('\t');
        out.push_str(&escape_field(&name));
        out.push('\n');
    }
    Ok(out)
}

/// # Safety
/// `device_id_utf8` 必须是有效的 NUL 结尾 UTF-8 字符串。
#[no_mangle]
pub unsafe extern "C" fn btc_open_device(device_id_utf8: *const c_char) -> i32 {
    if device_id_utf8.is_null() {
        return -4;
    }
    let Ok(s) = CStr::from_ptr(device_id_utf8).to_str() else {
        return -4;
    };
    let id = HSTRING::from(s);

    close_internal();

    let conn = match AudioPlaybackConnection::TryCreateFromId(&id) {
        Ok(c) => c,
        Err(e) => {
            emit(
                3,
                -1,
                &format!("TryCreateFromId failed (device may not support A2DP Sink): {e}"),
            );
            return -1;
        }
    };

    // 回调参数直接忽略；收到事件后由上层调用 btc_get_state 查询真实状态。
    let handler: TypedEventHandler<AudioPlaybackConnection, IInspectable> =
        TypedEventHandler::new(|_s: Ref<AudioPlaybackConnection>, _a: Ref<IInspectable>| {
            let st = current_state();
            emit(2, st, "state_changed");
            Ok(())
        });
    let _token = conn.StateChanged(&handler);

    if let Err(e) = conn.StartAsync().and_then(|a| a.join()) {
        emit(3, -1, &format!("StartAsync failed: {e}"));
        return -2;
    }

    let result = match conn.OpenAsync().and_then(|a| a.join()) {
        Ok(r) => r,
        Err(e) => {
            emit(3, -1, &format!("OpenAsync failed: {e}"));
            return -3;
        }
    };

    match result.Status() {
        Ok(status) => {
            let ok =
                status == windows::Media::Audio::AudioPlaybackConnectionOpenResultStatus::Success;
            if !ok {
                let ext = result
                    .ExtendedError()
                    .map(|h| format!("{h:?}"))
                    .unwrap_or_else(|_| "n/a".into());
                emit(3, -1, &format!("Open status = {status:?}, extended = {ext}"));
                return -3;
            }
        }
        Err(e) => {
            emit(3, -1, &format!("Status() failed: {e}"));
            return -3;
        }
    }

    {
        let mut g = inner().lock().unwrap();
        g.conn = Some(conn);
    }

    emit(2, 1, "opened");
    0
}

#[no_mangle]
pub extern "C" fn btc_close_device() -> i32 {
    close_internal();
    emit(2, 0, "closed");
    0
}

#[no_mangle]
pub extern "C" fn btc_get_state() -> i32 {
    current_state()
}

#[no_mangle]
pub extern "C" fn btc_set_event_callback(cb: Option<EventCb>, user: *mut c_void) {
    let mut g = inner().lock().unwrap();
    g.cb = cb;
    g.user = user as usize;
}

/// # Safety
/// `s` 必须来自本库的返回值，且只能释放一次。
#[no_mangle]
pub unsafe extern "C" fn btc_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

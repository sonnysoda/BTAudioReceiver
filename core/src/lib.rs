//! btcore — A2DP Sink 业务内核
//!
//! 通过 C ABI 暴露给 C++/WinRT 外壳。所有 WinRT 调用都发生在这里，
//! 上层完全不感知 WinRT 的存在。
//!
//! 注意：WinRT 公寓模型是**线程级**的。任何调用本库的连接/扫描接口的线程，
//! 都必须先调用 `btc_init()`（幂等）。外壳侧每个工作线程入口都要调用一次。

use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use windows::core::{IInspectable, Ref, HSTRING};
use windows::Devices::Enumeration::DeviceInformation;
use windows::Foundation::TypedEventHandler;
use windows::Media::Audio::{
    AudioPlaybackConnection, AudioPlaybackConnectionOpenResultStatus, AudioPlaybackConnectionState,
};
use windows::Win32::System::WinRT::{RoInitialize, RO_INIT_MULTITHREADED};
use windows_future::AsyncStatus;

/// OpenAsync 的最长等待时间。超过就认为对端不会响应，取消并报超时。
/// 没有这个上限时，对端不在/不响应会让工作线程永远挂着，界面停在“正在连接”。
const OPEN_TIMEOUT: Duration = Duration::from_secs(20);

type EventCb =
    unsafe extern "C" fn(code: i32, state: i32, payload: *const c_char, user: *mut c_void);

/// windows-rs 没有为事件委托实现 Send，但它的本体只是一个 COM 指针。
/// 我们只在注册它的线程上把它交给 WinRT，之后仅持有以保活，从不跨线程调用，
/// 所以跨线程保管是安全的。
#[allow(dead_code)] // 字段只用于持有 COM 引用，不被读取
struct HandlerSlot(TypedEventHandler<AudioPlaybackConnection, IInspectable>);
unsafe impl Send for HandlerSlot {}

struct Inner {
    conn: Option<AudioPlaybackConnection>,
    /// 事件委托与注册 token 必须保活：否则对象一旦释放就再也收不到 StateChanged
    handler: Option<HandlerSlot>,
    token: Option<i64>,
    cb: Option<EventCb>,
    user: usize,
}

static INNER: OnceLock<Mutex<Inner>> = OnceLock::new();

fn inner() -> &'static Mutex<Inner> {
    INNER.get_or_init(|| {
        Mutex::new(Inner {
            conn: None,
            handler: None,
            token: None,
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
    // 先把三样东西从全局状态里摘出来，避免在持锁期间调用 COM
    let (old, token, handler) = {
        let mut g = inner().lock().unwrap();
        (g.conn.take(), g.token.take(), g.handler.take())
    };

    if let Some(c) = old {
        // 先退订再 Close：否则 Close 自身导致的状态变化会再触发一次回调
        if let Some(t) = token {
            let _ = c.RemoveStateChanged(t);
        }
        let _ = c.Close();
    }
    drop(handler);
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

/// 在已存在的连接对象上做一次 Open，带超时。
///
/// 只负责“发起 + 等结果 + 判 Success”，**不碰全局状态、不销毁对象**，
/// 失败回滚由调用方决定——`btc_open_device` 失败会重建，`btc_reopen` 失败必须保留对象，
/// 因为端点一旦消失，对端就没法再连回来。
///
/// 返回 0 成功 / -3 Open 失败 / -5 超时
fn open_with_timeout(conn: &AudioPlaybackConnection, timeout: Duration) -> i32 {
    let op = match conn.OpenAsync() {
        Ok(o) => o,
        Err(e) => {
            emit(3, -1, &format!("OpenAsync failed: {e}"));
            return -3;
        }
    };

    // 不用 join()：对端不在、不响应时它会一直挂住
    let started = Instant::now();
    loop {
        match op.Status() {
            Ok(AsyncStatus::Started) => {
                if started.elapsed() >= timeout {
                    let _ = op.Cancel();
                    emit(
                        3,
                        -1,
                        &format!("OpenAsync timed out after {}ms", timeout.as_millis()),
                    );
                    return -5;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            _ => break,
        }
    }

    let result = match op.GetResults() {
        Ok(r) => r,
        Err(e) => {
            emit(3, -1, &format!("OpenAsync failed: {e}"));
            return -3;
        }
    };

    match result.Status() {
        Ok(status) => {
            if status != AudioPlaybackConnectionOpenResultStatus::Success {
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

    0
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

    // 状态回调来自 WinRT 线程池。直接用 sender 查询状态而不是走全局 INNER，
    // 可以避免回调与 open/close 抢锁，也不会出现“连接对象还没登记就收到事件”。
    let handler: TypedEventHandler<AudioPlaybackConnection, IInspectable> =
        TypedEventHandler::new(|sender: Ref<AudioPlaybackConnection>, _args: Ref<IInspectable>| {
            let st = match sender.as_ref() {
                Some(c) => match c.State() {
                    Ok(AudioPlaybackConnectionState::Opened) => 1,
                    Ok(_) => 0,
                    Err(_) => -1,
                },
                None => current_state(),
            };
            emit(2, st, if st == 1 { "state:opened" } else { "state:closed" });
            Ok(())
        });

    let token = conn.StateChanged(&handler).ok();

    // 先登记连接对象，这样连接过程中的查询与事件都能看到它；
    // 失败路径统一走 close_internal() 回滚。
    {
        let mut g = inner().lock().unwrap();
        g.conn = Some(conn.clone());
        g.handler = Some(HandlerSlot(handler));
        g.token = token;
    }

    if let Err(e) = conn.StartAsync().and_then(|a| a.join()) {
        emit(3, -1, &format!("StartAsync failed: {e}"));
        close_internal();
        return -2;
    }

    let rc = open_with_timeout(&conn, OPEN_TIMEOUT);
    if rc != 0 {
        close_internal();
        return rc;
    }

    emit(2, 1, "opened");
    0
}

/// 用**现有连接对象**再试一次 Open，不重建、不重新 Start。
///
/// 断线重连专用：对象留着，Windows 的 A2DP 端点就一直在，
/// 对端自己回连和本函数主动重连两条路都保留。
///
/// 返回 0 成功 / -1 当前没有可用连接对象 / -3 Open 失败 / -5 超时
#[no_mangle]
pub extern "C" fn btc_reopen(timeout_ms: i32) -> i32 {
    let conn = {
        let g = inner().lock().unwrap();
        g.conn.clone()
    };

    let Some(conn) = conn else {
        emit(3, -1, "btc_reopen: no connection object");
        return -1;
    };

    // 对端可能已经自己连回来了，先看状态，省一次 Open
    if let Ok(AudioPlaybackConnectionState::Opened) = conn.State() {
        emit(2, 1, "already opened");
        return 0;
    }

    let timeout = Duration::from_millis(timeout_ms.clamp(500, 60_000) as u64);
    let rc = open_with_timeout(&conn, timeout);
    if rc == 0 {
        emit(2, 1, "reopened");
    }
    rc
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

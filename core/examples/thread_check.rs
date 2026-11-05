//! 验证线程级公寓初始化的实际必要性。
//!
//! 结论（2026-11-05 实测）：
//!   未调用 btc_init 的全新线程同样能成功扫描。
//!   说明 Windows 在此场景下会自动完成 WinRT 初始化，
//!   **“每个线程必须显式 btc_init”是错误假设**。
//!
//! 保留此探针用于回归：若将来某个 WinRT 调用开始要求显式公寓，
//! 这里的 [A] 会先失败。
//!
//! 只做设备扫描，不连接任何设备，不会影响正在播放的音频。
//! 运行：cargo run --release --example thread_check

use std::ffi::CStr;
use std::thread;

fn count_devices(json: *mut std::ffi::c_char) -> String {
    if json.is_null() {
        return "NULL（失败）".into();
    }
    let s = unsafe { CStr::from_ptr(json) }
        .to_str()
        .unwrap_or("<invalid utf8>")
        .to_string();
    unsafe { btcore::btc_free_string(json) };
    format!("ok, {} devices", s.matches("\"name\"").count())
}

fn main() {
    // A：全新线程，不初始化公寓
    let a = thread::spawn(|| count_devices(btcore::btc_scan_devices_json()))
        .join()
        .unwrap();
    println!("[A] 未调用 btc_init 的线程 -> {a}");

    // B：全新线程，先调用 btc_init
    let b = thread::spawn(|| {
        let rc = btcore::btc_init();
        if rc != 0 {
            return format!("btc_init 失败 rc={rc}");
        }
        count_devices(btcore::btc_scan_devices_json())
    })
    .join()
    .unwrap();
    println!("[B] 先调用 btc_init 的线程 -> {b}");
}

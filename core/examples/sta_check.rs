//! 复现并验证 STA 场景下的初始化行为。
//!
//! XAML 的 wWinMain 会把 UI 线程初始化为 STA，而 btc_init 请求 MTA。
//! 若把 RPC_E_CHANGED_MODE 当失败，就会出现“引擎初始化失败”。
//!
//! 运行：cargo run --release --example sta_check

use windows::Win32::System::WinRT::{RoInitialize, RO_INIT_SINGLETHREADED};

fn main() {
    // 先占用线程为 STA，模拟 XAML UI 线程
    match unsafe { RoInitialize(RO_INIT_SINGLETHREADED) } {
        Ok(()) => println!("[1] RoInitialize(STA)      -> ok"),
        Err(e) => {
            println!("[1] RoInitialize(STA)      -> {e}");
            return;
        }
    }

    // 此时 btc_init 内部请求 MTA，必然返回 RPC_E_CHANGED_MODE，
    // 修复后应被容忍并返回 0。
    let rc = btcore::btc_init();
    println!("[2] btc_init() on STA      -> {rc}  (期望 0)");

    if rc != 0 {
        println!("失败：STA 线程下初始化仍被判定为错误");
        std::process::exit(1);
    }

    // 顺带验证 STA 线程上扫描也能工作
    let json = btcore::btc_scan_devices_json();
    if json.is_null() {
        println!("[3] btc_scan_devices_json  -> NULL（扫描失败）");
        std::process::exit(1);
    }

    let text = unsafe { std::ffi::CStr::from_ptr(json) }
        .to_str()
        .unwrap_or("<invalid utf8>")
        .to_string();
    unsafe { btcore::btc_free_string(json) };

    let count = text.matches("\"name\"").count();
    println!("[3] btc_scan_devices_json  -> ok, {count} devices");

    println!("\n结论：STA 线程下初始化与扫描均正常。");
}

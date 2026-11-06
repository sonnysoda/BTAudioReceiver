/* btc_core.h — Rust 内核的 C ABI 契约
 *
 * 设计原则：
 *   1. 只用 C 基础类型（int32_t / const char* / void*），不跨 FFI 传 STL 或 Rust 结构体
 *   2. 结构化数据用 UTF-8 JSON 传递 —— 增加字段不会破坏 ABI
 *   3. 所有返回的字符串必须用 btc_free_string 释放
 *   4. 事件回调可能来自任意线程，调用方负责封送到自己的 UI 线程
 *   5. 调用线程建议先 btc_init（幂等）。实测本机 WinRT 会自动初始化，
 *      但不要依赖这一点 —— 尤其是换到其他进程或调用栈时
 */
#ifndef BTC_CORE_H
#define BTC_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 事件类型 */
enum BtcEvent {
    BTC_EVENT_STATE_CHANGED = 2,   /* 连接状态变化，state 参数有效 */
    BTC_EVENT_ERROR         = 3,   /* 错误，payload 为可读消息 */
};

/* 连接状态 */
enum BtcState {
    BTC_STATE_NONE   = -1,  /* 尚未建立连接对象 */
    BTC_STATE_CLOSED = 0,   /* 已创建但未连接 / 已断开 */
    BTC_STATE_OPENED = 1,   /* 已连接，正在接收音频 */
};

/* 事件回调。payload 为 UTF-8 字符串，仅在回调期间有效。 */
typedef void (*btc_event_cb)(int32_t event_code, int32_t state, const char* payload, void* user);

/* 初始化当前线程的 WinRT 公寓。
 * 幂等：同一线程重复调用无副作用；线程已是其他公寓模型时视为成功。
 * 成功返回 0。 */
int32_t btc_init(void);

/* 释放连接资源。 */
void btc_shutdown(void);

/* 扫描可用于音频回放的蓝牙设备，返回 JSON 数组字符串：
 *   [{"name":"...","id":"...","enabled":true}, ...]
 * 调用方负责 btc_free_string。失败返回 NULL。 */
char* btc_scan_devices_json(void);

/* 扫描设备，返回「每行一条：id<TAB>名称」的 UTF-8 文本。
 * 字段内的 \\ \t \n \r 以反斜杠转义。
 * 调用方负责 btc_free_string。失败返回 NULL。 */
char* btc_scan_devices_text(void);

/* 主动连接指定设备（Start + Open）。阻塞直到有结果。
 * 成功返回 0；失败：-1 创建连接失败 / -2 Start 失败 / -3 Open 失败 / -4 参数非法 */
int32_t btc_open_device(const char* device_id_utf8);

/* 关闭当前连接。无连接时也返回 0。 */
int32_t btc_close_device(void);

/* 查询当前连接状态，见 BtcState。 */
int32_t btc_get_state(void);

/* 注册事件回调。cb 传 NULL 表示取消。 */
void btc_set_event_callback(btc_event_cb cb, void* user);

/* 释放由本库返回的字符串。 */
void btc_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif /* BTC_CORE_H */

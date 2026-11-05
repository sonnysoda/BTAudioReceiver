#pragma once

// 极简本地化：编译期表驱动，只有简体中文与英文。
// 用 X-macro 保证枚举与表严格同序，避免手工维护错位。

#include <winrt/Windows.Foundation.h>

namespace loc
{
    enum class Lang
    {
        Zh,
        En
    };

#define LOC_TABLE(X)                                                             \
    X(Title,            L"蓝牙音频接收器",              L"Bluetooth Audio Receiver") \
    X(NotConnected,     L"未连接",                      L"Not connected")            \
    X(StateIdle,        L"使用中的音频设备 · 空闲",      L"Active device · Idle")     \
    X(StateScanning,    L"使用中的音频设备 · 正在扫描…", L"Active device · Scanning…") \
    X(StateConnecting,  L"使用中的音频设备 · 正在连接…", L"Active device · Connecting…") \
    X(StateConnected,   L"使用中的音频设备 · 已连接",    L"Active device · Connected") \
    X(StateDisconnected,L"使用中的音频设备 · 已断开",    L"Active device · Disconnected") \
    X(StateFailed,      L"使用中的音频设备 · 连接失败",  L"Active device · Failed")    \
    X(StateNoDevice,    L"使用中的音频设备 · 未发现设备",L"Active device · No devices") \
    X(RowPairedIdle,    L"已配对 · 未连接",              L"Paired · Not connected")   \
    X(RowConnecting,    L"正在连接…",                    L"Connecting…")              \
    X(RowConnected,     L"已连接 · 正在接收音频",        L"Connected · Receiving audio") \
    X(RowDisconnected,  L"已断开",                       L"Disconnected")             \
    X(RowFailed,        L"连接失败（错误码 %d）",        L"Failed (code %d)")         \
    X(BtnRefresh,       L"刷新",                         L"Refresh")                  \
    X(BtnConnect,       L"连接",                         L"Connect")                  \
    X(BtnDisconnect,    L"断开",                         L"Disconnect")               \
    X(BtnSettings,      L"设置",                         L"Settings")                 \
    X(EmptyHint,        L"未发现可用设备\n请先在系统设置中配对手机", L"No devices found\nPair your phone in Windows Settings first") \
    X(DlgSettings,      L"设置",                         L"Settings")                 \
    X(LabelLanguage,    L"语言",                         L"Language")                 \
    X(LabelTheme,       L"外观",                         L"Appearance")               \
    X(ThemeLight,       L"浅色",                         L"Light")                    \
    X(ThemeDark,        L"深色",                         L"Dark")                     \
    X(ThemeSystem,      L"跟随系统",                     L"Use system setting")       \
    X(BtnClose,         L"关闭",                         L"Close")                    \
    X(BtnReset,         L"重置所有设置",                 L"Reset all settings")       \
    X(ResetDone,        L"已恢复默认设置",               L"Settings reset to defaults") \
    X(ErrSelectDevice,  L"请先在列表中选择一个设备",      L"Select a device first")    \
    X(ErrScanFailed,    L"扫描失败",                     L"Scan failed")              \
    X(ErrInitFailed,    L"音频引擎初始化失败",           L"Audio engine init failed") \
    X(ErrScanException, L"扫描异常: ",                   L"Scan exception: ")         \
    X(TrayShow,         L"显示主窗口",                   L"Show window")              \
    X(TrayDisconnect,   L"断开连接",                     L"Disconnect")               \
    X(TrayConnect,      L"连接设备",                     L"Connect device")           \
    X(TrayExit,         L"退出",                         L"Exit")                     \
    X(TrayNoRecent,     L"暂无最近设备",                 L"No recent devices")        \
    X(DevicesIdleFmt,   L"%d 个设备 · 空闲",             L"%d devices · Idle")

    enum class S
    {
#define X(name, zh, en) name,
        LOC_TABLE(X)
#undef X
            Count
    };

    struct Entry
    {
        const wchar_t* zh;
        const wchar_t* en;
    };

    inline Lang& Current()
    {
        static Lang lang = Lang::Zh;
        return lang;
    }

    inline winrt::hstring Get(S key)
    {
        static const Entry table[] = {
#define X(name, zh, en) { zh, en },
            LOC_TABLE(X)
#undef X
        };
        const Entry& e = table[static_cast<int>(key)];
        return winrt::hstring(Current() == Lang::Zh ? e.zh : e.en);
    }
}

#undef LOC_TABLE

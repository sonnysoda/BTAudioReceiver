#pragma once

// 双语文案。

namespace texts
{
    enum class Lang
    {
        Zh,
        En
    };

    inline Lang& Current()
    {
        static Lang lang = Lang::Zh;
        return lang;
    }

    inline void SetByCode(std::wstring const& code)
    {
        Current() = (code == L"en") ? Lang::En : Lang::Zh;
    }

    inline const wchar_t* Pick(const wchar_t* zh, const wchar_t* en)
    {
        return Current() == Lang::Zh ? zh : en;
    }

    // 顶部
    inline const wchar_t* Title() { return Pick(L"蓝牙音频接收器", L"Bluetooth Audio Receiver"); }
    inline const wchar_t* NotConnected() { return Pick(L"未连接", L"Not connected"); }
    inline const wchar_t* StateIdle() { return Pick(L"使用中的音频设备 · 空闲", L"Active device · Idle"); }
    inline const wchar_t* StateScanning() { return Pick(L"使用中的音频设备 · 正在扫描…", L"Active device · Scanning…"); }
    inline const wchar_t* StateConnecting() { return Pick(L"使用中的音频设备 · 正在连接…", L"Active device · Connecting…"); }
    inline const wchar_t* StateConnected() { return Pick(L"使用中的音频设备 · 已连接", L"Active device · Connected"); }
    inline const wchar_t* StateDisconnected() { return Pick(L"使用中的音频设备 · 已断开", L"Active device · Disconnected"); }
    inline const wchar_t* DevicesFmt() { return Pick(L"%d 个设备 · 空闲", L"%d device(s) · Idle"); }
    inline const wchar_t* NoDevices() { return Pick(L"未发现可用设备", L"No devices found"); }

    // 行状态
    inline const wchar_t* RowIdle() { return Pick(L"已配对 · 未连接", L"Paired · Not connected"); }
    inline const wchar_t* RowConnecting() { return Pick(L"正在连接…", L"Connecting…"); }
    inline const wchar_t* RowConnected() { return Pick(L"已连接 · 正在接收音频", L"Connected · Receiving audio"); }
    inline const wchar_t* RowDisconnected() { return Pick(L"已断开", L"Disconnected"); }
    inline const wchar_t* RowFailedFmt() { return Pick(L"连接失败（错误码 %d）", L"Failed (code %d)"); }

    // 按钮
    inline const wchar_t* BtnSettings() { return Pick(L"设置", L"Settings"); }
    inline const wchar_t* BtnRefresh() { return Pick(L"刷新", L"Refresh"); }
    inline const wchar_t* BtnConnect() { return Pick(L"连接", L"Connect"); }
    inline const wchar_t* BtnDisconnect() { return Pick(L"断开", L"Disconnect"); }

    // 设置面板
    inline const wchar_t* PanelTitle() { return Pick(L"设置", L"Settings"); }
    inline const wchar_t* LabelLanguage() { return Pick(L"语言", L"Language"); }
    inline const wchar_t* LabelTheme() { return Pick(L"外观", L"Appearance"); }
    inline const wchar_t* LangZh() { return L"简体中文"; }
    inline const wchar_t* LangEn() { return L"English"; }
    inline const wchar_t* ThemeLight() { return Pick(L"浅色", L"Light"); }
    inline const wchar_t* ThemeDark() { return Pick(L"深色", L"Dark"); }
    inline const wchar_t* ThemeSystem() { return Pick(L"跟随系统", L"System"); }
    inline const wchar_t* BtnReset() { return Pick(L"重置所有设置", L"Reset all settings"); }

    // 托盘
    inline const wchar_t* TrayShow() { return Pick(L"显示主窗口", L"Show window"); }
    inline const wchar_t* TrayDisconnect() { return Pick(L"断开连接", L"Disconnect"); }
    inline const wchar_t* TrayConnect() { return Pick(L"连接设备", L"Connect device"); }
    inline const wchar_t* TrayExit() { return Pick(L"退出", L"Exit"); }
    inline const wchar_t* TrayNoRecent() { return Pick(L"暂无最近设备", L"No recent devices"); }
}

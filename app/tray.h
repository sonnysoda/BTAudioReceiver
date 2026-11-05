#pragma once

// 任务栏托盘：WinUI 3 没有原生托盘，这里用 Shell_NotifyIconW
// 加一个独立消息线程 + 隐藏窗口实现。

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace tray
{
    enum Command : int
    {
        CmdShow = 1,
        CmdDisconnect = 2,
        CmdExit = 3,
        CmdRecentBase = 100
    };

    struct MenuData
    {
        std::wstring tooltip;
        std::wstring itemShow;
        std::wstring itemDisconnect;
        std::wstring itemConnect;
        std::wstring itemExit;
        std::wstring itemNoRecent;
        std::vector<std::wstring> recent;
        bool canDisconnect{ false };
    };

    // onCommand 在托盘线程上被调用，调用方需自行封送到 UI 线程。
    bool Start(HINSTANCE instance, std::function<void(int)> onCommand);
    void Stop();
    void Update(MenuData const& data);

    // 托盘窗口是否已就绪。关闭窗口前必须检查：
    // 图标没建成功还隐藏窗口，用户就再也调不回来了。
    bool IsRunning();

    // 供第二个实例调用：找到已有实例的托盘窗口并请求它显示主窗口。
    void RequestShowFromOtherInstance();
}

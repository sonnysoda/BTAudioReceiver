#include "pch.h"
#include "tray.h"
#include "log.h"

#include <algorithm>
#include <cwchar>
#include <mutex>

namespace
{
    constexpr UINT kTrayMessage = WM_APP + 1;
    constexpr UINT kShowRequest = WM_APP + 3;
    constexpr wchar_t kClassName[] = L"BTAudioReceiverTrayWindow";

    HINSTANCE g_instance{ nullptr };
    HWND g_hwnd{ nullptr };
    HANDLE g_thread{ nullptr };
    HICON g_icon{ nullptr };
    UINT g_taskbarCreated{ 0 };
    std::function<void(int)> g_callback;
    tray::MenuData g_data;
    std::mutex g_lock;

    tray::MenuData Snapshot()
    {
        std::lock_guard<std::mutex> lock(g_lock);
        return g_data;
    }

    // 用 GDI 现场绘制图标：圆角底 + 白色喇叭。
    // 不依赖 .ico 资源，也避免用通用应用图标。
    HICON CreateAppIcon()
    {
        constexpr int kSize = 32;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kSize;
        bi.bmiHeader.biHeight = -kSize;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screen = ::GetDC(nullptr);
        HDC dc = ::CreateCompatibleDC(screen);
        HBITMAP color = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!color || !bits)
        {
            ::DeleteDC(dc);
            ::ReleaseDC(nullptr, screen);
            return nullptr;
        }

        HGDIOBJ oldBitmap = ::SelectObject(dc, color);
        ::SetBkMode(dc, TRANSPARENT);

        const COLORREF accent = RGB(0x2B, 0x6C, 0xB0);
        HBRUSH brush = ::CreateSolidBrush(accent);
        HPEN nullPen = ::CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldBrush = ::SelectObject(dc, brush);
        HGDIOBJ oldPen = ::SelectObject(dc, nullPen);
        ::RoundRect(dc, 1, 1, kSize - 1, kSize - 1, 10, 10);

        HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
        ::SelectObject(dc, white);
        ::Rectangle(dc, 8, 13, 13, 20);
        POINT cone[4] = { { 13, 13 }, { 13, 20 }, { 19, 25 }, { 19, 8 } };
        ::Polygon(dc, cone, 4);

        ::SelectObject(dc, oldBrush);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(brush);
        ::DeleteObject(white);
        ::DeleteObject(nullPen);
        ::GdiFlush();

        // DIB 的 alpha 全为 0，直接当图标会完全透明。
        // 用亮度当 alpha：底色(蓝)与白喇叭都会保留，背景透明。
        auto* px = static_cast<uint32_t*>(bits);
        for (int i = 0; i < kSize * kSize; ++i)
        {
            const uint32_t p = px[i];
            const uint32_t b = p & 0xFF;
            const uint32_t g = (p >> 8) & 0xFF;
            const uint32_t r = (p >> 16) & 0xFF;
            const uint32_t a = (r > g ? (r > b ? r : b) : (g > b ? g : b));
            px[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        ::SelectObject(dc, oldBitmap);
        ::DeleteDC(dc);
        ::ReleaseDC(nullptr, screen);

        HBITMAP mask = ::CreateBitmap(kSize, kSize, 1, 1, nullptr);
        ICONINFO info{};
        info.fIcon = TRUE;
        info.hbmColor = color;
        info.hbmMask = mask;
        HICON icon = ::CreateIconIndirect(&info);

        ::DeleteObject(color);
        ::DeleteObject(mask);
        return icon;
    }

    void ShowContextMenu(HWND hwnd)
    {
        const tray::MenuData data = Snapshot();

        HMENU menu = ::CreatePopupMenu();
        ::AppendMenuW(menu, MF_STRING, tray::CmdShow, data.itemShow.c_str());
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        ::AppendMenuW(menu,
                      MF_STRING | (data.canDisconnect ? 0u : MF_GRAYED),
                      tray::CmdDisconnect, data.itemDisconnect.c_str());

        HMENU recentMenu = ::CreatePopupMenu();
        if (data.recent.empty())
        {
            ::AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, data.itemNoRecent.c_str());
        }
        else
        {
            for (size_t i = 0; i < data.recent.size(); ++i)
            {
                ::AppendMenuW(recentMenu, MF_STRING,
                              tray::CmdRecentBase + static_cast<int>(i),
                              data.recent[i].c_str());
            }
        }
        ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu),
                      data.itemConnect.c_str());

        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, tray::CmdExit, data.itemExit.c_str());

        POINT pt{};
        ::GetCursorPos(&pt);
        ::SetForegroundWindow(hwnd);

        const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, hwnd, nullptr);
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
        ::DestroyMenu(menu);

        if (cmd > 0 && g_callback)
        {
            g_callback(cmd);
        }
    }

    void AddOrUpdateIcon(HWND hwnd, bool add)
    {
        const tray::MenuData data = Snapshot();

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon = g_icon ? g_icon : ::LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid.szTip, data.tooltip.c_str(), _TRUNCATE);

        const BOOL added = ::Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &nid);
        if (add && !added)
        {
            btlog::Write(L"Shell_NotifyIconW(NIM_ADD) failed");
        }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // 资源管理器重启后需要重新注册图标
        if (msg == g_taskbarCreated && g_taskbarCreated != 0)
        {
            AddOrUpdateIcon(hwnd, true);
            return 0;
        }

        switch (msg)
        {
        case kShowRequest:
            // 第二个实例请求唤起主窗口
            if (g_callback)
            {
                g_callback(tray::CmdShow);
            }
            return 0;

        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            {
                ShowContextMenu(hwnd);
            }
            else if (lParam == WM_LBUTTONDBLCLK)
            {
                if (g_callback)
                {
                    g_callback(tray::CmdShow);
                }
            }
            return 0;

        case WM_ENDSESSION:
        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
        {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            ::Shell_NotifyIconW(NIM_DELETE, &nid);
            ::PostQuitMessage(0);
            return 0;
        }

        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    DWORD WINAPI ThreadProc(void*)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_instance;
        wc.lpszClassName = kClassName;
        ::RegisterClassExW(&wc);

        g_hwnd = ::CreateWindowExW(0, kClassName, L"BTAudioReceiverTray", WS_POPUP,
                                   0, 0, 0, 0, nullptr, nullptr, g_instance, nullptr);
        if (!g_hwnd)
        {
            btlog::Write(L"tray: CreateWindowExW failed");
            return 1;
        }

        g_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
        AddOrUpdateIcon(g_hwnd, true);

        MSG msg{};
        while (::GetMessageW(&msg, nullptr, 0, 0))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return 0;
    }
}

namespace tray
{
    bool Start(HINSTANCE instance, std::function<void(int)> onCommand)
    {
        if (g_thread)
        {
            return true;
        }
        g_instance = instance;
        g_callback = std::move(onCommand);
        g_icon = CreateAppIcon();

        g_thread = ::CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void Stop()
    {
        if (!g_thread)
        {
            return;
        }
        if (g_hwnd)
        {
            ::PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        }
        ::WaitForSingleObject(g_thread, 3000);
        ::CloseHandle(g_thread);
        g_thread = nullptr;
        g_hwnd = nullptr;
        g_callback = nullptr;

        if (g_icon)
        {
            ::DestroyIcon(g_icon);
            g_icon = nullptr;
        }
    }

    void Update(MenuData const& data)
    {
        std::wstring tooltip;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_data = data;
            tooltip = data.tooltip;
        }
        if (g_hwnd)
        {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = g_hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_TIP;
            wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);
            ::Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
    }

    bool IsRunning()
    {
        return g_hwnd != nullptr;
    }

    void RequestShowFromOtherInstance()
    {
        HWND hwnd = ::FindWindowW(kClassName, nullptr);
        if (hwnd)
        {
            ::PostMessageW(hwnd, kShowRequest, 0, 0);
        }
    }
}

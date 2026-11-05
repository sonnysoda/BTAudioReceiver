#include "pch.h"
#include "MainWindow.xaml.h"

#include "btc_core.h"
#include "log.h"
#include "settings.h"
#include "strings.h"
#include "tray.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Animation;
using namespace Windows::Data::Json;

namespace
{
    // ---------------- 全局状态 ----------------

    Microsoft::UI::Dispatching::DispatcherQueue g_dispatcher{ nullptr };
    winrt::weak_ref<InfoBar> g_error;
    winrt::weak_ref<ListView> g_list;
    winrt::weak_ref<TextBlock> g_empty;
    winrt::weak_ref<TextBlock> g_activeName;
    winrt::weak_ref<TextBlock> g_stateText;
    winrt::weak_ref<Button> g_connectBtn;

    // 供自由函数回调窗口成员（窗口成员才能访问 Title/RootGrid/AppWindow）
    std::function<void()> g_fnApplyTexts;
    std::function<void()> g_fnShowWindow;
    std::function<void()> g_fnOpenSettings;

    // 设置由 App 在启动时加载（日志开关依赖它），这里只持有引用
    AppSettings& g_settings = settings::Current();
    bool g_quitting = false;

    enum class RowState
    {
        Idle,
        Connecting,
        Connected,
        Disconnected,
        Failed
    };

    // 行模型必须持有**强引用**：曾用 weak_ref 存 Border，
    // MakeRow 返回后局部变量析构、引用计数归零，get() 得到 nullptr，
    // Append(nullptr) 抛异常直接崩溃（0xC000027B）。
    struct RowUi
    {
        hstring id;
        hstring name;
        Border root{ nullptr };
        TextBlock status{ nullptr };
        Button disconnect{ nullptr };
        RowState state{ RowState::Idle };
        int failCode{ 0 };
    };

    std::vector<RowUi> g_rows;
    int g_activeIndex = -1;
    hstring g_activeId;
    bool g_busy = false;

    enum class HeaderState
    {
        Idle,
        Scanning,
        NoDevices,
        Connecting,
        Connected,
        Disconnected,
        Failed
    };

    HeaderState g_headerState = HeaderState::Idle;
    hstring g_headerName;

    // ---------------- 工具 ----------------

    bool g_backdropApplied = false;

    // Mica 不可用时的兜底背景：取系统主题背景色，随浅色/深色自动变化。
    Brush SystemBackgroundBrush()
    {
        try
        {
            Windows::UI::ViewManagement::UISettings settings;
            auto c = settings.GetColorValue(Windows::UI::ViewManagement::UIColorType::Background);
            c.A = 0xFF;
            return SolidColorBrush(c);
        }
        catch (...)
        {
            return SolidColorBrush(Windows::UI::Color{ 0xFF, 0x20, 0x20, 0x20 });
        }
    }

    std::vector<Storyboard> g_marquees;

    void StopMarquees()
    {
        for (auto& storyboard : g_marquees)
        {
            try
            {
                storyboard.Stop();
            }
            catch (...)
            {
            }
        }
        g_marquees.clear();
    }

    // 超长设备名做折返滚动（跑马灯）：滚到末尾停一下，再退回起点。
    // 文本没有超出容器时保持静止，不做任何动画。
    void SetupMarquee(Grid const& host, TextBlock const& text)
    {
        auto transform = TranslateTransform();
        text.RenderTransform(transform);
        text.TextWrapping(TextWrapping::NoWrap);
        text.TextTrimming(TextTrimming::None);

        auto lastOverflow = std::make_shared<double>(-1.0);

        host.SizeChanged([host, text, transform, lastOverflow](
                             Windows::Foundation::IInspectable const&, SizeChangedEventArgs const&)
        {
            try
            {
                const double hostWidth = host.ActualWidth();
                const double hostHeight = host.ActualHeight();
                if (hostWidth <= 0.0 || hostHeight <= 0.0)
                {
                    return;
                }

                auto clip = RectangleGeometry();
                clip.Rect(Windows::Foundation::Rect{
                    0.0f, 0.0f, static_cast<float>(hostWidth), static_cast<float>(hostHeight) });
                host.Clip(clip);

                // 用无限宽测量文本的真实宽度
                text.Measure(Windows::Foundation::Size{
                    100000.0f, static_cast<float>(hostHeight) });
                const double textWidth = text.DesiredSize().Width;
                const double overflow = textWidth - hostWidth;

                if (overflow <= 2.0)
                {
                    transform.X(0.0);
                    return;
                }
                if (std::abs(*lastOverflow - overflow) < 1.0)
                {
                    return;  // 已按同一溢出量启动过
                }
                *lastOverflow = overflow;

                const double moveSeconds = (std::max)(1.5, overflow / 28.0);
                const double holdSeconds = 1.2;

                auto animation = DoubleAnimationUsingKeyFrames();
                auto addKey = [&animation](double atSeconds, double value)
                {
                    auto key = LinearDoubleKeyFrame();
                    key.KeyTime(KeyTimeHelper::FromTimeSpan(Windows::Foundation::TimeSpan{
                        std::chrono::milliseconds(static_cast<int64_t>(atSeconds * 1000.0)) }));
                    key.Value(value);
                    animation.KeyFrames().Append(key);
                };

                addKey(0.0, 0.0);
                addKey(holdSeconds, 0.0);
                addKey(holdSeconds + moveSeconds, -overflow);
                addKey(holdSeconds * 2 + moveSeconds, -overflow);
                addKey(holdSeconds * 2 + moveSeconds * 2, 0.0);

                animation.RepeatBehavior(RepeatBehaviorHelper::Forever());

                auto storyboard = Storyboard();
                Storyboard::SetTarget(animation, transform);
                Storyboard::SetTargetProperty(animation, L"X");
                storyboard.Children().Append(animation);
                storyboard.Begin();
                g_marquees.push_back(storyboard);
            }
            catch (...)
            {
            }
        });
    }

    hstring FmtInt(loc::S key, int value)
    {
        wchar_t buf[256]{};
        const auto fmt = loc::Get(key);
        swprintf_s(buf, fmt.c_str(), value);
        return hstring(buf);
    }

    SolidColorBrush AccentBrush(byte alpha)
    {
        try
        {
            Windows::UI::ViewManagement::UISettings settings;
            auto c = settings.GetColorValue(Windows::UI::ViewManagement::UIColorType::Accent);
            return SolidColorBrush(Windows::UI::Color{ alpha, c.R, c.G, c.B });
        }
        catch (...)
        {
            return SolidColorBrush(Windows::UI::Color{ alpha, 0x00, 0x78, 0xD4 });
        }
    }

    hstring RowStatusText(RowUi const& row)
    {
        switch (row.state)
        {
        case RowState::Connecting:
            return loc::Get(loc::S::RowConnecting);
        case RowState::Connected:
            return loc::Get(loc::S::RowConnected);
        case RowState::Disconnected:
            return loc::Get(loc::S::RowDisconnected);
        case RowState::Failed:
            return FmtInt(loc::S::RowFailed, row.failCode);
        default:
            return loc::Get(loc::S::RowPairedIdle);
        }
    }

    void RefreshHeader()
    {
        hstring title = (g_headerName.size() == 0) ? loc::Get(loc::S::NotConnected) : g_headerName;
        hstring state;
        switch (g_headerState)
        {
        case HeaderState::Scanning:
            state = loc::Get(loc::S::StateScanning);
            break;
        case HeaderState::Connecting:
            state = loc::Get(loc::S::StateConnecting);
            break;
        case HeaderState::Connected:
            state = loc::Get(loc::S::StateConnected);
            break;
        case HeaderState::Disconnected:
            state = loc::Get(loc::S::StateDisconnected);
            break;
        case HeaderState::Failed:
            state = loc::Get(loc::S::StateFailed);
            break;
        case HeaderState::NoDevices:
            state = loc::Get(loc::S::StateNoDevice);
            break;
        default:
            state = loc::Get(loc::S::StateIdle);
            break;
        }

        if (auto n = g_activeName.get())
        {
            n.Text(title);
        }
        if (auto s = g_stateText.get())
        {
            s.Text(state);
        }
    }

    // 高亮：左侧 3px 强调色竖条 + 半透明底，跟随系统强调色
    void ApplyRowVisual(size_t index)
    {
        if (index >= g_rows.size())
        {
            return;
        }
        auto& row = g_rows[index];
        const bool active = (row.state == RowState::Connected);

        if (row.status)
        {
            row.status.Text(RowStatusText(row));
            row.status.Opacity(active ? 1.0 : 0.62);
        }
        if (row.root)
        {
            row.root.BorderThickness(Thickness{ 3, 0, 0, 0 });
            row.root.BorderBrush(active ? AccentBrush(0xB0) : Brush{ nullptr });
            row.root.Background(active ? AccentBrush(0x1F) : Brush{ nullptr });
        }
        if (row.disconnect)
        {
            row.disconnect.Content(box_value(loc::Get(loc::S::BtnDisconnect)));
            row.disconnect.Visibility(active ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void RefreshConnectButton()
    {
        auto btn = g_connectBtn.get();
        if (!btn)
        {
            return;
        }
        auto list = g_list.get();
        const int sel = list ? list.SelectedIndex() : -1;
        const bool selectedIsActive = (sel >= 0 && sel == g_activeIndex);
        btn.IsEnabled(!g_busy && sel >= 0 && !selectedIsActive);
    }

    void UpdateTray()
    {
        tray::MenuData data;
        data.itemShow = loc::Get(loc::S::TrayShow);
        data.itemDisconnect = loc::Get(loc::S::TrayDisconnect);
        data.itemConnect = loc::Get(loc::S::TrayConnect);
        data.itemExit = loc::Get(loc::S::TrayExit);
        data.itemNoRecent = loc::Get(loc::S::TrayNoRecent);
        data.canDisconnect = (g_activeIndex >= 0);

        for (auto const& r : g_settings.recent)
        {
            data.recent.push_back(r.name.empty() ? r.id : r.name);
        }

        data.tooltip = loc::Get(loc::S::Title);
        if (g_activeIndex >= 0)
        {
            data.tooltip += L" - ";
            data.tooltip += g_headerName.c_str();
            data.tooltip += L" (";
            data.tooltip += loc::Get(loc::S::RowConnected).c_str();
            data.tooltip += L")";
        }
        tray::Update(data);
    }

    void BeginDisconnect(size_t index);

    void PushRecentAndSave(hstring const& id, hstring const& name)
    {
        settings::PushRecent(g_settings, std::wstring(id.c_str()), std::wstring(name.c_str()));
        settings::Save(g_settings);
        UpdateTray();
    }

    // 连接必须走工作线程：OpenAsync 的完成回调会投递回创建它的公寓，
    // 在 UI 线程（STA）上阻塞等待等于自己等自己。
    void BeginConnectById(hstring const& id, hstring const& name, int rowIndex)
    {
        if (g_busy)
        {
            return;
        }
        g_busy = true;

        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            g_rows[i].state = RowState::Idle;
            ApplyRowVisual(i);
        }
        if (rowIndex >= 0 && static_cast<size_t>(rowIndex) < g_rows.size())
        {
            g_rows[static_cast<size_t>(rowIndex)].state = RowState::Connecting;
            ApplyRowVisual(static_cast<size_t>(rowIndex));
        }

        g_headerName = name;
        g_headerState = HeaderState::Connecting;
        RefreshHeader();
        RefreshConnectButton();
        btlog::Write(L"BeginConnect: " + std::wstring(name.c_str()));

        if (auto bar = g_error.get())
        {
            bar.IsOpen(false);
        }

        auto dispatcher = g_dispatcher;
        std::thread([dispatcher, id, name, rowIndex]()
        {
            int32_t rc = -1;
            try
            {
                btc_init();
                rc = btc_open_device(to_string(id).c_str());
            }
            catch (...)
            {
                btlog::Write(L"exception in open worker");
            }

            dispatcher.TryEnqueue([rc, id, name, rowIndex]()
            {
                try
                {
                    g_busy = false;
                    if (rc == 0)
                    {
                        g_activeId = id;
                        g_activeIndex = rowIndex;
                        g_headerName = name;
                        g_headerState = HeaderState::Connected;

                        for (size_t i = 0; i < g_rows.size(); ++i)
                        {
                            g_rows[i].state =
                                (static_cast<int>(i) == rowIndex) ? RowState::Connected : RowState::Idle;
                            ApplyRowVisual(i);
                        }
                        if (auto l = g_list.get(); l && rowIndex >= 0)
                        {
                            l.SelectedIndex(rowIndex);
                        }
                        PushRecentAndSave(id, name);
                        btlog::Write(L"connected");
                    }
                    else
                    {
                        g_activeIndex = -1;
                        g_activeId = hstring{};
                        g_headerState = HeaderState::Failed;
                        if (rowIndex >= 0 && static_cast<size_t>(rowIndex) < g_rows.size())
                        {
                            g_rows[static_cast<size_t>(rowIndex)].state = RowState::Failed;
                            g_rows[static_cast<size_t>(rowIndex)].failCode = rc;
                            ApplyRowVisual(static_cast<size_t>(rowIndex));
                        }
                        if (auto bar = g_error.get())
                        {
                            bar.Message(L"连接失败，错误码 " + to_hstring(rc));
                            bar.IsOpen(true);
                        }
                        btlog::Write(L"connect failed rc=" + to_hstring(rc));
                    }

                    RefreshHeader();
                    RefreshConnectButton();
                    UpdateTray();
                }
                catch (hresult_error const& ex)
                {
                    btlog::Write(L"ui exception: " + ex.message());
                }
                catch (...)
                {
                    btlog::Write(L"ui unknown exception");
                }
            });
        }).detach();
    }

    void BeginConnect(size_t index)
    {
        if (index >= g_rows.size())
        {
            return;
        }
        BeginConnectById(g_rows[index].id, g_rows[index].name, static_cast<int>(index));
    }

    void BeginDisconnect(size_t index)
    {
        if (g_busy)
        {
            return;
        }
        g_busy = true;
        RefreshConnectButton();

        auto dispatcher = g_dispatcher;
        std::thread([dispatcher, index]()
        {
            try
            {
                btc_init();
                btc_close_device();
            }
            catch (...)
            {
                btlog::Write(L"exception in close worker");
            }

            dispatcher.TryEnqueue([index]()
            {
                try
                {
                    g_busy = false;
                    g_activeIndex = -1;
                    g_activeId = hstring{};
                    g_headerState = HeaderState::Disconnected;
                    if (index < g_rows.size())
                    {
                        g_rows[index].state = RowState::Disconnected;
                        ApplyRowVisual(index);
                    }
                    if (auto l = g_list.get())
                    {
                        l.SelectedIndex(-1);
                    }
                    RefreshHeader();
                    RefreshConnectButton();
                    UpdateTray();
                }
                catch (...)
                {
                    btlog::Write(L"ui exception in disconnect");
                }
            });
        }).detach();
    }

    void HandleTrayCommand(int cmd)
    {
        if (cmd == tray::CmdShow)
        {
            if (g_fnShowWindow)
            {
                g_fnShowWindow();
            }
            return;
        }
        if (cmd == tray::CmdExit)
        {
            g_quitting = true;
            tray::Stop();
            try
            {
                Application::Current().Exit();
            }
            catch (...)
            {
                ::PostQuitMessage(0);
            }
            return;
        }
        if (cmd == tray::CmdDisconnect)
        {
            if (g_activeIndex >= 0)
            {
                BeginDisconnect(static_cast<size_t>(g_activeIndex));
            }
            return;
        }
        if (cmd >= tray::CmdRecentBase)
        {
            const size_t i = static_cast<size_t>(cmd - tray::CmdRecentBase);
            if (i < g_settings.recent.size())
            {
                auto const& r = g_settings.recent[i];
                // 若该设备在当前列表里，顺带高亮对应行
                int rowIndex = -1;
                for (size_t k = 0; k < g_rows.size(); ++k)
                {
                    if (g_rows[k].id == hstring(r.id))
                    {
                        rowIndex = static_cast<int>(k);
                        break;
                    }
                }
                BeginConnectById(hstring(r.id), hstring(r.name), rowIndex);
            }
        }
    }

    // 内核事件来自 WinRT 线程池，统一封送回 UI 线程
    void __cdecl OnCoreEvent(int32_t code, int32_t state, const char* payload, void* /*user*/)
    {
        auto dispatcher = g_dispatcher;
        if (!dispatcher)
        {
            return;
        }

        auto message = to_hstring(std::string_view(payload ? payload : ""));
        dispatcher.TryEnqueue([code, state, message]()
        {
            try
            {
                if (code == BTC_EVENT_ERROR)
                {
                    btlog::Write(L"core error: " + message);
                    if (auto bar = g_error.get())
                    {
                        bar.Message(message);
                        bar.IsOpen(true);
                    }
                    return;
                }

                if (g_busy)
                {
                    return;
                }

                if (g_activeIndex >= 0 && static_cast<size_t>(g_activeIndex) < g_rows.size())
                {
                    const bool opened = (state == BTC_STATE_OPENED);
                    g_rows[static_cast<size_t>(g_activeIndex)].state =
                        opened ? RowState::Connected : RowState::Disconnected;
                    ApplyRowVisual(static_cast<size_t>(g_activeIndex));
                    g_headerState = opened ? HeaderState::Connected : HeaderState::Disconnected;
                    if (!opened)
                    {
                        g_activeIndex = -1;
                        g_activeId = hstring{};
                    }
                    RefreshHeader();
                    RefreshConnectButton();
                    UpdateTray();
                }
            }
            catch (...)
            {
                btlog::Write(L"ui exception in core event");
            }
        });
    }
}

namespace winrt::BTAudioReceiver::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        // 先建立 UI 引用，语言/主题/托盘都依赖它们
        g_dispatcher = DispatcherQueue();
        g_error = ErrorBar();
        g_list = DeviceList();
        g_empty = EmptyHint();
        g_activeName = ActiveDeviceText();
        g_stateText = StateText();
        g_connectBtn = ConnectButton();

        g_fnApplyTexts = [this]() { ApplyTexts(); };
        g_fnShowWindow = [this]() { ShowMainWindow(); };
        g_fnOpenSettings = [this]() { OpenSettings(); };

        btc_set_event_callback(OnCoreEvent, nullptr);

        // Mica 背景必须在根元素透明时才能透出来；若设置失败，
        // 必须给根元素一个不透明兜底背景，否则整个窗口是全透明的
        //（内容照常渲染，但用户什么都看不见）。
        try
        {
            SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop());
            g_backdropApplied = true;
        }
        catch (...)
        {
            btlog::Write(L"MicaBackdrop unavailable, using solid background");
        }

        ApplyTheme();
        ApplyTexts();

        DeviceList().SelectionChanged(
            [](Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&)
            {
                RefreshConnectButton();
            });

        // 关闭窗口 = 收进托盘，不退出进程
        try
        {
            AppWindow().Closing(
                [](Microsoft::UI::Windowing::AppWindow const& sender,
                   Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
                {
                    if (g_quitting)
                    {
                        return;
                    }
                    // 托盘没起来就不能藏窗口，否则用户无法再唤回
                    if (!tray::IsRunning())
                    {
                        btlog::Write(L"tray unavailable, closing for real");
                        return;
                    }
                    args.Cancel(true);
                    sender.Hide();
                });
        }
        catch (...)
        {
            btlog::Write(L"AppWindow.Closing hook failed");
        }

        try
        {
            AppWindow().Resize(Windows::Graphics::SizeInt32{ 480, 660 });

            // 固定窗口尺寸：不允许拖拽缩放，也不允许最大化
            if (auto presenter =
                    AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            {
                presenter.IsResizable(false);
                presenter.IsMaximizable(false);
            }
        }
        catch (...)
        {
        }

        tray::Start(::GetModuleHandleW(nullptr), [](int cmd)
        {
            auto dispatcher = g_dispatcher;
            if (dispatcher)
            {
                dispatcher.TryEnqueue([cmd]() { HandleTrayCommand(cmd); });
            }
        });
        UpdateTray();

        btlog::Write(L"MainWindow constructed");

        RootGrid().Loaded([this](auto const&, auto const&)
        {
            try
            {
                LoadDevices();
            }
            catch (hresult_error const& ex)
            {
                btlog::Write(L"ex in Loaded: " + ex.message());
            }
            catch (...)
            {
                btlog::Write(L"unknown ex in Loaded");
            }
        });
    }

    void MainWindow::ApplyTheme()
    {
        ElementTheme theme = ElementTheme::Default;
        if (g_settings.theme == L"light")
        {
            theme = ElementTheme::Light;
        }
        else if (g_settings.theme == L"dark")
        {
            theme = ElementTheme::Dark;
        }
        try
        {
            RootGrid().RequestedTheme(theme);

            // 没有 Mica 时根元素必须有实色背景，且要随主题重算
            if (!g_backdropApplied)
            {
                RootGrid().Background(SystemBackgroundBrush());
            }
        }
        catch (hresult_error const& ex)
        {
            // 切换主题失败不应该让整个应用挂掉
            btlog::Write(L"ApplyTheme failed: " + ex.message());
        }
        catch (...)
        {
            btlog::Write(L"ApplyTheme failed: unknown");
        }
    }

    void MainWindow::ApplyTexts()
    {
        loc::Current() = (g_settings.language == L"en") ? loc::Lang::En : loc::Lang::Zh;

        Title(loc::Get(loc::S::Title));
        SettingsButton().Content(box_value(loc::Get(loc::S::BtnSettings)));
        RefreshButton().Content(box_value(loc::Get(loc::S::BtnRefresh)));
        ConnectButton().Content(box_value(loc::Get(loc::S::BtnConnect)));
        EmptyHint().Text(loc::Get(loc::S::EmptyHint));

        RefreshHeader();
        for (size_t i = 0; i < g_rows.size(); ++i)
        {
            ApplyRowVisual(i);
        }
        RefreshConnectButton();
        UpdateTray();
    }

    void MainWindow::ShowMainWindow()
    {
        try
        {
            AppWindow().Show();
        }
        catch (...)
        {
        }
        Activate();
    }

    void MainWindow::OpenSettings()
    {
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(loc::Get(loc::S::DlgSettings)));
        dialog.CloseButtonText(loc::Get(loc::S::BtnClose));

        StackPanel panel;
        panel.Spacing(16);
        panel.Width(320);

        TextBlock langLabel;
        langLabel.Text(loc::Get(loc::S::LabelLanguage));
        langLabel.FontSize(12);
        langLabel.Opacity(0.7);

        ComboBox langBox;
        langBox.HorizontalAlignment(HorizontalAlignment::Stretch);
        langBox.Items().Append(box_value(hstring(L"简体中文")));
        langBox.Items().Append(box_value(hstring(L"English")));
        langBox.SelectedIndex(g_settings.language == L"en" ? 1 : 0);
        langBox.SelectionChanged([this](Windows::Foundation::IInspectable const& sender,
                                        SelectionChangedEventArgs const&)
        {
            auto box = sender.try_as<ComboBox>();
            if (!box)
            {
                return;
            }
            g_settings.language = (box.SelectedIndex() == 1) ? L"en" : L"zh";
            settings::Save(g_settings);
            ApplyTexts();
        });

        TextBlock themeLabel;
        themeLabel.Text(loc::Get(loc::S::LabelTheme));
        themeLabel.FontSize(12);
        themeLabel.Opacity(0.7);

        ComboBox themeBox;
        themeBox.HorizontalAlignment(HorizontalAlignment::Stretch);
        themeBox.Items().Append(box_value(loc::Get(loc::S::ThemeLight)));
        themeBox.Items().Append(box_value(loc::Get(loc::S::ThemeDark)));
        themeBox.Items().Append(box_value(loc::Get(loc::S::ThemeSystem)));
        themeBox.SelectedIndex(g_settings.theme == L"light" ? 0
                              : g_settings.theme == L"dark" ? 1
                                                            : 2);
        themeBox.SelectionChanged([this](Windows::Foundation::IInspectable const& sender,
                                         SelectionChangedEventArgs const&)
        {
            auto box = sender.try_as<ComboBox>();
            if (!box)
            {
                return;
            }
            switch (box.SelectedIndex())
            {
            case 0:
                g_settings.theme = L"light";
                break;
            case 1:
                g_settings.theme = L"dark";
                break;
            default:
                g_settings.theme = L"system";
                break;
            }
            settings::Save(g_settings);
            ApplyTheme();
        });

        panel.Children().Append(langLabel);
        panel.Children().Append(langBox);
        panel.Children().Append(themeLabel);
        panel.Children().Append(themeBox);

        Button resetButton;
        resetButton.Content(box_value(loc::Get(loc::S::BtnReset)));
        resetButton.HorizontalAlignment(HorizontalAlignment::Stretch);
        resetButton.Click([this](Windows::Foundation::IInspectable const&,
                                 RoutedEventArgs const&)
        {
            settings::Reset();
            ApplyTheme();
            ApplyTexts();
            btlog::Write(L"settings reset");
        });

        panel.Children().Append(resetButton);

        dialog.Content(panel);
        dialog.ShowAsync();
    }

    void MainWindow::LoadDevices()
    {
        if (g_busy)
        {
            return;
        }

        g_headerState = HeaderState::Scanning;
        RefreshHeader();
        EmptyHint().Visibility(Visibility::Collapsed);
        ErrorBar().IsOpen(false);
        RefreshConnectButton();

        auto dispatcher = DispatcherQueue();

        std::thread([dispatcher]()
        {
            std::vector<hstring> names;
            std::vector<hstring> ids;
            hstring error;

            try
            {
                btc_init();
                if (char* json = btc_scan_devices_json())
                {
                    auto raw = to_hstring(json);
                    btc_free_string(json);
                    for (auto const& entry : JsonArray::Parse(raw))
                    {
                        auto object = entry.GetObject();
                        names.push_back(object.GetNamedString(L"name", L"(unknown)"));
                        ids.push_back(object.GetNamedString(L"id", L""));
                    }
                }
                else
                {
                    error = loc::Get(loc::S::ErrScanFailed);
                }
            }
            catch (hresult_error const& ex)
            {
                error = loc::Get(loc::S::ErrScanException) + ex.message();
            }
            catch (...)
            {
                error = loc::Get(loc::S::ErrScanFailed);
            }

            dispatcher.TryEnqueue([names, ids, error]()
            {
                try
                {
                    auto list = g_list.get();
                    auto empty = g_empty.get();
                    auto bar = g_error.get();

                    if (list)
                    {
                        list.Items().Clear();
                    }
                    StopMarquees();
                    g_rows.clear();

                    if (error.size() > 0)
                    {
                        btlog::Write(L"scan error: " + error);
                        g_headerState = HeaderState::NoDevices;
                        RefreshHeader();
                        if (bar)
                        {
                            bar.Message(error);
                            bar.IsOpen(true);
                        }
                        if (empty)
                        {
                            empty.Visibility(Visibility::Visible);
                        }
                        UpdateTray();
                        return;
                    }

                    if (list)
                    {
                        for (size_t i = 0; i < names.size(); ++i)
                        {
                            RowUi row;
                            row.id = ids[i];
                            row.name = names[i];

                            Border root;
                            root.CornerRadius(CornerRadius{ 6, 6, 6, 6 });
                            root.Padding(Thickness{ 9, 8, 8, 8 });
                            root.MinHeight(48);

                            Grid grid;
                            ColumnDefinition star;
                            star.Width(GridLength{ 1.0, GridUnitType::Star });
                            ColumnDefinition autoCol;
                            autoCol.Width(GridLength{ 0.0, GridUnitType::Auto });
                            grid.ColumnDefinitions().Append(star);
                            grid.ColumnDefinitions().Append(autoCol);

                            // 用 Grid 而不是 StackPanel：垂直 StackPanel 按 DesiredSize
                            // 排列子元素，跑马灯容器量不到真实可用宽度，永远不会判定为溢出。
                            Grid texts;
                            texts.RowSpacing(2);
                            texts.VerticalAlignment(VerticalAlignment::Center);
                            RowDefinition rowTitle;
                            rowTitle.Height(GridLength{ 0.0, GridUnitType::Auto });
                            RowDefinition rowSub;
                            rowSub.Height(GridLength{ 0.0, GridUnitType::Auto });
                            texts.RowDefinitions().Append(rowTitle);
                            texts.RowDefinitions().Append(rowSub);

                            TextBlock title;
                            title.Text(names[i]);
                            title.FontSize(14);
                            title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

                            // 标题放在独立容器里，超长时由容器裁剪 + 折返滚动
                            Grid titleHost;
                            titleHost.VerticalAlignment(VerticalAlignment::Center);
                            titleHost.Children().Append(title);
                            SetupMarquee(titleHost, title);

                            TextBlock sub;
                            sub.Text(loc::Get(loc::S::RowPairedIdle));
                            sub.FontSize(12);
                            sub.Opacity(0.62);
                            sub.TextTrimming(TextTrimming::CharacterEllipsis);

                            Grid::SetRow(titleHost, 0);
                            Grid::SetRow(sub, 1);
                            texts.Children().Append(titleHost);
                            texts.Children().Append(sub);
                            Grid::SetColumn(texts, 0);

                            Button disc;
                            disc.Content(box_value(loc::Get(loc::S::BtnDisconnect)));
                            disc.Visibility(Visibility::Collapsed);
                            disc.VerticalAlignment(VerticalAlignment::Center);
                            disc.Margin(Thickness{ 8, 0, 0, 0 });
                            disc.Click([i](Windows::Foundation::IInspectable const&,
                                           RoutedEventArgs const&) { BeginDisconnect(i); });
                            Grid::SetColumn(disc, 1);

                            grid.Children().Append(texts);
                            grid.Children().Append(disc);
                            root.Child(grid);

                            row.root = root;
                            row.status = sub;
                            row.disconnect = disc;

                            // 扫描刷新后恢复已连接行的高亮
                            if (g_activeId.size() > 0 && row.id == g_activeId)
                            {
                                row.state = RowState::Connected;
                                g_activeIndex = static_cast<int>(i);
                                g_headerName = row.name;
                                g_headerState = HeaderState::Connected;
                            }

                            list.Items().Append(root);
                            g_rows.push_back(row);
                        }
                    }

                    if (empty)
                    {
                        empty.Visibility(names.empty() ? Visibility::Visible : Visibility::Collapsed);
                    }

                    for (size_t i = 0; i < g_rows.size(); ++i)
                    {
                        ApplyRowVisual(i);
                    }

                    if (g_activeIndex < 0)
                    {
                        g_headerName = hstring{};
                        g_headerState = names.empty() ? HeaderState::NoDevices : HeaderState::Idle;
                    }
                    else if (auto l = g_list.get())
                    {
                        l.SelectedIndex(g_activeIndex);
                    }

                    RefreshHeader();
                    RefreshConnectButton();
                    UpdateTray();
                    btlog::Write(L"devices loaded: " + to_hstring(names.size()));
                }
                catch (hresult_error const& ex)
                {
                    btlog::Write(L"ui exception building rows: " + ex.message());
                }
                catch (...)
                {
                    btlog::Write(L"ui unknown exception building rows");
                }
            });
        }).detach();
    }

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        LoadDevices();
    }

    void MainWindow::OnConnectClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto index = DeviceList().SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= g_rows.size())
        {
            if (auto bar = g_error.get())
            {
                bar.Message(loc::Get(loc::S::ErrSelectDevice));
                bar.IsOpen(true);
            }
            return;
        }
        BeginConnect(static_cast<size_t>(index));
    }

    void MainWindow::OnSettingsClick(IInspectable const&, RoutedEventArgs const&)
    {
        OpenSettings();
    }
}

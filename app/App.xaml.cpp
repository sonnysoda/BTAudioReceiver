#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "cli.h"
#include "log.h"
#include "settings.h"
#include "single.h"
#include "tray.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::BTAudioReceiver::implementation
{
    App::App()
    {
        // 命令行旗标决定日志行为；没有 --debug 就完全不产生文件
        const CliOptions cli = ParseCommandLine();

        try
        {
            settings::Current() = settings::Load();
        }
        catch (...)
        {
        }

        btlog::Sink sink = btlog::Sink::None;
        std::wstring logPath;
        if (cli.debug)
        {
            if (!cli.outputPath.empty())
            {
                sink = btlog::Sink::ConsoleAndFile;
                logPath = btlog::ResolveOutputPath(cli.outputPath);
            }
            else if (cli.outputToExeDir)
            {
                sink = btlog::Sink::ConsoleAndFile;
                logPath = btlog::ExeDir() + btlog::DefaultLogName();
            }
            else
            {
                sink = btlog::Sink::Console;
            }
        }
        btlog::Start(sink, logPath);

        if (btlog::Mode() != btlog::Sink::None)
        {
            btlog::Write(std::wstring(L"BTAudioReceiver starting, log sink = ") +
                         (sink == btlog::Sink::ConsoleAndFile ? L"console+file" : L"console"));
            if (!logPath.empty())
            {
                btlog::Write(std::wstring(L"log file: ") + logPath);
            }
        }

        // 单实例：第二个进程只负责唤起已有实例的主窗口，然后立刻退出。
        // 放在这里是因为 App 构造早于任何窗口创建，能避免多开抢占 A2DP 连接。
        if (!single::Acquire())
        {
            btlog::Write(L"another instance is running, requesting show");
            tray::RequestShowFromOtherInstance();
            ::ExitProcess(0);
        }

        InitializeComponent();
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        m_window = make<MainWindow>();
        m_window.Activate();
    }
}

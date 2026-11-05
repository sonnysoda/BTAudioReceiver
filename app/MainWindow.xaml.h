#pragma once

#include "MainWindow.g.h"

#include <vector>

namespace winrt::BTAudioReceiver::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnRefreshClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnConnectClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSettingsClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void LoadDevices();
        void ApplyTexts();
        void ApplyTheme();
        void ShowMainWindow();
        void OpenSettings();
    };
}

// 生成的 MainWindow.g.cpp 需要具体工厂
namespace winrt::BTAudioReceiver::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}

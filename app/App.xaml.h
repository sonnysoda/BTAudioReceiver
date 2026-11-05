#pragma once

#include "App.g.h"

namespace winrt::BTAudioReceiver::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}

// 注意：App 不声明公开构造函数（入口点由生成的 wWinMain 直接 make<implementation::App>()），
// 因此 cppwinrt 不会生成 factory_implementation::App，这里也不需要提供。

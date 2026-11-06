#include "pch.h"
#include "single.h"
#include "log.h"

namespace
{
    HANDLE g_mutex{ nullptr };
}

namespace single
{
    bool Acquire()
    {
        g_mutex = ::CreateMutexW(nullptr, TRUE, L"Local\\BTAudioReceiver.SingleInstance");
        if (!g_mutex)
        {
            // 创建失败时不要拦住用户，宁可多开也不要打不开
            btlog::Write(L"single: CreateMutex failed, continue");
            return true;
        }

        if (::GetLastError() == ERROR_ALREADY_EXISTS)
        {
            ::CloseHandle(g_mutex);
            g_mutex = nullptr;
            btlog::Write(L"single: another instance is running");
            return false;
        }
        return true;
    }

    void Release()
    {
        if (g_mutex)
        {
            ::ReleaseMutex(g_mutex);
            ::CloseHandle(g_mutex);
            g_mutex = nullptr;
        }
    }
}

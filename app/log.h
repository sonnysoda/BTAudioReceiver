#pragma once

// 调试日志，由命令行旗标控制：
//   --debug            日志打印到控制台
//   --debug --output   控制台 + 在 exe 同目录生成 BTAudioReceiver-时间.log
//   --debug -d <路径>  控制台 + 写入指定路径（指向目录则自动生成文件名）
//
// 没有 --debug 时完全不产生文件，Write() 是空操作。

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace btlog
{
    enum class Sink
    {
        None,
        Console,
        ConsoleAndFile
    };

    inline Sink& Mode()
    {
        static Sink mode = Sink::None;
        return mode;
    }

    inline std::wstring& FilePath()
    {
        static std::wstring path;
        return path;
    }

    inline std::wstring ExeDir()
    {
        wchar_t exe[MAX_PATH]{};
        if (::GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0)
        {
            return {};
        }
        std::wstring dir(exe);
        const auto slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            dir.resize(slash + 1);
        }
        else
        {
            dir.clear();
        }
        return dir;
    }

    inline std::wstring DefaultLogName()
    {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        wchar_t name[128]{};
        std::swprintf(name, sizeof(name) / sizeof(name[0]),
                      L"BTAudioReceiver-%04d%02d%02d-%02d%02d%02d.log", st.wYear, st.wMonth,
                      st.wDay, st.wHour, st.wMinute, st.wSecond);
        return name;
    }

    // -d 的路径可能是目录：是目录（或以分隔符结尾）就在里面按默认规则命名
    inline std::wstring ResolveOutputPath(std::wstring const& raw)
    {
        if (raw.empty())
        {
            return {};
        }
        std::wstring path = raw;
        if (!path.empty() && path.back() == L'"')
        {
            path.pop_back();
        }

        const DWORD attr = ::GetFileAttributesW(path.c_str());
        const bool isDir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ||
                           (!path.empty() && (path.back() == L'\\' || path.back() == L'/'));
        if (isDir)
        {
            if (path.back() != L'\\' && path.back() != L'/')
            {
                path += L'\\';
            }
            path += DefaultLogName();
        }
        return path;
    }

    inline void Start(Sink sink, std::wstring filePath)
    {
        Mode() = sink;
        FilePath() = (sink == Sink::ConsoleAndFile) ? filePath : std::wstring{};

        if (sink == Sink::None)
        {
            return;
        }

        // 有可用的 stdout（含被重定向到文件）就不再新建控制台
        HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (out == nullptr || out == INVALID_HANDLE_VALUE)
        {
            if (!::AttachConsole(ATTACH_PARENT_PROCESS))
            {
                ::AllocConsole();
            }
            ::SetConsoleOutputCP(CP_UTF8);
        }
    }

    inline void WriteLine(std::string const& line)
    {
        const Sink sink = Mode();
        if (sink == Sink::None)
        {
            return;
        }

        static std::mutex m;
        std::lock_guard<std::mutex> lock(m);

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        char stamp[32]{};
        std::snprintf(stamp, sizeof(stamp), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

        // 控制台：优先按宽字符写，失败则退回 fwrite（被重定向时走这条）
        {
            const std::string utf8 = std::string(stamp) + line + "\r\n";
            const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                                   static_cast<int>(utf8.size()), nullptr, 0);
            bool consoleOk = false;
            if (need > 0)
            {
                std::wstring wide(static_cast<size_t>(need), L'\0');
                ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      wide.data(), need);
                HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
                DWORD written = 0;
                consoleOk = ::WriteConsoleW(out, wide.data(), static_cast<DWORD>(wide.size()),
                                            &written, nullptr) != 0;
            }
            if (!consoleOk)
            {
                std::fwrite(utf8.data(), 1, utf8.size(), stdout);
                std::fflush(stdout);
            }
        }

        if (sink != Sink::ConsoleAndFile)
        {
            return;
        }

        HANDLE h = ::CreateFileW(FilePath().c_str(), FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        ::WriteFile(h, stamp, static_cast<DWORD>(std::strlen(stamp)), &written, nullptr);
        ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        ::WriteFile(h, "\r\n", 2, &written, nullptr);
        ::CloseHandle(h);
    }

    inline void Write(std::string_view line)
    {
        if (Mode() == Sink::None)
        {
            return;
        }
        WriteLine(std::string(line));
    }

    inline void Write(std::wstring_view line)
    {
        if (Mode() == Sink::None)
        {
            return;
        }
        if (line.empty())
        {
            WriteLine({});
            return;
        }
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                               static_cast<int>(line.size()), nullptr, 0, nullptr,
                                               nullptr);
        if (need <= 0)
        {
            return;
        }
        std::string utf8(static_cast<size_t>(need), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(),
                              need, nullptr, nullptr);
        WriteLine(utf8);
    }
}

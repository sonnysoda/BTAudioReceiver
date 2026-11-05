#pragma once

// 命令行旗标解析。
//   --debug            日志打印到控制台
//   --debug --output   控制台 + 在 exe 同目录写日志文件
//   --debug -d <路径>  控制台 + 写入指定路径

#include <windows.h>
#include <shellapi.h>

#include <string>

struct CliOptions
{
    bool debug{ false };
    bool outputToExeDir{ false };
    std::wstring outputPath;
};

inline CliOptions ParseCommandLine()
{
    CliOptions options;

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
    {
        return options;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];

        if (arg == L"--debug" || arg == L"-debug")
        {
            options.debug = true;
        }
        else if (arg == L"--output" || arg == L"-output")
        {
            options.outputToExeDir = true;
        }
        else if (arg == L"-d" || arg == L"--d")
        {
            if (i + 1 < argc)
            {
                options.outputPath = argv[++i];
            }
        }
        else if (arg.rfind(L"-d=", 0) == 0)
        {
            options.outputPath = arg.substr(3);
        }
    }

    ::LocalFree(argv);
    return options;
}

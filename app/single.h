#pragma once

// 单实例守卫：命名 Mutex，作用域为当前用户会话（Local\）。

#include <windows.h>

namespace single
{
    // true = 本进程是第一个实例；false = 已有实例在运行
    bool Acquire();
    void Release();
}

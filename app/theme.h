#pragma once

// 配色。自绘 UI 不依赖系统主题资源，两套色板硬编码。

#include "pch.h"

struct Theme
{
    bool dark{ false };

    // 窗口背景（Mica 不可用时的实色兜底；可用时保持透明让 DWM 透出）
    D2D1_COLOR_F background;

    D2D1_COLOR_F textPrimary;
    D2D1_COLOR_F textSecondary;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accentSoft;    // 选中行底色（低透明度）
    D2D1_COLOR_F stroke;        // 分隔线 / 边框
    D2D1_COLOR_F buttonFace;    // 按钮底色
    D2D1_COLOR_F buttonHover;
};

inline D2D1_COLOR_F Rgba(BYTE r, BYTE g, BYTE b, BYTE a = 255)
{
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

inline Theme MakeTheme(bool dark)
{
    Theme t;
    t.dark = dark;
    if (dark)
    {
        t.background = Rgba(0x20, 0x20, 0x20);
        t.textPrimary = Rgba(0xFA, 0xFA, 0xFA);
        t.textSecondary = Rgba(0xFA, 0xFA, 0xFA, 0x9E);
        t.accent = Rgba(0x60, 0xCD, 0xFF);
        t.accentSoft = Rgba(0x60, 0xCD, 0xFF, 0x2E);
        t.stroke = Rgba(0xFF, 0xFF, 0xFF, 0x1A);
        t.buttonFace = Rgba(0xFF, 0xFF, 0xFF, 0x0F);
        t.buttonHover = Rgba(0xFF, 0xFF, 0xFF, 0x1F);
    }
    else
    {
        t.background = Rgba(0xF3, 0xF3, 0xF3);
        t.textPrimary = Rgba(0x1A, 0x1A, 0x1A);
        t.textSecondary = Rgba(0x1A, 0x1A, 0x1A, 0x9E);
        t.accent = Rgba(0x00, 0x5F, 0xB8);
        t.accentSoft = Rgba(0x00, 0x5F, 0xB8, 0x24);
        t.stroke = Rgba(0x00, 0x00, 0x00, 0x14);
        t.buttonFace = Rgba(0xFF, 0xFF, 0xFF, 0xB0);
        t.buttonHover = Rgba(0xFF, 0xFF, 0xFF, 0xFF);
    }
    return t;
}

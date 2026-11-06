#pragma once

// 渲染栈：Direct2D HwndRenderTarget + DirectWrite。
//
// 不使用 Mica，因此不需要 D3D11 合成交换链与 DirectComposition——
// 那套组合只在需要 DWM 背景透出时才必要。

namespace gfx
{
    bool Init(HWND hwnd);
    void Shutdown();

    // 尺寸或 DPI 变化时调用
    void Resize(UINT width, UINT height);

    void BeginDraw(D2D1_COLOR_F const& background);
    bool EndDraw();

    ID2D1RenderTarget* Rt();
    ID2D1Factory* Factory();
    IDWriteFactory* Dw();

    UINT Width();
    UINT Height();
    float Dpi();
}

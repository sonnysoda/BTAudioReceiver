#include "pch.h"
#include "gfx.h"

using Microsoft::WRL::ComPtr;

namespace
{
    HWND g_hwnd{ nullptr };
    UINT g_pixelWidth{ 0 };
    UINT g_pixelHeight{ 0 };
    float g_dpi{ 96.0f };

    ComPtr<ID2D1Factory> g_factory;
    ComPtr<ID2D1HwndRenderTarget> g_rt;
    ComPtr<IDWriteFactory> g_dw;

    bool CreateTarget()
    {
        g_rt.Reset();
        if (!g_factory || g_hwnd == nullptr || g_pixelWidth == 0 || g_pixelHeight == 0)
        {
            return false;
        }

        const auto rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), g_dpi, g_dpi);

        const auto hwndProps = D2D1::HwndRenderTargetProperties(
            g_hwnd, D2D1::SizeU(g_pixelWidth, g_pixelHeight), D2D1_PRESENT_OPTIONS_NONE);

        return SUCCEEDED(g_factory->CreateHwndRenderTarget(rtProps, hwndProps, &g_rt));
    }
}

namespace gfx
{
    bool Init(HWND hwnd)
    {
        g_hwnd = hwnd;

        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        g_pixelWidth = static_cast<UINT>(rc.right - rc.left);
        g_pixelHeight = static_cast<UINT>(rc.bottom - rc.top);
        g_dpi = static_cast<float>(::GetDpiForWindow(hwnd));

        // ComPtr 的 operator& 不能用于模板重载的自动推导，这里用显式 IID 版本
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                                     nullptr,
                                     reinterpret_cast<void**>(g_factory.GetAddressOf()))))
        {
            return false;
        }
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(g_dw.GetAddressOf()))))
        {
            return false;
        }

        return CreateTarget();
    }

    void Shutdown()
    {
        g_rt.Reset();
        g_factory.Reset();
        g_dw.Reset();
        g_hwnd = nullptr;
    }

    void Resize(UINT width, UINT height)
    {
        if (width == 0 || height == 0 || g_hwnd == nullptr)
        {
            return;
        }

        g_pixelWidth = width;
        g_pixelHeight = height;

        // DPI 变了（拖动到另一块屏）要按新 DPI 重建，否则字号会错
        const float dpi = static_cast<float>(::GetDpiForWindow(g_hwnd));
        if (dpi != g_dpi)
        {
            g_dpi = dpi;
            CreateTarget();
            return;
        }

        if (g_rt)
        {
            g_rt->Resize(D2D1::SizeU(width, height));
        }
        else
        {
            CreateTarget();
        }
    }

    void BeginDraw(D2D1_COLOR_F const& background)
    {
        if (!g_rt)
        {
            return;
        }
        g_rt->BeginDraw();
        g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
        g_rt->Clear(background);
    }

    bool EndDraw()
    {
        if (!g_rt)
        {
            return false;
        }

        const HRESULT hr = g_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            CreateTarget();
            return false;
        }
        return SUCCEEDED(hr);
    }

    ID2D1RenderTarget* Rt() { return g_rt.Get(); }
    ID2D1Factory* Factory() { return g_factory.Get(); }
    IDWriteFactory* Dw() { return g_dw.Get(); }
    UINT Width() { return g_pixelWidth; }
    UINT Height() { return g_pixelHeight; }
    float Dpi() { return g_dpi; }
}

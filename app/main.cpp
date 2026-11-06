#include "pch.h"

#include "btc_core.h"
#include "cli.h"
#include "gfx.h"
#include "log.h"
#include "settings.h"
#include "single.h"
#include "texts.h"
#include "theme.h"
#include "tray.h"

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <thread>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace
{
    // ---------------- 常量 ----------------
    constexpr wchar_t kClassName[] = L"BTAudioReceiverWindow";
    constexpr UINT kTrayCmd = WM_APP + 11;
    constexpr UINT kScanDone = WM_APP + 12;
    constexpr UINT kConnectDone = WM_APP + 13;
    constexpr UINT kDisconnectDone = WM_APP + 14;

    constexpr float kWidth = 480.0f;
    constexpr float kHeight = 660.0f;
    constexpr float kPad = 20.0f;
    constexpr float kListTop = 84.0f;
    constexpr float kRowHeight = 56.0f;
    constexpr float kRowGap = 4.0f;
    constexpr int kVisibleRows = 6;
    constexpr float kBtnHeight = 32.0f;
    constexpr float kBtnWidth = 76.0f;
    constexpr float kConnectWidth = 88.0f;
    constexpr float kFooterTop = kHeight - kPad - kBtnHeight;

    // ---------------- 状态 ----------------
    enum class RowState
    {
        Idle,
        Connecting,
        Connected,
        Disconnected,
        Failed
    };

    struct Row
    {
        std::wstring id;
        std::wstring name;
        RowState state{ RowState::Idle };
        int failCode{ 0 };
    };

    struct FRect
    {
        float l{ 0 }, t{ 0 }, r{ 0 }, b{ 0 };
    };

    inline bool Contains(FRect const& rc, float x, float y)
    {
        return x >= rc.l && x < rc.r && y >= rc.t && y < rc.b;
    }

    struct RowHit
    {
        int index{ -1 };
        FRect rect{};
        FRect disc{};
        bool hasDisc{ false };
    };

    HWND g_hwnd{ nullptr };
    Theme g_theme = MakeTheme(false);
    bool g_quitting = false;
    bool g_scanning = false;
    bool g_busy = false;

    std::vector<Row> g_rows;
    int g_selected = -1;
    int g_active = -1;
    int g_scroll = 0;

    // 当前连接的设备（g_active 可能为 -1，例如从托盘最近列表连的设备不在当前列表里）
    std::wstring g_activeId;
    std::wstring g_activeName;

    // 正在连接的设备（连接在工作线程，回调时用）
    std::wstring g_pendingId;
    std::wstring g_pendingName;

    // 最近连接的设备，最多 3 条
    std::vector<settings::RecentDevice> g_recent;

    FRect g_rcSettings{};
    FRect g_rcRefresh{};
    FRect g_rcConnect{};
    std::vector<RowHit> g_rowHits{};

    // 0=无 1=设置 2=刷新 3=连接 4=行内断开
    int g_hoverKind = 0;
    int g_hoverIndex = -1;
    HCURSOR g_hand{ nullptr };

    // ---- 设置面板（居中模态，直接出现）----
    bool g_panelOpen = false;

    // ---- 按压反馈 ----
    int g_pressedKind{ 0 };            // 与 g_hoverKind 同义
    int g_pressedIndex{ -1 };

    FRect g_rcPanel{};
    FRect g_rcPanelClose{};
    FRect g_rcLang[2]{};
    FRect g_rcTheme[3]{};
    FRect g_rcReset{};

    // ---- 折返滚动 ----
    bool g_metricsDirty{ true };
    std::vector<float> g_rowTextWidth{};
    double g_marqueeSeconds{ 0.0 };
    ULONGLONG g_marqueeLast{ 0 };
    bool g_marqueeRunning{ false };

    ComPtr<IDWriteTextFormat> g_fmtTitle;
    ComPtr<IDWriteTextFormat> g_fmtName;
    ComPtr<IDWriteTextFormat> g_fmtCaption;
    ComPtr<IDWriteTextFormat> g_fmtButton;
    ComPtr<IDWriteTextFormat> g_fmtPanelTitle;
    ComPtr<IDWriteTextFormat> g_fmtNameScroll;   // 不做省略号裁剪，用于滚动

    ComPtr<ID2D1SolidColorBrush> g_brPrimary;
    ComPtr<ID2D1SolidColorBrush> g_brSecondary;
    ComPtr<ID2D1SolidColorBrush> g_brAccent;
    ComPtr<ID2D1SolidColorBrush> g_brAccentSoft;
    ComPtr<ID2D1SolidColorBrush> g_brStroke;
    ComPtr<ID2D1SolidColorBrush> g_brBtnFace;
    ComPtr<ID2D1SolidColorBrush> g_brBtnHover;
    ComPtr<ID2D1SolidColorBrush> g_brOnAccent;
    ComPtr<ID2D1SolidColorBrush> g_brScrim;
    ComPtr<ID2D1SolidColorBrush> g_brPanel;
    ComPtr<ID2D1SolidColorBrush> g_brPress;        // 按压时压暗
    ComPtr<ID2D1SolidColorBrush> g_brPanelBorder;  // 面板描边（强调色低透明）

    // ---------------- 字符串转换 ----------------
    std::string ToUtf8(std::wstring const& s)
    {
        if (s.empty())
        {
            return {};
        }
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                               nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(need), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need,
                              nullptr, nullptr);
        return out;
    }

    std::wstring FromUtf8(std::string const& s)
    {
        if (s.empty())
        {
            return {};
        }
        const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                               nullptr, 0);
        std::wstring out(static_cast<size_t>(need), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
        return out;
    }

    std::wstring Unescape(std::wstring const& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == L'\\' && i + 1 < s.size())
            {
                const wchar_t next = s[i + 1];
                if (next == L'\\')
                {
                    out.push_back(L'\\');
                    ++i;
                    continue;
                }
                if (next == L't')
                {
                    out.push_back(L'\t');
                    ++i;
                    continue;
                }
                if (next == L'n')
                {
                    out.push_back(L'\n');
                    ++i;
                    continue;
                }
                if (next == L'r')
                {
                    out.push_back(L'\r');
                    ++i;
                    continue;
                }
            }
            out.push_back(s[i]);
        }
        return out;
    }

    using DeviceList = std::vector<std::pair<std::wstring, std::wstring>>;

    DeviceList ParseDeviceText(const char* text)
    {
        DeviceList out;
        const std::wstring all = FromUtf8(text);
        size_t pos = 0;
        while (pos < all.size())
        {
            size_t end = all.find(L'\n', pos);
            if (end == std::wstring::npos)
            {
                end = all.size();
            }
            const std::wstring line = all.substr(pos, end - pos);
            pos = end + 1;
            if (line.empty())
            {
                continue;
            }
            const size_t tab = line.find(L'\t');
            if (tab == std::wstring::npos)
            {
                continue;
            }
            out.emplace_back(Unescape(line.substr(0, tab)), Unescape(line.substr(tab + 1)));
        }
        return out;
    }

    // ---------------- 排版 ----------------
    // 折返滚动：停留 → 左移到底 → 停留 → 退回，循环
    float MarqueeOffset(float overflow, double seconds)
    {
        double move = static_cast<double>(overflow) / 28.0;
        if (move < 1.5)
        {
            move = 1.5;
        }
        constexpr double hold = 1.2;
        const double cycle = 2.0 * (hold + move);
        const double t = std::fmod(seconds, cycle);

        if (t < hold)
        {
            return 0.0f;
        }
        if (t < hold + move)
        {
            return overflow * static_cast<float>((t - hold) / move);
        }
        if (t < 2.0 * hold + move)
        {
            return overflow;
        }
        return overflow * static_cast<float>(1.0 - (t - (2.0 * hold + move)) / move);
    }

    // 折返滚动只在确实有文本溢出时才跑定时器，空闲不重绘
    void SetMarqueeActive(bool active)
    {
        if (active == g_marqueeRunning)
        {
            return;
        }
        g_marqueeRunning = active;
        if (active)
        {
            g_marqueeLast = ::GetTickCount64();
            ::SetTimer(g_hwnd, 2, 16, nullptr);
        }
        else
        {
            if (g_hwnd)
            {
                ::KillTimer(g_hwnd, 2);
            }
            g_marqueeSeconds = 0.0;
        }
    }

    void Layout(float width)
    {
        g_rowHits.clear();
        const float right = width - kPad;

        g_rcConnect = FRect{ right - kConnectWidth, kFooterTop, right, kFooterTop + kBtnHeight };
        g_rcRefresh = FRect{ g_rcConnect.l - 8.0f - kBtnWidth, kFooterTop, g_rcConnect.l - 8.0f,
                             kFooterTop + kBtnHeight };
        g_rcSettings = FRect{ kPad, kFooterTop, kPad + kBtnWidth, kFooterTop + kBtnHeight };

        for (int i = 0; i < kVisibleRows; ++i)
        {
            const int index = g_scroll + i;
            if (index >= static_cast<int>(g_rows.size()))
            {
                break;
            }
            RowHit hit;
            hit.index = index;
            const float top = kListTop + static_cast<float>(i) * (kRowHeight + kRowGap);
            hit.rect = FRect{ kPad, top, right, top + kRowHeight };
            if (g_rows[static_cast<size_t>(index)].state == RowState::Connected)
            {
                hit.hasDisc = true;
                hit.disc = FRect{ right - 8.0f - 68.0f, top + 14.0f, right - 8.0f, top + 42.0f };
            }
            g_rowHits.push_back(hit);
        }

        // ---- 设置面板：居中固定尺寸 ----
        const float panelW = 360.0f;
        const float panelH = 240.0f;
        const float panelX = (width - panelW) / 2.0f;
        const float panelY = (kHeight - panelH) / 2.0f;
        g_rcPanel = FRect{ panelX, panelY, panelX + panelW, panelY + panelH };

        const float inner = panelW - kPad * 2.0f;
        const float innerLeft = panelX + kPad;
        g_rcPanelClose = FRect{ panelX + panelW - kPad - 24.0f, panelY + 12.0f,
                                panelX + panelW - kPad, panelY + 36.0f };

        const float segW2 = (inner - 8.0f) / 2.0f;
        g_rcLang[0] = FRect{ innerLeft, panelY + 72.0f, innerLeft + segW2, panelY + 106.0f };
        g_rcLang[1] = FRect{ innerLeft + segW2 + 8.0f, panelY + 72.0f,
                             innerLeft + segW2 * 2.0f + 8.0f, panelY + 106.0f };

        const float segW3 = (inner - 16.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float left = innerLeft + static_cast<float>(i) * (segW3 + 8.0f);
            g_rcTheme[i] = FRect{ left, panelY + 136.0f, left + segW3, panelY + 170.0f };
        }
        g_rcReset = FRect{ innerLeft, panelY + 188.0f, innerLeft + 168.0f, panelY + 222.0f };

        // ---- 测量每个设备名的实际宽度（用于折返滚动）----
        if (g_metricsDirty)
        {
            g_rowTextWidth.assign(g_rows.size(), 0.0f);
            IDWriteFactory* dw = gfx::Dw();
            if (dw && g_fmtNameScroll)
            {
                for (size_t i = 0; i < g_rows.size(); ++i)
                {
                    ComPtr<IDWriteTextLayout> textLayout;
                    const std::wstring& name = g_rows[i].name;
                    if (SUCCEEDED(dw->CreateTextLayout(name.c_str(),
                                                       static_cast<UINT32>(name.size()),
                                                       g_fmtNameScroll.Get(), 4096.0f, 100.0f,
                                                       &textLayout)))
                    {
                        DWRITE_TEXT_METRICS metrics{};
                        if (SUCCEEDED(textLayout->GetMetrics(&metrics)))
                        {
                            g_rowTextWidth[i] = metrics.widthIncludingTrailingWhitespace;
                        }
                    }
                }
            }
            g_metricsDirty = false;
        }

        bool anyOverflow = false;
        for (RowHit const& hit : g_rowHits)
        {
            const float textLeft = hit.rect.l + 14.0f;
            const float textRight = hit.rect.r - (hit.hasDisc ? 88.0f : 14.0f);
            const size_t idx = static_cast<size_t>(hit.index);
            if (idx < g_rowTextWidth.size() &&
                g_rowTextWidth[idx] - (textRight - textLeft) > 2.0f)
            {
                anyOverflow = true;
                break;
            }
        }
        SetMarqueeActive(anyOverflow);
    }

    // ---------------- 绘制 ----------------
    void FillRound(FRect const& rc, float radius, ID2D1Brush* fill, ID2D1Brush* stroke)
    {
        ID2D1RenderTarget* rt = gfx::Rt();
        if (!rt)
        {
            return;
        }
        const auto rr = D2D1::RoundedRect(D2D1::RectF(rc.l, rc.t, rc.r, rc.b), radius, radius);
        if (fill)
        {
            rt->FillRoundedRectangle(rr, fill);
        }
        if (stroke)
        {
            rt->DrawRoundedRectangle(rr, stroke, 1.0f);
        }
    }

    void DrawTextIn(const wchar_t* text, IDWriteTextFormat* format, ID2D1Brush* brush,
                    FRect const& rc)
    {
        ID2D1RenderTarget* rt = gfx::Rt();
        if (!rt || !format || !brush || !text)
        {
            return;
        }
        const D2D1_RECT_F r = D2D1::RectF(rc.l, rc.t, rc.r, rc.b);
        rt->DrawTextW(text, static_cast<UINT32>(std::wcslen(text)), format, &r, brush,
                      D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }

    void DrawButton(FRect const& rc, const wchar_t* label, bool accent, bool hover, bool enabled,
                    bool pressed)
    {
        ID2D1Brush* fill = accent ? g_brAccent.Get() : (hover ? g_brBtnHover.Get() : g_brBtnFace.Get());
        ID2D1Brush* text = accent ? g_brOnAccent.Get() : g_brPrimary.Get();
        ID2D1Brush* stroke = accent ? nullptr : g_brStroke.Get();

        if (!enabled)
        {
            fill = g_brBtnFace.Get();
            text = g_brSecondary.Get();
        }

        FillRound(rc, 6.0f, fill, stroke);

        // 按下：压暗 + 文字下沉 1px，制造"被按进去"的手感
        if (pressed && enabled)
        {
            FillRound(rc, 6.0f, g_brPress.Get(), nullptr);
        }

        FRect textRect = rc;
        if (pressed && enabled)
        {
            textRect.t += 1.0f;
            textRect.b += 1.0f;
        }
        DrawTextIn(label, g_fmtButton.Get(), text, textRect);
    }

    void DrawSegment(FRect const& rc, const wchar_t* label, bool selected, bool pressed)
    {
        FillRound(rc, 6.0f, selected ? g_brAccentSoft.Get() : g_brBtnFace.Get(),
                  selected ? g_brAccent.Get() : g_brStroke.Get());
        if (pressed)
        {
            FillRound(rc, 6.0f, g_brPress.Get(), nullptr);
        }
        FRect textRect = rc;
        if (pressed)
        {
            textRect.t += 1.0f;
            textRect.b += 1.0f;
        }
        DrawTextIn(label, g_fmtButton.Get(), selected ? g_brAccent.Get() : g_brPrimary.Get(),
                   textRect);
    }

    const wchar_t* RowStatusText(Row const& row, wchar_t* buffer, size_t count)
    {
        switch (row.state)
        {
        case RowState::Connecting:
            return texts::RowConnecting();
        case RowState::Connected:
            return texts::RowConnected();
        case RowState::Disconnected:
            return texts::RowDisconnected();
        case RowState::Failed:
            swprintf_s(buffer, count, texts::RowFailedFmt(), row.failCode);
            return buffer;
        default:
            return texts::RowIdle();
        }
    }

    void Render()
    {
        gfx::BeginDraw(g_theme.background);
        ID2D1RenderTarget* rt = gfx::Rt();
        if (!rt)
        {
            return;
        }

        const D2D1_SIZE_F size = rt->GetSize();
        Layout(size.width);

        const wchar_t* title = texts::NotConnected();
        if (g_active >= 0 && g_active < static_cast<int>(g_rows.size()))
        {
            title = g_rows[static_cast<size_t>(g_active)].name.c_str();
        }
        else if (!g_activeName.empty())
        {
            title = g_activeName.c_str();
        }
        DrawTextIn(title, g_fmtTitle.Get(), g_brPrimary.Get(),
                   FRect{ kPad, 16.0f, size.width - kPad, 44.0f });

        wchar_t statusBuffer[160]{};
        const wchar_t* status = texts::StateIdle();
        if (g_scanning)
        {
            status = texts::StateScanning();
        }
        else if (!g_activeId.empty())
        {
            status = texts::StateConnected();
        }
        else if (g_rows.empty())
        {
            status = texts::NoDevices();
        }
        else
        {
            swprintf_s(statusBuffer, texts::DevicesFmt(), static_cast<int>(g_rows.size()));
            status = statusBuffer;
        }
        DrawTextIn(status, g_fmtCaption.Get(), g_brSecondary.Get(),
                   FRect{ kPad, 44.0f, size.width - kPad, 66.0f });

        for (RowHit const& hit : g_rowHits)
        {
            const Row& row = g_rows[static_cast<size_t>(hit.index)];
            const bool isActive = (row.state == RowState::Connected);
            const bool isSelected = (hit.index == g_selected);
            const bool isHovered = (g_hoverKind == 0 && g_hoverIndex == hit.index);

            if (isActive)
            {
                FillRound(hit.rect, 6.0f, g_brAccentSoft.Get(), nullptr);
                const FRect bar{ hit.rect.l, hit.rect.t + 6.0f, hit.rect.l + 3.0f,
                                 hit.rect.b - 6.0f };
                FillRound(bar, 1.5f, g_brAccent.Get(), nullptr);
            }
            else if (isSelected || isHovered)
            {
                FillRound(hit.rect, 6.0f, g_brBtnFace.Get(), nullptr);
            }

            wchar_t rowBuffer[160]{};
            const wchar_t* sub = RowStatusText(row, rowBuffer, 160);
            const float textLeft = hit.rect.l + 14.0f;
            const float textRight = hit.rect.r - (hit.hasDisc ? 88.0f : 14.0f);
            const float available = textRight - textLeft;
            const float nameWidth = (static_cast<size_t>(hit.index) < g_rowTextWidth.size())
                                        ? g_rowTextWidth[static_cast<size_t>(hit.index)]
                                        : 0.0f;
            const float overflow = nameWidth - available;

            if (overflow > 2.0f)
            {
                const float offset = MarqueeOffset(overflow, g_marqueeSeconds);
                rt->PushAxisAlignedClip(
                    D2D1::RectF(textLeft, hit.rect.t + 6.0f, textRight, hit.rect.t + 32.0f),
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                DrawTextIn(row.name.c_str(), g_fmtNameScroll.Get(), g_brPrimary.Get(),
                           FRect{ textLeft - offset, hit.rect.t + 9.0f,
                                  textRight - offset + overflow + 4.0f, hit.rect.t + 30.0f });
                rt->PopAxisAlignedClip();
            }
            else
            {
                DrawTextIn(row.name.c_str(), g_fmtName.Get(), g_brPrimary.Get(),
                           FRect{ textLeft, hit.rect.t + 9.0f, textRight, hit.rect.t + 30.0f });
            }

            DrawTextIn(sub, g_fmtCaption.Get(),
                       isActive ? g_brPrimary.Get() : g_brSecondary.Get(),
                       FRect{ textLeft, hit.rect.t + 31.0f, textRight, hit.rect.t + 49.0f });

            if (hit.hasDisc)
            {
                const bool hover = (g_hoverKind == 4 && g_hoverIndex == hit.index);
                const bool pressed = (g_pressedKind == 4 && g_pressedIndex == hit.index);
                DrawButton(hit.disc, texts::BtnDisconnect(), false, hover, true, pressed);
            }
        }

        DrawButton(g_rcSettings, texts::BtnSettings(), false, g_hoverKind == 1 || g_panelOpen, true,
                   g_pressedKind == 1);
        DrawButton(g_rcRefresh, texts::BtnRefresh(), false, g_hoverKind == 2, !g_scanning && !g_busy,
                   g_pressedKind == 2);

        const bool canConnect = (g_selected >= 0 && g_selected != g_active && !g_busy &&
                                 g_selected < static_cast<int>(g_rows.size()));
        DrawButton(g_rcConnect, texts::BtnConnect(), true, g_hoverKind == 3, canConnect,
                   g_pressedKind == 3);

        // ---- 设置面板 ----
        if (g_panelOpen)
        {
            if (g_brScrim)
            {
                rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height),
                                  g_brScrim.Get());
            }

            FillRound(g_rcPanel, 12.0f, g_brPanel.Get(), g_brPanelBorder.Get());

            DrawTextIn(texts::PanelTitle(), g_fmtPanelTitle.Get(), g_brPrimary.Get(),
                       FRect{ g_rcPanel.l + kPad, g_rcPanel.t + 14.0f, g_rcPanel.r - kPad - 30.0f,
                              g_rcPanel.t + 38.0f });
            DrawTextIn(L"\u2715", g_fmtButton.Get(),
                       g_hoverKind == 5 ? g_brPrimary.Get() : g_brSecondary.Get(), g_rcPanelClose);

            DrawTextIn(texts::LabelLanguage(), g_fmtCaption.Get(), g_brSecondary.Get(),
                       FRect{ g_rcPanel.l + kPad, g_rcPanel.t + 46.0f, g_rcPanel.r - kPad,
                              g_rcPanel.t + 66.0f });
            const bool zhSelected = (settings::Current().language != L"en");
            DrawSegment(g_rcLang[0], texts::LangZh(), zhSelected, g_pressedKind == 11);
            DrawSegment(g_rcLang[1], texts::LangEn(), !zhSelected, g_pressedKind == 12);

            DrawTextIn(texts::LabelTheme(), g_fmtCaption.Get(), g_brSecondary.Get(),
                       FRect{ g_rcPanel.l + kPad, g_rcPanel.t + 110.0f, g_rcPanel.r - kPad,
                              g_rcPanel.t + 130.0f });
            const std::wstring& currentTheme = settings::Current().theme;
            DrawSegment(g_rcTheme[0], texts::ThemeLight(), currentTheme == L"light",
                        g_pressedKind == 21);
            DrawSegment(g_rcTheme[1], texts::ThemeDark(), currentTheme == L"dark",
                        g_pressedKind == 22);
            DrawSegment(g_rcTheme[2], texts::ThemeSystem(), currentTheme == L"system",
                        g_pressedKind == 23);

            DrawButton(g_rcReset, texts::BtnReset(), false, g_hoverKind == 6, true,
                       g_pressedKind == 6);
        }

        gfx::EndDraw();
    }

    // ---------------- 主题 ----------------
    void CreateFormats()
    {
        IDWriteFactory* dw = gfx::Dw();
        if (!dw)
        {
            return;
        }

        g_fmtTitle.Reset();
        g_fmtName.Reset();
        g_fmtCaption.Reset();
        g_fmtButton.Reset();
        g_fmtPanelTitle.Reset();
        g_fmtNameScroll.Reset();

        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-CN",
                             &g_fmtTitle);

        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN",
                             &g_fmtName);
        if (g_fmtName)
        {
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(g_fmtName.Get(), &ellipsis)))
            {
                const DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                g_fmtName->SetTrimming(&trimming, ellipsis.Get());
            }
            g_fmtName->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-CN",
                             &g_fmtCaption);
        if (g_fmtCaption)
        {
            g_fmtCaption->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN",
                             &g_fmtButton);
        if (g_fmtButton)
        {
            g_fmtButton->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_fmtButton->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN",
                             &g_fmtPanelTitle);

        // 滚动用：不裁剪、不换行，宽度靠调用方给足
        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN",
                             &g_fmtNameScroll);
        if (g_fmtNameScroll)
        {
            g_fmtNameScroll->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void CreateBrushes()
    {
        ID2D1RenderTarget* rt = gfx::Rt();
        if (!rt)
        {
            return;
        }

        g_brPrimary.Reset();
        g_brSecondary.Reset();
        g_brAccent.Reset();
        g_brAccentSoft.Reset();
        g_brStroke.Reset();
        g_brBtnFace.Reset();
        g_brBtnHover.Reset();
        g_brOnAccent.Reset();

        rt->CreateSolidColorBrush(g_theme.textPrimary, &g_brPrimary);
        rt->CreateSolidColorBrush(g_theme.textSecondary, &g_brSecondary);
        rt->CreateSolidColorBrush(g_theme.accent, &g_brAccent);
        rt->CreateSolidColorBrush(g_theme.accentSoft, &g_brAccentSoft);
        rt->CreateSolidColorBrush(g_theme.stroke, &g_brStroke);
        rt->CreateSolidColorBrush(g_theme.buttonFace, &g_brBtnFace);
        rt->CreateSolidColorBrush(g_theme.buttonHover, &g_brBtnHover);
        rt->CreateSolidColorBrush(g_theme.dark ? D2D1::ColorF(0.0f, 0.0f, 0.0f)
                                              : D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                  &g_brOnAccent);

        g_brScrim.Reset();
        g_brPanel.Reset();
        rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.42f), &g_brScrim);
        rt->CreateSolidColorBrush(g_theme.dark ? D2D1::ColorF(0.17f, 0.17f, 0.17f)
                                              : D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                  &g_brPanel);

        g_brPress.Reset();
        rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.20f), &g_brPress);

        g_brPanelBorder.Reset();
        rt->CreateSolidColorBrush(g_theme.accent, &g_brPanelBorder);
        if (g_brPanelBorder)
        {
            g_brPanelBorder->SetOpacity(0.45f);
        }
    }

    void ApplyTheme()
    {
        g_theme = MakeTheme(settings::ResolveDark());
        CreateBrushes();
        const BOOL dark = g_theme.dark ? TRUE : FALSE;
        ::DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));
        if (g_hwnd)
        {
            ::InvalidateRect(g_hwnd, nullptr, FALSE);
        }
    }

    void UpdateTray()
    {
        tray::MenuData data;
        data.tooltip = texts::Title();
        data.itemShow = texts::TrayShow();
        data.itemDisconnect = texts::TrayDisconnect();
        data.itemConnect = texts::TrayConnect();
        data.itemExit = texts::TrayExit();
        data.itemNoRecent = texts::TrayNoRecent();
        data.canDisconnect = !g_activeId.empty();
        for (auto const& device : g_recent)
        {
            data.recent.push_back(device.name.empty() ? device.id : device.name);
        }
        tray::Update(data);
    }

    // ---------------- 业务 ----------------
    void Invalidate()
    {
        if (g_hwnd)
        {
            ::InvalidateRect(g_hwnd, nullptr, FALSE);
        }
    }

    void SetPanelOpen(bool open)
    {
        if (g_panelOpen == open)
        {
            return;
        }
        g_panelOpen = open;
        Invalidate();
    }

    void ApplyLanguageChoice(std::wstring const& code)
    {
        settings::Current().language = code;
        settings::Save(settings::Current());
        texts::SetByCode(code);
        if (g_hwnd)
        {
            ::SetWindowTextW(g_hwnd, texts::Title());
        }
        UpdateTray();
        Invalidate();
    }

    void ApplyThemeChoice(std::wstring const& theme)
    {
        settings::Current().theme = theme;
        settings::Save(settings::Current());
        ApplyTheme();
        UpdateTray();
    }

    void DoResetSettings()
    {
        settings::Reset();
        g_recent.clear();
        texts::SetByCode(settings::Current().language);
        if (g_hwnd)
        {
            ::SetWindowTextW(g_hwnd, texts::Title());
        }
        ApplyTheme();
        UpdateTray();
        Invalidate();
    }

    // index < 0 表示该设备不在当前列表里（例如从托盘最近列表发起）
    void BeginConnectById(std::wstring const& id, std::wstring const& name, int index)
    {
        if (g_busy || id.empty())
        {
            return;
        }

        g_busy = true;
        for (auto& row : g_rows)
        {
            if (row.state == RowState::Connected || row.state == RowState::Connecting)
            {
                row.state = RowState::Idle;
            }
        }
        if (index >= 0 && index < static_cast<int>(g_rows.size()))
        {
            g_rows[static_cast<size_t>(index)].state = RowState::Connecting;
        }

        g_selected = index;
        g_active = -1;
        g_activeId.clear();
        g_activeName.clear();
        g_pendingId = id;
        g_pendingName = name;
        Invalidate();

        std::thread([id, index]()
        {
            int rc = -1;
            if (btc_init() == 0)
            {
                const std::string utf8 = ToUtf8(id);
                rc = btc_open_device(utf8.c_str());
            }
            if (g_hwnd)
            {
                ::PostMessageW(g_hwnd, kConnectDone, static_cast<WPARAM>(rc),
                               static_cast<LPARAM>(index));
            }
        }).detach();
    }

    void BeginConnect(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_rows.size()))
        {
            return;
        }
        BeginConnectById(g_rows[static_cast<size_t>(index)].id,
                         g_rows[static_cast<size_t>(index)].name, index);
    }

    void BeginDisconnect()
    {
        if (g_busy || g_activeId.empty())
        {
            return;
        }
        g_busy = true;
        Invalidate();

        std::thread([]()
        {
            if (btc_init() == 0)
            {
                btc_close_device();
            }
            if (g_hwnd)
            {
                ::PostMessageW(g_hwnd, kDisconnectDone, 0, 0);
            }
        }).detach();
    }

    void StartScan()
    {
        if (g_scanning || g_busy)
        {
            return;
        }
        g_scanning = true;
        Invalidate();

        std::thread([]()
        {
            DeviceList devices;
            if (btc_init() == 0)
            {
                if (char* text = btc_scan_devices_text())
                {
                    devices = ParseDeviceText(text);
                    btc_free_string(text);
                }
            }
            btlog::Write(L"scan finished");
            auto* payload = new DeviceList(std::move(devices));
            if (g_hwnd)
            {
                ::PostMessageW(g_hwnd, kScanDone, 0, reinterpret_cast<LPARAM>(payload));
            }
            else
            {
                delete payload;
            }
        }).detach();
    }

    void ApplyScanResult(DeviceList const& devices)
    {
        const std::wstring activeId = g_activeId;

        g_rows.clear();
        g_active = -1;
        g_selected = -1;
        g_scroll = 0;

        for (auto const& entry : devices)
        {
            Row row;
            row.id = entry.first;
            row.name = entry.second.empty() ? L"(unknown)" : entry.second;
            if (!activeId.empty() && row.id == activeId)
            {
                row.state = RowState::Connected;
                g_active = static_cast<int>(g_rows.size());
                g_selected = g_active;
            }
            g_rows.push_back(std::move(row));
        }

        g_metricsDirty = true;

        wchar_t line[64]{};
        swprintf_s(line, L"rows: %d", static_cast<int>(g_rows.size()));
        btlog::Write(line);
    }

    // ---------------- 命中测试 ----------------
    // 返回值即 g_hoverKind 的取值：
    //   0=无（配合 g_hoverIndex 表示设备行） 1=设置 2=刷新 3=连接 4=行内断开
    //   5=面板关闭 6=重置 8=面板空白 11/12=语言 21/22/23=外观
    int HitTestKind(float x, float y, int* rowIndex)
    {
        if (rowIndex)
        {
            *rowIndex = -1;
        }

        if (g_panelOpen)
        {
            if (Contains(g_rcPanelClose, x, y)) return 5;
            if (Contains(g_rcLang[0], x, y)) return 11;
            if (Contains(g_rcLang[1], x, y)) return 12;
            if (Contains(g_rcTheme[0], x, y)) return 21;
            if (Contains(g_rcTheme[1], x, y)) return 22;
            if (Contains(g_rcTheme[2], x, y)) return 23;
            if (Contains(g_rcReset, x, y)) return 6;
            if (Contains(g_rcPanel, x, y)) return 8;
            return 0;
        }

        if (Contains(g_rcSettings, x, y)) return 1;
        if (Contains(g_rcRefresh, x, y)) return 2;
        if (Contains(g_rcConnect, x, y)) return 3;

        for (RowHit const& hit : g_rowHits)
        {
            if (hit.hasDisc && Contains(hit.disc, x, y))
            {
                if (rowIndex) *rowIndex = hit.index;
                return 4;
            }
            if (Contains(hit.rect, x, y))
            {
                if (rowIndex) *rowIndex = hit.index;
                return 0;
            }
        }
        return 0;
    }

    void UpdateHover(float x, float y)
    {
        const int prevKind = g_hoverKind;
        const int prevIndex = g_hoverIndex;

        int rowIndex = -1;
        g_hoverKind = HitTestKind(x, y, &rowIndex);
        g_hoverIndex = rowIndex;

        if (g_hoverKind != prevKind || g_hoverIndex != prevIndex)
        {
            Invalidate();
        }
    }

    void ActivateHover()
    {
        switch (g_hoverKind)
        {
        case 1:
            SetPanelOpen(true);
            break;
        case 2:
            StartScan();
            break;
        case 3:
            BeginConnect(g_selected);
            break;
        case 4:
            BeginDisconnect();
            break;
        case 5:
            SetPanelOpen(false);
            break;
        case 6:
            DoResetSettings();
            break;
        case 11:
            ApplyLanguageChoice(L"zh");
            break;
        case 12:
            ApplyLanguageChoice(L"en");
            break;
        case 21:
            ApplyThemeChoice(L"light");
            break;
        case 22:
            ApplyThemeChoice(L"dark");
            break;
        case 23:
            ApplyThemeChoice(L"system");
            break;
        default:
            break;
        }
    }

    void OnMouseDown(float x, float y)
    {
        UpdateHover(x, y);
        g_pressedKind = g_hoverKind;
        g_pressedIndex = g_hoverIndex;
        if (g_hwnd)
        {
            ::SetCapture(g_hwnd);
        }
        Invalidate();
    }

    void OnMouseUp(float x, float y)
    {
        const int pressedKind = g_pressedKind;
        const int pressedIndex = g_pressedIndex;

        g_pressedKind = 0;
        g_pressedIndex = -1;
        if (g_hwnd)
        {
            ::ReleaseCapture();
        }

        UpdateHover(x, y);

        // 只有按下与松开落在同一个目标上才算点击
        if (pressedKind == g_hoverKind && pressedIndex == g_hoverIndex)
        {
            if (pressedKind == 0)
            {
                if (pressedIndex >= 0)
                {
                    g_selected = pressedIndex;
                }
                else if (g_panelOpen)
                {
                    SetPanelOpen(false);
                }
            }
            else if (pressedKind != 8)
            {
                ActivateHover();
            }
        }

        Invalidate();
    }

    float ToDipX(LPARAM lParam)
    {
        return static_cast<float>(GET_X_LPARAM(lParam)) / (gfx::Dpi() / 96.0f);
    }

    float ToDipY(LPARAM lParam)
    {
        return static_cast<float>(GET_Y_LPARAM(lParam)) / (gfx::Dpi() / 96.0f);
    }

    // ---------------- 托盘 ----------------
    void OnTrayCommand(int cmd)
    {
        switch (cmd)
        {
        case tray::CmdShow:
            ::ShowWindow(g_hwnd, SW_SHOW);
            ::SetForegroundWindow(g_hwnd);
            return;
        case tray::CmdExit:
            g_quitting = true;
            tray::Stop();
            ::DestroyWindow(g_hwnd);
            return;
        case tray::CmdDisconnect:
            BeginDisconnect();
            return;
        default:
            if (cmd >= tray::CmdRecentBase)
            {
                const size_t index = static_cast<size_t>(cmd - tray::CmdRecentBase);
                if (index < g_recent.size())
                {
                    const settings::RecentDevice device = g_recent[index];
                    int rowIndex = -1;
                    for (size_t k = 0; k < g_rows.size(); ++k)
                    {
                        if (g_rows[k].id == device.id)
                        {
                            rowIndex = static_cast<int>(k);
                            break;
                        }
                    }
                    BeginConnectById(device.id, device.name, rowIndex);
                }
            }
            return;
        }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            ::BeginPaint(hwnd, &ps);
            Render();
            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            gfx::Resize(LOWORD(lParam), HIWORD(lParam));
            Invalidate();
            return 0;

        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_MOUSEMOVE:
            UpdateHover(ToDipX(lParam), ToDipY(lParam));
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && g_hoverKind != 0 && g_hoverKind != 8)
            {
                ::SetCursor(g_hand);
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN:
            OnMouseDown(ToDipX(lParam), ToDipY(lParam));
            return 0;

        case WM_LBUTTONUP:
            OnMouseUp(ToDipX(lParam), ToDipY(lParam));
            return 0;

        case WM_CAPTURECHANGED:
            if (g_pressedKind != 0 || g_pressedIndex >= 0)
            {
                g_pressedKind = 0;
                g_pressedIndex = -1;
                Invalidate();
            }
            return 0;

        case WM_MOUSEWHEEL:
        {
            const int total = static_cast<int>(g_rows.size());
            const int maxScroll = total > kVisibleRows ? total - kVisibleRows : 0;
            if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
            {
                if (g_scroll < maxScroll)
                {
                    ++g_scroll;
                    Invalidate();
                }
            }
            else if (g_scroll > 0)
            {
                --g_scroll;
                Invalidate();
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && g_panelOpen)
            {
                SetPanelOpen(false);
                return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == 2)
            {
                const ULONGLONG now = ::GetTickCount64();
                g_marqueeSeconds += static_cast<double>(now - g_marqueeLast) / 1000.0;
                g_marqueeLast = now;
                Invalidate();
                return 0;
            }
            break;

        case kScanDone:
        {
            auto* payload = reinterpret_cast<DeviceList*>(lParam);
            g_scanning = false;
            if (payload)
            {
                ApplyScanResult(*payload);
                delete payload;
            }
            UpdateTray();
            Invalidate();
            return 0;
        }

        case kConnectDone:
        {
            const int rc = static_cast<int>(wParam);
            const int index = static_cast<int>(lParam);
            g_busy = false;

            if (rc == 0)
            {
                g_active = index;
                g_activeId = g_pendingId;
                g_activeName = g_pendingName;
                if (index >= 0 && index < static_cast<int>(g_rows.size()))
                {
                    g_rows[static_cast<size_t>(index)].state = RowState::Connected;
                    g_selected = index;
                }
                if (!g_activeId.empty())
                {
                    settings::PushRecent(g_recent, g_activeId, g_activeName);
                    settings::SaveRecent(g_recent);
                }
                btlog::Write(L"connected");
            }
            else
            {
                if (index >= 0 && index < static_cast<int>(g_rows.size()))
                {
                    g_rows[static_cast<size_t>(index)].state = RowState::Failed;
                    g_rows[static_cast<size_t>(index)].failCode = rc;
                }
                g_active = -1;
                g_activeId.clear();
                g_activeName.clear();
                btlog::Write(L"connect failed");
            }

            g_pendingId.clear();
            g_pendingName.clear();
            UpdateTray();
            Invalidate();
            return 0;
        }

        case kDisconnectDone:
            g_busy = false;
            if (g_active >= 0 && g_active < static_cast<int>(g_rows.size()))
            {
                g_rows[static_cast<size_t>(g_active)].state = RowState::Disconnected;
            }
            g_active = -1;
            g_activeId.clear();
            g_activeName.clear();
            UpdateTray();
            Invalidate();
            return 0;

        case kTrayCmd:
            OnTrayCommand(static_cast<int>(wParam));
            return 0;

        case WM_CLOSE:
            if (!g_quitting && tray::IsRunning())
            {
                ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    const CliOptions cli = ParseCommandLine();

    settings::Current() = settings::Load();
    g_recent = settings::LoadRecent();
    texts::SetByCode(settings::Current().language);

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
    btlog::Write(L"BTAudioReceiver (win32) starting");
    {
        wchar_t recentInfo[64]{};
        swprintf_s(recentInfo, L"recent devices loaded: %d",
                   static_cast<int>(g_recent.size()));
        btlog::Write(recentInfo);
    }

    if (!single::Acquire())
    {
        btlog::Write(L"another instance is running");
        tray::RequestShowFromOtherInstance();
        ::ExitProcess(0);
    }

    g_hand = ::LoadCursorW(nullptr, IDC_HAND);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    if (!::RegisterClassExW(&wc))
    {
        btlog::Write(L"RegisterClassExW failed");
        return 1;
    }

    const UINT dpi = ::GetDpiForSystem();
    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    const DWORD exStyle = 0;

    RECT rect{ 0, 0, MulDiv(static_cast<int>(kWidth), static_cast<int>(dpi), 96),
               MulDiv(static_cast<int>(kHeight), static_cast<int>(dpi), 96) };
    ::AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);

    const int windowWidth = rect.right - rect.left;
    const int windowHeight = rect.bottom - rect.top;
    const int x = (::GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
    const int y = (::GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

    g_hwnd = ::CreateWindowExW(exStyle, kClassName, texts::Title(), style, x, y, windowWidth,
                               windowHeight, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd)
    {
        btlog::Write(L"CreateWindowExW failed");
        return 1;
    }

    if (!gfx::Init(g_hwnd))
    {
        btlog::Write(L"gfx::Init failed");
        ::MessageBoxW(nullptr, L"渲染初始化失败", texts::Title(), MB_ICONERROR);
        return 1;
    }

    CreateFormats();
    ApplyTheme();

    tray::Start(instance, [](int cmd)
    {
        if (g_hwnd)
        {
            ::PostMessageW(g_hwnd, kTrayCmd, static_cast<WPARAM>(cmd), 0);
        }
    });
    UpdateTray();

    ::ShowWindow(g_hwnd, SW_SHOW);
    ::UpdateWindow(g_hwnd);
    StartScan();

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    tray::Stop();
    gfx::Shutdown();
    single::Release();
    return 0;
}

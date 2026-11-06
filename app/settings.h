#pragma once

// 设置持久化：HKCU\Software\BTAudioReceiver，一个字符串值。
// Win32 版不再依赖 Windows.Data.Json，用最朴素的 key=value;key=value 格式。

#include "pch.h"

struct AppSettings
{
    std::wstring language{ L"zh" };   // zh | en
    std::wstring theme{ L"system" };  // light | dark | system
};

namespace settings
{
    inline constexpr wchar_t kRegKey[] = L"Software\\BTAudioReceiver";
    inline constexpr wchar_t kRegValue[] = L"Config";

    inline AppSettings& Current()
    {
        static AppSettings instance;
        return instance;
    }

    inline std::wstring ReadRaw()
    {
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return {};
        }

        DWORD type = 0;
        DWORD size = 0;
        std::wstring value;
        if (::RegQueryValueExW(key, kRegValue, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
            type == REG_SZ && size > sizeof(wchar_t))
        {
            value.resize(size / sizeof(wchar_t));
            if (::RegQueryValueExW(key, kRegValue, nullptr, nullptr,
                                   reinterpret_cast<LPBYTE>(value.data()), &size) == ERROR_SUCCESS)
            {
                while (!value.empty() && value.back() == L'\0')
                {
                    value.pop_back();
                }
            }
            else
            {
                value.clear();
            }
        }
        ::RegCloseKey(key);
        return value;
    }

    inline void WriteRaw(std::wstring const& text)
    {
        HKEY key{};
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                              nullptr) != ERROR_SUCCESS)
        {
            return;
        }
        ::RegSetValueExW(key, kRegValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                         static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(key);
    }

    inline std::wstring_view Value(std::wstring const& raw, std::wstring_view key)
    {
        size_t pos = 0;
        while (pos < raw.size())
        {
            const size_t end = raw.find(L';', pos);
            const std::wstring_view item(raw.data() + pos,
                                         (end == std::wstring::npos ? raw.size() : end) - pos);
            const size_t eq = item.find(L'=');
            if (eq != std::wstring_view::npos && item.substr(0, eq) == key)
            {
                return item.substr(eq + 1);
            }
            if (end == std::wstring::npos)
            {
                break;
            }
            pos = end + 1;
        }
        return {};
    }

    inline AppSettings Load()
    {
        AppSettings s;
        const std::wstring raw = ReadRaw();
        if (raw.empty())
        {
            return s;
        }
        if (auto v = Value(raw, L"language"); !v.empty())
        {
            s.language = std::wstring(v);
        }
        if (auto v = Value(raw, L"theme"); !v.empty())
        {
            s.theme = std::wstring(v);
        }
        return s;
    }

    inline void Save(AppSettings const& s)
    {
        WriteRaw(L"language=" + s.language + L";theme=" + s.theme);
    }

    inline void Reset()
    {
        ::RegDeleteKeyW(HKEY_CURRENT_USER, kRegKey);
        Current() = AppSettings{};
    }

    // 跟随系统时用来判断明暗：AppsUseLightTheme == 0 表示深色
    inline bool SystemPrefersDark()
    {
        DWORD light = 1;
        DWORD size = sizeof(light);
        if (::RegGetValueW(HKEY_CURRENT_USER,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) !=
            ERROR_SUCCESS)
        {
            return false;
        }
        return light == 0;
    }

    inline bool ResolveDark()
    {
        const std::wstring& theme = Current().theme;
        if (theme == L"dark")
        {
            return true;
        }
        if (theme == L"light")
        {
            return false;
        }
        return SystemPrefersDark();
    }
}

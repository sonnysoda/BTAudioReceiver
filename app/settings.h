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
    inline constexpr wchar_t kRecentValue[] = L"Recent";

    inline AppSettings& Current()
    {
        static AppSettings instance;
        return instance;
    }

    inline std::wstring ReadString(wchar_t const* valueName)
    {
        HKEY key{};
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return {};
        }

        DWORD type = 0;
        DWORD size = 0;
        std::wstring value;
        if (::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
            type == REG_SZ && size > sizeof(wchar_t))
        {
            value.resize(size / sizeof(wchar_t));
            if (::RegQueryValueExW(key, valueName, nullptr, nullptr,
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

    inline void WriteString(wchar_t const* valueName, std::wstring const& text)
    {
        HKEY key{};
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                              nullptr) != ERROR_SUCCESS)
        {
            return;
        }
        ::RegSetValueExW(key, valueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(text.c_str()),
                         static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(key);
    }

    inline std::wstring ReadRaw()
    {
        return ReadString(kRegValue);
    }

    inline void WriteRaw(std::wstring const& text)
    {
        WriteString(kRegValue, text);
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

    // ---------------- 最近连接设备 ----------------
    // 单独存一个注册表值，每行「id<TAB>名称」，字段内的 \ \t \n \r 转义。
    struct RecentDevice
    {
        std::wstring id;
        std::wstring name;
    };

    inline std::wstring EscapeField(std::wstring const& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'\\': out += L"\\\\"; break;
            case L'\t': out += L"\\t"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            default: out.push_back(c); break;
            }
        }
        return out;
    }

    inline std::wstring UnescapeField(std::wstring const& s)
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

    inline std::vector<RecentDevice> LoadRecent()
    {
        std::vector<RecentDevice> list;
        const std::wstring raw = ReadString(kRecentValue);
        size_t pos = 0;
        while (pos < raw.size() && list.size() < 3)
        {
            size_t end = raw.find(L'\n', pos);
            if (end == std::wstring::npos)
            {
                end = raw.size();
            }
            const std::wstring line = raw.substr(pos, end - pos);
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
            RecentDevice device;
            device.id = UnescapeField(line.substr(0, tab));
            device.name = UnescapeField(line.substr(tab + 1));
            if (!device.id.empty())
            {
                list.push_back(std::move(device));
            }
        }
        return list;
    }

    inline void SaveRecent(std::vector<RecentDevice> const& list)
    {
        std::wstring out;
        for (auto const& device : list)
        {
            out += EscapeField(device.id);
            out.push_back(L'\t');
            out += EscapeField(device.name);
            out.push_back(L'\n');
        }
        WriteString(kRecentValue, out);
    }

    // 去重、置顶、最多 3 条
    inline void PushRecent(std::vector<RecentDevice>& list, std::wstring const& id,
                           std::wstring const& name)
    {
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            if (it->id == id)
            {
                list.erase(it);
                break;
            }
        }
        list.insert(list.begin(), RecentDevice{ id, name });
        if (list.size() > 3)
        {
            list.resize(3);
        }
    }
}

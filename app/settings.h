#pragma once

// 设置持久化：一律写注册表 HKCU\Software\BTAudioReceiver，不落地任何文件。
// 原因：需求要求「调试模式关闭时不产生任何文件」，而设置本身也必须能保存。

#include <windows.h>

#include <string>
#include <vector>

#include <winrt/Windows.Data.Json.h>

struct RecentDevice
{
    std::wstring id;
    std::wstring name;
};

struct AppSettings
{
    std::wstring language{ L"zh" };    // zh | en
    std::wstring theme{ L"system" };   // light | dark | system
    std::vector<RecentDevice> recent;  // 最近的在前，最多 3 条
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
        if (::RegQueryValueExW(key, kRegValue, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
            type != REG_SZ || size == 0)
        {
            ::RegCloseKey(key);
            return {};
        }

        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (::RegQueryValueExW(key, kRegValue, nullptr, nullptr,
                               reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS)
        {
            ::RegCloseKey(key);
            return {};
        }
        ::RegCloseKey(key);

        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
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

    inline void ParseInto(AppSettings& s, std::wstring const& json)
    {
        if (json.empty())
        {
            return;
        }

        using namespace winrt::Windows::Data::Json;
        auto obj = JsonObject::Parse(winrt::hstring(json));

        if (obj.HasKey(L"language"))
        {
            s.language = obj.GetNamedString(L"language", L"zh").c_str();
        }
        if (obj.HasKey(L"theme"))
        {
            s.theme = obj.GetNamedString(L"theme", L"system").c_str();
        }
        if (obj.HasKey(L"recent"))
        {
            s.recent.clear();
            auto arr = obj.GetNamedArray(L"recent");
            const uint32_t n = arr.Size();
            for (uint32_t i = 0; i < n && s.recent.size() < 3; ++i)
            {
                auto d = arr.GetObjectAt(i);
                RecentDevice r;
                r.id = d.GetNamedString(L"id", L"").c_str();
                r.name = d.GetNamedString(L"name", L"").c_str();
                if (!r.id.empty())
                {
                    s.recent.push_back(r);
                }
            }
        }
    }

    // 清空注册表键并恢复默认值
    inline void Reset()
    {
        ::RegDeleteKeyW(HKEY_CURRENT_USER, kRegKey);
        Current() = AppSettings{};
    }

    inline AppSettings Load()
    {
        AppSettings s;
        try
        {
            ParseInto(s, ReadRaw());
        }
        catch (...)
        {
            // 配置损坏时回到默认值，不要影响启动
        }
        return s;
    }

    inline void Save(AppSettings const& s)
    {
        try
        {
            using namespace winrt::Windows::Data::Json;

            JsonObject root;
            root.SetNamedValue(L"language", JsonValue::CreateStringValue(winrt::hstring(s.language)));
            root.SetNamedValue(L"theme", JsonValue::CreateStringValue(winrt::hstring(s.theme)));

            JsonArray arr;
            for (auto const& r : s.recent)
            {
                JsonObject d;
                d.SetNamedValue(L"id", JsonValue::CreateStringValue(winrt::hstring(r.id)));
                d.SetNamedValue(L"name", JsonValue::CreateStringValue(winrt::hstring(r.name)));
                arr.Append(d);
            }
            root.SetNamedValue(L"recent", arr);

            WriteRaw(std::wstring(root.Stringify().c_str()));
        }
        catch (...)
        {
        }
    }

    // 记录一次成功连接，去重并保持最近 3 条
    inline void PushRecent(AppSettings& s, std::wstring const& id, std::wstring const& name)
    {
        for (auto it = s.recent.begin(); it != s.recent.end(); ++it)
        {
            if (it->id == id)
            {
                s.recent.erase(it);
                break;
            }
        }
        RecentDevice r;
        r.id = id;
        r.name = name;
        s.recent.insert(s.recent.begin(), r);
        if (s.recent.size() > 3)
        {
            s.recent.resize(3);
        }
    }
}

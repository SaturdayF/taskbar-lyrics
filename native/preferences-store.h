#pragma once
#include "preferences.h"
#include <filesystem>
#include <fstream>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace preferences {
inline Settings load(const std::filesystem::path& path) {
    using namespace winrt::Windows::Data::Json;
    Settings result;
    if (!std::filesystem::exists(path)) return result;
    if (std::filesystem::file_size(path) > 2 * 1024 * 1024) throw std::runtime_error("Settings file too large");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read settings");
    std::string body((std::istreambuf_iterator<char>(input)), {});
    auto json = JsonObject::Parse(winrt::to_hstring(body));
    result.hideWhenPaused = json.GetNamedBoolean(L"hideWhenPaused", false);
    result.clickThrough = json.GetNamedBoolean(L"clickThrough", false);
    result.globalOffset = clamp(json.GetNamedNumber(L"globalOffset", 0));
    if (json.HasKey(L"trackOffsets")) {
        for (const auto& entry : json.GetNamedObject(L"trackOffsets")) {
            if (entry.Value().ValueType() == JsonValueType::Number)
                result.setTrackOffset(std::wstring(entry.Key()), entry.Value().GetNumber());
        }
    }
    return result;
}
inline void save(const Settings& settings, const std::filesystem::path& path) {
    using namespace winrt::Windows::Data::Json;
    JsonObject json, offsets;
    json.Insert(L"hideWhenPaused", JsonValue::CreateBooleanValue(settings.hideWhenPaused));
    json.Insert(L"clickThrough", JsonValue::CreateBooleanValue(settings.clickThrough));
    json.Insert(L"globalOffset", JsonValue::CreateNumberValue(clamp(settings.globalOffset)));
    for (const auto& [key, value] : settings.trackOffsets) offsets.Insert(key, JsonValue::CreateNumberValue(clamp(value)));
    json.Insert(L"trackOffsets", offsets);
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << winrt::to_string(json.Stringify()); output.flush();
        if (!output) throw std::runtime_error("Cannot write settings");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
}
}

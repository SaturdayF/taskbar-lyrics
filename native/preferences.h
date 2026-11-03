#pragma once
#include "lyrics.h"
#include <map>

namespace preferences {
inline double clamp(double value) { return std::clamp(std::isfinite(value) ? value : 0.0, -10.0, 10.0); }
struct Settings {
    bool operator==(const Settings&) const = default;
    bool hideWhenPaused = false, clickThrough = false;
    bool online = true, docked = true, darkTheme = true;
    int fontSize = 19;
    double globalOffset = 0;
    std::map<std::wstring, double> trackOffsets;
    double trackOffset(const std::wstring& key) const {
        const auto it = trackOffsets.find(key);
        return it == trackOffsets.end() ? 0 : clamp(it->second);
    }
    void setTrackOffset(const std::wstring& key, double value) {
        if (key.empty()) return;
        value = clamp(value);
        if (value == 0) trackOffsets.erase(key);
        else trackOffsets[key] = value;
    }
    double effectiveOffset(const std::wstring& key) const { return clamp(globalOffset) + trackOffset(key); }
    bool shouldHide(bool playing) const { return hideWhenPaused && !playing; }
};
// Apply only fields changed in the dialog, preserving intervening tray edits.
inline Settings mergeDraft(Settings current, const Settings& baseline, const Settings& draft) {
    if (draft.hideWhenPaused != baseline.hideWhenPaused) current.hideWhenPaused = draft.hideWhenPaused;
    if (draft.clickThrough != baseline.clickThrough) current.clickThrough = draft.clickThrough;
    if (draft.online != baseline.online) current.online = draft.online;
    if (draft.docked != baseline.docked) current.docked = draft.docked;
    if (draft.darkTheme != baseline.darkTheme) current.darkTheme = draft.darkTheme;
    if (draft.fontSize != baseline.fontSize) current.fontSize = std::clamp(draft.fontSize, 12, 26);
    if (draft.globalOffset != baseline.globalOffset) current.globalOffset = clamp(draft.globalOffset);
    for (const auto& [key, value] : baseline.trackOffsets) {
        (void)value;
        if (baseline.trackOffset(key) != draft.trackOffset(key)) current.setTrackOffset(key, draft.trackOffset(key));
    }
    for (const auto& [key, value] : draft.trackOffsets) {
        if (baseline.trackOffset(key) != clamp(value)) current.setTrackOffset(key, value);
    }
    return current;
}
inline std::wstring copyableLine(const std::vector<lyrics::Line>& lines, double position) {
    const auto index = lyrics::find(lines, position);
    return index >= 0 ? lines[index].text : L"";
}
}

#pragma once
#include "lyrics.h"
#include <map>

namespace preferences {
inline double clamp(double value) { return std::clamp(std::isfinite(value) ? value : 0.0, -10.0, 10.0); }
struct Settings {
    bool hideWhenPaused = false, clickThrough = false;
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
inline std::wstring copyableLine(const std::vector<lyrics::Line>& lines, double position) {
    const auto index = lyrics::find(lines, position);
    return index >= 0 ? lines[index].text : L"";
}
}

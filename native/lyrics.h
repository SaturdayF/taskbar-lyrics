#pragma once
#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace lyrics {
struct Line { double time; std::wstring text; };
inline std::vector<Line> parse(const std::wstring& lrc) {
    static const std::wregex stamp(LR"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])");
    static const std::wregex offsetTag(LR"(\[offset:([+-]?\d+)\])", std::regex::icase);
    std::wsmatch offsetMatch;
    double offset = 0;
    if (std::regex_search(lrc, offsetMatch, offsetTag)) {
        try { offset = std::stod(offsetMatch[1].str()) / 1000; } catch (...) {}
    }
    std::vector<Line> lines;
    std::wistringstream input(lrc);
    std::wstring row;
    while (std::getline(input, row)) {
        auto text = std::regex_replace(row, stamp, L"");
        const auto first = text.find_first_not_of(L" \t\r\n\xFEFF");
        text = first == std::wstring::npos ? L"" : text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
        for (auto it = std::wsregex_iterator(row.begin(), row.end(), stamp); it != std::wsregex_iterator(); ++it) {
            auto tag = *it;
            const auto seconds = std::stoi(tag[2].str());
            if (seconds >= 60) continue;
            const auto fraction = tag[3].str();
            const double time = std::stoi(tag[1].str()) * 60 + seconds + offset +
                (fraction.empty() ? 0 : std::stod(fraction) / std::pow(10.0, fraction.size()));
            lines.push_back({time, text});
        }
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return lines;
}
inline int find(const std::vector<Line>& lines, double position) {
    const auto it = std::upper_bound(lines.begin(), lines.end(), position,
        [](double value, const Line& line) { return value < line.time; });
    return static_cast<int>(it - lines.begin()) - 1;
}
}

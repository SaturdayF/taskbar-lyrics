#pragma once
#include <algorithm>
#include <optional>
#include <vector>
namespace layout {
struct Span { int start, end; };
inline std::optional<Span> choose(int left, int right, int desiredCenter, int width, int minimum, std::vector<Span> occupied, int gap) {
    if (right <= left || gap < 0 || width < minimum || minimum <= 0 || right - left < minimum + 2 * gap) return {};
    left += gap; right -= gap;
    std::sort(occupied.begin(), occupied.end(), [](auto a, auto b) { return a.start < b.start; });
    std::vector<Span> free;
    int cursor = left;
    for (auto item : occupied) {
        const int start = std::clamp(item.start - gap, left, right);
        const int end = std::clamp(item.end + gap, left, right);
        if (start > cursor) free.push_back({cursor, start});
        cursor = std::max(cursor, end);
    }
    if (cursor < right) free.push_back({cursor, right});
    std::optional<Span> best;
    for (auto lane : free) {
        const int size = std::min(width, lane.end - lane.start);
        if (size < minimum) continue;
        const int x = std::clamp(desiredCenter - size / 2, lane.start, lane.end - size);
        Span candidate{x, x + size};
        if (!best || size > best->end - best->start ||
            (size == best->end - best->start && std::abs(x + size / 2 - desiredCenter) < std::abs((best->start + best->end) / 2 - desiredCenter))) best = candidate;
    }
    return best;
}
}

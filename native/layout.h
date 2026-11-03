#pragma once
#include <algorithm>
#include <optional>
#include <vector>
namespace layout {
struct Span { int start, end; };
inline std::optional<Span> choose(int left, int right, int desiredCenter, int width, int minimum, std::vector<Span> occupied, int gap, bool manual = false) {
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
        const int available = lane.end - lane.start;
        if (available < minimum) continue;
        const int size = std::min(width, manual ? std::max(minimum, available - 32) : available);
        if (size < minimum) continue;
        const int x = std::clamp(desiredCenter - size / 2, lane.start, lane.end - size);
        Span candidate{x, x + size};
        if (!best) { best = candidate; continue; }
        const int distance = std::abs(x + size / 2 - desiredCenter);
        const int bestDistance = std::abs((best->start + best->end) / 2 - desiredCenter);
        const int bestSize = best->end - best->start;
        if (manual ? (distance < bestDistance || (distance == bestDistance && size > bestSize))
                   : (size > bestSize || (size == bestSize && distance < bestDistance))) best = candidate;
    }
    return best;
}
}

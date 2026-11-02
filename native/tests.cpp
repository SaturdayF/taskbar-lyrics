#include "lyrics.h"
#include "layout.h"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        auto lines = lyrics::parse(L"[offset:-500]\n[00:02.25][00:03:500] 第二行\r\n[00:01.1]第一行\n[00:99]invalid\n[ar:artist]");
        check(lines.size() == 3, "multi timestamps / invalid seconds");
        check(std::abs(lines[0].time - .6) < .0001 && lines[0].text == L"第一行", "offset and sorting");
        check(std::abs(lines[1].time - 1.75) < .0001 && lines[1].text == L"第二行", "fraction and trimming");
        check(std::abs(lines[2].time - 3) < .0001, "colon fraction");
        check(lyrics::find(lines, .59) == -1 && lyrics::find(lines, .6) == 0, "timestamp boundary");
        check(lyrics::find(lines, 20) == 2 && lyrics::find(lines, 0) == -1, "seek both directions");
        check(lyrics::parse(L"plain text").empty() && lyrics::find({}, 0) == -1, "no timestamps");
        check(lyrics::parse(L"[offset:9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999]\n[00:00]safe").size() == 1, "malformed offset does not crash");
        auto slot = layout::choose(0, 1920, 960, 520, 160, {{800, 1250}, {1700, 1920}}, 8);
        check(slot && slot->start >= 8 && slot->end <= 792 && slot->end - slot->start == 520, "avoid centered icons");
        check(!layout::choose(0, 500, 250, 520, 160, {{0, 500}}, 8), "no room hides");
        slot = layout::choose(-1920, 0, -960, 520, 160, {{-1920, -1500}, {-1200, 0}}, 8);
        check(slot && slot->start >= -1492 && slot->end <= -1208, "negative screen and shrinking");
        slot = layout::choose(0, 1000, 500, 520, 160, {{100, 600}, {400, 800}}, 8);
        check(slot && slot->start >= 808, "merge occupied intervals");
        std::cout << "PASS: 8 LRC and 4 taskbar layout checks\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}

#include "lyrics.h"
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
        std::cout << "PASS: 8 native LRC checks\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}

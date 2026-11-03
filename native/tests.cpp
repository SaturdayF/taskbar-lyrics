#include "lyrics.h"
#include "layout.h"
#include "preferences.h"
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
        const auto first = layout::choose(0, 800, 375, 520, 160, {{0, 200}, {600, 800}}, 8, true);
        const auto last = layout::choose(0, 800, 425, 520, 160, {{0, 200}, {600, 800}}, 8, true);
        check(first && last && first->start < last->start && last->end <= 592, "drag inside a lane narrower than the default width");
        preferences::Settings preferences;
        preferences.setTrackOffset(L"song A", 1.5); preferences.globalOffset = -.5;
        check(preferences.effectiveOffset(L"song A") == 1 && preferences.effectiveOffset(L"song B") == -.5, "track calibration isolated and added to global");
        preferences.setTrackOffset(L"song A", 100);
        check(preferences.trackOffset(L"song A") == 10, "offset clamped");
        preferences.setTrackOffset(L"song A", 0);
        check(preferences.trackOffsets.empty(), "reset removes calibration");
        check(!preferences.shouldHide(false), "auto hide opt in");
        preferences.hideWhenPaused = true;
        check(preferences.shouldHide(false) && !preferences.shouldHide(true), "pause hides and play restores");
        check(preferences::copyableLine(lines, .59).empty() && preferences::copyableLine(lines, .6) == L"第一行", "copy only a timestamped current lyric");
        check(preferences::copyableLine(lyrics::parse(L"[00:00]song\n[00:02]"), 3).empty(), "do not copy instrumental placeholder");
        preferences::Settings baseline, draft, live;
        draft.fontSize = 22; live.hideWhenPaused = true;
        auto merged = preferences::mergeDraft(live, baseline, draft);
        check(merged.fontSize == 22 && merged.hideWhenPaused, "dialog save preserves concurrent changes from tray");
        baseline.setTrackOffset(L"A", 1); draft = baseline; draft.setTrackOffset(L"A", 0);
        live = baseline; live.setTrackOffset(L"B", 2);
        merged = preferences::mergeDraft(live, baseline, draft);
        check(merged.trackOffset(L"A") == 0 && merged.trackOffset(L"B") == 2, "reset draft preserves another song calibration");
        draft = baseline; draft.fontSize = 99; draft.globalOffset = -99;
        merged = preferences::mergeDraft(live, baseline, draft);
        check(merged.fontSize == 26 && merged.globalOffset == -10, "draft values bounded before application");
        check(preferences::mergeDraft(live, baseline, baseline) == live, "unmodified draft leaves live settings intact");
        std::cout << "PASS: 8 LRC, 5 taskbar layout and 11 preference checks\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}

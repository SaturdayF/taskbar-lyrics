#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <memory>
#include "preferences.h"

namespace settingsui {
inline constexpr wchar_t Version[] = L"0.3.1";
enum class Action { Previous, Toggle, Next, Copy, Import, Retry, Center, Demo };
struct Snapshot {
    preferences::Settings preferences;
    std::wstring key, title, artist, album, lyric, status;
    double position = 0, duration = 0;
    bool playing = false, demo = false, canPrevious = false, canToggle = false, canNext = false, canCopy = false;
};
struct Callbacks {
    std::function<Snapshot()> read;
    std::function<std::wstring(const preferences::Settings&, const preferences::Settings&)> save;
    std::function<void(Action)> action;
};
// Native controls remain discoverable by accessibility and keyboard navigation.
class Window {
public:
    explicit Window(Callbacks callbacks, bool preview = false);
    ~Window();
    void show();
    HWND handle() const;
    bool translate(MSG& message);
    void selectPage(int page);
    void capture(const std::filesystem::path& file);
    void scrollTo(int offset);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
int checkUi(const std::filesystem::path& directory);
}

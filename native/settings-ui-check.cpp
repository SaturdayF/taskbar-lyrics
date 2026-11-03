#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "settings-ui.h"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 2) { if (args) LocalFree(args); return 2; }
    const std::filesystem::path directory(args[1]); LocalFree(args);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return settingsui::checkUi(directory);
}

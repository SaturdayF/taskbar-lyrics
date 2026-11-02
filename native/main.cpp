#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include "lyrics.h"

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Data::Json;
using Clock = std::chrono::steady_clock;
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshMessage = WM_APP + 2;
constexpr UINT Import = 100, Online = 101, Demo = 102, Center = 103, Exit = 104;
constexpr size_t MaxBody = 2 * 1024 * 1024;
struct State {
    std::wstring key, title, artist, album, status = L"等待播放器；右键查看菜单";
    std::vector<lyrics::Line> lines;
    double position = 0, duration = 0, rate = 1;
    bool playing = false;
    Clock::time_point sampled = Clock::now();
    unsigned revision = 0;
};
State state;
std::mutex gate;
std::atomic<bool> online{true}, demo{false}, stopping{false};
HWND windowHandle = nullptr;
HFONT font = nullptr;
UINT taskbarCreated = 0;

std::wstring decode(const std::string& data) {
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(), static_cast<int>(data.size()), nullptr, 0);
    if (!size && !data.empty()) throw std::runtime_error("LRC must use UTF-8");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(), static_cast<int>(data.size()), result.data(), size);
    return result;
}
std::wstring encodeQuery(const std::wstring& text) {
    const auto utf8 = winrt::to_string(text);
    const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring result;
    for (const unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += L'%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
struct Internet {
    HINTERNET handle;
    explicit Internet(HINTERNET value) : handle(value) { if (!value) throw std::runtime_error("HTTP handle"); }
    ~Internet() { WinHttpCloseHandle(handle); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
    operator HINTERNET() const { return handle; }
};
[[noreturn]] void httpFailure(const char* operation) {
    throw std::runtime_error(std::string(operation) + "; WinHTTP=" + std::to_string(GetLastError()));
}
std::string request(const std::wstring& path) {
    Internet session(WinHttpOpen(L"TaskbarLyricsNative/0.1-prototype", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
    Internet connection(WinHttpConnect(session, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet call(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!WinHttpSendRequest(call, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(call, nullptr)) httpFailure("HTTP unavailable");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(call, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &length, nullptr)) httpFailure("HTTP headers");
    if (status == 404) return {};
    if (status != 200) throw std::runtime_error("HTTP status");
    const auto deadline = Clock::now() + std::chrono::seconds(12);
    std::string body;
    char buffer[8192];
    DWORD received;
    while (!stopping && online && Clock::now() < deadline) {
        if (!WinHttpReadData(call, buffer, sizeof(buffer), &received)) httpFailure("HTTP read");
        if (!received) return body;
        if (body.size() + received > MaxBody) throw std::runtime_error("HTTP body too large");
        body.append(buffer, received);
    }
    throw std::runtime_error("HTTP cancelled or timed out");
}
std::wstring normalize(const std::wstring& text) {
    const int size = NormalizeString(NormalizationKC, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring value(size, 0);
    const int written = NormalizeString(NormalizationKC, text.data(), static_cast<int>(text.size()), value.data(), size);
    if (written <= 0) return {};
    value.resize(written);
    std::wstring result;
    for (const wchar_t c : value) {
        WORD type = 0;
        if (GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & (C1_ALPHA | C1_DIGIT))) result += static_cast<wchar_t>(towlower(c));
    }
    return result;
}
std::vector<lyrics::Line> fetch(const State& track) {
    if (track.title.empty() || track.artist.empty()) return {};
    std::wstring path = L"/api/get?track_name=" + encodeQuery(track.title) + L"&artist_name=" + encodeQuery(track.artist);
    if (!track.album.empty()) path += L"&album_name=" + encodeQuery(track.album);
    if (track.duration >= 1 && track.duration <= 3600) path += L"&duration=" + std::to_wstring(std::lround(track.duration));
    auto body = request(path);
    if (body.empty()) return {};
    auto json = JsonObject::Parse(decode(body));
    auto title = std::wstring(json.GetNamedString(L"trackName", L""));
    auto artist = std::wstring(json.GetNamedString(L"artistName", L""));
    const double duration = json.GetNamedNumber(L"duration", 0);
    const auto expectedTitle = normalize(track.title), expectedArtist = normalize(track.artist);
    if (expectedTitle.empty() || expectedArtist.empty() || normalize(title) != expectedTitle || normalize(artist) != expectedArtist ||
        (track.duration > 0 && duration > 0 && std::abs(duration - track.duration) > 3)) return {};
    if (!json.HasKey(L"syncedLyrics") || json.GetNamedValue(L"syncedLyrics").ValueType() != JsonValueType::String) return {};
    return lyrics::parse(std::wstring(json.GetNamedString(L"syncedLyrics")));
}
bool supported(std::wstring id) {
    std::transform(id.begin(), id.end(), id.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    for (auto part : {L"qqmusic", L"qq.music", L"cloudmusic", L"netease", L"163music", L"applemusic", L"itunes"})
        if (id.find(part) != std::wstring::npos) return true;
    return false;
}
template<typename Operation> auto awaitMedia(Operation operation) {
    if (operation.wait_for(std::chrono::seconds(3)) == winrt::Windows::Foundation::AsyncStatus::Started) {
        operation.Cancel();
        throw std::runtime_error("Media operation timed out");
    }
    return operation.GetResults();
}
State readMedia(GlobalSystemMediaTransportControlsSessionManager const& manager, const std::wstring& previous) {
    GlobalSystemMediaTransportControlsSession chosen{nullptr};
    int best = -1;
    for (auto const& session : manager.GetSessions()) {
        try {
            const std::wstring id(session.SourceAppUserModelId());
            if (!supported(id)) continue;
            const int score = (session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? 2 : 0) + (id == previous ? 1 : 0);
            if (score > best) { chosen = session; best = score; }
        } catch (...) { /* One closed session must not hide other players. */ }
    }
    State result;
    if (!chosen) return result;
    const auto properties = awaitMedia(chosen.TryGetMediaPropertiesAsync());
    const auto timeline = chosen.GetTimelineProperties();
    const auto playback = chosen.GetPlaybackInfo();
    result.title = properties.Title(); result.artist = properties.Artist(); result.album = properties.AlbumTitle();
    if (result.title.empty()) return State{};
    result.key = std::wstring(chosen.SourceAppUserModelId()) + L"\n" + result.title + L"\n" + result.artist + L"\n" + result.album;
    result.duration = std::max(0.0, std::chrono::duration<double>(timeline.EndTime() - timeline.StartTime()).count());
    result.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    auto rate = playback.PlaybackRate(); result.rate = rate ? rate.Value() : 1;
    result.position = std::chrono::duration<double>(timeline.Position() - timeline.StartTime()).count();
    const double elapsed = std::chrono::duration<double>(winrt::clock::now() - timeline.LastUpdatedTime()).count();
    if (result.playing) result.position += std::max(0.0, elapsed) * result.rate;
    result.position = std::clamp(result.position, 0.0, result.duration > 0 ? result.duration : 1e9);
    result.sampled = Clock::now();
    result.status = L"未找到同步歌词；可右键导入 UTF-8 LRC";
    return result;
}
void worker() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::wstring previous;
    auto nextFetch = Clock::now();
    while (!stopping) {
        if (!demo) {
            try {
                if (!manager) manager = awaitMedia(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
                auto next = readMedia(manager, previous);
                previous = next.key.substr(0, next.key.find(L'\n'));
                State snapshot;
                {
                    std::lock_guard lock(gate);
                    if (demo) continue;
                    if (next.key != state.key) { next.revision = state.revision + 1; state = next; nextFetch = Clock::now(); }
                    else { state.position = next.position; state.duration = next.duration; state.playing = next.playing; state.rate = next.rate; state.sampled = next.sampled; }
                    snapshot = state;
                }
                if (online && !snapshot.key.empty() && snapshot.lines.empty() && Clock::now() >= nextFetch) {
                    nextFetch = Clock::now() + std::chrono::minutes(2);
                    try {
                        auto lines = fetch(snapshot);
                        std::lock_guard lock(gate);
                        if (!demo && online && state.key == snapshot.key && state.revision == snapshot.revision) {
                            state.lines = std::move(lines);
                            state.status = state.lines.empty() ? L"未找到同步歌词；可右键导入 LRC" : L"LRCLIB 在线歌词";
                        }
                    } catch (...) {
                        std::lock_guard lock(gate);
                        if (state.key == snapshot.key && state.revision == snapshot.revision) state.status = L"在线歌词暂不可用；可右键导入 LRC";
                    }
                }
                PostMessageW(windowHandle, RefreshMessage, 0, 0);
            } catch (...) {
                manager = nullptr;
                std::lock_guard lock(gate);
                if (!demo) { state.playing = false; state.status = L"媒体会话暂不可用，正在重试"; }
            }
        }
        for (int i = 0; i < 5 && !stopping; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    winrt::uninit_apartment();
}
void setDemo(bool enabled) {
    std::lock_guard lock(gate);
    demo = enabled;
    const auto revision = state.revision + 1;
    state = State{}; state.revision = revision;
    if (enabled) {
        state.key = L"demo"; state.title = L"原生歌词演示（模拟数据）"; state.playing = true;
        state.duration = 24; state.status = L"模拟数据";
        state.lines = lyrics::parse(L"[00:00]原生歌词演示 · 模拟数据\n[00:04]无需安装 .NET 或浏览器运行时\n[00:08]右键导入本地 UTF-8 LRC\n[00:12]支持系统媒体会话与播放进度\n[00:16]这是任务栏上方的悬浮原型\n[00:20]右键关闭演示，读取真实播放器");
    }
}
void addTray() {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = windowHandle; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; data.uCallbackMessage = TrayMessage;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wcscpy_s(data.szTip, L"Taskbar Lyrics Native · 原型");
    Shell_NotifyIconW(NIM_ADD, &data);
}
void centerWindow() {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const auto dpi = GetDpiForWindow(windowHandle);
    const int width = std::min(MulDiv(640, dpi, 96), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = MulDiv(68, dpi, 96);
    SetWindowPos(windowHandle, HWND_TOPMOST, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.bottom - height - MulDiv(8, dpi, 96), width, height, SWP_NOACTIVATE);
}
void importLrc() {
    State snapshot;
    { std::lock_guard lock(gate); snapshot = state; }
    if (snapshot.key.empty()) { MessageBoxW(windowHandle, L"请先播放歌曲，或开启演示模式。", L"导入歌词", MB_OK); return; }
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = windowHandle; dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path)); dialog.lpstrFilter = L"UTF-8 LRC\0*.lrc\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    try {
        if (std::filesystem::file_size(path) > MaxBody) throw std::runtime_error("too large");
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        if (!input) throw std::runtime_error("read error");
        std::string body((std::istreambuf_iterator<char>(input)), {});
        auto lines = lyrics::parse(decode(body));
        if (lines.empty()) throw std::runtime_error("no timestamps");
        std::lock_guard lock(gate);
        if (state.key != snapshot.key || state.revision != snapshot.revision) throw std::runtime_error("track changed");
        state.lines = std::move(lines); state.status = L"本地 LRC（仅当前会话）"; ++state.revision;
    } catch (...) { MessageBoxW(windowHandle, L"导入失败：请使用 2 MB 以内、含时间戳的 UTF-8 LRC，并保持当前歌曲不变。", L"导入歌词", MB_OK | MB_ICONWARNING); }
}
void menu() {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, Import, L"导入当前歌曲的 LRC…");
    AppendMenuW(popup, MF_STRING | (online ? MF_CHECKED : 0), Online, L"在线匹配（向 LRCLIB 发送歌曲信息）");
    AppendMenuW(popup, MF_STRING | (demo ? MF_CHECKED : 0), Demo, L"演示模式（模拟数据）");
    AppendMenuW(popup, MF_STRING, Center, L"恢复居中");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, Exit, L"退出");
    POINT cursor; GetCursorPos(&cursor); SetForegroundWindow(windowHandle);
    const auto command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, windowHandle, nullptr);
    DestroyMenu(popup); PostMessageW(windowHandle, WM_NULL, 0, 0);
    if (command) SendMessageW(windowHandle, WM_COMMAND, command, 0);
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreated && taskbarCreated) { addTray(); return 0; }
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc = BeginPaint(hwnd, &paint);
        RECT bounds; GetClientRect(hwnd, &bounds);
        FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(245, 245, 245));
        auto old = SelectObject(dc, font);
        State snapshot; { std::lock_guard lock(gate); snapshot = state; }
        double position = snapshot.position + (snapshot.playing ? std::chrono::duration<double>(Clock::now() - snapshot.sampled).count() * snapshot.rate : 0);
        if (demo) position = std::fmod(std::max(0.0, position), 24.0);
        else if (snapshot.duration > 0) position = std::min(position, snapshot.duration);
        const int index = lyrics::find(snapshot.lines, position);
        auto text = index >= 0 ? snapshot.lines[index].text : snapshot.title;
        if (text.empty()) text = L"Taskbar Lyrics Native · 原型";
        const auto inset = MulDiv(12, GetDpiForWindow(hwnd), 96);
        RECT top{inset, 0, bounds.right - inset, bounds.bottom * 3 / 5};
        DrawTextW(dc, text.c_str(), -1, &top, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(dc, RGB(160, 170, 180));
        auto detail = snapshot.status;
        if (!snapshot.title.empty() && !demo) detail = snapshot.title + L" · " + snapshot.artist + L"  |  " + snapshot.status;
        RECT bottom{inset, bounds.bottom * 3 / 5, bounds.right - inset, bounds.bottom};
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc, detail.c_str(), -1, &bottom, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old); EndPaint(hwnd, &paint); return 0;
    }
    case WM_TIMER: case RefreshMessage: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: ReleaseCapture(); SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0;
    case WM_CONTEXTMENU: menu(); return 0;
    case TrayMessage: if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) menu(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case Import: importLrc(); break;
        case Online: online = !online; { std::lock_guard lock(gate); ++state.revision; } break;
        case Demo: setDemo(!demo); break;
        case Center: centerWindow(); break;
        case Exit: DestroyWindow(hwnd); break;
        } return 0;
    case WM_DPICHANGED: {
        if (font) DeleteObject(font);
        font = CreateFontW(-MulDiv(19, HIWORD(wParam), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        const auto rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER); return 0;
    }
    case WM_DISPLAYCHANGE: centerWindow(); return 0;
    case WM_DESTROY: {
        stopping = true; KillTimer(hwnd, 1);
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = hwnd; data.uID = 1; Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
int diagnostics(const std::wstring& mode, const std::filesystem::path& path) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::string result;
        if (mode == L"--probe") {
            const auto manager = awaitMedia(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            unsigned count = 0;
            for (const auto& session : manager.GetSessions()) { (void)session; ++count; }
            result = "SMTC OK; session count=" + std::to_string(count) + "\n";
        } else {
            State track; track.title = L"Hello"; track.artist = L"Adele"; track.album = L"25"; track.duration = 295;
            const auto lines = fetch(track);
            result = "LRCLIB synchronized line count=" + std::to_string(lines.size()) + "\n";
            if (lines.empty()) throw std::runtime_error("No synchronized lyrics from LRCLIB");
        }
        std::ofstream output(path); output << result;
        return output ? 0 : 1;
    } catch (const winrt::hresult_error& error) {
        std::ofstream(path) << "WinRT failure HRESULT=" << std::hex << static_cast<uint32_t>(error.code().value)
            << ": " << winrt::to_string(error.message()) << '\n'; return 1;
    } catch (const std::exception& error) {
        std::ofstream(path) << "Diagnostic failed: " << error.what() << "; Win32=" << GetLastError() << '\n'; return 1;
    } catch (...) { std::ofstream(path) << "Unknown diagnostic failure\n"; return 1; }
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
    if (args.size() == 2 && (args[0] == L"--probe" || args[0] == L"--network-check")) return diagnostics(args[0], args[1]);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TaskbarLyrics.Native.Prototype");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }
    online = std::find(args.begin(), args.end(), L"--offline") == args.end();
    if (std::find(args.begin(), args.end(), L"--demo") != args.end()) setDemo(true);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = instance; wc.lpszClassName = L"TaskbarLyricsNative";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    windowHandle = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName,
        L"Taskbar Lyrics Native · 原型", WS_POPUP, 0, 0, 640, 68, nullptr, nullptr, instance, nullptr);
    if (!windowHandle) { CloseHandle(mutex); return 1; }
    SetLayeredWindowAttributes(windowHandle, 0, 230, LWA_ALPHA);
    const auto dpi = GetDpiForWindow(windowHandle);
    font = CreateFontW(-MulDiv(19, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    centerWindow(); addTray(); SetTimer(windowHandle, 1, 200, nullptr); ShowWindow(windowHandle, SW_SHOWNOACTIVATE);
    std::thread background(worker);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    stopping = true; background.join();
    if (font) DeleteObject(font);
    CloseHandle(mutex);
    return 0;
}

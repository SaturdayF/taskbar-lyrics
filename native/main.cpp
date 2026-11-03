#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include "lyrics.h"
#include "taskbar.h"
#include "preferences-store.h"

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Data::Json;
using Clock = std::chrono::steady_clock;
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshMessage = WM_APP + 2;
constexpr UINT Import = 100, Online = 101, Demo = 102, Center = 103, Exit = 104, Dock = 105, Retry = 106, Details = 107;
constexpr UINT PreviousTrack = 108, PlayPause = 109, NextTrack = 110;
constexpr UINT CopyLine = 111, HidePaused = 112, ClickThrough = 113, Earlier = 114, Later = 115, ResetOffset = 116;
constexpr UINT GlobalEarlier = 117, GlobalLater = 118, GlobalReset = 119;
constexpr COLORREF TransparentKey = RGB(0, 0, 0);
constexpr size_t MaxBody = 2 * 1024 * 1024;
struct State {
    std::wstring key, source, title, artist, album, status = L"等待播放器；右键查看菜单";
    std::vector<lyrics::Line> lines;
    double position = 0, duration = 0, rate = 1;
    bool playing = false, hasTimeline = false, canToggle = false, canPrevious = false, canNext = false;
    Clock::time_point sampled = Clock::now();
    unsigned revision = 0;
};
State state;
std::mutex gate;
std::wstring controlStatus;
std::vector<std::pair<UINT, std::wstring>> commands;
std::atomic<bool> online{true}, demo{false}, stopping{false};
bool docked = true;
int horizontalOffset = 0, dragStart = 0, dragOffset = 0;
bool dragging = false;
bool manuallyPlaced = false;
preferences::Settings preferencesState;
std::filesystem::path preferencesPath;
std::wstring preferencesStatus;
struct BarStore { std::mutex gate; std::vector<BarSnapshot> bars; std::atomic<bool> stop{false}; };
auto barStore = std::make_shared<BarStore>();
HWND windowHandle = nullptr;
HFONT font = nullptr;
UINT taskbarCreated = 0;

void savePreferences() {
    if (demo || preferencesPath.empty()) return;
    try { preferences::save(preferencesState, preferencesPath); preferencesStatus.clear(); }
    catch (...) {
        preferencesStatus = L"设置保存失败，请检查应用数据目录是否可写。";
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = windowHandle; data.uID = 1; data.uFlags = NIF_INFO;
        wcscpy_s(data.szInfoTitle, L"Taskbar Lyrics"); wcscpy_s(data.szInfo, preferencesStatus.c_str()); data.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
}
double lyricPosition(const State& snapshot) {
    double position = snapshot.position + (snapshot.playing ? std::chrono::duration<double>(Clock::now() - snapshot.sampled).count() * snapshot.rate : 0);
    if (demo) position = std::fmod(std::max(0.0, position), 24.0);
    else if (snapshot.duration > 0) position = std::min(position, snapshot.duration);
    return position + preferencesState.effectiveOffset(snapshot.key);
}
void adjustOffset(double delta, bool global, bool reset = false) {
    if (global) preferencesState.globalOffset = reset ? 0 : preferences::clamp(std::round((preferencesState.globalOffset + delta) * 10) / 10);
    else {
        std::lock_guard lock(gate);
        preferencesState.setTrackOffset(state.key, reset ? 0 : std::round((preferencesState.trackOffset(state.key) + delta) * 10) / 10);
    }
    savePreferences(); InvalidateRect(windowHandle, nullptr, FALSE);
}
void updateInput() {
    if (preferencesState.clickThrough && dragging) { dragging = false; ReleaseCapture(); }
    const auto style = GetWindowLongPtrW(windowHandle, GWL_EXSTYLE);
    SetWindowLongPtrW(windowHandle, GWL_EXSTYLE, preferencesState.clickThrough ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT);
    InvalidateRect(windowHandle, nullptr, FALSE);
}
void copyLine() {
    State snapshot; { std::lock_guard lock(gate); snapshot = state; }
    const auto text = preferences::copyableLine(snapshot.lines, lyricPosition(snapshot));
    if (text.empty()) return;
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return;
    auto destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return; }
    std::memcpy(destination, text.c_str(), bytes); GlobalUnlock(memory);
    if (!OpenClipboard(windowHandle)) {
        GlobalFree(memory); MessageBoxW(windowHandle, L"剪贴板正忙，请稍后重试。", L"复制歌词", MB_OK); return;
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    CloseClipboard();
}

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
std::string request(const std::wstring& path, const wchar_t* host = L"lrclib.net", const wchar_t* referer = nullptr) {
    Internet session(WinHttpOpen(L"TaskbarLyricsNative/0.1-prototype", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
    Internet connection(WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet call(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, referer, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!WinHttpSendRequest(call, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(call, nullptr)) httpFailure("HTTP unavailable");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(call, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &length, nullptr)) httpFailure("HTTP headers");
    if (status == 404) return {};
    if (status != 200) throw std::runtime_error("HTTP " + std::to_string(status));
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
bool match(const State& track, const std::wstring& title, const std::vector<std::wstring>& artists, double duration) {
    const auto expectedTitle = normalize(track.title), expectedArtist = normalize(track.artist);
    return !expectedTitle.empty() && !expectedArtist.empty() && normalize(title) == expectedTitle &&
        std::any_of(artists.begin(), artists.end(), [&](const auto& artist) { return normalize(artist) == expectedArtist; }) &&
        (track.duration <= 0 || duration <= 0 || std::abs(duration - track.duration) <= 3);
}
std::vector<lyrics::Line> lrclibLines(const State& track, const JsonObject& json) {
    if (!match(track, std::wstring(json.GetNamedString(L"trackName", L"")), {std::wstring(json.GetNamedString(L"artistName", L""))}, json.GetNamedNumber(L"duration", 0))) return {};
    if (!json.HasKey(L"syncedLyrics") || json.GetNamedValue(L"syncedLyrics").ValueType() != JsonValueType::String) return {};
    return lyrics::parse(std::wstring(json.GetNamedString(L"syncedLyrics")));
}
std::vector<lyrics::Line> fetchLrclib(const State& track) {
    if (track.title.empty() || track.artist.empty()) return {};
    std::wstring path = L"/api/get?track_name=" + encodeQuery(track.title) + L"&artist_name=" + encodeQuery(track.artist);
    if (!track.album.empty()) path += L"&album_name=" + encodeQuery(track.album);
    if (track.duration >= 1 && track.duration <= 3600) path += L"&duration=" + std::to_wstring(std::lround(track.duration));
    // An exact album lookup can miss another edition of the same recording.
    try {
        auto body = request(path);
        if (!body.empty()) { auto lines = lrclibLines(track, JsonObject::Parse(decode(body))); if (!lines.empty()) return lines; }
    } catch (...) { /* Search may still work when the exact lookup is unavailable. */ }
    if (!online || stopping) return {};
    auto body = request(L"/api/search?track_name=" + encodeQuery(track.title) + L"&artist_name=" + encodeQuery(track.artist));
    if (body.empty()) return {};
    const auto candidates = JsonArray::Parse(decode(body));
    // Album titles can differ across storefronts. Rank the same album first,
    // while keeping title, artist and any reported duration as matching criteria.
    for (int pass = 0; pass < 2; ++pass) for (auto value : candidates) {
        const auto candidate = value.GetObject();
        const bool sameAlbum = normalize(std::wstring(candidate.GetNamedString(L"albumName", L""))) == normalize(track.album);
        if ((pass == 0) != sameAlbum) continue;
        auto lines = lrclibLines(track, candidate);
        if (!lines.empty()) return lines;
    }
    return {};
}
std::vector<lyrics::Line> fetchNetease(const State& track) {
    auto body = request(L"/api/search/get?s=" + encodeQuery(track.title + L" " + track.artist) + L"&type=1&limit=20", L"music.163.com", L"https://music.163.com/");
    if (body.empty()) return {};
    const auto json = JsonObject::Parse(decode(body));
    if (!json.HasKey(L"result")) return {};
    auto result = json.GetNamedObject(L"result");
    if (!result.HasKey(L"songs")) return {};
    for (const auto& value : result.GetNamedArray(L"songs")) {
        if (!online || stopping) return {};
        const auto song = value.GetObject(); std::vector<std::wstring> artists;
        for (const auto& artist : song.GetNamedArray(L"artists")) artists.emplace_back(artist.GetObject().GetNamedString(L"name"));
        if (!match(track, std::wstring(song.GetNamedString(L"name")), artists, song.GetNamedNumber(L"duration", 0) / 1000)) continue;
        auto lyric = JsonObject::Parse(decode(request(L"/api/song/lyric?id=" + std::to_wstring(static_cast<long long>(song.GetNamedNumber(L"id"))) + L"&lv=-1&kv=-1&tv=-1", L"music.163.com", L"https://music.163.com/")));
        if (lyric.HasKey(L"lrc")) { auto lines = lyrics::parse(std::wstring(lyric.GetNamedObject(L"lrc").GetNamedString(L"lyric", L""))); if (!lines.empty()) return lines; }
    }
    return {};
}
struct FetchResult { std::vector<lyrics::Line> lines; std::wstring status; };
FetchResult fetch(const State& track) {
    std::wstring failures;
    for (int provider = 0; provider < 2 && online && !stopping; ++provider) {
        try {
            auto lines = provider == 0 ? fetchLrclib(track) : fetchNetease(track);
            if (!lines.empty()) return {std::move(lines), provider == 0 ? L"LRCLIB 在线歌词" : L"网易云在线歌词"};
        } catch (const std::exception& ex) { failures += (provider == 0 ? L"LRCLIB: " : L"网易: ") + decode(ex.what()) + L"；"; }
        catch (...) { failures += provider == 0 ? L"LRCLIB 响应异常；" : L"网易响应异常；"; }
    }
    return {{}, failures.empty() ? L"未找到同步歌词；右键重试或导入 LRC" : failures + L"右键可重试或导入 LRC"};
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
State readMedia(GlobalSystemMediaTransportControlsSessionManager const& manager, const std::wstring& previous,
    GlobalSystemMediaTransportControlsSession* active = nullptr) {
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
    if (active) *active = chosen;
    if (!chosen) return result;
    const auto properties = awaitMedia(chosen.TryGetMediaPropertiesAsync());
    const auto timeline = chosen.GetTimelineProperties();
    const auto playback = chosen.GetPlaybackInfo();
    result.title = properties.Title(); result.artist = properties.Artist(); result.album = properties.AlbumTitle();
    if (result.artist.empty()) result.artist = properties.AlbumArtist();
    if (result.title.empty()) return State{};
    result.source = chosen.SourceAppUserModelId();
    result.key = result.source + L"\n" + result.title + L"\n" + result.artist + L"\n" + result.album;
    result.duration = std::max(0.0, std::chrono::duration<double>(timeline.EndTime() - timeline.StartTime()).count());
    result.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    auto rate = playback.PlaybackRate();
    result.rate = rate && std::isfinite(rate.Value()) && rate.Value() > 0 ? rate.Value() : 1;
    result.position = std::chrono::duration<double>(timeline.Position() - timeline.StartTime()).count();
    const double elapsed = std::chrono::duration<double>(winrt::clock::now() - timeline.LastUpdatedTime()).count();
    result.hasTimeline = timeline.LastUpdatedTime().time_since_epoch().count() != 0 || result.position > 0;
    if (result.playing && elapsed > 0 && elapsed <= (result.duration > 0 ? result.duration : 300.0)) result.position += elapsed * result.rate;
    const auto controls = playback.Controls();
    result.canToggle = controls.IsPlayPauseToggleEnabled() || (result.playing ? controls.IsPauseEnabled() : controls.IsPlayEnabled());
    result.canPrevious = controls.IsPreviousEnabled();
    result.canNext = controls.IsNextEnabled();
    result.position = std::clamp(result.position, 0.0, result.duration > 0 ? result.duration : 1e9);
    result.sampled = Clock::now();
    result.status = L"未找到同步歌词；可右键导入 UTF-8 LRC";
    return result;
}
bool control(const GlobalSystemMediaTransportControlsSession& session, UINT command) {
    if (command == PreviousTrack) return awaitMedia(session.TrySkipPreviousAsync());
    if (command == NextTrack) return awaitMedia(session.TrySkipNextAsync());
    const auto playback = session.GetPlaybackInfo();
    if (playback.Controls().IsPlayPauseToggleEnabled()) return awaitMedia(session.TryTogglePlayPauseAsync());
    if (playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
        return awaitMedia(session.TryPauseAsync());
    return awaitMedia(session.TryPlayAsync());
}
void worker() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::wstring previous;
    while (!stopping) {
        if (!demo) {
            try {
                if (!manager) manager = awaitMedia(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
                GlobalSystemMediaTransportControlsSession active{nullptr};
                auto next = readMedia(manager, previous, &active);
                previous = next.source;
                std::vector<std::pair<UINT, std::wstring>> pending;
                {
                    std::lock_guard lock(gate);
                    if (demo) continue;
                    pending.swap(commands);
                    if (next.key != state.key) {
                        next.revision = state.revision + 1;
                        state = next;
                        controlStatus.clear();
                    } else {
                        if (!next.hasTimeline && state.playing)
                            state.position += std::chrono::duration<double>(next.sampled - state.sampled).count() * state.rate;
                        if (next.hasTimeline) state.position = next.position;
                        state.duration = next.duration; state.playing = next.playing;
                        state.rate = next.rate; state.sampled = next.sampled;
                        state.canToggle = next.canToggle; state.canPrevious = next.canPrevious; state.canNext = next.canNext;
                    }
                }
                for (const auto& [command, source] : pending) {
                    bool accepted = false;
                    try { if (!demo && !stopping && active && next.source == source) accepted = control(active, command); }
                    catch (...) { /* A player may close or reject the command. */ }
                    std::lock_guard lock(gate);
                    controlStatus = accepted ? L"" : L"当前播放器未接受此操作";
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
void lyricWorker() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::wstring key;
    unsigned revision = 0;
    auto nextFetch = Clock::now();
    while (!stopping) {
        State snapshot; { std::lock_guard lock(gate); snapshot = state; }
        if (key != snapshot.key || revision != snapshot.revision) { key = snapshot.key; revision = snapshot.revision; nextFetch = Clock::now(); }
        if (!demo && online && !key.empty() && snapshot.lines.empty() && Clock::now() >= nextFetch) {
            { std::lock_guard lock(gate); if (state.key == key && state.revision == revision) state.status = L"正在匹配同步歌词…"; }
            auto result = fetch(snapshot);
            std::lock_guard lock(gate);
            if (!demo && online && state.key == key && state.revision == revision) { state.lines = std::move(result.lines); state.status = result.status; }
            nextFetch = Clock::now() + std::chrono::seconds(30);
        }
        for (int i = 0; i < 5 && !stopping; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    winrt::uninit_apartment();
}
void barWorker(std::shared_ptr<BarStore> store) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        auto automation = winrt::create_instance<IUIAutomation>(__uuidof(CUIAutomation8));
        if (auto version2 = automation.try_as<IUIAutomation2>()) { version2->put_ConnectionTimeout(1000); version2->put_TransactionTimeout(1000); }
        while (!store->stop) {
            std::vector<HWND> handles;
            EnumWindows([](HWND handle, LPARAM parameter) -> BOOL {
                wchar_t name[128]{}; GetClassNameW(handle, name, 128);
                if (wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(handle);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&handles));
            std::vector<BarSnapshot> found;
            for (const auto handle : handles) { if (store->stop) break; found.push_back(inspectBar(automation.get(), handle)); }
            { std::lock_guard lock(store->gate); store->bars = std::move(found); }
            for (int i = 0; i < 10 && !store->stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (...) { /* Hide docked lyrics if the taskbar cannot be read safely. */ }
    winrt::uninit_apartment();
}
void setDemo(bool enabled) {
    std::lock_guard lock(gate);
    demo = enabled;
    commands.clear(); controlStatus.clear();
    const auto revision = state.revision + 1;
    state = State{}; state.revision = revision;
    if (enabled) {
        state.key = L"demo"; state.title = L"原生歌词演示（模拟数据）"; state.playing = true;
        state.canToggle = true;
        state.duration = 24; state.status = L"模拟数据";
        state.lines = lyrics::parse(L"[00:00]原生歌词演示 · 模拟数据\n[00:04]无需安装 .NET 或浏览器运行时\n[00:08]右键导入本地 UTF-8 LRC\n[00:12]支持系统媒体会话与播放进度\n[00:16]歌词在任务栏空闲区域显示\n[00:20]右键关闭演示，读取真实播放器");
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
void placeWindow() {
    bool playing; { std::lock_guard lock(gate); playing = state.playing; }
    if (preferencesState.shouldHide(playing)) { ShowWindow(windowHandle, SW_HIDE); return; }
    if (!docked) { ShowWindow(windowHandle, SW_SHOWNOACTIVATE); return; }
    BarSnapshot selected;
    const auto monitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY);
    { std::lock_guard lock(barStore->gate); for (const auto& bar : barStore->bars) if (MonitorFromWindow(bar.handle, MONITOR_DEFAULTTOPRIMARY) == monitor) { selected = bar; break; } }
    RECT live{};
    if (!selected.reliable || Clock::now() - selected.sampled > std::chrono::seconds(5) || !IsWindowVisible(selected.handle) ||
        !GetWindowRect(selected.handle, &live) || !EqualRect(&live, &selected.bounds)) { ShowWindow(windowHandle, SW_HIDE); return; }
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    RECT visible{}; IntersectRect(&visible, &live, &info.rcMonitor);
    const auto dpi = GetDpiForWindow(selected.handle);
    const int height = std::min(MulDiv(34, dpi, 96), static_cast<int>(visible.bottom - visible.top) - MulDiv(6, dpi, 96));
    if (height < MulDiv(12, dpi, 96) || live.right - live.left <= live.bottom - live.top) { ShowWindow(windowHandle, SW_HIDE); return; }
    const auto span = layout::choose(live.left, live.right, (live.left + live.right) / 2 + horizontalOffset,
        MulDiv(520, dpi, 96), MulDiv(160, dpi, 96), selected.occupied, MulDiv(8, dpi, 96), manuallyPlaced);
    if (!span) { ShowWindow(windowHandle, SW_HIDE); return; }
    const auto foreground = GetForegroundWindow(); RECT front{}; wchar_t name[64]{}; GetClassNameW(foreground, name, 64);
    if (foreground != windowHandle && foreground != selected.handle && wcscmp(name, L"Progman") != 0 && wcscmp(name, L"WorkerW") != 0 &&
        GetWindowRect(foreground, &front) && front.left <= info.rcMonitor.left && front.top <= info.rcMonitor.top && front.right >= info.rcMonitor.right && front.bottom >= info.rcMonitor.bottom) { ShowWindow(windowHandle, SW_HIDE); return; }
    SetWindowPos(windowHandle, HWND_TOPMOST, span->start, live.top + (live.bottom - live.top - height) / 2,
        span->end - span->start, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    {
        std::lock_guard lock(gate);
        AppendMenuW(popup, MF_STRING | (state.canPrevious ? 0 : MF_GRAYED), PreviousTrack, L"上一首");
        AppendMenuW(popup, MF_STRING | (state.canToggle ? 0 : MF_GRAYED), PlayPause, state.playing ? L"暂停" : L"播放");
        AppendMenuW(popup, MF_STRING | (state.canNext ? 0 : MF_GRAYED), NextTrack, L"下一首");
    }
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    State snapshot; { std::lock_guard lock(gate); snapshot = state; }
    const auto copyText = preferences::copyableLine(snapshot.lines, lyricPosition(snapshot));
    AppendMenuW(popup, MF_STRING | (copyText.empty() ? MF_GRAYED : 0), CopyLine, L"复制当前歌词");
    const auto timing = CreatePopupMenu();
    const auto unavailable = snapshot.key.empty() ? MF_GRAYED : 0;
    AppendMenuW(timing, MF_STRING | unavailable, Earlier, L"当前歌曲提前 0.5 秒   Ctrl + 滚轮向上");
    AppendMenuW(timing, MF_STRING | unavailable, Later, L"当前歌曲延后 0.5 秒   Ctrl + 滚轮向下");
    AppendMenuW(timing, MF_STRING | unavailable, ResetOffset, L"重置当前歌曲校准");
    AppendMenuW(timing, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timing, MF_STRING, GlobalEarlier, L"全局提前 0.5 秒   Ctrl + Shift + 滚轮向上");
    AppendMenuW(timing, MF_STRING, GlobalLater, L"全局延后 0.5 秒   Ctrl + Shift + 滚轮向下");
    AppendMenuW(timing, MF_STRING, GlobalReset, L"重置全局校准");
    wchar_t timingLabel[128]{};
    swprintf_s(timingLabel, L"歌词校准（单曲 %+.1f 秒 / 全局 %+.1f 秒）", preferencesState.trackOffset(snapshot.key), preferencesState.globalOffset);
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(timing), timingLabel);
    AppendMenuW(popup, MF_STRING | (preferencesState.hideWhenPaused ? MF_CHECKED : 0), HidePaused, L"暂停或无播放时隐藏");
    AppendMenuW(popup, MF_STRING | (preferencesState.clickThrough ? MF_CHECKED : 0), ClickThrough, L"鼠标穿透（在托盘取消）");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, Details, L"当前歌曲与歌词状态…");
    AppendMenuW(popup, MF_STRING, Import, L"导入当前歌曲的 LRC…");
    AppendMenuW(popup, MF_STRING, Retry, L"重新获取歌词");
    AppendMenuW(popup, MF_STRING | (online ? MF_CHECKED : 0), Online, L"在线匹配（向歌词来源发送歌曲信息）");
    AppendMenuW(popup, MF_STRING | (docked ? MF_CHECKED : 0), Dock, L"在任务栏空闲区域显示");
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
        const int index = lyrics::find(snapshot.lines, lyricPosition(snapshot));
        auto text = index >= 0 ? snapshot.lines[index].text : snapshot.title;
        if (text.empty()) text = index >= 0 ? L"♫  间奏" : L"Taskbar Lyrics Native · 原型";
        const auto inset = MulDiv(25, GetDpiForWindow(hwnd), 96);
        // Solid grip pixels remain draggable even though the background passes clicks through.
        const int gripLeft = MulDiv(5, GetDpiForWindow(hwnd), 96);
        const int gripHalfHeight = MulDiv(6, GetDpiForWindow(hwnd), 96);
        RECT grip{gripLeft, bounds.bottom / 2 - gripHalfHeight, inset - gripLeft, bounds.bottom / 2 + gripHalfHeight};
        if (!preferencesState.clickThrough) FillRect(dc, &grip, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        RECT top{inset, 0, bounds.right - inset, docked ? bounds.bottom : bounds.bottom * 3 / 5};
        DrawTextW(dc, text.c_str(), -1, &top, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(dc, RGB(160, 170, 180));
        auto detail = snapshot.status;
        if (!snapshot.title.empty() && !demo) detail = snapshot.title + L" · " + snapshot.artist + L"  |  " + snapshot.status;
        RECT bottom{inset, bounds.bottom * 3 / 5, bounds.right - inset, bounds.bottom};
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        if (!docked) DrawTextW(dc, detail.c_str(), -1, &bottom, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old); EndPaint(hwnd, &paint); return 0;
    }
    case WM_TIMER: placeWindow(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case RefreshMessage: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN:
        if (preferencesState.clickThrough) return 0;
        if (docked) {
            POINT cursor{}; RECT rect{}; RECT bar{};
            if (GetCursorPos(&cursor) && GetWindowRect(hwnd, &rect)) {
                const auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                std::lock_guard lock(barStore->gate);
                for (const auto& snapshot : barStore->bars) {
                    if (MonitorFromWindow(snapshot.handle, MONITOR_DEFAULTTONULL) == monitor) { bar = snapshot.bounds; break; }
                }
                if (bar.right > bar.left) {
                    dragStart = cursor.x;
                    dragOffset = (rect.left + rect.right) / 2 - (bar.left + bar.right) / 2;
                    dragging = true; SetCapture(hwnd);
                }
            }
        }
        else { ReleaseCapture(); SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); } return 0;
    case WM_MOUSEMOVE: if (dragging) { POINT cursor{}; if (GetCursorPos(&cursor) && (manuallyPlaced || cursor.x != dragStart)) { manuallyPlaced = true; horizontalOffset = dragOffset + cursor.x - dragStart; placeWindow(); } } return 0;
    case WM_LBUTTONUP: dragging = false; ReleaseCapture(); return 0;
    case WM_CAPTURECHANGED: dragging = false; return 0;
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            adjustOffset(static_cast<short>(HIWORD(wParam)) / 120.0 * .5, (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0);
            return 0;
        }
        break;
    case WM_CONTEXTMENU: menu(); return 0;
    case TrayMessage: if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) menu(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case Import: importLrc(); break;
        case CopyLine: copyLine(); break;
        case Earlier: case Later: adjustOffset(LOWORD(wParam) == Earlier ? .5 : -.5, false); break;
        case ResetOffset: adjustOffset(0, false, true); break;
        case GlobalEarlier: case GlobalLater: adjustOffset(LOWORD(wParam) == GlobalEarlier ? .5 : -.5, true); break;
        case GlobalReset: adjustOffset(0, true, true); break;
        case HidePaused: preferencesState.hideWhenPaused = !preferencesState.hideWhenPaused; savePreferences(); placeWindow(); break;
        case ClickThrough: preferencesState.clickThrough = !preferencesState.clickThrough; updateInput(); savePreferences(); break;
        case Details: { State snapshot; { std::lock_guard lock(gate); snapshot = state; }
            std::wstring commandResult; { std::lock_guard lock(gate); commandResult = controlStatus; }
            wchar_t timing[128]{};
            swprintf_s(timing, L"\n单曲校准 %+.1f 秒 / 全局 %+.1f 秒\n正值使歌词提前出现；每项范围 ±10 秒。", preferencesState.trackOffset(snapshot.key), preferencesState.globalOffset);
            const auto description = snapshot.title + L"\n" + snapshot.artist + L"\n" + snapshot.album + L"\n\n" + snapshot.status + L"\n" + commandResult + timing + L"\n" + preferencesStatus;
            MessageBoxW(hwnd, description.c_str(), L"当前歌曲与歌词状态", MB_OK); break; }
        case Online: online = !online; { std::lock_guard lock(gate); ++state.revision; } break;
        case Retry: { std::lock_guard lock(gate); state.lines.clear(); ++state.revision; } break;
        case Dock: docked = !docked; if (!docked) centerWindow(); placeWindow(); break;
        case Demo: setDemo(!demo); break;
        case Center: horizontalOffset = 0; manuallyPlaced = false; centerWindow(); placeWindow(); break;
        case PreviousTrack: case PlayPause: case NextTrack: {
            std::lock_guard lock(gate);
            if (demo && LOWORD(wParam) == PlayPause) {
                state.position = lyricPosition(state) - preferencesState.effectiveOffset(state.key);
                state.sampled = Clock::now(); state.playing = !state.playing;
            }
            if (!demo && !state.source.empty() && commands.size() < 3) commands.emplace_back(LOWORD(wParam), state.source);
            break;
        }
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
        stopping = true; barStore->stop = true; KillTimer(hwnd, 1);
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
        if (mode == L"--preferences-check") {
            auto temporary = path; temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".settings.json";
            if (std::filesystem::exists(temporary)) throw std::runtime_error("Temporary test file already exists");
            preferences::Settings fixture; fixture.globalOffset = -.5; fixture.hideWhenPaused = true; fixture.clickThrough = true;
            fixture.setTrackOffset(L"AppleMusic\n测试歌曲\n歌手\n专辑", 1.5);
            preferences::save(fixture, temporary);
            const auto restored = preferences::load(temporary);
            std::filesystem::remove(temporary);
            if (!restored.hideWhenPaused || !restored.clickThrough || restored.effectiveOffset(L"AppleMusic\n测试歌曲\n歌手\n专辑") != 1)
                throw std::runtime_error("Preferences round trip failed");
            result = "PASS: Unicode track calibration and visibility settings survive atomic save/load\n";
        } else if (mode == L"--probe") {
            const auto manager = awaitMedia(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            unsigned count = 0;
            for (const auto& session : manager.GetSessions()) { (void)session; ++count; }
            result = "SMTC OK; session count=" + std::to_string(count) + "\n";
        } else if (mode == L"--taskbar-probe") {
            auto automation = winrt::create_instance<IUIAutomation>(__uuidof(CUIAutomation8));
            auto bar = inspectBar(automation.get(), FindWindowW(L"Shell_TrayWnd", nullptr));
            const auto dpi = GetDpiForWindow(bar.handle);
            auto span = layout::choose(bar.bounds.left, bar.bounds.right, (bar.bounds.left + bar.bounds.right) / 2,
                MulDiv(520, dpi, 96), MulDiv(160, dpi, 96), bar.occupied, MulDiv(8, dpi, 96));
            result = "{\"reliable\":" + std::string(bar.reliable ? "true" : "false") + ",\"occupied\":[";
            for (size_t i = 0; i < bar.occupied.size(); ++i) {
                if (i) result += ',';
                result += "[" + std::to_string(bar.occupied[i].start) + "," + std::to_string(bar.occupied[i].end) + "]";
            }
            result += "],\"slot\":" + (span ? "[" + std::to_string(span->start) + "," + std::to_string(span->end) + "]" : std::string("null")) + "}\n";
        } else {
            State track; track.title = L"Hello"; track.artist = L"Adele"; track.album = L"25"; track.duration = 295;
            if (mode == L"--superstar-check") { track.title = L"Super Star"; track.artist = L"S.H.E"; track.album = L"Super Star"; track.duration = 196; }
            if (mode == L"--current-check") {
                const auto manager = awaitMedia(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
                track = readMedia(manager, L"");
                if (track.key.empty()) { std::ofstream(path) << "No supported media session\n"; return 2; }
                result = "Current track=" + winrt::to_string(track.title) + "; artist=" + winrt::to_string(track.artist) + "; album=" + winrt::to_string(track.album) + "; duration=" + std::to_string(track.duration) + "\n";
            }
            const auto lyrics = fetch(track);
            result += "Synchronized line count=" + std::to_string(lyrics.lines.size()) + "; " + winrt::to_string(lyrics.status) + "\n";
            if (lyrics.lines.empty()) { std::ofstream(path) << result; return 2; }
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
    if (args.size() == 2 && (args[0] == L"--probe" || args[0] == L"--network-check" || args[0] == L"--superstar-check" || args[0] == L"--taskbar-probe" || args[0] == L"--current-check" || args[0] == L"--preferences-check")) return diagnostics(args[0], args[1]);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TaskbarLyrics.Native.Prototype");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    online = std::find(args.begin(), args.end(), L"--offline") == args.end();
    docked = std::find(args.begin(), args.end(), L"--floating") == args.end();
    if (std::find(args.begin(), args.end(), L"--demo") != args.end()) setDemo(true);
    if (!demo) {
        PWSTR localData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localData))) {
            preferencesPath = std::filesystem::path(localData) / L"TaskbarLyrics" / L"Native" / L"settings.json";
            CoTaskMemFree(localData);
            try { preferencesState = preferences::load(preferencesPath); }
            catch (...) { preferencesStatus = L"设置文件无法读取，暂用默认值。"; }
        }
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = instance; wc.lpszClassName = L"TaskbarLyricsNative";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    windowHandle = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName,
        L"Taskbar Lyrics Native · 原型", WS_POPUP, 0, 0, 640, 68, nullptr, nullptr, instance, nullptr);
    if (!windowHandle) { CloseHandle(mutex); return 1; }
    SetLayeredWindowAttributes(windowHandle, TransparentKey, 255, LWA_COLORKEY);
    updateInput();
    const auto dpi = GetDpiForWindow(windowHandle);
    font = CreateFontW(-MulDiv(19, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    centerWindow(); addTray(); SetTimer(windowHandle, 1, 200, nullptr); if (!docked) placeWindow();
    std::thread background(worker);
    std::thread lyricsBackground(lyricWorker);
    // Explorer UIA providers may block indefinitely. The inspector owns its heap state;
    // it must never hold up UI shutdown or access destroyed application globals.
    std::thread taskbarBackground(barWorker, barStore);
    taskbarBackground.detach();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    stopping = true; background.join(); lyricsBackground.join();
    if (font) DeleteObject(font);
    CloseHandle(mutex);
    winrt::uninit_apartment();
    return 0;
}

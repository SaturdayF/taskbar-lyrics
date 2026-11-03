#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "settings-ui.h"
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace settingsui {
using namespace Gdiplus;
namespace {
enum Id {
    Appearance = 2000, Playback, About, Save, Cancel, Theme,
    FontLess, FontMore, Dock, Through, HidePaused, Center,
    Previous, Toggle, Next, Copy, GlobalLess, GlobalMore, GlobalReset,
    TrackLess, TrackMore, TrackReset, Online, Import, Retry, Demo
};
constexpr wchar_t WindowClass[] = L"TaskbarLyrics.Settings";
constexpr wchar_t ContentClass[] = L"TaskbarLyrics.Settings.Content";
struct Palette { Color background, sidebar, ink, muted, line, card, selected, accent; };
Palette palette(bool dark) {
    if (dark) return {Color(255, 38, 40, 44), Color(255, 28, 29, 32), Color(255, 241, 242, 244),
        Color(255, 172, 177, 186), Color(255, 62, 65, 71), Color(255, 47, 50, 55), Color(255, 63, 66, 73), Color(255, 155, 188, 247)};
    return {Color(255, 255, 255, 255), Color(255, 246, 247, 249), Color(255, 31, 33, 38),
        Color(255, 106, 112, 123), Color(255, 231, 233, 237), Color(255, 246, 247, 249), Color(255, 229, 232, 237), Color(255, 50, 105, 190)};
}
void rounded(Graphics& g, RectF r, float radius, Color fill, Color edge = Color(0, 0, 0, 0)) {
    GraphicsPath path;
    const auto diameter = std::min({radius * 2, r.Width, r.Height});
    path.AddArc(r.X, r.Y, diameter, diameter, 180, 90);
    path.AddArc(r.GetRight() - diameter, r.Y, diameter, diameter, 270, 90);
    path.AddArc(r.GetRight() - diameter, r.GetBottom() - diameter, diameter, diameter, 0, 90);
    path.AddArc(r.X, r.GetBottom() - diameter, diameter, diameter, 90, 90); path.CloseFigure();
    SolidBrush brush(fill); g.FillPath(&brush, &path);
    if (edge.GetA()) { Pen pen(edge, 1); g.DrawPath(&pen, &path); }
}
void label(Graphics& g, const std::wstring& text, RectF r, float size, Color color, bool bold = false, bool centered = false) {
    Font font(L"Microsoft YaHei UI", size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color); StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap); format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(StringAlignmentCenter); format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.DrawString(text.c_str(), -1, &font, r, &format, &brush);
}
struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr; HGDIOBJ old = nullptr; void* pixels = nullptr;
    int width, height;
    Surface(int w, int h) : width(std::max(1, w)), height(std::max(1, h)) {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap) throw std::runtime_error("Cannot allocate settings surface");
        old = SelectObject(dc, bitmap);
    }
    ~Surface() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
};
std::wstring seconds(double value) { wchar_t text[32]{}; swprintf_s(text, L"%+.1f 秒", value); return text; }
std::wstring time(double value) {
    const int total = static_cast<int>(std::clamp(value, 0.0, 360000.0));
    wchar_t text[32]{}; swprintf_s(text, L"%02d:%02d", total / 60, total % 60); return text;
}
bool isToggle(int id) { return id == Dock || id == Through || id == HidePaused || id == Online; }
}

struct Window::Impl {
    Callbacks callbacks;
    HWND window = nullptr, content = nullptr;
    std::vector<HWND> controls;
    preferences::Settings baseline, draft;
    Snapshot snapshot;
    ULONG_PTR graphicsToken = 0;
    HFONT font = nullptr;
    int dpi = 96, page = 0, scroll = 0, contentWidth = 600, viewportHeight = 500, pageHeight = 630;
    int thumbGrab = -1;
    bool preview = false, dirty = false;
    std::wstring error;
    explicit Impl(Callbacks value, bool testing) : callbacks(std::move(value)), preview(testing) {
        GdiplusStartupInput input;
        if (GdiplusStartup(&graphicsToken, &input, nullptr) != Ok) throw std::runtime_error("GDI+ initialization failed");
    }
    ~Impl() { if (window) DestroyWindow(window); if (font) DeleteObject(font); GdiplusShutdown(graphicsToken); }
    int px(double value) const { return static_cast<int>(std::round(value * dpi / 96.0)); }
    HWND control(int id) const {
        auto result = GetDlgItem(window, id); return result ? result : GetDlgItem(content, id);
    }
    bool checked(int id) const {
        switch (id) { case Dock: return draft.docked; case Through: return draft.clickThrough;
        case HidePaused: return draft.hideWhenPaused; case Online: return draft.online; default: return false; }
    }
    void button(const wchar_t* name, int id, bool root = false) {
        HWND item = CreateWindowExW(0, L"BUTTON", name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, root ? window : content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!item) throw std::runtime_error("Cannot create settings control");
        SetWindowSubclass(item, buttonProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        controls.push_back(item);
    }
    void create() {
        snapshot = callbacks.read(); baseline = draft = snapshot.preferences; dirty = false; error.clear(); page = 0; scroll = 0;
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(1));
        cls.lpfnWndProc = procedure; cls.lpszClassName = WindowClass; RegisterClassExW(&cls);
        cls.lpszClassName = ContentClass; RegisterClassExW(&cls);
        window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, WindowClass, L"Taskbar Lyrics · 设置",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 740,
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!window) throw std::runtime_error("Cannot create settings window");
        dpi = static_cast<int>(GetDpiForWindow(window));
        content = CreateWindowExW(WS_EX_CONTROLPARENT, ContentClass, L"设置内容", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr), this);
        button(L"外观", Appearance, true); button(L"歌词与播放", Playback, true); button(L"关于", About, true);
        button(L"保存更改", Save, true); button(L"取消", Cancel, true);
        button(L"切换设置页主题", Theme); button(L"减小字号", FontLess); button(L"增大字号", FontMore);
        button(L"固定在任务栏", Dock); button(L"鼠标穿透", Through); button(L"暂停或无播放时隐藏", HidePaused);
        button(L"恢复居中", Center);
        button(L"上一首", Previous); button(L"播放 / 暂停", Toggle); button(L"下一首", Next); button(L"复制歌词", Copy);
        button(L"全局延后 0.5 秒", GlobalLess); button(L"全局提前 0.5 秒", GlobalMore); button(L"重置", GlobalReset);
        button(L"单曲延后 0.5 秒", TrackLess); button(L"单曲提前 0.5 秒", TrackMore); button(L"重置", TrackReset);
        button(L"在线匹配歌词", Online); button(L"导入本地 LRC", Import); button(L"重新匹配", Retry); button(L"开启离线演示", Demo);
        updateFont(); applyTheme(); layout(); refresh(); SetTimer(window, 1, 500, nullptr);
    }
    void updateFont() {
        if (font) DeleteObject(font);
        font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        for (auto item : controls) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    void applyTheme() {
        BOOL dark = draft.darkTheme; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        int corners = 2; DwmSetWindowAttribute(window, 33, &corners, sizeof(corners));
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void move(int id, int x, int y, int width, int height, bool visible = true) {
        auto item = control(id);
        if (GetParent(item) == content) y -= scroll;
        SetWindowPos(item, nullptr, px(x), px(y), px(width), px(height), SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    void layout() {
        if (!content) return;
        RECT rect{}; GetClientRect(window, &rect);
        const int width = MulDiv(rect.right, 96, dpi), height = MulDiv(rect.bottom, 96, dpi);
        viewportHeight = std::max(100, height - 104); contentWidth = std::max(400, width - 250);
        MoveWindow(content, px(218), px(24), px(contentWidth + 16), px(viewportHeight), TRUE);
        RECT inner{}; GetClientRect(content, &inner); contentWidth = MulDiv(inner.right, 96, dpi) - 16;
        pageHeight = page == 0 ? 650 : page == 1 ? 660 : 560;
        scroll = std::clamp(scroll, 0, std::max(0, pageHeight - viewportHeight));
        for (auto item : controls) if (GetParent(item) == content) {
            const int id = GetDlgCtrlID(item);
            const bool visible = page == 0 ? (id >= Theme && id <= Center) : page == 1 ? (id >= Previous && id <= Retry) : id == Demo;
            if (!visible) ShowWindow(item, SW_HIDE);
        }
        for (int i = 0; i < 3; ++i) move(Appearance + i, 16, 88 + i * 46, 164, 38);
        move(Cancel, width - 246, height - 59, 94, 36); move(Save, width - 140, height - 59, 112, 36);
        const auto right = contentWidth;
        if (page == 0) {
            move(Theme, right - 132, 3, 132, 34);
            move(FontLess, right - 164, 100, 36, 34); move(FontMore, right - 36, 100, 36, 34);
            move(Dock, right - 48, 328, 46, 28); move(Through, right - 48, 408, 46, 28);
            move(HidePaused, right - 48, 488, 46, 28); move(Center, right - 112, 570, 112, 34);
        } else if (page == 1) {
            move(Previous, 0, 244, 88, 34); move(Toggle, 98, 244, 88, 34);
            move(Next, 196, 244, 88, 34); move(Copy, right - 100, 244, 100, 34);
            move(GlobalLess, right - 246, 318, 34, 34); move(GlobalMore, right - 118, 318, 34, 34); move(GlobalReset, right - 74, 318, 74, 34);
            move(TrackLess, right - 246, 406, 34, 34); move(TrackMore, right - 118, 406, 34, 34); move(TrackReset, right - 74, 406, 74, 34);
            move(Online, right - 48, 496, 46, 28);
            move(Import, 0, 580, 140, 36); move(Retry, 152, 580, 112, 36);
        } else move(Demo, right - 140, 450, 140, 36);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void refresh() {
        if (!content) return;
        snapshot = callbacks.read();
        if (!dirty && baseline != snapshot.preferences) {
            baseline = draft = snapshot.preferences; applyTheme();
        }
        EnableWindow(control(Previous), snapshot.canPrevious); EnableWindow(control(Toggle), snapshot.canToggle);
        EnableWindow(control(Next), snapshot.canNext); EnableWindow(control(Copy), snapshot.canCopy);
        for (int id : {TrackLess, TrackMore, TrackReset, Import, Retry}) EnableWindow(control(id), !snapshot.key.empty());
        EnableWindow(control(FontLess), draft.fontSize > 12); EnableWindow(control(FontMore), draft.fontSize < 26);
        EnableWindow(control(GlobalLess), draft.globalOffset > -10); EnableWindow(control(GlobalMore), draft.globalOffset < 10);
        if (!snapshot.key.empty()) {
            EnableWindow(control(TrackLess), draft.trackOffset(snapshot.key) > -10);
            EnableWindow(control(TrackMore), draft.trackOffset(snapshot.key) < 10);
        }
        SetWindowTextW(control(Toggle), snapshot.playing ? L"暂停" : L"播放");
        SetWindowTextW(control(Demo), snapshot.demo ? L"退出演示" : L"开启离线演示");
        EnableWindow(control(Save), dirty);
        for (const auto& item : std::vector<std::pair<int, std::wstring>>{{Dock, L"固定在任务栏"}, {Through, L"鼠标穿透"}, {HidePaused, L"暂停或无播放时隐藏"}, {Online, L"在线匹配歌词"}})
            SetWindowTextW(control(item.first), (item.second + (checked(item.first) ? L"，已开启" : L"，已关闭")).c_str());
        InvalidateRect(content, nullptr, FALSE);
    }
    void changed() { dirty = draft != baseline; error.clear(); refresh(); InvalidateRect(window, nullptr, FALSE); }
    void action(int id) {
        if (id >= Appearance && id <= About) { page = id - Appearance; scroll = 0; layout(); return; }
        if (id == Cancel || id == IDCANCEL) { DestroyWindow(window); return; }
        if (id == Save) {
            error = callbacks.save(baseline, draft);
            if (error.empty()) DestroyWindow(window);
            else InvalidateRect(window, nullptr, FALSE);
            return;
        }
        switch (id) {
        case Theme: draft.darkTheme = !draft.darkTheme; applyTheme(); break;
        case FontLess: draft.fontSize = std::max(12, draft.fontSize - 1); break;
        case FontMore: draft.fontSize = std::min(26, draft.fontSize + 1); break;
        case Dock: draft.docked = !draft.docked; break;
        case Through: draft.clickThrough = !draft.clickThrough; break;
        case HidePaused: draft.hideWhenPaused = !draft.hideWhenPaused; break;
        case Online: draft.online = !draft.online; break;
        case GlobalLess: case GlobalMore: draft.globalOffset = preferences::clamp(draft.globalOffset + (id == GlobalMore ? .5 : -.5)); break;
        case GlobalReset: draft.globalOffset = 0; break;
        case TrackLess: case TrackMore: case TrackReset:
            if (!snapshot.key.empty()) draft.setTrackOffset(snapshot.key, id == TrackReset ? 0 : draft.trackOffset(snapshot.key) + (id == TrackMore ? .5 : -.5));
            break;
        default: {
            Action command;
            switch (id) {
            case Previous: command = Action::Previous; break; case Toggle: command = Action::Toggle; break;
            case Next: command = Action::Next; break; case Copy: command = Action::Copy; break;
            case Import: command = Action::Import; break; case Retry: command = Action::Retry; break;
            case Center: command = Action::Center; break; case Demo: command = Action::Demo; break; default: return;
            }
            callbacks.action(command); refresh(); return;
        }
        }
        changed();
    }
    void draw(HDC dc, bool inner) {
        RECT rect{}; GetClientRect(inner ? content : window, &rect);
        if (rect.right <= 0 || rect.bottom <= 0) return;
        Surface buffer(rect.right, rect.bottom);
        {
            Graphics g(buffer.dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
            auto colors = palette(draft.darkTheme); g.Clear(colors.background);
            const float scale = static_cast<float>(dpi) / 96.f; g.ScaleTransform(scale, scale);
            const float width = static_cast<float>(rect.right) / scale, height = static_cast<float>(rect.bottom) / scale;
            Pen divider(colors.line, 1);
            if (!inner) {
                SolidBrush sidebar(colors.sidebar); g.FillRectangle(&sidebar, RectF(0, 0, 196, height));
                g.DrawLine(&divider, 196.f, 0.f, 196.f, height);
                label(g, L"设置", {28, 26, 150, 46}, 28, colors.ink, true);
                label(g, L"Taskbar Lyrics", {24, height - 92, 156, 26}, 14, colors.muted);
                label(g, Version, {24, height - 64, 150, 24}, 12, colors.muted);
                g.DrawLine(&divider, 218.f, height - 78, width - 28, height - 78);
                const auto footer = !error.empty() ? error : snapshot.demo ? L"演示数据 · 更改仅本次有效" : dirty ? L"有未保存的更改" : L"偏好在保存后生效";
                label(g, footer, {218, height - 61, width - 478, 38}, 12, error.empty() ? colors.muted : Color(255, 216, 108, 89));
            } else {
                auto contentTransform = g.Save();
                g.TranslateTransform(0, -static_cast<float>(scroll));
                const float right = static_cast<float>(contentWidth);
                const wchar_t* titles[] = {L"外观", L"歌词与播放", L"关于"};
                const wchar_t* descriptions[] = {L"调整任务栏歌词的显示方式。", L"控制播放，让歌词与音乐同步。", L"连接状态、快捷操作与应用信息。"};
                label(g, titles[page], {0, 0, right - (page == 0 ? 150 : 0), 40}, 27, colors.ink, true);
                label(g, descriptions[page], {0, 44, right, 28}, 13, colors.muted);
                auto row = [&](float y, const std::wstring& title, const std::wstring& hint, float reserve = 150) {
                    label(g, title, {0, y, right - reserve, 27}, 16, colors.ink);
                    label(g, hint, {0, y + 30, right, 26}, 12, colors.muted);
                    g.DrawLine(&divider, 0.f, y + 70, right, y + 70);
                };
                if (page == 0) {
                    row(98, L"歌词字号", L"预览随字号变化；保存后应用到任务栏。", 190);
                    label(g, std::to_wstring(draft.fontSize) + L" px", {right - 128, 100, 92, 34}, 14, colors.ink, false, true);
                    rounded(g, {0, 194, right, 102}, 12, colors.card);
                    const bool simulated = snapshot.lyric.empty() || snapshot.demo;
                    label(g, simulated ? L"歌词预览 · 模拟数据" : L"歌词预览 · 当前歌曲", {18, 206, right - 36, 22}, 11, colors.muted);
                    label(g, snapshot.lyric.empty() ? L"音乐在播放，歌词在身旁" : snapshot.lyric,
                        {18, 238, right - 36, 40}, static_cast<float>(draft.fontSize), colors.ink, false, true);
                    row(324, L"固定在任务栏", L"垂直居中，仅左右拖动；关闭后使用悬浮窗口。");
                    row(404, L"鼠标穿透", L"点击交给下方任务栏；从托盘或此处关闭后可拖动。");
                    row(484, L"暂停或无播放时隐藏", L"播放恢复后自动显示，托盘控制始终可用。");
                    row(566, L"水平位置", L"恢复居中立即执行，歌词背景始终透明。");
                } else if (page == 1) {
                    rounded(g, {0, 90, right, 136}, 12, colors.card);
                    label(g, snapshot.demo ? L"演示数据" : snapshot.key.empty() ? L"等待播放器" : snapshot.playing ? L"正在播放" : L"已暂停",
                        {18, 101, right - 36, 24}, 12, colors.accent);
                    label(g, snapshot.title.empty() ? L"打开播放器，播放一首歌" : snapshot.title, {18, 132, right - 36, 30}, 19, colors.ink, true);
                    label(g, snapshot.artist.empty() ? L"QQ 音乐 · 网易云音乐 · Apple Music" : snapshot.artist + L" · " + snapshot.album,
                        {18, 167, right - 36, 24}, 12, colors.muted);
                    rounded(g, {18, 207, right - 120, 3}, 1.5f, colors.line);
                    if (snapshot.duration > 0) rounded(g, {18, 207, std::max(1.f, (right - 120) * static_cast<float>(std::clamp(snapshot.position / snapshot.duration, 0.0, 1.0))), 3}, 1.5f, colors.accent);
                    label(g, time(snapshot.position), {right - 92, 194, 74, 27}, 12, colors.muted, false, true);
                    row(314, L"全局歌词校准", L"用于所有歌曲，正值提前出现，负值延后。", 264);
                    label(g, seconds(draft.globalOffset), {right - 212, 318, 94, 34}, 13, colors.ink, false, true);
                    row(402, L"当前歌曲校准", snapshot.key.empty() ? L"播放歌曲后可调整。" : L"校准按歌曲独立保存，与全局值叠加。", 264);
                    label(g, snapshot.key.empty() ? L"—" : seconds(draft.trackOffset(snapshot.key)), {right - 212, 406, 94, 34}, 13, colors.ink, false, true);
                    row(492, L"在线匹配歌词", L"向 LRCLIB / 网易发送歌曲信息；关闭后可导入 LRC。");
                    label(g, L"导入及重新匹配立即执行。切歌后单曲草稿仍保留至保存。", {0, 624, right, 24}, 11, colors.muted);
                } else {
                    rounded(g, {0, 90, right, 150}, 12, colors.card);
                    label(g, L"连接与歌词状态", {18, 105, right - 36, 28}, 16, colors.ink, true);
                    label(g, snapshot.status.empty() ? L"等待媒体会话" : snapshot.status, {18, 144, right - 36, 28}, 13, colors.muted);
                    label(g, L"播放器需开启系统媒体控制（SMTC）。", {18, 184, right - 36, 28}, 12, colors.muted);
                    row(264, L"Ctrl + 滚轮", L"在歌词栏调整当前歌曲校准，每格 0.5 秒。", 0);
                    row(344, L"Ctrl + Shift + 滚轮", L"调整全局校准；右键可复制歌词或控制播放。", 0);
                    row(446, L"离线演示", L"使用模拟歌曲验证显示和交互，不请求在线歌词。", 160);
                    label(g, L"Taskbar Lyrics 0.3.0  ·  C++  ·  Windows 10 / 11", {0, 530, right, 24}, 12, colors.muted);
                }
                g.Restore(contentTransform);
                const auto thumb = scrollThumb();
                if (thumb.bottom > thumb.top) rounded(g, {static_cast<float>(thumb.left) / scale, static_cast<float>(thumb.top) / scale,
                    static_cast<float>(thumb.right - thumb.left) / scale, static_cast<float>(thumb.bottom - thumb.top) / scale}, 2, colors.muted);
            }
        }
        BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer.dc, 0, 0, SRCCOPY);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        const int id = static_cast<int>(item.CtlID);
        const bool navigation = id >= Appearance && id <= About;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const auto colors = palette(draft.darkTheme);
        const int width = item.rcItem.right - item.rcItem.left, height = item.rcItem.bottom - item.rcItem.top;
        Surface buffer(width, height);
        {
            Graphics g(buffer.dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.Clear(navigation ? colors.sidebar : colors.background);
            const float d = static_cast<float>(dpi) / 96.f; g.ScaleTransform(d, d);
            RectF bounds(.5f, .5f, static_cast<float>(width) / d - 1, static_cast<float>(height) / d - 1);
            if (isToggle(id)) {
                const bool on = checked(id);
                rounded(g, {1, 1, bounds.Width - 1, bounds.Height - 1}, 14, on ? colors.accent : (draft.darkTheme ? Color(255, 91, 95, 103) : Color(255, 181, 185, 193)));
                const float x = on ? bounds.Width - 23 : 4;
                rounded(g, {x, 4, 20, 20}, 10, Color(255, 255, 255, 255));
            } else {
                Color ink = disabled ? colors.muted : colors.ink;
                if (navigation) {
                    if (id - Appearance == page || pressed) rounded(g, bounds, 8, colors.selected);
                } else {
                    const bool primary = id == Save && !disabled;
                    rounded(g, bounds, 8, primary ? colors.ink : pressed ? colors.selected : colors.card, primary ? colors.ink : colors.line);
                    if (primary) ink = colors.background;
                }
                wchar_t text[128]{}; GetWindowTextW(item.hwndItem, text, 128);
                std::wstring name = text;
                if (id == Theme) name = draft.darkTheme ? L"深色模式" : L"浅色模式";
                if (id == FontLess || id == GlobalLess || id == TrackLess) name = L"−";
                if (id == FontMore || id == GlobalMore || id == TrackMore) name = L"+";
                label(g, name, {navigation ? 14.f : 4.f, 0, bounds.Width - (navigation ? 22 : 8), bounds.Height},
                    navigation ? 15.f : 13.f, ink, id == Save, !navigation);
            }
            if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
                Pen focus(colors.accent, 1); g.DrawRectangle(&focus, RectF(2, 2, bounds.Width - 4, bounds.Height - 4));
            }
        }
        BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, buffer.dc, 0, 0, SRCCOPY);
    }
    void scrollTo(int value) {
        const auto next = std::clamp(value, 0, std::max(0, pageHeight - viewportHeight));
        if (next == scroll) return;
        scroll = next; layout();
    }
    RECT scrollThumb() const {
        if (pageHeight <= viewportHeight) return {};
        RECT rect{}; GetClientRect(content, &rect);
        const int track = rect.bottom - px(12);
        const int length = std::max(px(30), track * viewportHeight / pageHeight);
        const int top = px(6) + (track - length) * scroll / (pageHeight - viewportHeight);
        return {rect.right - px(7), top, rect.right - px(3), top + length};
    }
    static LRESULT CALLBACK buttonProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Impl*>(data);
        if (message == WM_MOUSEWHEEL) return SendMessageW(self->content, message, wp, lp);
        if (message == WM_SETFOCUS && GetParent(hwnd) == self->content) {
            RECT rect{}; GetWindowRect(hwnd, &rect); MapWindowPoints(nullptr, self->content, reinterpret_cast<POINT*>(&rect), 2);
            if (rect.top < 0) self->scrollTo(self->scroll + MulDiv(rect.top, 96, self->dpi) - 8);
            else if (rect.bottom > self->px(self->viewportHeight))
                self->scrollTo(self->scroll + MulDiv(rect.bottom, 96, self->dpi) - self->viewportHeight + 8);
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, buttonProcedure, 1);
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        const bool inner = hwnd == self->content;
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT paint{}; auto dc = BeginPaint(hwnd, &paint); self->draw(dc, inner); EndPaint(hwnd, &paint); return 0; }
        case WM_PRINTCLIENT: self->draw(reinterpret_cast<HDC>(wp), inner); return 0;
        case WM_DRAWITEM: self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_COMMAND: self->action(LOWORD(wp)); return 0;
        case WM_SIZE: if (!inner) self->layout(); return 0;
        case WM_TIMER: self->refresh(); return 0;
        case WM_MOUSEWHEEL: self->scrollTo(self->scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 64); return 0;
        case WM_LBUTTONDOWN: if (inner) {
            const auto thumb = self->scrollThumb();
            POINT pointer{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (thumb.bottom > thumb.top && pointer.x >= thumb.left - self->px(5)) {
                if (PtInRect(&thumb, pointer)) { self->thumbGrab = pointer.y - thumb.top; SetCapture(hwnd); }
                else self->scrollTo(self->scroll + (pointer.y < thumb.top ? -self->viewportHeight : self->viewportHeight));
                return 0;
            }
            break;
        } break;
        case WM_MOUSEMOVE: if (inner && self->thumbGrab >= 0) {
            RECT rect{}; GetClientRect(hwnd, &rect); const auto thumb = self->scrollThumb();
            const int travel = rect.bottom - self->px(12) - (thumb.bottom - thumb.top);
            if (travel > 0) self->scrollTo((GET_Y_LPARAM(lp) - self->thumbGrab - self->px(6)) * (self->pageHeight - self->viewportHeight) / travel);
            return 0;
        } break;
        case WM_LBUTTONUP: if (self->thumbGrab >= 0) { self->thumbGrab = -1; ReleaseCapture(); } return 0;
        case WM_CAPTURECHANGED: self->thumbGrab = -1; return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_TRACKPOS}; GetScrollInfo(hwnd, SB_VERT, &info);
            int next = self->scroll;
            switch (LOWORD(wp)) {
            case SB_LINEUP: next -= 32; break; case SB_LINEDOWN: next += 32; break;
            case SB_PAGEUP: next -= self->viewportHeight; break; case SB_PAGEDOWN: next += self->viewportHeight; break;
            case SB_THUMBPOSITION: case SB_THUMBTRACK: next = info.nTrackPos; break;
            case SB_TOP: next = 0; break; case SB_BOTTOM: next = self->pageHeight; break;
            }
            self->scrollTo(next); return 0;
        }
        case WM_GETMINMAXINFO: {
            auto limits = reinterpret_cast<MINMAXINFO*>(lp);
            limits->ptMinTrackSize = {self->px(780), self->px(540)}; return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp); self->updateFont();
            const auto rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout(); return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(Save, DC_HASDEFID);
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_NCDESTROY:
            if (hwnd == self->window) { KillTimer(hwnd, 1); self->window = nullptr; self->content = nullptr; self->controls.clear(); }
            break;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};

Window::Window(Callbacks callbacks, bool preview) : impl(std::make_unique<Impl>(std::move(callbacks), preview)) {}
Window::~Window() = default;
HWND Window::handle() const { return impl->window; }
void Window::show() {
    if (!impl->window) {
        impl->create();
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(impl->window, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const int width = std::min(impl->px(1000), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) - 24);
        const int height = std::min(impl->px(740), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - 24);
        SetWindowPos(impl->window, nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width, height, SWP_NOZORDER | SWP_SHOWWINDOW);
        impl->layout();
    } else ShowWindow(impl->window, IsIconic(impl->window) ? SW_RESTORE : SW_SHOW);
    if (!impl->preview) SetForegroundWindow(impl->window);
}
bool Window::translate(MSG& message) {
    if (!impl->window || (message.hwnd != impl->window && !IsChild(impl->window, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == 'S' && GetKeyState(VK_CONTROL) < 0) { impl->action(Save); return true; }
    return IsDialogMessageW(impl->window, &message) != FALSE;
}
void Window::selectPage(int page) { impl->action(Appearance + std::clamp(page, 0, 2)); }
void Window::scrollTo(int offset) { impl->scrollTo(offset); }
void Window::capture(const std::filesystem::path& file) {
    RedrawWindow(impl->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RECT rect{}; GetClientRect(impl->window, &rect);
    Surface image(rect.right, rect.bottom);
    impl->draw(image.dc, false);
    std::function<void(HWND)> children = [&](HWND parent) {
        for (auto child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!IsWindowVisible(child)) continue;
            RECT bounds{}; GetClientRect(child, &bounds); POINT origin{}; MapWindowPoints(child, impl->window, &origin, 1);
            const int saved = SaveDC(image.dc);
            SetViewportOrgEx(image.dc, origin.x, origin.y, nullptr);
            IntersectClipRect(image.dc, 0, 0, bounds.right, bounds.bottom);
            if (child == impl->content) { impl->draw(image.dc, true); children(child); }
            else {
                DRAWITEMSTRUCT item{}; item.CtlType = ODT_BUTTON; item.CtlID = static_cast<UINT>(GetDlgCtrlID(child));
                item.hwndItem = child; item.hDC = image.dc; item.rcItem = bounds;
                if (!IsWindowEnabled(child)) item.itemState |= ODS_DISABLED;
                impl->drawButton(item);
            }
            RestoreDC(image.dc, saved);
        }
    };
    children(impl->window);
    GdiFlush();
    auto bytes = static_cast<BYTE*>(image.pixels);
    for (size_t i = 3; i < static_cast<size_t>(image.width) * image.height * 4; i += 4) bytes[i] = 255;
    Bitmap bitmap(image.width, image.height, image.width * 4, PixelFormat32bppARGB, bytes);
    CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    if (bitmap.Save(file.c_str(), &png, nullptr) != Ok) throw std::runtime_error("Cannot save settings preview");
}

int checkUi(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream log(directory / L"checks.txt"); int passed = 0;
    auto check = [&](bool valid, const char* text) {
        if (!valid) throw std::runtime_error(text);
        log << "PASS: " << text << '\n'; ++passed;
    };
    try {
        Snapshot sample; sample.key = L"demo-a"; sample.title = L"演示歌曲"; sample.artist = L"Taskbar Lyrics";
        sample.album = L"模拟数据"; sample.lyric = L"音乐在播放，歌词在身旁"; sample.status = L"演示模式：歌曲与进度均为模拟数据。";
        sample.demo = true; sample.playing = true; sample.canToggle = true; sample.canCopy = true; sample.duration = 240; sample.position = 82;
        int saves = 0, actions = 0; bool rejectSave = false;
        Window window({[&] { return sample; }, [&](const auto& base, const auto& draft) -> std::wstring {
            if (rejectSave) return L"模拟保存失败";
            sample.preferences = preferences::mergeDraft(sample.preferences, base, draft); ++saves; return L"";
        }, [&](Action) { ++actions; }}, true);
        auto command = [&](int id) { SendMessageW(window.handle(), WM_COMMAND, id, 0); };
        window.show(); window.capture(directory / L"appearance-dark.png");
        const auto first = window.handle(); window.show(); check(first == window.handle(), "reopening reuses one settings window");
        command(FontMore); check(sample.preferences.fontSize == 19 && saves == 0, "editing leaves live settings unchanged");
        command(Cancel); check(!window.handle() && saves == 0, "cancel discards draft without saving");
        window.show(); command(FontMore); sample.preferences.hideWhenPaused = true;
        command(Save); check(sample.preferences.fontSize == 20 && sample.preferences.hideWhenPaused, "save merges edited fields and preserves tray updates");
        window.show(); window.selectPage(1); window.capture(directory / L"playback-dark.png");
        command(TrackMore); sample.key = L"demo-b"; SendMessageW(window.handle(), WM_TIMER, 1, 0); command(TrackLess);
        command(Save); check(sample.preferences.trackOffset(L"demo-a") == .5 && sample.preferences.trackOffset(L"demo-b") == -.5, "track drafts survive song changes without crossing recordings");
        window.show(); command(Theme); window.capture(directory / L"appearance-light.png");
        window.selectPage(2); window.capture(directory / L"about-light.png");
        command(Retry); check(actions == 1, "immediate playback or source actions reach application callback");
        rejectSave = true; command(Save); check(window.handle() != nullptr && sample.preferences.darkTheme, "failed save retains draft and window");
        rejectSave = false; command(Save); check(!sample.preferences.darkTheme && !window.handle(), "successful save applies theme and closes");
        window.show();
        const auto dpi = GetDpiForWindow(window.handle());
        SetWindowPos(window.handle(), nullptr, 0, 0, MulDiv(800, dpi, 96), MulDiv(560, dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
        window.scrollTo(10000); window.capture(directory / L"appearance-compact.png");
        auto content = FindWindowExW(window.handle(), nullptr, ContentClass, nullptr);
        RECT resetBounds{}, viewport{}; GetWindowRect(GetDlgItem(content, Center), &resetBounds); GetWindowRect(content, &viewport);
        check(resetBounds.top >= viewport.top && resetBounds.bottom <= viewport.bottom, "compact window scrolls to the last setting");
        window.scrollTo(0); SetFocus(GetDlgItem(content, Center));
        GetWindowRect(GetDlgItem(content, Center), &resetBounds);
        check(resetBounds.bottom <= viewport.bottom, "keyboard focus reveals a setting below the fold");
        for (int page = 0; page < 3; ++page) {
            window.selectPage(page);
            check(IsWindowVisible(GetDlgItem(window.handle(), Save)) && IsWindowVisible(GetDlgItem(window.handle(), Cancel)), "footer remains accessible on every page");
        }
        for (UINT scale : {96u, 120u, 144u, 192u}) {
            RECT position{}; GetWindowRect(window.handle(), &position);
            position.right = position.left + MulDiv(800, scale, 96); position.bottom = position.top + MulDiv(560, scale, 96);
            SendMessageW(window.handle(), WM_DPICHANGED, MAKEWPARAM(scale, scale), reinterpret_cast<LPARAM>(&position));
            RECT client{}, save{}; GetClientRect(window.handle(), &client); GetWindowRect(GetDlgItem(window.handle(), Save), &save);
            MapWindowPoints(nullptr, window.handle(), reinterpret_cast<POINT*>(&save), 2);
            check(save.left >= 0 && save.top >= 0 && save.right <= client.right && save.bottom <= client.bottom,
                "save button stays within client area at simulated DPI");
        }
        command(Cancel); log << passed << " UI checks passed. Screenshots use simulated data.\n"; return 0;
    } catch (const std::exception& ex) { log << "FAIL: " << ex.what() << '\n'; return 1; }
}
}

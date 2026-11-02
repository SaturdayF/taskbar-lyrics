using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using TaskbarLyrics.Core;
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox;

namespace TaskbarLyrics.App;

public sealed class Settings
{
    public PlayerKind Player { get; set; }
    public double Offset { get; set; }
    public double FontSize { get; set; } = 16;
    public double HorizontalOffset { get; set; }
    public bool Online { get; set; } = true;
    public bool Locked { get; set; }
}

public static class Program
{
    public static readonly string DataDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TaskbarLyrics");
    [STAThread]
    public static void Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--render-previews")
        {
            var previewApp = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            try { PreviewRenderer.Render(args[1]); }
            catch (Exception ex) { Directory.CreateDirectory(args[1]); File.WriteAllText(Path.Combine(args[1], "error.txt"), ex.ToString()); Environment.ExitCode = 1; }
            previewApp.Shutdown();
            return;
        }
        Directory.CreateDirectory(DataDirectory);
        using var mutex = new Mutex(true, "Local\\TaskbarLyrics-0.1", out var first);
        if (!first && !args.Contains("--probe") && !args.Contains("--render-previews")) return;
        if (args.Contains("--probe"))
        {
            try { File.WriteAllLines(Path.Combine(DataDirectory, "sessions.txt"), new MediaReader().ProbeAsync().GetAwaiter().GetResult()); }
            catch (Exception ex) { File.WriteAllText(Path.Combine(DataDirectory, "sessions.txt"), ex.ToString()); }
            return;
        }
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.DispatcherUnhandledException += (_, e) =>
        {
            File.WriteAllText(Path.Combine(DataDirectory, "last-error.txt"), e.Exception.ToString());
            MessageBox.Show("运行出错，详情已保存到应用数据目录。\n" + e.Exception.Message, "Taskbar Lyrics");
            e.Handled = true;
            app.Shutdown(1);
        };
        var overlay = new LyricsWindow(args.Contains("--demo"));
        app.Run(overlay);
    }
}

public sealed class LyricsWindow : Window
{
    private readonly TextBlock lyric = new() { Text = "♫  Taskbar Lyrics · 等待音乐", TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center, TextAlignment = TextAlignment.Center };
    private readonly MediaReader reader = new();
    private readonly LyricService service = new(Path.Combine(Program.DataDirectory, "lyrics"));
    private readonly Settings settings;
    private readonly System.Windows.Forms.NotifyIcon tray;
    private readonly DispatcherTimer poll = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private readonly DispatcherTimer tick = new() { Interval = TimeSpan.FromMilliseconds(80) };
    private readonly Stopwatch positionClock = Stopwatch.StartNew();
    private readonly bool demo;
    private bool polling, exiting, dragging;
    private Playback? playback;
    private LyricResult? lyrics;
    private CancellationTokenSource? lookup;
    private SettingsWindow? settingsWindow;
    private int dragStartX;
    private double dragStartOffset;
    private readonly Border lyricBorder;
    private string status = "打开 QQ 音乐、网易云音乐或 Apple Music，并启用系统媒体控制。";
    private DateTime lastAttempt;
    private static string SettingsPath => Path.Combine(Program.DataDirectory, "settings.json");

    public LyricsWindow(bool demo)
    {
        this.demo = demo;
        try { settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath)) ?? new(); }
        catch (Exception ex) when (ex is IOException or JsonException) { settings = new(); }
        settings.FontSize = Math.Clamp(double.IsFinite(settings.FontSize) ? settings.FontSize : 16, 11, 28);
        settings.Offset = Math.Clamp(double.IsFinite(settings.Offset) ? settings.Offset : 0, -10, 10);
        if (!Enum.IsDefined(settings.Player)) settings.Player = PlayerKind.Auto;
        Width = 460; Height = 36;
        Title = "Taskbar Lyrics"; WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize;
        AllowsTransparency = true; Background = Brushes.Transparent; Topmost = true; ShowInTaskbar = false; ShowActivated = false;
        lyric.FontFamily = new FontFamily("Microsoft YaHei UI, Segoe UI");
        lyric.FontSize = settings.FontSize; lyric.LineHeight = settings.FontSize + 2;
        lyric.LineStackingStrategy = LineStackingStrategy.BlockLineHeight; lyric.Foreground = Brushes.White;
        lyricBorder = new Border { Background = new SolidColorBrush(Color.FromArgb(210, 24, 24, 24)), BorderBrush = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10), Padding = new Thickness(14, 0, 14, 0), Child = lyric };
        Content = lyricBorder;
        Cursor = settings.Locked ? Cursors.Arrow : Cursors.SizeWE;
        MouseEnter += (_, _) => lyricBorder.Background = new SolidColorBrush(Color.FromArgb(245, 38, 38, 38));
        MouseLeave += (_, _) => lyricBorder.Background = new SolidColorBrush(Color.FromArgb(210, 24, 24, 24));
        var menu = new ContextMenu();
        AddMenu(menu, "设置与当前歌曲", ShowSettings);
        AddMenu(menu, "导入当前歌曲的 LRC…", Import);
        AddMenu(menu, "重新获取歌词", () => { if (playback is not null) _ = LoadLyricsAsync(playback.Track); });
        AddMenu(menu, "恢复任务栏居中", Center);
        AddMenu(menu, "退出", Exit);
        ContextMenu = menu;
        MouseLeftButtonDown += (_, e) =>
        {
            if (e.ClickCount == 2) { ShowSettings(); return; }
            if (settings.Locked) return;
            if (!GetCursorPos(out var cursor)) return;
            dragStartX = cursor.X;
            dragStartOffset = settings.HorizontalOffset;
            dragging = CaptureMouse();
            e.Handled = true;
        };
        MouseMove += (_, e) =>
        {
            if (!dragging) return;
            if (e.LeftButton != MouseButtonState.Pressed) { ReleaseMouseCapture(); return; }
            if (!GetCursorPos(out var cursor)) return;
            var scale = PresentationSource.FromVisual(this)?.CompositionTarget?.TransformFromDevice.M11 ?? 1;
            settings.HorizontalOffset = dragStartOffset + (cursor.X - dragStartX) * scale;
            Place();
        };
        MouseLeftButtonUp += (_, _) => { if (dragging) ReleaseMouseCapture(); };
        LostMouseCapture += (_, _) => { if (dragging) { dragging = false; Place(); Save(); } };
        SourceInitialized += (_, _) =>
        {
            var handle = new WindowInteropHelper(this).Handle;
            SetWindowLongPtr(handle, -20, GetWindowLongPtr(handle, -20) | 0x08000080); // No activate; tool window.
            Place();
        };
        tray = new System.Windows.Forms.NotifyIcon { Icon = System.Drawing.SystemIcons.Information, Text = "Taskbar Lyrics", Visible = true };
        var trayMenu = new System.Windows.Forms.ContextMenuStrip();
        trayMenu.Items.Add("设置与当前歌曲", null, (_, _) => Dispatcher.Invoke(ShowSettings));
        trayMenu.Items.Add("恢复任务栏居中", null, (_, _) => Dispatcher.Invoke(Center));
        trayMenu.Items.Add("退出", null, (_, _) => Dispatcher.Invoke(Exit));
        tray.ContextMenuStrip = trayMenu;
        tray.DoubleClick += (_, _) => Dispatcher.Invoke(ShowSettings);
        poll.Tick += async (_, _) => await PollAsync();
        tick.Tick += (_, _) => RenderLine();
        Closed += (_, _) => { if (!exiting) Exit(); };
        Loaded += async (_, _) =>
        {
            poll.Start(); tick.Start();
            if (demo)
            {
                playback = new(new(PlayerKind.AppleMusic, "演示歌曲", "Taskbar Lyrics", "演示数据", 24), 0, true, 1, "demo");
                lyrics = new("演示 · 非真实播放", "[00:00.00]♫  音乐在播放，歌词在身旁\n[00:05.00]QQ 音乐 · 网易云音乐 · Apple Music\n[00:10.00]双击打开设置，拖动调整位置\n[00:15.00]此处为演示歌词，可随时退出\n[00:20.00]Taskbar Lyrics · 让音乐留在任务栏");
                status = "演示模式：歌词和播放进度均为模拟数据。"; positionClock.Restart();
            }
            else await PollAsync();
            ShowSettings();
        };
    }
    private static void AddMenu(ContextMenu menu, string text, Action action)
    {
        var item = new MenuItem { Header = text }; item.Click += (_, _) => action(); menu.Items.Add(item);
    }
    private async Task PollAsync()
    {
        Place();
        if (demo || polling || exiting) return;
        polling = true;
        try
        {
            var next = await reader.ReadAsync(settings.Player);
            if (exiting) return;
            var changed = next?.Track.Key != playback?.Track.Key;
            playback = next; positionClock.Restart();
            if (next is null)
            {
                lookup?.Cancel(); lyrics = null;
                status = "未检测到支持的媒体会话。请在播放器设置中启用系统媒体控制（SMTC）。";
            }
            else if (changed || (lyrics?.Lines.Count == 0 && DateTime.UtcNow - lastAttempt > TimeSpan.FromMinutes(2)))
            { _ = LoadLyricsAsync(next.Track); }
        }
        catch (Exception ex)
        {
            lookup?.Cancel(); playback = null; lyrics = null;
            status = "读取媒体会话失败：" + ex.Message;
        }
        finally { polling = false; }
    }
    private async Task LoadLyricsAsync(Track track)
    {
        lookup?.Cancel(); lookup?.Dispose();
        lookup = new CancellationTokenSource();
        var token = lookup.Token;
        lyrics = null; status = "正在匹配同步歌词…"; lastAttempt = DateTime.UtcNow;
        try
        {
            var result = settings.Online ? await service.FindAsync(track, token)
                : File.Exists(service.LocalPath(track)) ? new LyricResult("本地 LRC", await File.ReadAllTextAsync(service.LocalPath(track), token))
                : new LyricResult("离线模式", "", "在线匹配已关闭，请导入当前歌曲的 LRC。");
            if (token.IsCancellationRequested || playback?.Track.Key != track.Key) return;
            lyrics = result; status = result.Message ?? "已载入同步歌词";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { if (!token.IsCancellationRequested) status = "读取歌词失败：" + ex.Message; }
    }
    private void RenderLine()
    {
        var position = playback is null ? 0 : playback.Position + (playback.Playing ? positionClock.Elapsed.TotalSeconds * playback.Rate : 0);
        if (demo) position %= 24;
        if (playback?.Track.Duration > 0) position = Math.Min(position, playback.Track.Duration);
        var lines = lyrics?.Lines;
        var index = lines is null ? -1 : LrcParser.FindLine(lines, position + settings.Offset);
        lyric.Text = playback is null ? "♫  Taskbar Lyrics · 等待音乐"
            : index >= 0 ? (string.IsNullOrWhiteSpace(lines![index].Text) ? "♫  间奏" : lines[index].Text)
            : $"♫  {playback.Track.Title} · {playback.Track.Artist}";
        lyric.Opacity = playback is null || playback.Playing ? 1 : 0.65;
        var summary = playback is null ? status : $"{playback.Track.Title}\n{playback.Track.Artist} · {playback.Track.Player}\n{(playback.Playing ? "播放中" : "已暂停")}  {TimeSpan.FromSeconds(Math.Max(0, position)):mm\\:ss}\n歌词来源：{lyrics?.Source ?? "匹配中"}\n{status}";
        ToolTip = summary;
        settingsWindow?.UpdatePlayback(lyric.Text, playback, lyrics?.Source, status, position, demo);
    }
    private void Place()
    {
        var bar = FindWindow("Shell_TrayWnd", null);
        if (bar == IntPtr.Zero || !GetWindowRect(bar, out var rect)) return;
        var source = PresentationSource.FromVisual(this);
        if (source?.CompositionTarget is null) return;
        var fromDevice = source.CompositionTarget.TransformFromDevice;
        var a = fromDevice.Transform(new System.Windows.Point(rect.Left, rect.Top));
        var b = fromDevice.Transform(new System.Windows.Point(rect.Right, rect.Bottom));
        var bounds = TaskbarLayout.Calculate(a.X, a.Y, b.X - a.X, b.Y - a.Y, settings.HorizontalOffset);
        Opacity = bounds is null ? 0 : 1;
        IsHitTestVisible = bounds is not null;
        if (bounds is null) return;
        Width = bounds.Width; Height = bounds.Height;
        Left = bounds.Left; Top = bounds.Top;
        settings.HorizontalOffset = bounds.Offset;
    }
    private void Center() { settings.HorizontalOffset = 0; Place(); Save(); }
    private void Save()
    {
        try { File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true })); }
        catch (IOException ex) { status = "设置保存失败：" + ex.Message; }
    }
    private void Import()
    {
        if (playback is null || demo) { MessageBox.Show("请先在支持的播放器中播放歌曲。", "导入歌词"); return; }
        var track = playback.Track;
        var picker = new Microsoft.Win32.OpenFileDialog { Filter = "LRC 歌词 (*.lrc)|*.lrc", CheckFileExists = true };
        if (picker.ShowDialog((Window?)settingsWindow ?? this) != true) return;
        try
        {
            if (new FileInfo(picker.FileName).Length > 2 * 1024 * 1024) throw new IOException("歌词文件不能超过 2 MB。");
            service.Import(track, File.ReadAllText(picker.FileName));
            if (playback?.Track.Key == track.Key) _ = LoadLyricsAsync(track);
        }
        catch (Exception ex) when (ex is IOException or FormatException or UnauthorizedAccessException) { MessageBox.Show(ex.Message, "导入失败"); }
    }
    private void ShowSettings()
    {
        if (settingsWindow is not null) { settingsWindow.Activate(); return; }
        var window = new SettingsWindow(settings, ApplySettings, Import,
            () => { if (playback is not null && !demo) _ = LoadLyricsAsync(playback.Track); }, Center, Exit);
        settingsWindow = window;
        window.Closed += (_, _) => settingsWindow = null;
        RenderLine();
        window.Show();
    }
    private void ApplySettings(bool reload)
    {
        lyric.FontSize = settings.FontSize;
        lyric.LineHeight = settings.FontSize + 2;
        Cursor = settings.Locked ? Cursors.Arrow : Cursors.SizeWE;
        Place(); Save();
        if (reload && !demo)
        {
            lookup?.Cancel(); playback = null; lyrics = null;
            _ = PollAsync();
        }
    }
    private void Exit()
    {
        if (exiting) return;
        exiting = true; lookup?.Cancel(); poll.Stop(); tick.Stop(); Save(); tray.Dispose(); service.Dispose();
        Application.Current.Shutdown();
    }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct CursorPoint { public int X, Y; }
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool GetCursorPos(out CursorPoint point);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string name, string? title);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern nint GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern nint SetWindowLongPtr(IntPtr window, int index, nint value);
}

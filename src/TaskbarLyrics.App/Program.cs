using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using TaskbarLyrics.Core;
using Application = System.Windows.Application;
using Button = System.Windows.Controls.Button;
using Orientation = System.Windows.Controls.Orientation;
using MessageBox = System.Windows.MessageBox;

namespace TaskbarLyrics.App;

public sealed class Settings
{
    public PlayerKind Player { get; set; }
    public double Offset { get; set; }
    public double FontSize { get; set; } = 16;
    public double? X { get; set; }
    public bool Online { get; set; } = true;
    public bool Locked { get; set; }
}

public static class Program
{
    public static readonly string DataDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TaskbarLyrics");
    [STAThread]
    public static void Main(string[] args)
    {
        Directory.CreateDirectory(DataDirectory);
        using var mutex = new Mutex(true, "Local\\TaskbarLyrics-0.1", out var first);
        if (!first && !args.Contains("--probe")) return;
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
    private TextBlock details = new() { TextWrapping = TextWrapping.Wrap, Foreground = Brushes.LightGray };
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
    private Window? settingsWindow;
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
        Width = 460; Height = 36;
        Title = "Taskbar Lyrics"; WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize;
        AllowsTransparency = true; Background = Brushes.Transparent; Topmost = true; ShowInTaskbar = false; ShowActivated = false;
        lyric.FontFamily = new FontFamily("Microsoft YaHei UI, Segoe UI");
        lyric.FontSize = settings.FontSize; lyric.Foreground = Brushes.White;
        Content = new Border { Background = new SolidColorBrush(Color.FromArgb(20, 30, 30, 35)), CornerRadius = new CornerRadius(8), Padding = new Thickness(12, 0, 12, 0), Child = lyric };
        var menu = new ContextMenu();
        AddMenu(menu, "设置与当前歌曲", ShowSettings);
        AddMenu(menu, "导入当前歌曲的 LRC…", Import);
        AddMenu(menu, "重新获取歌词", () => { if (playback is not null) _ = LoadLyricsAsync(playback.Track); });
        AddMenu(menu, "重置任务栏位置", () => { settings.X = null; Place(); Save(); });
        AddMenu(menu, "退出", Exit);
        ContextMenu = menu;
        MouseLeftButtonDown += (_, e) =>
        {
            if (e.ClickCount == 2) { ShowSettings(); return; }
            if (settings.Locked) return;
            dragging = true;
            try { DragMove(); settings.X = Left; }
            finally { dragging = false; }
            Place(); Save();
        };
        SourceInitialized += (_, _) =>
        {
            var handle = new WindowInteropHelper(this).Handle;
            SetWindowLongPtr(handle, -20, GetWindowLongPtr(handle, -20) | 0x08000080); // No activate; tool window.
            Place();
        };
        tray = new System.Windows.Forms.NotifyIcon { Icon = System.Drawing.SystemIcons.Information, Text = "Taskbar Lyrics", Visible = true };
        var trayMenu = new System.Windows.Forms.ContextMenuStrip();
        trayMenu.Items.Add("设置与当前歌曲", null, (_, _) => Dispatcher.Invoke(ShowSettings));
        trayMenu.Items.Add("重置位置", null, (_, _) => Dispatcher.Invoke(() => { settings.X = null; Place(); Save(); }));
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
        details.Text = playback is null ? status : $"{playback.Track.Title}\n{playback.Track.Artist} · {playback.Track.Player}\n{(playback.Playing ? "播放中" : "已暂停")}  {TimeSpan.FromSeconds(Math.Max(0, position)):mm\\:ss}\n歌词来源：{lyrics?.Source ?? "匹配中"}\n{status}";
        ToolTip = details.Text;
    }
    private void Place()
    {
        if (dragging) return;
        var bar = FindWindow("Shell_TrayWnd", null);
        if (bar == IntPtr.Zero || !GetWindowRect(bar, out var rect)) return;
        var source = PresentationSource.FromVisual(this);
        if (source?.CompositionTarget is null) return;
        var fromDevice = source.CompositionTarget.TransformFromDevice;
        var a = fromDevice.Transform(new System.Windows.Point(rect.Left, rect.Top));
        var b = fromDevice.Transform(new System.Windows.Point(rect.Right, rect.Bottom));
        var horizontal = b.X - a.X > b.Y - a.Y;
        var barVisible = horizontal ? b.Y - a.Y >= 12 : b.X - a.X >= 12;
        Opacity = barVisible ? 1 : 0;
        if (horizontal)
        {
            Width = Math.Min(460, Math.Max(160, b.X - a.X - 16));
            Left = Math.Clamp(settings.X is { } x && double.IsFinite(x) ? x : a.X + 180, a.X + 4, Math.Max(a.X + 4, b.X - Width - 4));
            Top = a.Y + Math.Max(0, (b.Y - a.Y - Height) / 2);
        }
        else { Left = b.X + 8; Top = a.Y + 80; }
    }
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
        if (picker.ShowDialog(settingsWindow ?? this) != true) return;
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
        var window = new Window { Title = "Taskbar Lyrics · 设置", Width = 510, Height = 640, MinWidth = 450, MinHeight = 560,
            Background = new SolidColorBrush(Color.FromRgb(23, 26, 34)), Foreground = Brushes.White, WindowStartupLocation = WindowStartupLocation.CenterScreen };
        settingsWindow = window;
        details = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Brushes.LightGray };
        var panel = new StackPanel { Margin = new Thickness(28) };
        panel.Children.Add(new TextBlock { Text = "♫  Taskbar Lyrics", FontSize = 27, FontWeight = FontWeights.SemiBold });
        panel.Children.Add(new TextBlock { Text = "让歌词留在任务栏", Foreground = Brushes.LightSteelBlue, Margin = new Thickness(0, 4, 0, 22) });
        panel.Children.Add(details);
        Label("播放器");
        var players = new ComboBox { ItemsSource = new[] { "自动选择正在播放", "QQ 音乐", "网易云音乐", "Apple Music" }, SelectedIndex = (int)settings.Player, Foreground = Brushes.Black };
        players.SelectionChanged += (_, _) => { settings.Player = (PlayerKind)players.SelectedIndex; playback = null; lyrics = null; lookup?.Cancel(); Save(); };
        panel.Children.Add(players);
        Label("歌词时间调整（正值让歌词提前）");
        var offsetText = new TextBlock();
        var offset = new Slider { Minimum = -10, Maximum = 10, TickFrequency = 0.1, IsSnapToTickEnabled = true, Value = settings.Offset };
        offsetText.Text = $"{settings.Offset:+0.0;-0.0;0.0} 秒";
        offset.ValueChanged += (_, _) => { settings.Offset = Math.Round(offset.Value, 1); offsetText.Text = $"{settings.Offset:+0.0;-0.0;0.0} 秒"; Save(); };
        panel.Children.Add(offset); panel.Children.Add(offsetText);
        Label("歌词字号");
        var size = new Slider { Minimum = 11, Maximum = 28, TickFrequency = 1, IsSnapToTickEnabled = true, Value = settings.FontSize };
        size.ValueChanged += (_, _) => { settings.FontSize = size.Value; lyric.FontSize = size.Value; Save(); }; panel.Children.Add(size);
        var online = new CheckBox { Content = "在线匹配歌词（会发送歌名、歌手、专辑和时长）", IsChecked = settings.Online, Margin = new Thickness(0, 14, 0, 8), Foreground = Brushes.White };
        online.Click += (_, _) => { settings.Online = online.IsChecked == true; Save(); if (playback is not null) _ = LoadLyricsAsync(playback.Track); }; panel.Children.Add(online);
        var locked = new CheckBox { Content = "锁定歌词位置", IsChecked = settings.Locked, Foreground = Brushes.White };
        locked.Click += (_, _) => { settings.Locked = locked.IsChecked == true; Save(); }; panel.Children.Add(locked);
        var buttons = new WrapPanel { Margin = new Thickness(0, 20, 0, 12), Orientation = Orientation.Horizontal };
        AddButton("导入 LRC", Import); AddButton("重新匹配", () => { if (playback is not null) _ = LoadLyricsAsync(playback.Track); });
        AddButton("重置位置", () => { settings.X = null; Place(); Save(); }); AddButton("退出", Exit);
        panel.Children.Add(buttons);
        panel.Children.Add(new TextBlock { Text = "拖动歌词调整横向位置，双击打开设置。关闭此窗口后继续运行，可从托盘退出。\nApple Music 使用 LRCLIB／本地歌词；三个播放器均需提供系统媒体会话。", TextWrapping = TextWrapping.Wrap, Foreground = Brushes.LightSlateGray, FontSize = 12 });
        window.Content = new ScrollViewer { Content = panel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        window.Closed += (_, _) => settingsWindow = null;
        window.Show();
        void Label(string text) => panel.Children.Add(new TextBlock { Text = text, Margin = new Thickness(0, 16, 0, 6), Foreground = Brushes.LightSteelBlue });
        void AddButton(string text, Action action)
        {
            var button = new Button { Content = text, Foreground = Brushes.Black, Padding = new Thickness(12, 6, 12, 6), Margin = new Thickness(0, 0, 8, 8) };
            button.Click += (_, _) => action(); buttons.Children.Add(button);
        }
    }
    private void Exit()
    {
        if (exiting) return;
        exiting = true; lookup?.Cancel(); poll.Stop(); tick.Stop(); Save(); tray.Dispose(); service.Dispose();
        Application.Current.Shutdown();
    }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string name, string? title);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern nint GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern nint SetWindowLongPtr(IntPtr window, int index, nint value);
}

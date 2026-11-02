using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;
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
    public Dictionary<string, double> ScreenOffsets { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    public DisplayMode DisplayMode { get; set; }
    public string? DisplayDevice { get; set; }
    public bool AvoidIcons { get; set; } = true;
    public bool Online { get; set; } = true;
    public bool Locked { get; set; }
}

public static class Program
{
    public static readonly string DataDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TaskbarLyrics");
    [STAThread]
    public static void Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--check-placement")
        {
            var checkApp = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            checkApp.Startup += async (_, _) => checkApp.Shutdown(await PlacementCheck.RunAsync(args[1]));
            Environment.ExitCode = checkApp.Run(); return;
        }
        if (args.Length == 2 && args[0] == "--render-previews")
        {
            var previewApp = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            try { PreviewRenderer.Render(args[1]); }
            catch (Exception ex) { Directory.CreateDirectory(args[1]); File.WriteAllText(Path.Combine(args[1], "error.txt"), ex.ToString()); Environment.ExitCode = 1; }
            previewApp.Shutdown(); return;
        }
        if (args.Length == 2 && args[0] == "--taskbar-probe")
        {
            using var inspector = new TaskbarInspector();
            var watch = Stopwatch.StartNew();
            while (inspector.Snapshots.Length == 0 && watch.Elapsed < TimeSpan.FromSeconds(6)) Thread.Sleep(100);
            var bars = inspector.Snapshots;
            var result = bars.Select(x => new { x.Device, x.IsPrimary, x.Bounds, x.Scale, x.Occupied, x.Reliable, x.CandidateCount, x.Error,
                AgeSeconds = (DateTime.UtcNow - x.CapturedAt).TotalSeconds,
                Layout = TaskbarLayout.Calculate(0, 0, x.Bounds.Width / x.Scale, x.Bounds.Height / x.Scale, 0,
                    x.Occupied.Select(r => new HorizontalSpan((r.Left - x.Bounds.Left) / x.Scale, (r.Right - x.Bounds.Left) / x.Scale)).ToArray(), true) });
            File.WriteAllText(args[1], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            if (bars.Length == 0 || bars.Any(x => !x.Reliable)) Environment.ExitCode = 1;
            return;
        }
        Directory.CreateDirectory(DataDirectory);
        if (args.Contains("--probe"))
        {
            try { File.WriteAllLines(Path.Combine(DataDirectory, "sessions.txt"), new MediaReader().ProbeAsync().GetAwaiter().GetResult()); }
            catch (Exception ex) { File.WriteAllText(Path.Combine(DataDirectory, "sessions.txt"), ex.ToString()); }
            return;
        }
        using var mutex = new Mutex(true, "Local\\TaskbarLyrics-0.1", out var first);
        if (!first) return;
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.DispatcherUnhandledException += (_, e) =>
        {
            File.WriteAllText(Path.Combine(DataDirectory, "last-error.txt"), e.Exception.ToString());
            MessageBox.Show("运行出错，详情已保存到应用数据目录。\n" + e.Exception.Message, "Taskbar Lyrics");
            e.Handled = true; app.Shutdown(1);
        };
        using var controller = new LyricsController(args.Contains("--demo"));
        controller.Start(); app.Run();
    }
}

internal sealed class LyricsController : IDisposable
{
    private readonly MediaReader reader = new();
    private readonly LyricService service = new(Path.Combine(Program.DataDirectory, "lyrics"));
    private readonly Settings settings;
    private readonly System.Windows.Forms.NotifyIcon tray;
    private readonly System.Drawing.Icon trayIcon;
    private readonly TaskbarInspector inspector = new();
    private readonly Dictionary<string, LyricOverlay> overlays = new(StringComparer.OrdinalIgnoreCase);
    private readonly DispatcherTimer poll = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private readonly DispatcherTimer tick = new() { Interval = TimeSpan.FromMilliseconds(80) };
    private readonly Stopwatch positionClock = Stopwatch.StartNew();
    private readonly bool demo;
    private bool polling, exiting, migratedOffset;
    private Playback? playback;
    private LyricResult? lyrics;
    private CancellationTokenSource? lookup;
    private SettingsWindow? settingsWindow;
    private string status = "打开 QQ 音乐、网易云音乐或 Apple Music，并启用系统媒体控制。";
    private string placementStatus = "正在检测任务栏…";
    private string line = "♫  Taskbar Lyrics · 等待音乐", tooltip = "";
    private DateTime lastAttempt;
    private static string SettingsPath => Path.Combine(Program.DataDirectory, "settings.json");

    public LyricsController(bool demo)
    {
        this.demo = demo;
        try { settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath)) ?? new(); }
        catch (Exception ex) when (ex is IOException or JsonException) { settings = new(); }
        settings.FontSize = Math.Clamp(double.IsFinite(settings.FontSize) ? settings.FontSize : 16, 11, 28);
        settings.Offset = Math.Clamp(double.IsFinite(settings.Offset) ? settings.Offset : 0, -10, 10);
        settings.ScreenOffsets = new(settings.ScreenOffsets ?? new(), StringComparer.OrdinalIgnoreCase);
        if (!Enum.IsDefined(settings.Player)) settings.Player = PlayerKind.Auto;
        if (!Enum.IsDefined(settings.DisplayMode)) settings.DisplayMode = DisplayMode.Primary;
        inspector.SetAvoidance(settings.AvoidIcons);
        trayIcon = AppAssets.CreateTrayIcon();
        tray = new System.Windows.Forms.NotifyIcon { Icon = trayIcon, Text = "Taskbar Lyrics", Visible = true };
        var menu = new System.Windows.Forms.ContextMenuStrip();
        menu.Items.Add("设置与当前歌曲", null, (_, _) => Application.Current.Dispatcher.Invoke(ShowSettings));
        menu.Items.Add("恢复任务栏居中", null, (_, _) => Application.Current.Dispatcher.Invoke(Center));
        menu.Items.Add("退出", null, (_, _) => Application.Current.Dispatcher.Invoke(Exit));
        tray.ContextMenuStrip = menu;
        tray.DoubleClick += (_, _) => Application.Current.Dispatcher.Invoke(ShowSettings);
        poll.Tick += async (_, _) => await PollAsync(); tick.Tick += (_, _) => RenderLine();
    }
    public void Start()
    {
        if (demo)
        {
            playback = new(new(PlayerKind.AppleMusic, "演示歌曲", "Taskbar Lyrics", "演示数据", 24), 0, true, 1, "demo");
            lyrics = new("演示 · 非真实播放", "[00:00.00]♫  音乐在播放，歌词在身旁\n[00:05.00]QQ 音乐 · 网易云音乐 · Apple Music\n[00:10.00]图标自动避让，多屏同步显示\n[00:15.00]此处为演示歌词，可随时退出\n[00:20.00]Taskbar Lyrics · 让音乐留在任务栏");
            status = "演示模式：歌词和播放进度均为模拟数据。"; positionClock.Restart();
        }
        poll.Start(); tick.Start(); _ = PollAsync(); ShowSettings();
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
                _ = LoadLyricsAsync(next.Track);
        }
        catch (Exception ex) { lookup?.Cancel(); playback = null; lyrics = null; status = "读取媒体会话失败：" + ex.Message; }
        finally { polling = false; }
    }
    private async Task LoadLyricsAsync(Track track)
    {
        lookup?.Cancel(); lookup?.Dispose(); lookup = new CancellationTokenSource(); var token = lookup.Token;
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
        line = playback is null ? "♫  Taskbar Lyrics · 等待音乐"
            : index >= 0 ? (string.IsNullOrWhiteSpace(lines![index].Text) ? "♫  间奏" : lines[index].Text)
            : $"♫  {playback.Track.Title} · {playback.Track.Artist}";
        tooltip = playback is null ? status : $"{playback.Track.Title}\n{playback.Track.Artist} · {playback.Track.Player}\n{(playback.Playing ? "播放中" : "已暂停")}  {TimeSpan.FromSeconds(Math.Max(0, position)):mm\\:ss}\n歌词来源：{lyrics?.Source ?? "匹配中"}\n{status}";
        foreach (var overlay in overlays.Values) overlay.Update(line, tooltip, playback?.Playing != false);
        settingsWindow?.UpdatePlayback(line, playback, lyrics?.Source, status, position, demo);
        settingsWindow?.UpdatePlacementStatus(placementStatus);
    }
    private void Place()
    {
        if (exiting) return;
        inspector.SetAvoidance(settings.AvoidIcons);
        var bars = inspector.Snapshots.Where(TaskbarInspector.IsCurrent).GroupBy(x => x.Device, StringComparer.OrdinalIgnoreCase)
            .Select(x => x.First()).ToArray();
        var selected = DisplaySelection.Resolve(bars.Select(x => new DisplayTarget(x.Device, x.IsPrimary)).ToArray(), settings.DisplayMode, settings.DisplayDevice);
        if (!migratedOffset && bars.Length > 0)
        {
            var primary = bars.FirstOrDefault(x => x.IsPrimary) ?? bars[0];
            if (settings.ScreenOffsets.Count == 0 && double.IsFinite(settings.HorizontalOffset)) settings.ScreenOffsets[primary.Device] = settings.HorizontalOffset;
            migratedOffset = true;
        }
        foreach (var key in overlays.Keys.Where(x => !selected.Contains(x, StringComparer.OrdinalIgnoreCase)).ToArray())
        { overlays[key].Close(); overlays.Remove(key); }
        int shown = 0, pending = 0, crowded = 0;
        foreach (var device in selected)
        {
            var bar = bars.First(x => x.Device.Equals(device, StringComparison.OrdinalIgnoreCase));
            if (!overlays.TryGetValue(device, out var overlay))
            {
                overlay = new LyricOverlay(settings, Move, ShowSettings, Import, Refresh, Center, Exit);
                overlays.Add(device, overlay); overlay.Show(); overlay.Update(line, tooltip, playback?.Playing != false);
            }
            var fresh = DateTime.UtcNow - bar.CapturedAt < TimeSpan.FromSeconds(8);
            var canPlace = fresh && (!settings.AvoidIcons || bar.Reliable);
            settings.ScreenOffsets.TryGetValue(device, out var offset);
            var spans = bar.Occupied.Select(x => new HorizontalSpan((x.Left - bar.Bounds.Left) / bar.Scale, (x.Right - bar.Bounds.Left) / bar.Scale)).ToArray();
            var bounds = canPlace ? TaskbarLayout.Calculate(0, 0, bar.Bounds.Width / bar.Scale, bar.Bounds.Height / bar.Scale, offset, spans, settings.AvoidIcons) : null;
            overlay.Place(bar, bounds);
            if (!canPlace) pending++; else if (bounds is null) crowded++; else shown++;
        }
        placementStatus = bars.Length == 0 ? "正在检测可用任务栏…"
            : $"已在 {shown} 个屏幕显示" + (settings.AvoidIcons ? " · 图标避让已开启" : " · 图标避让已关闭")
                + (crowded > 0 ? $" · {crowded} 个屏幕空间不足或任务栏方向不受支持，已隐藏" : "")
                + (pending > 0 ? $" · {pending} 个屏幕正在检测布局" : "");
        if (settings.DisplayMode == DisplayMode.Selected && !bars.Any(x => x.Device.Equals(settings.DisplayDevice, StringComparison.OrdinalIgnoreCase)))
            placementStatus += " · 指定屏幕的任务栏不可用，暂用主屏";
        settingsWindow?.UpdateDisplays(TaskbarInspector.Displays());
        settingsWindow?.UpdatePlacementStatus(placementStatus);
    }
    private void Move(string device, double offset, bool save)
    {
        if (double.IsFinite(offset)) settings.ScreenOffsets[device] = offset;
        if (save) Save(); else Place();
    }
    private void Center()
    {
        foreach (var key in overlays.Keys) settings.ScreenOffsets[key] = 0;
        settings.HorizontalOffset = 0; Place(); Save();
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
        var accepted = settingsWindow is null ? picker.ShowDialog() : picker.ShowDialog(settingsWindow);
        if (accepted != true) return;
        try
        {
            if (new FileInfo(picker.FileName).Length > 2 * 1024 * 1024) throw new IOException("歌词文件不能超过 2 MB。");
            service.Import(track, File.ReadAllText(picker.FileName));
            if (playback?.Track.Key == track.Key) _ = LoadLyricsAsync(track);
        }
        catch (Exception ex) when (ex is IOException or FormatException or UnauthorizedAccessException) { MessageBox.Show(ex.Message, "导入失败"); }
    }
    private void Refresh() { if (playback is not null && !demo) _ = LoadLyricsAsync(playback.Track); }
    private void ShowSettings()
    {
        if (settingsWindow is not null) { settingsWindow.Activate(); return; }
        var window = new SettingsWindow(settings, ApplySettings, Import, Refresh, Center, Exit);
        settingsWindow = window; window.Closed += (_, _) => settingsWindow = null;
        RenderLine(); window.UpdateDisplays(TaskbarInspector.Displays()); window.Show();
    }
    private void ApplySettings(bool reload)
    {
        Place(); Save(); RenderLine();
        if (reload && !demo) { lookup?.Cancel(); playback = null; lyrics = null; _ = PollAsync(); }
    }
    private void Exit() { Dispose(); Application.Current.Shutdown(); }
    public void Dispose()
    {
        if (exiting) return;
        exiting = true; lookup?.Cancel(); poll.Stop(); tick.Stop(); inspector.Dispose(); Save();
        foreach (var overlay in overlays.Values.ToArray()) overlay.Close(); overlays.Clear();
        tray.Dispose(); trayIcon.Dispose(); service.Dispose();
    }
}

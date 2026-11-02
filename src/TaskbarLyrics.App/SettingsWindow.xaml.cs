using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using TaskbarLyrics.Core;

namespace TaskbarLyrics.App;

public partial class SettingsWindow : Window
{
    private readonly Settings settings;
    private readonly Action<bool> changed;
    private readonly Action import, refresh, center, exit;
    private bool ready;
    private bool updatingDisplays;
    private DisplayOption[] displayOptions = [];
    public SettingsWindow(Settings settings, Action<bool> changed, Action import, Action refresh, Action center, Action exit)
    {
        this.settings = settings; this.changed = changed;
        this.import = import; this.refresh = refresh; this.center = center; this.exit = exit;
        InitializeComponent();
        Players.SelectedIndex = (int)settings.Player;
        FontSlider.Value = settings.FontSize; OffsetSlider.Value = settings.Offset;
        LockedToggle.IsChecked = settings.Locked; OnlineToggle.IsChecked = settings.Online;
        DisplayModes.SelectedIndex = (int)settings.DisplayMode;
        AvoidIconsToggle.IsChecked = settings.AvoidIcons;
        UpdateDisplays(TaskbarInspector.Displays());
        ready = true;
        UpdateValues(); SelectPage(0);
        SourceInitialized += (_, _) =>
        {
            var dark = 1;
            _ = DwmSetWindowAttribute(new WindowInteropHelper(this).Handle, 20, ref dark, sizeof(int));
        };
    }
    public void UpdatePlayback(string line, Playback? playback, string? source, string status, double position, bool demo)
    {
        CurrentLyric.Text = playback is null ? "音乐响起，歌词就会在这里。" : line;
        AppearanceLyric.Text = playback is null ? "音乐在播放，歌词在身旁" : line;
        TrackTitle.Text = playback?.Track.Title ?? "尚未播放歌曲";
        TrackArtist.Text = playback is null ? "QQ 音乐 · 网易云音乐 · Apple Music" : $"{playback.Track.Artist} · {PlayerName(playback.Track.Player)}";
        PlaybackBadge.Text = demo ? "演示数据" : playback is null ? "等待播放" : playback.Playing ? "正在播放" : "已暂停";
        PlaybackBadge.Foreground = playback?.Playing == true ? Brush("#B4D9C9") : Brush("#B9B9B9");
        PositionLabel.Text = playback is null ? "--:--" : Time(position);
        DurationLabel.Text = playback?.Track.Duration > 0 ? Time(playback.Track.Duration) : "--:--";
        SongProgress.Value = playback?.Track.Duration > 0 ? Math.Clamp(position / playback.Track.Duration, 0, 1) : 0;
        StatusText.Text = demo ? "演示模式：此处为模拟歌曲和歌词。" : status;
        SourceText.Text = "当前来源：" + (source ?? "尚未载入");
        UpdateValues();
    }
    private void UpdateValues()
    {
        FontValue.Text = $"{settings.FontSize:0} px";
        OffsetValue.Text = $"{settings.Offset:+0.0;-0.0;0.0} 秒";
        AppearanceLyric.FontSize = settings.FontSize;
        PlacementText.Text = "各屏独立记忆 · 仅支持左右拖动 · 高度固定";
    }
    public void UpdatePlacementStatus(string text) => PlacementStatus.Text = text;
    public void UpdateDisplays(IReadOnlyList<DisplayOption> options)
    {
        if (displayOptions.SequenceEqual(options) && DisplayDevices.ItemsSource is not null) return;
        updatingDisplays = true;
        try
        {
            displayOptions = options.ToArray();
            var shown = displayOptions.ToList();
            if (!string.IsNullOrWhiteSpace(settings.DisplayDevice) && !shown.Any(x => x.Id.Equals(settings.DisplayDevice, StringComparison.OrdinalIgnoreCase)))
                shown.Add(new(settings.DisplayDevice, "已断开的显示器（恢复连接后使用）"));
            DisplayDevices.ItemsSource = shown;
            DisplayDevices.SelectedItem = shown.FirstOrDefault(x => x.Id.Equals(settings.DisplayDevice, StringComparison.OrdinalIgnoreCase)) ?? shown.FirstOrDefault();
            DisplayDevices.IsEnabled = settings.DisplayMode == DisplayMode.Selected;
        }
        finally { updatingDisplays = false; }
    }
    public void SelectPage(int index)
    {
        var pages = new[] { OverviewPage, AppearancePage, SourcesPage };
        var nav = new[] { OverviewNav, AppearanceNav, SourcesNav };
        var titles = new[] { "正在播放", "外观与位置", "歌词与来源" };
        var subtitles = new[] { "让歌词留在任务栏，让注意力留在当下。", "调整你的歌词栏，找到舒服的阅读方式。", "找到合适的歌词，也保留自己的选择。" };
        for (var i = 0; i < pages.Length; i++)
        {
            pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;
            nav[i].Background = i == index ? Brush("#303030") : Brushes.Transparent;
            nav[i].Foreground = i == index ? Brush("#ECECEC") : Brush("#AAAAAA");
            System.Windows.Automation.AutomationProperties.SetItemStatus(nav[i], i == index ? "已选中" : "");
        }
        PageTitle.Text = titles[index]; PageSubtitle.Text = subtitles[index]; PageScroll.ScrollToTop();
    }
    private static SolidColorBrush Brush(string value) => new((Color)ColorConverter.ConvertFromString(value));
    private static string PlayerName(PlayerKind player) => player switch { PlayerKind.QQMusic => "QQ 音乐", PlayerKind.NetEase => "网易云音乐", PlayerKind.AppleMusic => "Apple Music", _ => "自动选择" };
    private static string Time(double seconds) => TimeSpan.FromSeconds(Math.Max(0, seconds)).ToString(@"mm\:ss", CultureInfo.InvariantCulture);
    private void Navigate_Click(object sender, RoutedEventArgs e) => SelectPage(int.Parse((string)((Button)sender).Tag, CultureInfo.InvariantCulture));
    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Close_Click(object sender, RoutedEventArgs e) => Close();
    private void Players_Changed(object sender, SelectionChangedEventArgs e) { if (!ready) return; settings.Player = (PlayerKind)Players.SelectedIndex; changed(true); }
    private void Font_Changed(object sender, RoutedPropertyChangedEventArgs<double> e) { if (!ready) return; settings.FontSize = FontSlider.Value; UpdateValues(); changed(false); }
    private void Offset_Changed(object sender, RoutedPropertyChangedEventArgs<double> e) { if (!ready) return; settings.Offset = Math.Round(OffsetSlider.Value, 1); UpdateValues(); changed(false); }
    private void Locked_Click(object sender, RoutedEventArgs e) { settings.Locked = LockedToggle.IsChecked == true; changed(false); }
    private void Online_Click(object sender, RoutedEventArgs e) { settings.Online = OnlineToggle.IsChecked == true; changed(true); }
    private void AvoidIcons_Click(object sender, RoutedEventArgs e) { settings.AvoidIcons = AvoidIconsToggle.IsChecked == true; changed(false); }
    private void DisplayMode_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!ready) return;
        settings.DisplayMode = (DisplayMode)DisplayModes.SelectedIndex;
        DisplayDevices.IsEnabled = settings.DisplayMode == DisplayMode.Selected;
        if (settings.DisplayMode == DisplayMode.Selected && DisplayDevices.SelectedItem is DisplayOption option) settings.DisplayDevice = option.Id;
        changed(false);
    }
    private void DisplayDevice_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!ready || updatingDisplays || DisplayDevices.SelectedItem is not DisplayOption option) return;
        settings.DisplayDevice = option.Id; changed(false);
    }
    private void Center_Click(object sender, RoutedEventArgs e) { center(); UpdateValues(); }
    private void Import_Click(object sender, RoutedEventArgs e) => import();
    private void Refresh_Click(object sender, RoutedEventArgs e) => refresh();
    private void Exit_Click(object sender, RoutedEventArgs e) => exit();
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);
}

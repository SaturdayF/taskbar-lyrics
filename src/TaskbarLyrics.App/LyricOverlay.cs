using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using TaskbarLyrics.Core;

namespace TaskbarLyrics.App;

internal sealed class LyricOverlay : Window
{
    private readonly TextBlock lyric = new() { TextTrimming = TextTrimming.CharacterEllipsis,
        VerticalAlignment = VerticalAlignment.Center, TextAlignment = TextAlignment.Center,
        FontFamily = new FontFamily("Microsoft YaHei UI, Segoe UI"), Foreground = Brushes.White,
        LineStackingStrategy = LineStackingStrategy.BlockLineHeight };
    private readonly Settings settings;
    private readonly Action<string, double, bool> move;
    private TaskbarSnapshot? target;
    private nint handle;
    private int dragStartX;
    private double dragStartOffset;
    private bool dragging, displayed;
    private readonly TextBlock grip;
    public bool IsDragging => dragging;
    public LyricOverlay(Settings settings, Action<string, double, bool> move, Action showSettings,
        Action import, Action refresh, Action center, Action exit, Action<PlaybackCommand>? control = null,
        Action? copy = null, Action<double, bool>? adjustOffset = null, Action? resetOffset = null,
        Action? toggleHide = null, Action? toggleThrough = null, Func<bool>? canCopy = null, Func<Track?>? currentTrack = null)
    {
        this.settings = settings; this.move = move;
        Title = "Taskbar Lyrics"; Icon = AppAssets.Image; Width = 520; Height = 34;
        WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize;
        AllowsTransparency = true; Background = Brushes.Transparent; ShowInTaskbar = false; ShowActivated = false; Topmost = true;
        Opacity = 0; IsHitTestVisible = false;
        var row = new DockPanel { Background = Brushes.Transparent, Margin = new Thickness(10, 0, 10, 0) };
        grip = new TextBlock { Text = "⠿", Foreground = Brushes.White, VerticalAlignment = VerticalAlignment.Center,
            Padding = new Thickness(2, 0, 8, 0), ToolTip = "左右拖动歌词栏" };
        DockPanel.SetDock(grip, Dock.Left);
        row.Children.Add(grip);
        row.Children.Add(lyric);
        Content = row;
        var menu = new ContextMenu();
        if (control is not null)
        {
            Add("上一首", () => control(PlaybackCommand.Previous));
            Add("播放 / 暂停", () => control(PlaybackCommand.Toggle));
            Add("下一首", () => control(PlaybackCommand.Next));
            menu.Items.Add(new Separator());
        }
        if (copy is not null && adjustOffset is not null && resetOffset is not null && toggleHide is not null && toggleThrough is not null)
        {
            var copyItem = Add("复制当前歌词", copy);
            var timing = new MenuItem { Header = "当前歌曲校准" };
            var earlier = new MenuItem { Header = "提前 0.5 秒" }; earlier.Click += (_, _) => adjustOffset(.5, false);
            var later = new MenuItem { Header = "延后 0.5 秒" }; later.Click += (_, _) => adjustOffset(-.5, false);
            var reset = new MenuItem { Header = "重置当前歌曲校准" }; reset.Click += (_, _) => resetOffset();
            timing.Items.Add(earlier); timing.Items.Add(later); timing.Items.Add(reset); menu.Items.Add(timing);
            var hideItem = Add("暂停或无播放时隐藏", toggleHide);
            var throughItem = Add("鼠标穿透（从托盘关闭）", toggleThrough);
            menu.Opened += (_, _) =>
            {
                copyItem.IsEnabled = canCopy?.Invoke() == true;
                var track = currentTrack?.Invoke();
                timing.IsEnabled = track is not null;
                timing.Header = $"当前歌曲校准 {settings.Lyrics.TrackOffset(track):+0.0;-0.0;0.0} 秒";
                hideItem.IsChecked = settings.Lyrics.HideWhenPaused; throughItem.IsChecked = settings.Lyrics.ClickThrough;
            };
            menu.Items.Add(new Separator());
        }
        Add("设置与当前歌曲", showSettings); Add("导入当前歌曲的 LRC…", import); Add("重新获取歌词", refresh);
        Add("恢复任务栏居中", center); Add("退出", exit); ContextMenu = menu;
        SourceInitialized += (_, _) =>
        {
            handle = new WindowInteropHelper(this).Handle;
            NativeTaskbar.SetWindowLongPtr(handle, -20, NativeTaskbar.GetWindowLongPtr(handle, -20) | 0x08000080);
            UpdateInput();
        };
        MouseLeftButtonDown += (_, e) =>
        {
            if (e.ClickCount == 2) { showSettings(); return; }
            if (settings.Locked || settings.Lyrics.ClickThrough || target is null || !NativeTaskbar.GetCursorPos(out var cursor)
                || !NativeTaskbar.GetWindowRect(handle, out var rect)) return;
            dragStartX = cursor.X;
            dragStartOffset = (rect.Left + (rect.Right - rect.Left) / 2d - (target.Bounds.Left + target.Bounds.Width / 2d)) / target.Scale;
            dragging = CaptureMouse(); e.Handled = true;
        };
        MouseMove += (_, e) =>
        {
            if (!dragging || target is null) return;
            if (e.LeftButton != MouseButtonState.Pressed) { ReleaseMouseCapture(); return; }
            if (NativeTaskbar.GetCursorPos(out var cursor)) move(target.Device, dragStartOffset + (cursor.X - dragStartX) / target.Scale, false);
        };
        MouseLeftButtonUp += (_, _) => { if (dragging) ReleaseMouseCapture(); };
        LostMouseCapture += (_, _) => { if (dragging) { dragging = false; if (target is not null) move(target.Device, double.NaN, true); } };
        PreviewMouseWheel += (_, e) =>
        {
            if (adjustOffset is null || (Keyboard.Modifiers & ModifierKeys.Control) == 0) return;
            adjustOffset(e.Delta / 120d * .5, (Keyboard.Modifiers & ModifierKeys.Shift) != 0); e.Handled = true;
        };
        MenuItem Add(string text, Action action) { var item = new MenuItem { Header = text }; item.Click += (_, _) => action(); menu.Items.Add(item); return item; }
    }
    private void UpdateInput()
    {
        if (handle == 0) return;
        var through = settings.Lyrics.ClickThrough;
        if (through && dragging) ReleaseMouseCapture();
        var style = NativeTaskbar.GetWindowLongPtr(handle, -20);
        var updated = through ? style | 0x20 : style & ~0x20;
        if (updated != style) NativeTaskbar.SetWindowLongPtr(handle, -20, updated);
        IsHitTestVisible = displayed && !through;
        grip.Visibility = through ? Visibility.Collapsed : Visibility.Visible;
    }
    public void Update(string text, string tooltip, bool playing)
    {
        lyric.Text = text; lyric.Opacity = playing ? 1 : .65; ToolTip = tooltip;
        lyric.FontSize = settings.FontSize; lyric.LineHeight = settings.FontSize + 2;
        Cursor = settings.Locked ? Cursors.Arrow : Cursors.SizeWE;
        UpdateInput();
    }
    public void Place(TaskbarSnapshot bar, LyricBounds? bounds)
    {
        target = bar;
        if (bounds is null)
        {
            if (dragging) ReleaseMouseCapture();
            IsHitTestVisible = false; Opacity = 0;
            if (displayed) NativeTaskbar.ShowWindow(handle, 0);
            displayed = false; return;
        }
        var x = bar.Bounds.Left + (int)Math.Round(bounds.Left * bar.Scale);
        var y = bar.Bounds.Top + (int)Math.Round(bounds.Top * bar.Scale);
        var width = (int)Math.Floor(bounds.Width * bar.Scale);
        var height = (int)Math.Round(bounds.Height * bar.Scale);
        NativeTaskbar.SetWindowPos(handle, -1, x, y, width, height, 0x0010 | 0x0040);
        Opacity = 1; displayed = true; UpdateInput();
    }
}

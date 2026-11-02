using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using TaskbarLyrics.Core;

namespace TaskbarLyrics.App;

internal static class PreviewRenderer
{
    public static void Render(string directory)
    {
        Directory.CreateDirectory(directory);
        var preferences = new Settings();
        var window = new SettingsWindow(preferences, _ => { }, () => { }, () => { },
            () => preferences.HorizontalOffset = 0, () => { })
        { ShowInTaskbar = false, ShowActivated = false, WindowStartupLocation = WindowStartupLocation.Manual, Left = -20000, Top = -20000 };
        window.Show();
        window.UpdatePlayback("音乐在播放，歌词在身旁", new Playback(new Track(PlayerKind.AppleMusic,
            "演示歌曲", "Taskbar Lyrics", "界面预览", 240), 82, true, 1, "preview"), "演示来源 · 非真实播放", "演示数据", 82, true);
        Capture(window, "overview.png");
        window.SelectPage(1); Capture(window, "appearance.png");
        window.SelectPage(2); Capture(window, "sources.png");
        window.Width = 840; window.Height = 660;
        window.SelectPage(1); Capture(window, "appearance-compact.png");
        window.Close();
        // Reopen the same settings to cover the previous logical-parent regression.
        var reopened = new SettingsWindow(preferences, _ => { }, () => { }, () => { }, () => { }, () => { });
        reopened.Measure(new Size(960, 760)); reopened.Close();
        File.WriteAllText(Path.Combine(directory, "result.txt"), "Rendered four previews and reopened settings successfully.");
        void Capture(Window target, string name)
        {
            target.UpdateLayout();
            var bitmap = new RenderTargetBitmap((int)target.ActualWidth, (int)target.ActualHeight, 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(target);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using var stream = File.Create(Path.Combine(directory, name)); encoder.Save(stream);
        }
    }
}

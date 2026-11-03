using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using TaskbarLyrics.Core;

namespace TaskbarLyrics.App;

internal static class PlacementCheck
{
    public static async Task<int> RunAsync(string path)
    {
        var reports = new List<object>();
        var success = true;
        try
        {
            using var inspector = new TaskbarInspector();
            for (var attempt = 0; attempt < 60 && inspector.Snapshots.Length == 0; attempt++) await Task.Delay(100);
            var bars = inspector.Snapshots;
            if (bars.Length == 0) throw new InvalidOperationException("No taskbar snapshot available.");
            foreach (var bar in bars)
            {
                if (!bar.Reliable) throw new InvalidOperationException("Taskbar layout is not reliable: " + bar.Error);
                var spans = bar.Occupied.Select(r => new HorizontalSpan((r.Left - bar.Bounds.Left) / bar.Scale,
                    (r.Right - bar.Bounds.Left) / bar.Scale)).ToArray();
                var layout = TaskbarLayout.Calculate(0, 0, bar.Bounds.Width / bar.Scale, bar.Bounds.Height / bar.Scale, 0, spans, true);
                if (layout is null) { reports.Add(new { bar.Device, bar.Scale, Hidden = true, Reason = "No free lane" }); continue; }
                var settings = new Settings();
                var overlay = new LyricOverlay(settings, (_, _, _) => { }, () => { }, () => { }, () => { }, () => { }, () => { });
                try
                {
                    overlay.Show(); overlay.Update("图标避让验证 · 模拟文字", "位置验证", true); overlay.Place(bar, layout);
                    await Dispatcher.Yield(DispatcherPriority.ApplicationIdle);
                    await Task.Delay(150);
                    var handle = new WindowInteropHelper(overlay).Handle;
                    if (!NativeTaskbar.GetWindowRect(handle, out var rect)) throw new InvalidOperationException("Overlay has no window bounds.");
                    var actual = rect.ToPixelRect();
                    var overlaps = bar.Occupied.Any(actual.Intersects);
                    var centeredY = Math.Abs((actual.Top + actual.Bottom) - (bar.Bounds.Top + bar.Bounds.Bottom)) <= 1;
                    overlay.Place(bar, null);
                    var hidden = !NativeTaskbar.IsWindowVisible(handle);
                    overlay.Place(bar, layout);
                    var restored = NativeTaskbar.IsWindowVisible(handle);
                    settings.Lyrics.ClickThrough = true; overlay.Update("鼠标穿透验证", "", true);
                    var through = (NativeTaskbar.GetWindowLongPtr(handle, -20) & 0x20) != 0 && !overlay.IsHitTestVisible;
                    settings.Lyrics.ClickThrough = false; overlay.Update("恢复交互验证", "", true);
                    var interactive = (NativeTaskbar.GetWindowLongPtr(handle, -20) & 0x20) == 0 && overlay.IsHitTestVisible;
                    success &= !overlaps && centeredY && hidden && restored && through && interactive;
                    reports.Add(new { bar.Device, bar.Scale, Actual = actual, Overlaps = overlaps, VerticalCenter = centeredY,
                        HiddenWhenUnavailable = hidden, Restored = restored, ClickThrough = through, InteractionRestored = interactive });
                }
                finally { overlay.Close(); }
            }
        }
        catch (Exception ex) { success = false; reports.Add(new { Error = ex.ToString() }); }
        File.WriteAllText(path, JsonSerializer.Serialize(new { Success = success, Reports = reports }, new JsonSerializerOptions { WriteIndented = true }));
        return success ? 0 : 1;
    }
}

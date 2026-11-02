using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Automation;

namespace TaskbarLyrics.App;

internal sealed record PixelRect(int Left, int Top, int Right, int Bottom)
{
    public int Width => Right - Left;
    public int Height => Bottom - Top;
    public bool Intersects(PixelRect other) => Left < other.Right && Right > other.Left && Top < other.Bottom && Bottom > other.Top;
}
internal sealed record TaskbarSnapshot(nint Handle, string Device, bool IsPrimary, PixelRect Bounds, double Scale,
    PixelRect[] Occupied, bool Reliable, DateTime CapturedAt, int CandidateCount = 0, string? Error = null);
public sealed record DisplayOption(string Id, string Label)
{
    public override string ToString() => Label;
}

internal sealed class TaskbarInspector : IDisposable
{
    private readonly ManualResetEvent stop = new(false);
    private readonly Thread worker;
    private volatile TaskbarSnapshot[] snapshots = [];
    private volatile bool avoidIcons = true;
    public TaskbarSnapshot[] Snapshots => snapshots;
    public void SetAvoidance(bool enabled) => avoidIcons = enabled;
    public TaskbarInspector()
    {
        worker = new Thread(Run) { IsBackground = true, Name = "Taskbar layout reader" };
        worker.SetApartmentState(ApartmentState.MTA);
        worker.Start();
    }
    public static DisplayOption[] Displays() => System.Windows.Forms.Screen.AllScreens
        .OrderByDescending(x => x.Primary).ThenBy(x => x.DeviceName)
        .Select((x, i) => new DisplayOption(x.DeviceName, $"显示器 {i + 1}{(x.Primary ? "（主屏）" : "")} · {x.Bounds.Width} × {x.Bounds.Height}")).ToArray();
    private void Run()
    {
        while (!stop.WaitOne(0))
        {
            try
            {
                var found = new List<TaskbarSnapshot>();
                NativeTaskbar.EnumWindows((handle, _) =>
                {
                    var name = new StringBuilder(128);
                    NativeTaskbar.GetClassName(handle, name, name.Capacity);
                    if (name.ToString() is not ("Shell_TrayWnd" or "Shell_SecondaryTrayWnd")) return true;
                    if (!NativeTaskbar.IsWindowVisible(handle) || !NativeTaskbar.GetWindowRect(handle, out var rect)) return true;
                    var screen = System.Windows.Forms.Screen.FromHandle(handle);
                    var bounds = rect.ToPixelRect();
                    if (bounds.Width <= 0 || bounds.Height <= 0) return true;
                    var scale = Math.Max(96, NativeTaskbar.GetDpiForWindow(handle)) / 96d;
                    found.Add(new(handle, screen.DeviceName, screen.Primary, bounds, scale, [], false, DateTime.UtcNow));
                    return true;
                }, 0);
                var results = new List<TaskbarSnapshot>();
                foreach (var bar in found)
                {
                    if (stop.WaitOne(0)) return;
                    results.Add(avoidIcons ? ReadOccupied(bar) : bar with { CapturedAt = DateTime.UtcNow });
                }
                snapshots = results.ToArray();
            }
            catch (Exception ex) when (ex is InvalidOperationException or COMException or ElementNotAvailableException or System.ComponentModel.Win32Exception)
            { snapshots = []; }
            stop.WaitOne(1000);
        }
    }
    private static TaskbarSnapshot ReadOccupied(TaskbarSnapshot bar)
    {
        try
        {
            var occupied = new List<PixelRect>();
            // Native notification-area children reserve the clock/tray even when UIA groups its buttons.
            NativeTaskbar.EnumChildWindows(bar.Handle, (handle, _) =>
            {
                var name = new StringBuilder(128);
                NativeTaskbar.GetClassName(handle, name, name.Capacity);
                if (name.ToString() is "TrayNotifyWnd" or "TrayClockWClass" or "TrayShowDesktopButtonWClass")
                    if (NativeTaskbar.IsWindowVisible(handle) && NativeTaskbar.GetWindowRect(handle, out var rect)
                        && rect.ToPixelRect().Intersects(bar.Bounds)) occupied.Add(rect.ToPixelRect());
                return true;
            }, 0);
            var types = new[] { ControlType.Button, ControlType.MenuItem, ControlType.TabItem,
                ControlType.ListItem, ControlType.CheckBox, ControlType.RadioButton, ControlType.Slider, ControlType.Hyperlink };
            var condition = new OrCondition(types.Select(x => new PropertyCondition(AutomationElement.ControlTypeProperty, x)).ToArray());
            var cache = new CacheRequest { TreeScope = TreeScope.Element, AutomationElementMode = AutomationElementMode.None };
            cache.Add(AutomationElement.BoundingRectangleProperty);
            cache.Add(AutomationElement.IsOffscreenProperty);
            AutomationElementCollection elements;
            var root = AutomationElement.FromHandle(bar.Handle);
            using (cache.Activate()) elements = root.FindAll(TreeScope.Descendants, condition);
            var count = 0;
            foreach (AutomationElement element in elements)
            {
                if (element.Cached.IsOffscreen) continue;
                var rect = element.Cached.BoundingRectangle;
                if (rect.IsEmpty || !double.IsFinite(rect.X + rect.Y + rect.Width + rect.Height)) continue;
                var pixels = new PixelRect((int)Math.Floor(rect.Left), (int)Math.Floor(rect.Top),
                    (int)Math.Ceiling(rect.Right), (int)Math.Ceiling(rect.Bottom));
                if (pixels.Width <= 0 || pixels.Height <= 0 || !pixels.Intersects(bar.Bounds)) continue;
                occupied.Add(pixels); count++;
            }
            return bar with { Occupied = occupied.ToArray(), Reliable = count > 0,
                CandidateCount = elements.Count, Error = count > 0 ? null : "No visible interactive elements intersect the taskbar." };
        }
        catch (Exception ex) when (ex is InvalidOperationException or COMException or ElementNotAvailableException or UnauthorizedAccessException)
        { return bar with { Reliable = false, CapturedAt = DateTime.UtcNow, Error = ex.GetType().Name + ": " + ex.Message }; }
    }
    public static bool IsCurrent(TaskbarSnapshot bar) => NativeTaskbar.IsWindow(bar.Handle)
        && NativeTaskbar.GetWindowRect(bar.Handle, out var rect) && rect.ToPixelRect() == bar.Bounds;
    public void Dispose()
    {
        stop.Set();
        // Explorer providers can block in COM. Never join that thread from the UI.
        // The background thread exits on its own once the provider call returns.
    }
}

internal static class NativeTaskbar
{
    [StructLayout(LayoutKind.Sequential)] internal struct Rect
    {
        public int Left, Top, Right, Bottom;
        public readonly PixelRect ToPixelRect() => new(Left, Top, Right, Bottom);
    }
    [StructLayout(LayoutKind.Sequential)] internal struct Point { public int X, Y; }
    internal delegate bool EnumProc(nint handle, nint parameter);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool EnumWindows(EnumProc callback, nint parameter);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool EnumChildWindows(nint parent, EnumProc callback, nint parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] internal static extern int GetClassName(nint handle, StringBuilder name, int count);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetWindowRect(nint handle, out Rect rect);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool IsWindowVisible(nint handle);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool IsWindow(nint handle);
    [DllImport("user32.dll")] internal static extern uint GetDpiForWindow(nint handle);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetWindowPos(nint handle, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool ShowWindow(nint handle, int command);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] internal static extern nint GetWindowLongPtr(nint handle, int index);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] internal static extern nint SetWindowLongPtr(nint handle, int index, nint value);
}

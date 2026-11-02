using System.Reflection;
using System.Windows.Media.Imaging;

namespace TaskbarLyrics.App;

internal static class AppAssets
{
    public static BitmapImage Image { get; } = LoadImage();
    private static BitmapImage LoadImage()
    {
        var image = new BitmapImage(new Uri("pack://application:,,,/TaskbarLyrics.App;component/Assets/app.png"));
        image.Freeze();
        return image;
    }
    public static System.Drawing.Icon CreateTrayIcon()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("TaskbarLyrics.App.app.ico")
            ?? throw new InvalidOperationException("Missing embedded app icon.");
        using var icon = new System.Drawing.Icon(stream, 32, 32);
        return (System.Drawing.Icon)icon.Clone();
    }
}

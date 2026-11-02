namespace TaskbarLyrics.Core;

public sealed record LyricBounds(double Left, double Top, double Width, double Height, double Offset);

public static class TaskbarLayout
{
    // All values use device-independent pixels. Only horizontal offset is user controlled.
    public static LyricBounds? Calculate(double left, double top, double width, double height, double offset)
    {
        if (!double.IsFinite(left + top + width + height) || height < 12 || width <= height || width < 32) return null;
        var lyricWidth = Math.Min(520, width - 16);
        var lyricHeight = Math.Min(34, height - 6);
        var centeredLeft = left + (width - lyricWidth) / 2;
        var limit = Math.Max(0, (width - lyricWidth) / 2 - 8);
        var appliedOffset = Math.Clamp(double.IsFinite(offset) ? offset : 0, -limit, limit);
        return new(centeredLeft + appliedOffset, top + (height - lyricHeight) / 2, lyricWidth, lyricHeight, appliedOffset);
    }
}

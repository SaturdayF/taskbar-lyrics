namespace TaskbarLyrics.Core;

public sealed record LyricBounds(double Left, double Top, double Width, double Height, double Offset);
public sealed record HorizontalSpan(double Start, double End);

public static class TaskbarLayout
{
    // All values use device-independent pixels. Only horizontal offset is user controlled.
    public static LyricBounds? Calculate(double left, double top, double width, double height, double offset,
        IReadOnlyList<HorizontalSpan>? occupied = null, bool avoidIcons = false)
    {
        if (!double.IsFinite(left + top + width + height) || height < 12 || width <= height || width < 32) return null;
        var lyricWidth = Math.Min(520, width - 16);
        var lyricHeight = Math.Min(34, height - 6);
        var centeredLeft = left + (width - lyricWidth) / 2;
        var limit = Math.Max(0, (width - lyricWidth) / 2 - 8);
        var appliedOffset = Math.Clamp(double.IsFinite(offset) ? offset : 0, -limit, limit);
        if (!avoidIcons) return new(centeredLeft + appliedOffset, top + (height - lyricHeight) / 2, lyricWidth, lyricHeight, appliedOffset);
        var start = left + 8;
        var end = left + width - 8;
        var spans = (occupied ?? []).Where(x => double.IsFinite(x.Start) && double.IsFinite(x.End) && x.End > x.Start)
            .Select(x => new HorizontalSpan(Math.Max(start, x.Start - 8), Math.Min(end, x.End + 8)))
            .Where(x => x.End > x.Start).OrderBy(x => x.Start).ToArray();
        var free = new List<HorizontalSpan>();
        var cursor = start;
        foreach (var span in spans)
        {
            if (span.Start > cursor) free.Add(new(cursor, span.Start));
            cursor = Math.Max(cursor, span.End);
        }
        if (cursor < end) free.Add(new(cursor, end));
        var candidates = free.Where(x => x.End - x.Start >= 160).Select(x =>
        {
            var size = Math.Min(lyricWidth, x.End - x.Start);
            var targetCenter = left + width / 2 + appliedOffset;
            var xPosition = Math.Clamp(targetCenter - size / 2, x.Start, x.End - size);
            return new LyricBounds(xPosition, top + (height - lyricHeight) / 2, size, lyricHeight,
                xPosition + size / 2 - (left + width / 2));
        }).ToArray();
        // Retain full width when possible, then use the nearest available space.
        // When no full-width lane remains, prefer the widest readable lane.
        var full = candidates.Where(x => x.Width >= lyricWidth - .01).ToArray();
        return (full.Length > 0 ? full.OrderBy(x => Math.Abs(x.Offset - appliedOffset))
            : candidates.OrderByDescending(x => x.Width).ThenBy(x => Math.Abs(x.Offset - appliedOffset))).FirstOrDefault();
    }
}

namespace TaskbarLyrics.Core;

public sealed class LyricPreferences
{
    public bool HideWhenPaused { get; set; }
    public bool ClickThrough { get; set; }
    public Dictionary<string, double> TrackOffsets { get; set; } = new();

    public static double ClampOffset(double value) => Math.Clamp(double.IsFinite(value) ? value : 0, -10, 10);
    public double TrackOffset(Track? track) => track is not null && TrackOffsets.TryGetValue(track.SyncKey, out var value) ? ClampOffset(value) : 0;
    public void SetTrackOffset(Track track, double value)
    {
        value = ClampOffset(value);
        if (value == 0) TrackOffsets.Remove(track.SyncKey);
        else TrackOffsets[track.SyncKey] = value;
    }
    public double EffectiveOffset(Track? track, double globalOffset) => ClampOffset(globalOffset) + TrackOffset(track);
    public bool ShouldHide(bool playing) => HideWhenPaused && !playing;
    public static string? CopyableLine(IReadOnlyList<LyricLine>? lines, double position)
    {
        if (lines is null) return null;
        var index = LrcParser.FindLine(lines, position);
        return index >= 0 && !string.IsNullOrWhiteSpace(lines[index].Text) ? lines[index].Text : null;
    }
}

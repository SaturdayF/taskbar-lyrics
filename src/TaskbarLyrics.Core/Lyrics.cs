using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace TaskbarLyrics.Core;

public enum PlayerKind { Auto, QQMusic, NetEase, AppleMusic }
public sealed record Track(PlayerKind Player, string Title, string Artist, string Album, double Duration)
{
    // SMTC can briefly omit duration. Calibration stays attached to the recording.
    public string SyncKey => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
        $"{Player}\n{TrackMatch.Normalize(Title)}\n{TrackMatch.Normalize(Artist)}\n{TrackMatch.Normalize(Album)}")));
    public string Key => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
        $"{Player}\n{Title}\n{Artist}\n{Album}\n{Math.Round(Duration)}")));
}
public sealed record LyricLine(double Seconds, string Text);
public sealed record LyricResult(string Source, string Lrc, string? Message = null)
{
    public IReadOnlyList<LyricLine> Lines { get; } = LrcParser.Parse(Lrc);
}

public static partial class LrcParser
{
    [GeneratedRegex(@"\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]")]
    private static partial Regex Timestamp();
    [GeneratedRegex(@"\[offset:([+-]?\d+)\]", RegexOptions.IgnoreCase)]
    private static partial Regex Offset();
    public static IReadOnlyList<LyricLine> Parse(string lrc)
    {
        var offsetMatch = Offset().Match(lrc);
        var offset = offsetMatch.Success && double.TryParse(offsetMatch.Groups[1].Value,
            CultureInfo.InvariantCulture, out var milliseconds) ? milliseconds / 1000 : 0;
        var result = new List<LyricLine>();
        foreach (var raw in lrc.Replace("\r", "").Split('\n'))
        {
            var tags = Timestamp().Matches(raw);
            if (tags.Count == 0) continue;
            var text = Timestamp().Replace(raw, "").Trim();
            foreach (Match tag in tags)
            {
                var seconds = int.Parse(tag.Groups[2].Value, CultureInfo.InvariantCulture);
                if (seconds >= 60) continue;
                var fraction = tag.Groups[3].Value;
                var time = int.Parse(tag.Groups[1].Value, CultureInfo.InvariantCulture) * 60 + seconds;
                result.Add(new(time + (fraction.Length == 0 ? 0 : int.Parse(fraction,
                    CultureInfo.InvariantCulture) / Math.Pow(10, fraction.Length)) + offset, text));
            }
        }
        return result.OrderBy(x => x.Seconds).ToArray();
    }
    public static int FindLine(IReadOnlyList<LyricLine> lines, double seconds)
    {
        int low = 0, high = lines.Count - 1, found = -1;
        while (low <= high)
        {
            var middle = low + (high - low) / 2;
            if (lines[middle].Seconds <= seconds) { found = middle; low = middle + 1; }
            else high = middle - 1;
        }
        return found;
    }
}

public static class TrackMatch
{
    public static string Normalize(string text) => string.Concat(text.Normalize(NormalizationForm.FormKC)
        .Where(char.IsLetterOrDigit)).ToLowerInvariant();
    public static bool Accept(Track track, string title, IEnumerable<string> artists, double duration)
    {
        var artist = Normalize(track.Artist);
        // A matching title alone can select a different performer. Require a known artist too.
        if (artist.Length == 0 || Normalize(track.Title) != Normalize(title)) return false;
        if (!artists.Any(x => Normalize(x) == artist)) return false;
        return track.Duration <= 0 || duration <= 0 || Math.Abs(track.Duration - duration) <= 3;
    }
}

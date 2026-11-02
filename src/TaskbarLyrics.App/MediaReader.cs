using TaskbarLyrics.Core;
using Windows.Media.Control;

namespace TaskbarLyrics.App;

public sealed record Playback(Track Track, double Position, bool Playing, double Rate, string AppId);
public sealed class MediaReader
{
    private GlobalSystemMediaTransportControlsSessionManager? manager;
    private string? previousId;
    public static PlayerKind Identify(string id)
    {
        id = id.ToLowerInvariant();
        if (id.Contains("qqmusic") || id.Contains("qq.music")) return PlayerKind.QQMusic;
        if (id.Contains("cloudmusic") || id.Contains("netease") || id.Contains("163music")) return PlayerKind.NetEase;
        if (id.Contains("applemusic") || id.Contains("appleinc.applemusic") || id.Contains("itunes")) return PlayerKind.AppleMusic;
        return PlayerKind.Auto;
    }
    public async Task<Playback?> ReadAsync(PlayerKind preferred)
    {
        manager ??= await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
        var sessions = manager.GetSessions().Where(x => Identify(x.SourceAppUserModelId) != PlayerKind.Auto)
            .Where(x => preferred == PlayerKind.Auto || Identify(x.SourceAppUserModelId) == preferred).ToArray();
        var session = sessions.OrderByDescending(x => x.GetPlaybackInfo().PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
            .ThenByDescending(x => x.SourceAppUserModelId == previousId).FirstOrDefault();
        if (session is null) { previousId = null; return null; }
        var properties = await session.TryGetMediaPropertiesAsync();
        var timeline = session.GetTimelineProperties();
        var playback = session.GetPlaybackInfo();
        previousId = session.SourceAppUserModelId;
        if (string.IsNullOrWhiteSpace(properties.Title)) return null;
        var playing = playback.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
        var rate = playback.PlaybackRate ?? 1;
        var elapsed = playing ? Math.Max(0, (DateTimeOffset.UtcNow - timeline.LastUpdatedTime).TotalSeconds) * rate : 0;
        var duration = Math.Max(0, (timeline.EndTime - timeline.StartTime).TotalSeconds);
        var position = (timeline.Position - timeline.StartTime).TotalSeconds + elapsed;
        return new(new(Identify(previousId), properties.Title, properties.Artist, properties.AlbumTitle, duration),
            Math.Clamp(position, 0, duration > 0 ? duration : double.MaxValue), playing, rate, previousId);
    }
    public async Task<string[]> ProbeAsync()
    {
        manager ??= await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
        var rows = new List<string>();
        foreach (var session in manager.GetSessions())
        {
            var media = await session.TryGetMediaPropertiesAsync();
            rows.Add($"{session.SourceAppUserModelId} | {Identify(session.SourceAppUserModelId)} | {media.Title} | {media.Artist} | {session.GetTimelineProperties().Position}");
        }
        return rows.ToArray();
    }
}

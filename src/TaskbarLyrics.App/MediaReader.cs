using TaskbarLyrics.Core;
using Windows.Media.Control;

namespace TaskbarLyrics.App;

public sealed record Playback(Track Track, double Position, bool Playing, double Rate, string AppId);
internal enum PlaybackCommand { Previous, Toggle, Next }
public sealed class MediaReader
{
    private GlobalSystemMediaTransportControlsSessionManager? manager;
    private string? previousId;
    private GlobalSystemMediaTransportControlsSession? activeSession;
    private Playback? lastPlayback;
    private long lastSample;
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
        activeSession = session;
        if (session is null) { previousId = null; lastPlayback = null; return null; }
        var properties = await session.TryGetMediaPropertiesAsync();
        var timeline = session.GetTimelineProperties();
        var playback = session.GetPlaybackInfo();
        previousId = session.SourceAppUserModelId;
        if (string.IsNullOrWhiteSpace(properties.Title)) return null;
        var playing = playback.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
        var rate = playback.PlaybackRate is double speed && double.IsFinite(speed) && speed > 0 ? speed : 1;
        var duration = Math.Max(0, (timeline.EndTime - timeline.StartTime).TotalSeconds);
        var artist = string.IsNullOrWhiteSpace(properties.Artist) ? properties.AlbumArtist : properties.Artist;
        var track = new Track(Identify(previousId), properties.Title, artist, properties.AlbumTitle, duration);
        if (track.Player == PlayerKind.AppleMusic && duration == 0 && lastPlayback is { } last && last.AppId == previousId &&
            last.Track.Title == track.Title && last.Track.Artist == track.Artist && last.Track.Album == track.Album)
            track = track with { Duration = last.Track.Duration };
        var position = (timeline.Position - timeline.StartTime).TotalSeconds;
        var elapsed = (DateTimeOffset.UtcNow - timeline.LastUpdatedTime).TotalSeconds;
        if (playing && elapsed > 0 && elapsed <= (duration > 0 ? duration : 300)) position += elapsed * rate;
        var now = System.Diagnostics.Stopwatch.GetTimestamp();
        if (timeline.LastUpdatedTime == DateTimeOffset.FromFileTime(0) && position <= 0 &&
            lastPlayback is { } previous && previous.AppId == previousId && previous.Track.Key == track.Key)
        {
            position = previous.Position;
            if (previous.Playing) position += System.Diagnostics.Stopwatch.GetElapsedTime(lastSample, now).TotalSeconds * previous.Rate;
        }
        lastSample = now;
        return lastPlayback = new(track, Math.Clamp(position, 0, duration > 0 ? duration : double.MaxValue), playing, rate, previousId);
    }
    internal async Task<bool> ControlAsync(string appId, PlaybackCommand command)
    {
        var session = activeSession;
        if (session is null || session.SourceAppUserModelId != appId) return false;
        if (command == PlaybackCommand.Previous) return await session.TrySkipPreviousAsync();
        if (command == PlaybackCommand.Next) return await session.TrySkipNextAsync();
        var info = session.GetPlaybackInfo();
        if (info.Controls.IsPlayPauseToggleEnabled) return await session.TryTogglePlayPauseAsync();
        return info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing
            ? await session.TryPauseAsync() : await session.TryPlayAsync();
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

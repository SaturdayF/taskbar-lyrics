using System.Net;
using System.Text;
using System.Text.Json.Nodes;

namespace TaskbarLyrics.Core;

public sealed class LyricService : IDisposable
{
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(8), MaxResponseContentBufferSize = 2 * 1024 * 1024 };
    private readonly string folder;
    public LyricService(string folder)
    {
        this.folder = folder;
        Directory.CreateDirectory(folder);
        http.DefaultRequestHeaders.UserAgent.ParseAdd("TaskbarLyrics/0.4.0");
    }
    public string LocalPath(Track track) => Path.Combine(folder, track.Key + ".lrc");
    public void Import(Track track, string lrc)
    {
        if (LrcParser.Parse(lrc).Count == 0) throw new FormatException("文件没有可识别的 LRC 时间戳。");
        File.WriteAllText(LocalPath(track), lrc, Encoding.UTF8);
    }
    public async Task<LyricResult> FindAsync(Track track, CancellationToken token)
    {
        var local = LocalPath(track);
        if (File.Exists(local)) return new("本地 LRC", await File.ReadAllTextAsync(local, token));
        var cache = Path.Combine(folder, track.Key + ".cache.json");
        try
        {
            if (File.Exists(cache) && DateTime.UtcNow - File.GetLastWriteTimeUtc(cache) < TimeSpan.FromDays(7))
            {
                var saved = JsonNode.Parse(await File.ReadAllTextAsync(cache, token))!;
                return new(S(saved, "source") + " · 缓存", S(saved, "lrc"));
            }
        }
        catch (Exception ex) when (ex is System.Text.Json.JsonException or IOException) { /* Cache is disposable. */ }
        var errors = new List<string>();
        var providers = new List<(string Name, Func<Track, CancellationToken, Task<string?>> Fetch)>();
        if (track.Player == PlayerKind.QQMusic) providers.Add(("QQ 音乐", QQAsync));
        if (track.Player == PlayerKind.NetEase) providers.Add(("网易云音乐", NetEaseAsync));
        providers.Add(("LRCLIB", LrclibAsync));
        if (track.Player == PlayerKind.AppleMusic) providers.Add(("网易云音乐", NetEaseAsync));
        foreach (var provider in providers)
        {
            token.ThrowIfCancellationRequested();
            try
            {
                var lrc = await provider.Fetch(track, token);
                if (string.IsNullOrWhiteSpace(lrc) || LrcParser.Parse(lrc).Count == 0) continue;
                try { await File.WriteAllTextAsync(cache, new JsonObject { ["source"] = provider.Name, ["lrc"] = lrc }.ToJsonString(), token); }
                catch (IOException) { /* Lyrics can still be displayed without a disk cache. */ }
                return new(provider.Name, lrc);
            }
            catch (OperationCanceledException) when (!token.IsCancellationRequested) { errors.Add(provider.Name + "超时"); }
            catch (Exception ex) when (ex is HttpRequestException or System.Text.Json.JsonException or FormatException or InvalidOperationException)
            { errors.Add(provider.Name + "暂不可用"); }
        }
        return new("无同步歌词", "", errors.Count > 0 ? string.Join("；", errors) + "；可重试或导入 LRC" : "未找到匹配的同步歌词，可导入 LRC");
    }
    private static string S(JsonNode? node, string key) => node?[key]?.GetValue<string>() ?? "";
    private static double N(JsonNode? node, string key) => node?[key]?.GetValue<double>() ?? 0;
    private static string Q(string value) => Uri.EscapeDataString(value);
    private async Task<JsonNode?> GetAsync(string url, string? referer, CancellationToken token)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        if (referer is not null) request.Headers.Referrer = new Uri(referer);
        using var response = await http.SendAsync(request, token);
        if (response.StatusCode == HttpStatusCode.NotFound) return null;
        response.EnsureSuccessStatusCode();
        return JsonNode.Parse(await response.Content.ReadAsStringAsync(token));
    }
    private async Task<string?> LrclibAsync(Track track, CancellationToken token)
    {
        var url = $"https://lrclib.net/api/get?track_name={Q(track.Title)}&artist_name={Q(track.Artist)}";
        if (!string.IsNullOrWhiteSpace(track.Album)) url += "&album_name=" + Q(track.Album);
        if (track.Duration is >= 1 and <= 3600) url += "&duration=" + Math.Round(track.Duration).ToString(System.Globalization.CultureInfo.InvariantCulture);
        string? Match(JsonNode? candidate)
        {
            if (candidate is null || !TrackMatch.Accept(track, S(candidate, "trackName"), [S(candidate, "artistName")], N(candidate, "duration"))) return null;
            var text = S(candidate, "syncedLyrics");
            return LrcParser.Parse(text).Count > 0 ? text : null;
        }
        try
        {
            var exact = Match(await GetAsync(url, null, token));
            if (exact is not null) return exact;
        }
        catch (HttpRequestException) when (!token.IsCancellationRequested) { /* Search is independently available. */ }
        catch (OperationCanceledException) when (!token.IsCancellationRequested) { /* The exact lookup timed out. */ }
        var search = await GetAsync($"https://lrclib.net/api/search?track_name={Q(track.Title)}&artist_name={Q(track.Artist)}", null, token);
        if (search is JsonArray candidates)
        {
            var album = TrackMatch.Normalize(track.Album);
            foreach (var candidate in candidates.OrderByDescending(item => TrackMatch.Normalize(S(item, "albumName")) == album))
                if (Match(candidate) is { } text) return text;
        }
        return null;
    }
    private async Task<string?> NetEaseAsync(Track track, CancellationToken token)
    {
        var search = await GetAsync($"https://music.163.com/api/search/get?s={Q(track.Title + " " + track.Artist)}&type=1&limit=12", "https://music.163.com/", token);
        if (search?["result"]?["songs"] is not JsonArray songs) return null;
        foreach (var song in songs)
        {
            var artists = (song?["artists"] as JsonArray)?.Select(x => S(x, "name")) ?? [];
            if (!TrackMatch.Accept(track, S(song, "name"), artists, N(song, "duration") / 1000)) continue;
            var id = song?["id"]?.ToString();
            var lyrics = await GetAsync($"https://music.163.com/api/song/lyric?id={id}&lv=-1&kv=-1&tv=-1", "https://music.163.com/", token);
            var text = S(lyrics?["lrc"], "lyric");
            if (LrcParser.Parse(text).Count > 0) return text;
        }
        return null;
    }
    private async Task<string?> QQAsync(Track track, CancellationToken token)
    {
        var search = await GetAsync($"https://c.y.qq.com/soso/fcgi-bin/client_search_cp?format=json&w={Q(track.Title + " " + track.Artist)}&n=12&p=1&t=0&cr=1", "https://y.qq.com/", token);
        if (search?["data"]?["song"]?["list"] is not JsonArray songs) return null;
        foreach (var song in songs)
        {
            var artists = (song?["singer"] as JsonArray)?.Select(x => S(x, "name")) ?? [];
            if (!TrackMatch.Accept(track, S(song, "songname"), artists, N(song, "interval"))) continue;
            var mid = Q(S(song, "songmid"));
            var lyrics = await GetAsync($"https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid={mid}&format=json&nobase64=1&g_tk=5381&platform=yqq&loginUin=0&hostUin=0&inCharset=utf8&outCharset=utf-8&needNewCode=0", "https://y.qq.com/", token);
            return WebUtility.HtmlDecode(S(lyrics, "lyric"));
        }
        return null;
    }
    public void Dispose() => http.Dispose();
}

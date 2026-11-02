using TaskbarLyrics.Core;

int passed = 0;
void Check(bool valid, string name)
{
    if (!valid) throw new Exception("FAIL: " + name);
    Console.WriteLine("PASS: " + name); passed++;
}
var lines = LrcParser.Parse("[ar:示例]\n[offset:-250]\n[00:03.5][00:01.125]第二句\n[00:00.00]第一句\n[00:61]无效\n[00:04.12]\n");
Check(lines.Count == 4, "metadata ignored, multiple timestamps expanded, invalid seconds rejected");
Check(Math.Abs(lines[0].Seconds + .25) < .00001, "global negative LRC offset");
Check(Math.Abs(lines[1].Seconds - .875) < .00001, "three digit timestamp fraction");
Check(Math.Abs(lines[2].Seconds - 3.25) < .00001, "one digit timestamp fraction and chronological sort");
Check(Math.Abs(lines[3].Seconds - 3.87) < .00001 && lines[3].Text == "", "two digit fraction and instrumental interval");
Check(LrcParser.FindLine(lines, -1) == -1, "before first lyric");
Check(LrcParser.FindLine(lines, .875) == 1, "exact timestamp boundary");
Check(LrcParser.FindLine(lines, 100) == 3, "last line");
Check(LrcParser.FindLine(lines, 0) == 0, "seek backwards");
Check(LrcParser.FindLine([], 1) == -1, "empty lyrics");
var track = new Track(PlayerKind.AppleMusic, "Hello!", "Adele", "25", 295);
Check(TrackMatch.Accept(track, "Hello", ["Adele"], 296), "normalized title and matching artist");
Check(!TrackMatch.Accept(track, "Hello", ["Lionel Richie"], 295), "reject wrong performer");
Check(!TrackMatch.Accept(track, "Hello (Live)", ["Adele"], 295), "reject live version");
Check(!TrackMatch.Accept(track, "Hello", ["Adele"], 315), "reject wrong duration");
Check(!TrackMatch.Accept(track with { Artist = "" }, "Hello", [""], 295), "reject unknown performer");
Check(track.Key == (track with { }).Key && track.Key != (track with { Album = "Other" }).Key, "stable per recording cache key");
var folder = Path.Combine(Path.GetTempPath(), "TaskbarLyrics-test-" + Guid.NewGuid());
try
{
    using var service = new LyricService(folder);
    service.Import(track, "[00:00.00]Hello");
    var result = await service.FindAsync(track, CancellationToken.None);
    Check(result.Source == "本地 LRC" && result.Lines[0].Text == "Hello", "local LRC takes priority without network");
    try { service.Import(track, "plain text"); Check(false, "invalid import"); }
    catch (FormatException) { Check(true, "reject unsynchronized import"); }
}
finally { Directory.Delete(folder, true); }
Console.WriteLine($"{passed} checks passed.");
if (args.Contains("--smoke"))
{
    using var service = new LyricService(Path.Combine(Path.GetTempPath(), "TaskbarLyrics-smoke"));
    foreach (var sample in new[] {
        new Track(PlayerKind.QQMusic, "晴天", "周杰伦", "叶惠美", 269),
        new Track(PlayerKind.NetEase, "成全", "林宥嘉", "", 308),
        new Track(PlayerKind.AppleMusic, "Hello", "Adele", "25", 295) })
    {
        var result = await service.FindAsync(sample, CancellationToken.None);
        Console.WriteLine($"SMOKE {sample.Player}: source={result.Source}, timedLines={result.Lines.Count}, status={result.Message ?? "OK"}");
    }
}

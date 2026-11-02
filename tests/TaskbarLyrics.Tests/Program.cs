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
var centered = TaskbarLayout.Calculate(0, 1032, 1920, 48, 0)!;
Check(centered.Left + centered.Width / 2 == 960, "lyric bar horizontally centered by default");
Check(centered.Top + centered.Height / 2 == 1056, "lyric bar vertically centered in taskbar");
var moved = TaskbarLayout.Calculate(0, 1032, 1920, 48, 220)!;
Check(moved.Left - centered.Left == 220 && moved.Top == centered.Top, "horizontal movement preserves vertical position");
var leftEdge = TaskbarLayout.Calculate(-1920, -48, 1920, 48, -100000)!;
var rightEdge = TaskbarLayout.Calculate(-1920, -48, 1920, 48, 100000)!;
Check(leftEdge.Left == -1912 && rightEdge.Left + rightEdge.Width == -8, "both horizontal edges clamped on negative origin monitor");
Check(leftEdge.Top == rightEdge.Top, "vertical anchor unaffected by extreme drag offsets");
var compact = TaskbarLayout.Calculate(0, 0, 400, 24, 0)!;
Check(compact.Width <= 384 && compact.Height <= 18 && compact.Top == 3, "small taskbar sizing stays within bar");
Check(TaskbarLayout.Calculate(0, 1078, 1920, 2, 0) is null, "hidden taskbar does not expose floating lyrics");
Check(TaskbarLayout.Calculate(0, 0, 48, 1080, 0) is null, "vertical taskbar does not produce a free floating window");
Check(TaskbarLayout.Calculate(0, 0, 1920, 48, double.NaN)!.Offset == 0, "invalid saved offset returns to center");
var resized = TaskbarLayout.Calculate(0, 1032, 1280, 48, 100000)!;
Check(resized.Left + resized.Width <= 1272, "display resize clamps stored offset");
var blocked = new[] { new HorizontalSpan(650, 1270) };
var avoided = TaskbarLayout.Calculate(0, 1032, 1920, 48, 0, blocked, true)!;
Check(avoided.Width == 520 && (avoided.Left + avoided.Width <= 642 || avoided.Left >= 1278), "move full lyric bar away from centered taskbar icons");
Check(avoided.Top == centered.Top, "icon avoidance preserves vertical center");
var reduced = TaskbarLayout.Calculate(0, 0, 800, 48, 0, [new(0, 200), new(600, 800)], true)!;
Check(reduced.Width == 384 && reduced.Left == 208, "shorten lyrics to fit limited free space");
Check(TaskbarLayout.Calculate(0, 0, 800, 48, 0, [new(0, 350), new(500, 800)], true) is null, "hide when no readable free lane remains");
Check(TaskbarLayout.Calculate(0, 0, 800, 48, 0, [new(0, 800)], true) is null, "fully occupied taskbar hides lyrics");
var merged = TaskbarLayout.Calculate(0, 0, 1200, 48, 0, [new(500, 700), new(0, 300), new(250, 520)], true)!;
Check(merged.Left >= 708 && merged.Left + merged.Width <= 1192, "merge overlapping and unordered occupied rectangles");
var unblocked = TaskbarLayout.Calculate(0, 1032, 1920, 48, 0, blocked, false)!;
Check(unblocked == centered, "avoidance switch restores centered layout");
Check(TaskbarLayout.Calculate(0, 1032, 1920, 48, 0, [], true) == centered, "known empty taskbar uses normal center");
var negativeAvoid = TaskbarLayout.Calculate(-1920, -48, 1920, 48, -900, [new(-1920, -1400)], true)!;
Check(negativeAvoid.Left >= -1392 && negativeAvoid.Top == -41, "avoid icons on a monitor with negative screen coordinates");
var gapPreference = TaskbarLayout.Calculate(0, 0, 1600, 48, 0, [new(500, 1000)], true)!;
Check(gapPreference.Left >= 1008 && gapPreference.Width == 520, "prefer a full width lane over a closer short lane");
var before = TaskbarLayout.Calculate(0, 0, 1920, 48, 0, blocked, true);
var after = TaskbarLayout.Calculate(0, 0, 1920, 48, 0, [], true);
Check(before!.Left != after!.Left && after.Offset == 0, "return to requested center after icons disappear");
Check(TaskbarLayout.Calculate(0, 0, 1920, 48, 0, [new(double.NaN, 20), new(40, 30)], true)!.Offset == 0, "ignore malformed occupied spans");
var screens = new[] { new DisplayTarget("secondary", false), new DisplayTarget("primary", true) };
Check(DisplaySelection.Resolve(screens, DisplayMode.Primary, null).SequenceEqual(["primary"]), "choose primary screen independently of enumeration order");
Check(DisplaySelection.Resolve(screens, DisplayMode.Selected, "SECONDARY").SequenceEqual(["secondary"]), "select monitor by stable case insensitive device ID");
Check(DisplaySelection.Resolve(screens, DisplayMode.All, null).Count == 2, "all display mode returns every available taskbar");
Check(DisplaySelection.Resolve(screens, DisplayMode.Selected, "disconnected").SequenceEqual(["primary"]), "disconnected selected display falls back to primary");
Check(DisplaySelection.Resolve(screens, DisplayMode.Selected, "secondary").SequenceEqual(["secondary"]), "reconnected preferred display selected again");
Check(DisplaySelection.Resolve([], DisplayMode.All, null).Count == 0, "no taskbars produces no offscreen window");
Check(DisplaySelection.Resolve([new("only", false)], DisplayMode.Primary, null).SequenceEqual(["only"]), "fall back to available taskbar when primary is absent");
Check(DisplaySelection.Resolve([new("a", true), new("A", false)], DisplayMode.All, null).Count == 1, "deduplicate taskbars for same display");
foreach (var scale in new[] { 1d, 1.25, 1.5, 2 })
{
    var scaled = TaskbarLayout.Calculate(0, 0, 1920 / scale, 48, 0, [new(700 / scale, 1300 / scale)], true)!;
    var x = (int)Math.Round(scaled.Left * scale);
    var right = x + (int)Math.Floor(scaled.Width * scale);
    Check(right <= 700 || x >= 1300, $"physical icon bounds remain clear at {scale * 100:0}% scaling");
}
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

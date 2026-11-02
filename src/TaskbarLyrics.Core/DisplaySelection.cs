namespace TaskbarLyrics.Core;

public enum DisplayMode { Primary, Selected, All }
public sealed record DisplayTarget(string Id, bool IsPrimary);

public static class DisplaySelection
{
    public static IReadOnlyList<string> Resolve(IReadOnlyList<DisplayTarget> available, DisplayMode mode, string? selected)
    {
        if (available.Count == 0) return [];
        if (mode == DisplayMode.All) return available.Select(x => x.Id).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var choice = mode == DisplayMode.Selected
            ? available.FirstOrDefault(x => string.Equals(x.Id, selected, StringComparison.OrdinalIgnoreCase)) : null;
        choice ??= available.FirstOrDefault(x => x.IsPrimary) ?? available[0];
        return [choice.Id];
    }
}

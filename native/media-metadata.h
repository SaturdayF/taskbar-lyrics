#pragma once
#include <algorithm>
#include <cwctype>
#include <string>

namespace metadata {
struct Fields { std::wstring artist, album; };
inline std::wstring trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
inline Fields resolve(std::wstring source, const std::wstring& artist, const std::wstring& album, const std::wstring& albumArtist) {
    Fields result{trim(artist), trim(album)};
    const auto fallback = trim(albumArtist);
    if (result.artist.empty()) result.artist = fallback;
    std::transform(source.begin(), source.end(), source.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    // Apple Music for Windows can publish "artist — album" as Artist while
    // leaving AlbumTitle empty. Do not split ordinary hyphens or other players.
    if (source.find(L"applemusic") == std::wstring::npos) return result;
    const std::wstring separator = L" — ";
    const auto split = result.artist.find(separator);
    if (split == std::wstring::npos) return result;
    const auto prefix = trim(result.artist.substr(0, split));
    const auto suffix = trim(result.artist.substr(split + separator.size()));
    if (prefix.empty() || suffix.empty()) return result;
    // Some Apple Music versions repeat the same composite in AlbumArtist.
    // That duplicate is not independent evidence that the artist contains a dash.
    if (!fallback.empty() && fallback != prefix && fallback != result.artist) return result;
    if (!result.album.empty() && result.album != suffix) return result;
    if (result.album.empty() && fallback != prefix && suffix.find(separator) != std::wstring::npos) return result;
    result.artist = prefix;
    if (result.album.empty()) result.album = suffix;
    return result;
}
}

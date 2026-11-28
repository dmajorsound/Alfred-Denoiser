#pragma once
// Is there a newer Alfred? The pure part of the update check (no network), as in OLAAF:
// reading the answer of GitHub's release list and comparing version numbers.
#include <string>
#include <vector>

namespace alfred_update
{
// the first "key": "value" in the JSON text
inline std::string jsonString(const std::string& js, const char* key)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = js.find(k);
    if (p == std::string::npos) return {};
    p = js.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = js.find('"', p);
    if (p == std::string::npos) return {};
    const size_t e = js.find('"', p + 1);
    if (e == std::string::npos || e - p > 300) return {};
    return js.substr(p + 1, e - p - 1);
}

// "v0.36-beta" -> 0, 36, 0   (stops at the suffix)
inline std::vector<int> versionParts(const std::string& v)
{
    std::vector<int> r;
    int cur = -1;
    for (char ch : v)
    {
        if (ch >= '0' && ch <= '9') cur = (cur < 0 ? 0 : cur * 10) + (ch - '0');
        else if (cur >= 0)
        {
            r.push_back(cur);
            cur = -1;
            if (ch != '.') break;
        }
    }
    if (cur >= 0) r.push_back(cur);
    while (r.size() < 3) r.push_back(0);
    return r;
}

inline bool newer(const std::string& tag, const std::string& mine) { return versionParts(tag) > versionParts(mine); }

// The installer for this system among the newest release's files (its "browser_download_url"s): on macOS the
// one ending in "-mac.dmg", on Windows the one ending in "win64.exe" that is not the uninstaller. Only files of
// this repository's releases count; "" when there is none (Linux, or an odd release).
inline std::string assetUrl(const std::string& js, const std::string& prefix, bool mac)
{
    const std::string key = "\"browser_download_url\"";
    size_t p = 0;
    while ((p = js.find(key, p)) != std::string::npos)
    {
        p += key.size();
        const size_t q = js.find('"', js.find(':', p));
        if (q == std::string::npos) break;
        const size_t e = js.find('"', q + 1);
        if (e == std::string::npos || e - q > 400) break;
        const std::string u = js.substr(q + 1, e - q - 1);
        p = e;
        auto ends = [&](const char* sfx) { const size_t n = std::string(sfx).size(); return u.size() > n && u.compare(u.size() - n, n, sfx) == 0; };
        if (u.compare(0, prefix.size(), prefix) != 0 || u.find("uninstall") != std::string::npos) continue;
        if (u.find_first_of(" \"'`<>\\") != std::string::npos) continue;
        if (mac ? ends("-mac.dmg") : (ends("-win64.exe") || ends("_win64.exe"))) return u;
    }
    return {};
}

// from the release list: is its newest entry newer than this version? Fills tag and page (github.com only).
inline bool parse(const std::string& js, const std::string& mine, std::string& tag, std::string& url)
{
    tag = jsonString(js, "tag_name");
    url = jsonString(js, "html_url");
    return !tag.empty() && newer(tag, mine) && url.compare(0, 19, "https://github.com/") == 0;
}
}

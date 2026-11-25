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

// from the release list: is its newest entry newer than this version? Fills tag and page (github.com only).
inline bool parse(const std::string& js, const std::string& mine, std::string& tag, std::string& url)
{
    tag = jsonString(js, "tag_name");
    url = jsonString(js, "html_url");
    return !tag.empty() && newer(tag, mine) && url.compare(0, 19, "https://github.com/") == 0;
}
}

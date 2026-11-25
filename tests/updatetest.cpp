// Checks the update logic against a real answer of GitHub's release list (read from a file).
// updatetest <releases.json>
#include "../src/plugin/UpdateLogic.h"
#include <cstdio>
#include <fstream>
#include <sstream>
int main(int argc, char** argv)
{
    using namespace alfred_update;
    int bad = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++bad; std::printf("FAILED: %s\n", what); } };
    check(newer("v0.36-beta", "0.35.0"), "0.36-beta is newer than 0.35.0");
    check(!newer("v0.35-beta", "0.35.0"), "0.35-beta is not newer than 0.35.0");
    check(!newer("v0.9-beta", "0.35.0"), "0.9 is older than 0.35");
    check(newer("v1.0", "0.99.0"), "1.0 is newer than 0.99");
    check(newer("v0.35.1", "0.35.0"), "0.35.1 is newer than 0.35.0");
    std::string t, u;
    check(!parse("{\"tag_name\":\"v9.0\",\"html_url\":\"https://evil.example/x\"}", "0.35.0", t, u), "only github.com pages are accepted");
    check(!parse("not json at all", "0.35.0", t, u), "nonsense gives no update");
    if (argc > 1)
    {
        std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
        const bool upOld = parse(ss.str(), "0.1.0", t, u);
        std::printf("real answer: tag '%s', page '%s'\n", t.c_str(), u.c_str());
        check(upOld, "seen from 0.1.0 the real release is newer");
        check(!parse(ss.str(), "99.0.0", t, u), "seen from 99.0.0 nothing is newer");
    }
    std::printf(bad ? "%d checks failed\n" : "all checks passed\n", bad);
    return bad ? 1 : 0;
}

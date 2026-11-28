// Run by the GitHub build on macOS and Windows: downloads a released installer the way the update note in the
// footer does (the same code, src/plugin/UpdateDownload.h), and checks that it arrived whole.
//   AlfredUpdateTest <release file URL> <folder>
#include <juce_core/juce_core.h>
#include "../src/plugin/UpdateDownload.h"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: AlfredUpdateTest <url> <folder>\n"); return 2; }
    auto& d = UpdateDownload::get();
    const juce::File dir { juce::String(argv[2]) };
    dir.createDirectory();
    d.setFolder(dir, false);
    d.start(juce::String(argv[1]), false);
    double prog = 0.0; juce::File path; juce::String err;
    int st = UpdateDownload::kLoading;
    for (int i = 0; i < 3000 && (st = d.status(&prog, &path, &err)) == UpdateDownload::kLoading; ++i) juce::Thread::sleep(100);
    if (st != UpdateDownload::kThere) { std::fprintf(stderr, "download failed (state %d): %s\n", st, err.toRawUTF8()); return 1; }
    const auto size = path.getSize();
    std::printf("downloaded %s (%lld bytes)\n", path.getFullPathName().toRawUTF8(), (long long)size);
    if (path.getSiblingFile(path.getFileName() + ".part").exists()) { std::fprintf(stderr, "the .part file is still there\n"); return 1; }
    return size > 500000 ? 0 : 1;      // an installer, not an error page
}

#pragma once
// Fetches the newer Alfred's installer straight from GitHub, as OLAAF does: in the background, first as
// "<file>.part", renamed only when it is complete (its size equals the Content-Length), otherwise deleted.
// Redirects are followed (GitHub serves the files from objects.githubusercontent.com); the download stops when
// nothing arrives for 30 seconds. Only juce_core and juce_data_structures, so the GitHub build can test it with a
// small program.
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <functional>
#include <memory>

class UpdateDownload : private juce::Thread
{
public:
    enum { kNone = 0, kLoading = 1, kThere = 2, kFailed = 3 };

    static UpdateDownload& get() { static UpdateDownload d; return d; }

    // where downloads go: the folder chosen once (kept in Alfred's settings), else Downloads in the home folder
    juce::File folder()
    {
        const juce::String saved = settings().getValue("updateFolder");
        const juce::File f(saved);
        if (saved.isNotEmpty() && juce::File::isAbsolutePath(saved) && f.isDirectory()) return f;
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads");
    }
    void setFolder(const juce::File& dir, bool save = true)
    {
        if (save) { settings().setValue("updateFolder", dir.getFullPathName()); settings().saveIfNeeded(); }
        else testFolder = dir;
    }

    // starts the download of `url` (a release file of this repository); install: open it when it is there
    void start(const juce::String& url, bool install)
    {
        if (state.load() == kLoading) return;
        if (!url.startsWith("https://github.com/dmajorsound/Alfred-Denoiser/releases/download/") || url.containsAnyOf(" \"'`<>\\"))
        {
            fail("not a release file of Alfred");
            return;
        }
        stopThread(2000);
        { const juce::ScopedLock l(lock); source = url; errorText.clear(); }
        openWhenThere = install;
        progress = 0.0;
        state = kLoading;
        startThread();
    }

    // kNone, kLoading (progress 0..1, -1 when the size is unknown), kThere (path), kFailed (error)
    int status(double* prog = nullptr, juce::File* path = nullptr, juce::String* error = nullptr)
    {
        const int s = state.load();
        if (prog) *prog = progress.load();
        const juce::ScopedLock l(lock);
        if (path) *path = result;
        if (error) *error = errorText;
        return s;
    }

    // called on the message thread when an installer has arrived and should open (set by the editor)
    std::function<void(const juce::File&)> onReadyToInstall;

    // opens the installer: macOS mounts the disk image quietly and opens "Install Alfred.pkg" (if that fails, the
    // disk image itself), Windows starts the setup. No shell: the arguments go to the programs one by one.
    static void openInstaller(const juce::File& file)
    {
        if (!file.existsAsFile()) return;
       #if JUCE_MAC
        if (file.hasFileExtension("dmg"))
        {
            const juce::String mount = "/tmp/alfred-update";
            const juce::File pkg(mount + "/Install Alfred.pkg");
            if (!pkg.existsAsFile())
            {
                juce::ChildProcess attach;
                if (attach.start(juce::StringArray { "/usr/bin/hdiutil", "attach", "-nobrowse", "-noautoopen", "-quiet",
                                                     "-mountpoint", mount, file.getFullPathName() }, 0))
                    attach.waitForProcessToFinish(60000);
            }
            juce::ChildProcess open;
            if (pkg.existsAsFile() && open.start(juce::StringArray { "/usr/bin/open", pkg.getFullPathName() }, 0)) return;
            juce::ChildProcess dmg;
            dmg.start(juce::StringArray { "/usr/bin/open", file.getFullPathName() }, 0);
            return;
        }
       #endif
        file.startAsProcess();
    }

    static void reveal(const juce::File& file) { if (file.exists()) file.revealToUser(); }

private:
    UpdateDownload() : juce::Thread("Alfred update download") {}
    ~UpdateDownload() override { stopThread(5000); }

    juce::PropertiesFile& settings()
    {
        static juce::PropertiesFile p([]
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Alfred";
            o.filenameSuffix = ".settings";
            o.folderName = "dmajorsound";
            o.osxLibrarySubFolder = "Application Support";
            return o;
        }());
        return p;
    }

    void fail(const juce::String& why)
    {
        { const juce::ScopedLock l(lock); errorText = why; result = juce::File(); }
        state = kFailed;
    }

    struct Listener : juce::URL::DownloadTaskListener
    {
        std::atomic<juce::int64> got { 0 }, total { -1 };
        std::atomic<bool> done { false }, ok { false };
        void progress(juce::URL::DownloadTask*, juce::int64 bytes, juce::int64 length) override { got = bytes; total = length; }
        void finished(juce::URL::DownloadTask*, bool success) override { ok = success; done = true; }
    };

    void run() override
    {
        juce::String url;
        { const juce::ScopedLock l(lock); url = source; }
        const juce::File dir = testFolder != juce::File() ? testFolder : folder();
        if (!dir.isDirectory() && !dir.createDirectory()) { fail("cannot write to " + dir.getFullPathName()); return; }
        const juce::String name = juce::URL(url).getFileName();
        const juce::File target = dir.getChildFile(name), part = dir.getChildFile(name + ".part");
        part.deleteFile();

        Listener ls;
        auto opts = juce::URL::DownloadTaskOptions()
                        .withExtraHeaders("User-Agent: Alfred/" JucePlugin_VersionString "\r\nAccept: application/octet-stream")
                        .withListener(&ls);
        std::unique_ptr<juce::URL::DownloadTask> task = juce::URL(url).downloadToFile(part, opts);
        if (task == nullptr) { part.deleteFile(); fail("no connection to GitHub"); return; }

        juce::int64 last = -1;
        auto lastChange = juce::Time::getMillisecondCounterHiRes();
        while (!ls.done.load() && !task->isFinished())
        {
            if (threadShouldExit()) { task.reset(); part.deleteFile(); fail("stopped"); return; }
            const juce::int64 g = ls.got.load(), t = ls.total.load();
            progress = t > 0 ? juce::jlimit(0.0, 1.0, (double)g / (double)t) : -1.0;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (g != last) { last = g; lastChange = now; }
            else if (now - lastChange > 30000.0) { task.reset(); part.deleteFile(); fail("nothing arrived for 30 seconds"); return; }
            wait(100);
        }
        const bool error = task->hadError() || !ls.ok.load();
        const int code = task->statusCode();
        const juce::int64 total = task->getTotalLength();
        task.reset();
        const juce::int64 size = part.getSize();
        if (error || (code != 0 && code != 200)) { part.deleteFile(); fail(code > 0 ? "GitHub answered " + juce::String(code) : juce::String("the connection broke off")); return; }
        if (total <= 0 || size != total) { part.deleteFile(); fail("incomplete (" + juce::String(size) + " of " + juce::String(total) + " bytes)"); return; }
        target.deleteFile();
        if (!part.moveFileTo(target)) { part.deleteFile(); fail("cannot write " + target.getFullPathName()); return; }
        { const juce::ScopedLock l(lock); result = target; }
        progress = 1.0;
        state = kThere;
        if (openWhenThere && onReadyToInstall) onReadyToInstall(target);
    }

    juce::CriticalSection lock;
    juce::String source, errorText;
    juce::File result, testFolder;
    std::atomic<int> state { kNone };
    std::atomic<double> progress { 0.0 };
    std::atomic<bool> openWhenThere { false };
};

#pragma once
// Asks GitHub once per session, in the background, whether a newer Alfred has been released, and gives up
// quietly when there is no connection or no answer within a few seconds. Started when the plug-in window is
// first opened (not while a host only scans its plug-ins).
#include <juce_core/juce_core.h>
#include "UpdateLogic.h"
#include <atomic>

class UpdateCheck : private juce::Thread
{
public:
    UpdateCheck() : juce::Thread("Alfred update check") {}
    ~UpdateCheck() override { stopThread(9000); }

    void start() { if (!started().exchange(true)) startThread(); }

    // 0 still asking (or not started), 1 a newer release (numbers and page filled in), 2 nothing newer or no answer
    static int state(int& major, int& minor, juce::String& page)
    {
        const int s = result().load();
        if (s == 1) { const juce::ScopedLock l(lock()); major = data().major; minor = data().minor; page = data().url; }
        return s;
    }

    // the newer release's installer for this system, straight from GitHub ("" when there is none)
    static juce::String asset() { const juce::ScopedLock l(lock()); return data().asset; }
    static void setAssetForTest(const juce::String& u) { const juce::ScopedLock l(lock()); data().asset = u; }

    // opens the release page in the browser (only github.com addresses)
    static void openPage(const juce::String& page)
    {
        if (page.startsWith("https://github.com/") && !page.containsAnyOf(" \"'`<>")) juce::URL(page).launchInDefaultBrowser();
    }

private:
    struct Data { int major = 0, minor = 0; juce::String url, asset; };
    static std::atomic<bool>& started() { static std::atomic<bool> v { false }; return v; }
    static std::atomic<int>& result() { static std::atomic<int> v { 0 }; return v; }
    static juce::CriticalSection& lock() { static juce::CriticalSection c; return c; }
    static Data& data() { static Data d; return d; }

    void run() override
    {
        const juce::URL api("https://api.github.com/repos/dmajorsound/Alfred-Denoiser/releases?per_page=1");   // newest, betas included
        int status = 0;
        const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs(4000)
                              .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: Alfred/" JucePlugin_VersionString)
                              .withStatusCode(&status);
        std::string js;
        if (auto in = api.createInputStream(opts))
        {
            char buf[4096];
            while (!threadShouldExit() && !in->isExhausted() && js.size() < 2000000)
            {
                const int n = in->read(buf, (int)sizeof(buf));
                if (n <= 0) break;
                js.append(buf, (size_t)n);
            }
        }
        std::string tag, url;
        if (status != 200 || threadShouldExit() || !alfred_update::parse(js, JucePlugin_VersionString, tag, url)) { result() = 2; return; }
        const auto v = alfred_update::versionParts(tag);
       #if JUCE_MAC
        const bool mac = true;
       #else
        const bool mac = false;
       #endif
        std::string asset;
       #if JUCE_MAC || JUCE_WINDOWS
        asset = alfred_update::assetUrl(js, "https://github.com/dmajorsound/Alfred-Denoiser/releases/download/", mac);
       #else
        (void)mac;
       #endif
        { const juce::ScopedLock l(lock()); data().major = v[0]; data().minor = v[1]; data().url = juce::String(url); data().asset = juce::String(asset); }
        result() = 1;
    }
};

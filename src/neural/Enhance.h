#pragma once
// Enhance: studio restoration of the voice with one of two networks, both run through ONNX Runtime.
//
//   Restore  audiojs/restore (OpenRAIL): maps the STFT of the voice to the STFT of a studio take, 48 kHz.
//   Sidon    Sidon (MIT): a w2v-BERT 2.0 predictor and a DAC vocoder build the voice anew at 48 kHz.
//
// Neither works in real time: both look at several seconds at once. While the host renders offline every
// window is worked out on the spot. During playback a window passes unchanged the first time and is worked
// out in the background; the next time the same passage plays, the prepared window is used.
//
// The audio is cut into windows that advance by kHop and overlap by kFade. Every sample comes out exactly
// kLatency samples after it went in, whatever the host's block size is.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <thread>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct OrtApi; struct OrtEnv; struct OrtSession; struct OrtMemoryInfo;

class EnhanceModels        // one per process, shared by all instances: the sessions are large
{
public:
    enum { kNone = 0, kRestore = 1, kSidon = 2 };

    // libraryPath: the ONNX Runtime library; dir: the folder with the model files
    static std::shared_ptr<EnhanceModels> get(const std::string& libraryPath, const std::string& dir);
    ~EnhanceModels();

    bool installed(int model) const;                  // the files are there
    bool loaded(int model) const;
    bool load(int model, std::string& error);         // slow (seconds); never call on the audio thread

    // one window of mono audio at 48 kHz, in and out of the same length
    void restore(const float* in, int n, float* out);
    void sidon(const float* in, int n, float* out);

private:
    EnhanceModels(const std::string& libraryPath, const std::string& dir);
    bool openApi(std::string& error);
    OrtSession* open(const std::string& file, std::string& error);

    std::string libPath, folder;
    std::mutex loadLock;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtMemoryInfo* memInfo = nullptr;
    std::atomic<OrtSession*> sRestore { nullptr }, sPredictor { nullptr }, sVocoder { nullptr };
    bool hasRestore = false, hasSidon = false;
};

class EnhanceStage         // one per plug-in instance
{
public:
    static constexpr int kHop = 4 * 48000, kFade = 48000, kLatency = kHop + kFade;

    EnhanceStage();
    ~EnhanceStage();
    void prepare(int numChannels, int maxBlock);
    void reset();                              // playback starts: forget the audio, keep the prepared windows
    // The windows sit on a fixed grid of the timeline, so the same passage is always cut the same way and a
    // window that was worked out once is found again. Call after reset() and whenever the play position jumps.
    void align(long long timelineSample);
    void setModels(std::shared_ptr<EnhanceModels> m);

    // voice: [channels][n], in and out. rest: [channels][n] or nullptr; it is delayed and added back.
    // model: EnhanceModels::kRestore or kSidon. amount 0..1.
    // offline: the host renders, so every window is worked out on the spot. Otherwise (playback) a window that
    // is not prepared yet passes unchanged and is worked out in the background for the next time.
    void process(double* const* voice, const double* const* rest, int n, int model, double amount, bool offline);

    double secondsEnhanced() const { return (double)enhanced.load() / 48000.0; }
    int waiting() const { return pendingJobs.load(); }      // windows still to be worked out
    int hearing() const { return lastState.load(); }        // 0 nothing yet, 1 restored voice, 2 unchanged voice

private:
    struct Job { unsigned long long key = 0; int model = 0, past = 0; std::vector<float> win; };
    void chunk(bool everything);
    bool lookup(unsigned long long key, float* out, int n, bool wait);
    void store(unsigned long long key, const float* y, int n);
    void worker();

    std::shared_ptr<EnhanceModels> models;
    int nch = 1;
    // hist: [past][samples whose restored version has not left yet]
    std::vector<float> hist; int histFill = 0;
    int since = 0;                       // samples received since the last window
    bool first = true;                   // no window before this one to join with
    int hop0 = kHop;                     // length of the first window's own part, from the timeline grid
    long long age = 0;                   // samples received since reset()
    std::vector<float> tail; bool tailDry = true;      // the overlap of the window before
    std::vector<float> winOut, y;
    std::vector<float> outQ; int qRead = 0, qFill = 0; // restored voice waiting to leave
    std::vector<float> dV[2], dR[2], dM; // delay lines for the voice, the rest and the mono voice
    int dPos = 0;
    double mix = 0.0;
    int curModel = 0; bool curOffline = false, curWant = false;
    std::atomic<long long> enhanced { 0 };
    std::atomic<int> pendingJobs { 0 }, lastState { 0 };

    // prepared windows and the thread that works them out
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool quit = false;
    std::deque<Job> jobs;
    std::set<unsigned long long> queued;
    std::map<unsigned long long, std::vector<float>> cache;
    std::deque<unsigned long long> cacheOrder;
};

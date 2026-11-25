#pragma once
// Enhance: studio restoration of the voice with one of two networks, both run through ONNX Runtime.
//
//   Restore  audiojs/restore (OpenRAIL): maps the STFT of the voice to the STFT of a studio take, 48 kHz.
//   Sidon    Sidon (MIT): a w2v-BERT 2.0 predictor and a DAC vocoder build the voice anew at 48 kHz.
//
// Neither works in real time: both look at several seconds at once. The plug-in therefore runs them only
// while the host renders offline. During playback the same delay is kept and the voice passes unchanged.
//
// The audio is cut into windows that advance by kHop and overlap by kFade. Every sample comes out exactly
// kLatency samples after it went in, whatever the host's block size is.
#include <atomic>
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

    void prepare(int numChannels, int maxBlock);
    void reset();
    void setModels(std::shared_ptr<EnhanceModels> m) { models = std::move(m); }

    // voice: [channels][n], in and out. rest: [channels][n] or nullptr; it is delayed and added back.
    // model: EnhanceModels::kRestore or kSidon. amount 0..1. run: the networks may work (offline render).
    void process(double* const* voice, const double* const* rest, int n, int model, double amount, bool run);

    double secondsEnhanced() const { return (double)enhanced.load() / 48000.0; }

private:
    void chunk(int model, bool run);

    std::shared_ptr<EnhanceModels> models;
    int nch = 1;
    std::vector<float> hist;             // the newest kPast + kHop + kFade samples of the mono voice
    int histFill = 0;                    // valid samples in hist (they sit at its end)
    int since = 0;                       // samples received since the last window was started
    bool first = true;
    std::vector<float> res, win, winOut; // res: restored voice for the samples that leave next
    int resPos = 0;
    std::vector<float> dV[2], dR[2], dM; // delay lines for the voice, the rest and the mono voice
    int dPos = 0;
    double mix = 0.0;
    std::atomic<long long> enhanced { 0 };
};

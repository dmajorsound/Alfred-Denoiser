#pragma once
// Neural voice engine: DPDFNet (Ceva, Apache-2.0), a real-time speech enhancement network for 48 kHz,
// run through ONNX Runtime. The runtime library and the model file are loaded from disk when the plug-in
// starts; if either is missing the engine reports "not available" and the plug-in works without it.
//
// The engine separates the voice from everything else. It takes audio in blocks of any size and returns,
// time-aligned with each other and delayed by latency() samples:
//   speech  the voice as the network hears it
//   dry     the unprocessed input
// so "dry - speech" is exactly what the network took away.
#include <complex>
#include <memory>
#include <string>
#include <vector>

struct OrtApi; struct OrtEnv; struct OrtSession; struct OrtMemoryInfo; struct OrtValue;

class NeuralRuntime        // one per plug-in instance: the loaded library and the model session
{
public:
    NeuralRuntime();
    ~NeuralRuntime();
    // libraryPath: onnxruntime shared library, modelPath: dpdfnet2_48khz_hr.onnx (UTF-8 paths)
    bool load(const std::string& libraryPath, const std::string& modelPath, std::string& error);
    bool ready() const { return session != nullptr; }

    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memInfo = nullptr;
    void* library = nullptr;
    int stateSize = 0;
    std::vector<float> initialState;
};

class NeuralVoice
{
public:
    static constexpr int kN = 960, kHop = 480, kBins = 481;
    static constexpr int kLatency = 6 * kHop;      // samples, at 48 kHz (checked by tests/neuraltest.cpp)

    explicit NeuralVoice(NeuralRuntime& rt);
    ~NeuralVoice();

    void prepare(int numChannels);                 // 1 or 2; allocates everything, call off the audio thread
    void reset();
    // in/speech/dry: [channels][n]. active = false: no network, speech is the same as dry (still delayed)
    void process(const float* const* in, float* const* speech, float* const* dry, int n, bool active);

private:
    void hop(bool active);
    void forward(const float* frame, std::complex<float>* spec);
    void inverse(const std::complex<float>* spec, float* frame);
    void fft(std::complex<float>* x, int n, int stride, std::complex<float>* out, int sign) const;

    NeuralRuntime& rt;
    int channels = 1;
    int warm = 0;                                  // network frames since it was switched on
    static constexpr int kWarmHops = 8;
    std::vector<float> window;
    std::vector<std::complex<float>> roots, work, workOut;

    struct Channel
    {
        std::vector<float> hist;                   // last kN input samples
        std::vector<float> ola;                    // overlap of the speech output
        std::vector<float> inFifo, speechFifo;
        std::vector<float> dryDelay; int dryPos = 0;
        std::vector<std::complex<float>> X;
    };
    std::vector<Channel> ch;
    int fifoFill = 0;

    // network input / output buffers and the tensors that wrap them
    std::vector<float> specIn, specOut, stateA, stateB;
    std::vector<std::complex<float>> Xm, Ym, workSpec;
    std::vector<float> frame;
    OrtValue* tSpecIn = nullptr; OrtValue* tSpecOut = nullptr; OrtValue* tStateA = nullptr; OrtValue* tStateB = nullptr;
    bool stateInA = true;
    std::vector<std::vector<std::complex<float>>> specDelay; int specPos = 0;   // per channel, 4 frames
};

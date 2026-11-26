#pragma once
// Real-time neural voice engine: DeepFilterNet3 (Hendrik Schröter et al., MIT / Apache-2.0) at 48 kHz, written
// out in plain C++ - no runtime library, the weights come from one file (tools/export_dfn3.py).
//
// The network looks at an ERB envelope of 32 bands and the lowest 96 bins in full. It answers with a gain per
// band and, for the lowest 96 bins, a deep filter over five frames (two of them ahead). Frame by frame with its
// own memory, so it runs on any block size and gives the same result every time.
//
// Like NeuralVoice it returns, time-aligned and delayed by kLatency samples:
//   speech  the voice as the network hears it
//   dry     the unprocessed input
#include <complex>
#include <map>
#include <memory>
#include <string>
#include <vector>

class DfnModel             // the weights, shared by all instances
{
public:
    static std::shared_ptr<DfnModel> load(const std::string& path, std::string& error);
    const std::vector<float>& operator[](const char* name) const;
private:
    std::map<std::string, std::vector<float>> t;
    static const std::vector<float> none;
};

class DfnNet               // the network's state for one stream, one call per frame
{
public:
    explicit DfnNet(std::shared_ptr<DfnModel> m);
    void reset();
    // fe [32] ERB feature, fs [96] complex feature -> mask [32], coef [96 * 5] (deep filter, oldest tap first)
    void step(const float* fe, const std::complex<float>* fs, float* mask, std::complex<float>* coef);
private:
    std::shared_ptr<DfnModel> M;
    std::vector<float> pe, ps, pc, h[5];
    // scratch
    std::vector<float> e0, e1, e2, e3, t0, t1, d, c0, c1, emb, x, y, tmp, co, dd;
};

class DfnVoice
{
public:
    static constexpr int kN = 960, kHop = 480, kBins = 481, kErb = 32, kDf = 96, kOrder = 5, kAhead = 2;
    static constexpr int kLatency = 4 * kHop;      // 40 ms at 48 kHz

    explicit DfnVoice(std::shared_ptr<DfnModel> m);
    void prepare(int numChannels);
    void reset();
    void process(const float* const* in, float* const* speech, float* const* dry, int n, bool active);

private:
    void hop(bool active);
    void fft(std::complex<float>* x, int n, int stride, std::complex<float>* out, int sign) const;
    void forward(const float* frame, std::complex<float>* spec);
    void inverse(const std::complex<float>* spec, float* frame);

    std::shared_ptr<DfnModel> model;
    DfnNet net;
    int channels = 1, warm = 0;
    static constexpr int kWarmHops = 6;
    std::vector<float> window;
    std::vector<std::complex<float>> roots, work, workOut;
    std::vector<int> bandOf;  std::vector<int> bandEdge;
    // feature normalisation
    std::vector<float> erbMean, dfUnit;
    struct Channel
    {
        std::vector<float> hist, ola, inFifo, speechFifo, dryDelay; int dryPos = 0;
        std::vector<std::complex<float>> X;
    };
    std::vector<Channel> ch;
    int fifoFill = 0;
    // the last five spectra of each channel and of the middle (index channels)
    std::vector<std::vector<std::complex<float>>> past; int pastPos = 0;
    std::vector<std::complex<float>> Xm, Ym, Yc, coef;
    std::vector<float> fe, mask, frame;
    std::vector<std::complex<float>> fs;
};

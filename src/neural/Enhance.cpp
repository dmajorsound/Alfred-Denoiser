#include "Enhance.h"
#include "ort/onnxruntime_c_api.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace
{
const double kPi = 3.14159265358979323846;

#if defined(_WIN32)
std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
#endif

bool fileExists(const std::string& path)
{
#if defined(_WIN32)
    const DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
#endif
}

std::string join(const std::string& dir, const char* name)
{
#if defined(_WIN32)
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

bool failed(const OrtApi* api, OrtStatus* st, std::string& error, const char* what)
{
    if (st == nullptr) return false;
    error = std::string(what) + ": " + api->GetErrorMessage(st);
    api->ReleaseStatus(st);
    return true;
}

// in-place complex FFT, n a power of two; sign -1 forward, +1 inverse (not scaled)
void fft(std::complex<float>* x, int n, int sign)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = sign * 2.0 * kPi / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const std::complex<float> wf((float)w.real(), (float)w.imag());
                const std::complex<float> u = x[i + k], v = x[i + k + len / 2] * wf;
                x[i + k] = u + v; x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// level of the loud parts of a window: RMS of the 1024-sample pieces within 30 dB of the loudest one
double activeRms(const float* x, int n, double& peakPiece)
{
    const int step = 1024;
    std::vector<double> e;
    for (int i = 0; i + step <= n; i += step)
    {
        double s = 0.0;
        for (int k = 0; k < step; ++k) s += (double)x[i + k] * x[i + k];
        e.push_back(s / step);
    }
    peakPiece = 0.0;
    for (double v : e) peakPiece = std::max(peakPiece, v);
    double sum = 0.0; int cnt = 0;
    for (double v : e) if (v > peakPiece * 1.0e-3) { sum += v; ++cnt; }
    return cnt > 0 ? std::sqrt(sum / cnt) : 0.0;
}

// ------------------------------------------------------------------------------------------ Sidon front-end
// 48 kHz -> 16 kHz with a linear-phase low-pass, no time shift
void to16k(const float* x, int n, std::vector<float>& y)
{
    static const std::vector<float> h = []
    {
        const int taps = 191; const double fc = 7600.0 / 48000.0, beta = 8.0;
        auto i0 = [](double v) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (v / (2.0 * k)) * (v / (2.0 * k)); s += t; } return s; };
        std::vector<float> c((size_t)taps);
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const double m = i - (taps - 1) * 0.5, r = 2.0 * i / (taps - 1) - 1.0;
            const double sinc = std::abs(m) < 1.0e-9 ? 2.0 * fc : std::sin(2.0 * kPi * fc * m) / (kPi * m);
            const double v = sinc * i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
            c[(size_t)i] = (float)v; sum += v;
        }
        for (auto& v : c) v = (float)(v / sum);
        return c;
    }();
    const int taps = (int)h.size(), c = (taps - 1) / 2, m = n / 3;
    y.assign((size_t)m, 0.0f);
    for (int j = 0; j < m; ++j)
    {
        const int base = 3 * j - c;
        const int k0 = std::max(0, -base), k1 = std::min(taps, n - base);
        double s = 0.0;
        for (int k = k0; k < k1; ++k) s += (double)h[(size_t)k] * x[base + k];
        y[(size_t)j] = (float)s;
    }
}

// The log-mel features of w2v-BERT 2.0 (SeamlessM4T feature extractor): 80 mel bands, 25 ms Povey window every
// 10 ms, pre-emphasis 0.97, each band normalized over the window of audio, two frames stacked -> 160 values.
// After speech-core (Apache-2.0), src/audio/seamless_fbank.cpp.
void seamlessFeatures(const float* audio, int length, std::vector<float>& stacked, int& outFrames)
{
    const int kFrame = 400, kHop = 160, kFft = 512, kBins = 257, kMel = 80;
    static const std::vector<float> window = []
    {
        std::vector<float> w(400);
        for (int n = 0; n < 400; ++n) w[(size_t)n] = (float)std::pow(0.5 - 0.5 * std::cos(2.0 * kPi * n / 399.0), 0.85);
        return w;
    }();
    static const std::vector<float> bank = []
    {
        auto mel = [](double f) { return 1127.0 * std::log(1.0 + f / 700.0); };
        std::vector<float> fb((size_t)80 * 257, 0.0f);
        const double lo = mel(20.0), hi = mel(8000.0), d = (hi - lo) / 81.0;
        for (int m = 0; m < 80; ++m)
        {
            const double left = lo + m * d, center = lo + (m + 1) * d, right = lo + (m + 2) * d;
            for (int k = 0; k < 256; ++k)
            {
                const double v = mel(16000.0 / 512.0 * k);
                double w = 0.0;
                if (v > left && v < right) w = std::max(0.0, std::min((v - left) / (center - left), (right - v) / (right - center)));
                fb[(size_t)m * 257 + (size_t)k] = (float)w;
            }
        }
        return fb;
    }();

    outFrames = 0; stacked.clear();
    if (length < kFrame) return;
    const int frames = 1 + (length - kFrame) / kHop;
    if (frames < 2) return;
    std::vector<float> logmel((size_t)frames * kMel), frame((size_t)kFrame), power((size_t)kBins);
    std::vector<std::complex<float>> buf((size_t)kFft);
    for (int t = 0; t < frames; ++t)
    {
        const float* src = audio + (size_t)t * kHop;
        double mean = 0.0;
        for (int i = 0; i < kFrame; ++i) { frame[(size_t)i] = src[i] * 32768.0f; mean += frame[(size_t)i]; }
        mean /= kFrame;
        for (int i = 0; i < kFrame; ++i) frame[(size_t)i] = (float)(frame[(size_t)i] - mean);
        for (int i = kFrame - 1; i > 0; --i) frame[(size_t)i] -= 0.97f * frame[(size_t)i - 1];
        frame[0] -= 0.97f * frame[0];
        for (int i = 0; i < kFft; ++i) buf[(size_t)i] = i < kFrame ? std::complex<float>(frame[(size_t)i] * window[(size_t)i], 0.0f) : std::complex<float>(0.0f, 0.0f);
        fft(buf.data(), kFft, -1);
        for (int k = 0; k < kBins; ++k) power[(size_t)k] = std::norm(buf[(size_t)k]);
        float* row = logmel.data() + (size_t)t * kMel;
        for (int m = 0; m < kMel; ++m)
        {
            const float* fb = bank.data() + (size_t)m * kBins;
            float acc = 0.0f;
            for (int k = 0; k < kBins; ++k) acc += power[(size_t)k] * fb[k];
            row[m] = std::log(std::max(acc, 1.1920928955078125e-07f));
        }
    }
    for (int m = 0; m < kMel; ++m)
    {
        double sum = 0.0;
        for (int t = 0; t < frames; ++t) sum += logmel[(size_t)t * kMel + (size_t)m];
        const double mu = sum / frames;
        double var = 0.0;
        for (int t = 0; t < frames; ++t) { const double d = logmel[(size_t)t * kMel + (size_t)m] - mu; var += d * d; }
        var /= (frames - 1);
        const double inv = 1.0 / std::sqrt(var + 1.0e-5);
        for (int t = 0; t < frames; ++t) { float& v = logmel[(size_t)t * kMel + (size_t)m]; v = (float)((v - mu) * inv); }
    }
    const int T = frames / 2;
    stacked.resize((size_t)T * 160);
    for (int t = 0; t < T; ++t)
    {
        std::memcpy(stacked.data() + (size_t)t * 160, logmel.data() + (size_t)(2 * t) * kMel, sizeof(float) * kMel);
        std::memcpy(stacked.data() + (size_t)t * 160 + kMel, logmel.data() + (size_t)(2 * t + 1) * kMel, sizeof(float) * kMel);
    }
    outFrames = T;
}

std::mutex& registryLock() { static std::mutex m; return m; }
std::map<std::string, std::weak_ptr<EnhanceModels>>& registry() { static std::map<std::string, std::weak_ptr<EnhanceModels>> r; return r; }
}

// ---------------------------------------------------------------------------------------------------- models
std::shared_ptr<EnhanceModels> EnhanceModels::get(const std::string& libraryPath, const std::string& dir)
{
    const std::lock_guard<std::mutex> g(registryLock());
    auto& slot = registry()[libraryPath + "|" + dir];
    if (auto p = slot.lock()) return p;
    std::shared_ptr<EnhanceModels> p(new EnhanceModels(libraryPath, dir));
    slot = p;
    return p;
}

EnhanceModels::EnhanceModels(const std::string& libraryPath, const std::string& dir) : libPath(libraryPath), folder(dir)
{
    hasRestore = fileExists(join(dir, "restore.fp16.onnx"));
    hasSidon = fileExists(join(dir, "sidon-predictor.onnx")) && fileExists(join(dir, "sidon-vocoder.onnx"));
}

EnhanceModels::~EnhanceModels()
{
    if (!api) return;
    for (auto* s : { sRestore.load(), sPredictor.load(), sVocoder.load() }) if (s) api->ReleaseSession(s);
    if (memInfo) api->ReleaseMemoryInfo(memInfo);
    if (env) api->ReleaseEnv(env);
}

bool EnhanceModels::installed(int model) const { return model == kRestore ? hasRestore : model == kSidon ? hasSidon : false; }

bool EnhanceModels::loaded(int model) const
{
    return model == kRestore ? sRestore.load() != nullptr : model == kSidon ? (sPredictor.load() != nullptr && sVocoder.load() != nullptr) : false;
}

bool EnhanceModels::openApi(std::string& error)
{
    if (api && env && memInfo) return true;
    typedef const OrtApiBase* (ORT_API_CALL *GetBaseFn)(void);
    GetBaseFn getBase = nullptr;
#if defined(_WIN32)
    HMODULE h = LoadLibraryExW(widen(libPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { error = "could not load " + libPath; return false; }
    getBase = (GetBaseFn)(void*)GetProcAddress(h, "OrtGetApiBase");
#else
    void* h = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { error = "could not load " + libPath; return false; }
    getBase = (GetBaseFn)dlsym(h, "OrtGetApiBase");
#endif
    if (!getBase) { error = "not an ONNX Runtime library: " + libPath; return false; }
    const OrtApiBase* base = getBase();
    api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) { error = "the ONNX Runtime library is too old"; return false; }
    if (failed(api, api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "alfred-enhance", &env), error, "environment")) return false;
    if (failed(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memInfo), error, "memory")) return false;
    return true;
}

OrtSession* EnhanceModels::open(const std::string& file, std::string& error)
{
    OrtSessionOptions* so = nullptr;
    if (failed(api, api->CreateSessionOptions(&so), error, "options")) return nullptr;
    // these run only while the host renders offline, so they may use several cores
    const int threads = (int)std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
    api->ReleaseStatus(api->SetIntraOpNumThreads(so, threads));
    api->ReleaseStatus(api->SetInterOpNumThreads(so, 1));
    api->ReleaseStatus(api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL));
    api->ReleaseStatus(api->AddSessionConfigEntry(so, "session.intra_op.allow_spinning", "0"));
    OrtSession* s = nullptr;
    const std::string path = join(folder, file.c_str());
#if defined(_WIN32)
    OrtStatus* st = api->CreateSession(env, widen(path).c_str(), so, &s);
#else
    OrtStatus* st = api->CreateSession(env, path.c_str(), so, &s);
#endif
    api->ReleaseSessionOptions(so);
    if (failed(api, st, error, file.c_str())) return nullptr;
    return s;
}

bool EnhanceModels::load(int model, std::string& error)
{
    const std::lock_guard<std::mutex> g(loadLock);
    if (loaded(model)) return true;
    if (!installed(model)) { error = "the model files are not installed"; return false; }
    if (!openApi(error)) return false;
    if (model == kRestore)
    {
        OrtSession* s = open("restore.fp16.onnx", error);
        if (!s) return false;
        sRestore.store(s);
        return true;
    }
    if (model == kSidon)
    {
        if (!sPredictor.load()) { OrtSession* s = open("sidon-predictor.onnx", error); if (!s) return false; sPredictor.store(s); }
        if (!sVocoder.load()) { OrtSession* s = open("sidon-vocoder.onnx", error); if (!s) return false; sVocoder.store(s); }
        return true;
    }
    return false;
}

void EnhanceModels::restore(const float* in, int n, float* out)
{
    std::memcpy(out, in, sizeof(float) * (size_t)n);
    OrtSession* s = sRestore.load();
    const int N = 2048, H = 512, B = 1025, pad = N / 2;
    if (!s || n < N) return;
    double peak = 0.0;
    const double rms = activeRms(in, n, peak);
    if (peak < 1.0e-7 || rms <= 0.0) return;                    // nothing above -70 dBFS: leave it alone
    // the network was trained on voices at about -20 dBFS; bring the loud parts of this window to -18 dBFS
    const double g = std::min(100.0, 0.12589 / rms);

    static const std::vector<float> w = [] { std::vector<float> v(2048); for (int i = 0; i < 2048; ++i) v[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / 2048.0)); return v; }();
    const int T = 1 + n / H, len = n + 2 * pad;
    std::vector<float> p((size_t)len);
    for (int i = 0; i < len; ++i)
    {
        int k = i - pad;
        if (k < 0) k = -k; else if (k >= n) k = 2 * (n - 1) - k;
        p[(size_t)i] = (float)(in[k] * g);
    }
    std::vector<float> spec((size_t)2 * B * (size_t)T);
    std::vector<std::complex<float>> buf((size_t)N);
    for (int t = 0; t < T; ++t)
    {
        for (int i = 0; i < N; ++i) buf[(size_t)i] = std::complex<float>(p[(size_t)(t * H + i)] * w[(size_t)i], 0.0f);
        fft(buf.data(), N, -1);
        for (int f = 0; f < B; ++f)
        {
            spec[(size_t)f * (size_t)T + (size_t)t] = buf[(size_t)f].real();
            spec[(size_t)B * (size_t)T + (size_t)f * (size_t)T + (size_t)t] = buf[(size_t)f].imag();
        }
    }
    const int64_t shape[4] = { 1, 2, B, T };
    OrtValue* tin = nullptr; OrtValue* tout = nullptr;
    std::string err;
    if (failed(api, api->CreateTensorWithDataAsOrtValue(memInfo, spec.data(), spec.size() * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tin), err, "tensor")) return;
    const char* inName[] = { "spec" }; const char* outName[] = { "spec_out" };
    const OrtValue* ins[] = { tin };
    OrtStatus* st = api->Run(s, nullptr, inName, ins, 1, outName, 1, &tout);
    api->ReleaseValue(tin);
    if (failed(api, st, err, "run") || !tout) return;
    float* o = nullptr;
    if (failed(api, api->GetTensorMutableData(tout, (void**)&o), err, "data")) { api->ReleaseValue(tout); return; }

    std::vector<double> y((size_t)len, 0.0), ws((size_t)len, 0.0);
    for (int t = 0; t < T; ++t)
    {
        for (int f = 0; f < B; ++f) buf[(size_t)f] = std::complex<float>(o[(size_t)f * (size_t)T + (size_t)t], o[(size_t)B * (size_t)T + (size_t)f * (size_t)T + (size_t)t]);
        for (int f = 1; f < B - 1; ++f) buf[(size_t)(N - f)] = std::conj(buf[(size_t)f]);
        fft(buf.data(), N, +1);
        for (int i = 0; i < N; ++i)
        {
            y[(size_t)(t * H + i)] += (double)buf[(size_t)i].real() / N * w[(size_t)i];
            ws[(size_t)(t * H + i)] += (double)w[(size_t)i] * w[(size_t)i];
        }
    }
    api->ReleaseValue(tout);
    for (int i = 0; i < n; ++i) out[i] = (float)(y[(size_t)(i + pad)] / std::max(ws[(size_t)(i + pad)], 1.0e-8) / g);
}

void EnhanceModels::sidon(const float* in, int n, float* out)
{
    std::memcpy(out, in, sizeof(float) * (size_t)n);
    OrtSession* sp = sPredictor.load(); OrtSession* sv = sVocoder.load();
    if (!sp || !sv) return;
    double peak = 0.0;
    activeRms(in, n, peak);
    if (peak < 1.0e-7) return;
    std::vector<float> x16, feat;
    to16k(in, n, x16);
    int T = 0;
    seamlessFeatures(x16.data(), (int)x16.size(), feat, T);
    if (T < 1) return;

    std::string err;
    const int64_t shape[3] = { 1, T, 160 };
    OrtValue* tin = nullptr; OrtValue* hidden = nullptr; OrtValue* audio = nullptr;
    if (failed(api, api->CreateTensorWithDataAsOrtValue(memInfo, feat.data(), feat.size() * sizeof(float), shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tin), err, "tensor")) return;
    {
        const char* inName[] = { "input_features" }; const char* outName[] = { "features" };
        const OrtValue* ins[] = { tin };
        OrtStatus* st = api->Run(sp, nullptr, inName, ins, 1, outName, 1, &hidden);
        api->ReleaseValue(tin);
        if (failed(api, st, err, "predictor") || !hidden) return;
    }
    {
        const char* inName[] = { "features" }; const char* outName[] = { "audio" };
        const OrtValue* ins[] = { hidden };
        OrtStatus* st = api->Run(sv, nullptr, inName, ins, 1, outName, 1, &audio);
        api->ReleaseValue(hidden);
        if (failed(api, st, err, "vocoder") || !audio) return;
    }
    float* y = nullptr; size_t count = 0;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    if (!failed(api, api->GetTensorTypeAndShape(audio, &info), err, "shape"))
    {
        api->ReleaseStatus(api->GetTensorShapeElementCount(info, &count));
        api->ReleaseTensorTypeAndShapeInfo(info);
    }
    if (count == 0 || failed(api, api->GetTensorMutableData(audio, (void**)&y), err, "data")) { api->ReleaseValue(audio); return; }

    // The vocoder's voice comes at its own level and about 3 ms early. Bring it to the level of the input and
    // into place; what lies beyond the vocoder's last frame stays silent.
    const int shift = 144, m = (int)std::min<size_t>(count, (size_t)std::max(0, n - shift));
    double ei = 0.0, eo = 0.0;
    for (int i = 0; i < m; ++i) { ei += (double)in[i + shift] * in[i + shift]; eo += (double)y[i] * y[i]; }
    const double g = eo > 1.0e-12 ? std::min(4.0, std::sqrt(ei / eo)) : 0.0;
    std::fill(out, out + n, 0.0f);
    for (int i = 0; i < m; ++i) out[i + shift] = (float)(y[i] * g);
    api->ReleaseValue(audio);
}

// ----------------------------------------------------------------------------------------------------- stage
namespace
{
const int kPastRestore = 5 * 48000, kPastSidon = 48000;
const int kCap = kPastRestore + EnhanceStage::kHop + EnhanceStage::kFade;
}

void EnhanceStage::prepare(int numChannels, int)
{
    nch = numChannels < 1 ? 1 : (numChannels > 2 ? 2 : numChannels);
    hist.assign((size_t)kCap, 0.0f);
    res.assign((size_t)(kHop + kFade), 0.0f);
    win.assign((size_t)kCap, 0.0f); winOut.assign((size_t)kCap, 0.0f);
    for (int c = 0; c < 2; ++c) { dV[c].assign((size_t)kLatency, 0.0f); dR[c].assign((size_t)kLatency, 0.0f); }
    dM.assign((size_t)kLatency, 0.0f);
    reset();
}

void EnhanceStage::reset()
{
    histFill = 0; since = 0; first = true; resPos = 0; dPos = 0; mix = 0.0;
    std::fill(res.begin(), res.end(), 0.0f);
    for (int c = 0; c < 2; ++c) { std::fill(dV[c].begin(), dV[c].end(), 0.0f); std::fill(dR[c].begin(), dR[c].end(), 0.0f); }
    std::fill(dM.begin(), dM.end(), 0.0f);
}

// A window is complete: hist holds [past][kHop + kFade new samples]. Work out the restored voice for the new part.
void EnhanceStage::chunk(int model, bool run)
{
    const int span = kHop + kFade, n = histFill, past = n - span;
    const float* x = hist.data();
    const bool work = run && models && models->loaded(model);
    const float* y = x + past;
    if (work)
    {
        if (model == EnhanceModels::kRestore) models->restore(x, n, winOut.data());
        else models->sidon(x, n, winOut.data());
        y = winOut.data() + past;
        enhanced += kHop;
    }
    // join with the tail of the window before
    std::memcpy(win.data(), y, sizeof(float) * (size_t)span);
    if (!first && work)
    {
        const float* old = res.data() + kHop;
        if (model == EnhanceModels::kRestore)
        {
            for (int i = 0; i < kFade; ++i)
            {
                const float f = (float)(0.5 - 0.5 * std::cos(kPi * (i + 0.5) / kFade));
                win[(size_t)i] = old[i] * (1.0f - f) + y[i] * f;
            }
        }
        else
        {
            // Sidon builds each window's voice anew, so two windows do not agree in phase: change over within
            // 20 ms at the quietest place of the overlap
            const int L = 960;
            const float* src = x + past;
            double e = 0.0, best = 0.0; int at = 0;
            for (int i = 0; i < L; ++i) e += (double)src[i] * src[i];
            best = e;
            for (int i = 1; i + L <= kFade; ++i)
            {
                e += (double)src[i + L - 1] * src[i + L - 1] - (double)src[i - 1] * src[i - 1];
                if (e < best) { best = e; at = i; }
            }
            for (int i = 0; i < kFade; ++i)
            {
                const float f = i < at ? 0.0f : i >= at + L ? 1.0f : (float)(0.5 - 0.5 * std::cos(kPi * (i - at + 0.5) / L));
                win[(size_t)i] = old[i] * (1.0f - f) + y[i] * f;
            }
        }
    }
    std::memcpy(res.data(), win.data(), sizeof(float) * (size_t)span);
    resPos = 0; first = false;

    // keep what the next window needs as its past
    const int keep = std::min(n, (model == EnhanceModels::kSidon ? kPastSidon : kPastRestore) + kFade);
    if (keep < n) std::memmove(hist.data(), hist.data() + (n - keep), sizeof(float) * (size_t)keep);
    histFill = keep; since = 0;
}

void EnhanceStage::process(double* const* voice, const double* const* rest, int n, int model, double amount, bool run)
{
    const double target = model == EnhanceModels::kSidon ? (amount > 0.001 ? 1.0 : 0.0) : std::max(0.0, std::min(1.0, amount));
    const double k = 1.0 - std::exp(-1.0 / (0.010 * 48000.0));
    for (int i = 0; i < n; ++i)
    {
        // what leaves now went in kLatency samples ago
        const float r = first ? 0.0f : res[(size_t)resPos];
        if (!first) ++resPos;
        double m = 0.0;
        for (int c = 0; c < nch; ++c) m += voice[c][i];
        const float mono = (float)(m / nch);
        mix += (target - mix) * k;
        // restored voice minus the mono voice as the windows saw it: added to every channel
        const double diff = first ? 0.0 : (double)r - (double)dM[(size_t)dPos];
        dM[(size_t)dPos] = mono;
        for (int c = 0; c < nch; ++c)
        {
            const double vOld = (double)dV[c][(size_t)dPos], rOld = (double)dR[c][(size_t)dPos];
            dV[c][(size_t)dPos] = (float)voice[c][i];
            dR[c][(size_t)dPos] = rest ? (float)rest[c][i] : 0.0f;
            voice[c][i] = vOld + rOld + mix * diff;
        }
        dPos = dPos + 1 == kLatency ? 0 : dPos + 1;

        hist[(size_t)histFill++] = mono; ++since;
        if ((first && since == kHop + kFade) || (!first && since == kHop)) chunk(model, run);
    }
}

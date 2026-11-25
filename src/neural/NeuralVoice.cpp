#include "NeuralVoice.h"
#include "ort/onnxruntime_c_api.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------------------------------------
// runtime: library + session

namespace
{
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

bool failed(const OrtApi* api, OrtStatus* st, std::string& error, const char* what)
{
    if (st == nullptr) return false;
    error = std::string(what) + ": " + api->GetErrorMessage(st);
    api->ReleaseStatus(st);
    return true;
}

std::vector<float> parseFloats(const char* s)
{
    std::vector<float> v;
    while (s && *s)
    {
        char* end = nullptr;
        const float f = std::strtof(s, &end);
        if (end == s) break;
        v.push_back(f);
        s = (*end == ',') ? end + 1 : end;
    }
    return v;
}
}

NeuralRuntime::NeuralRuntime() = default;

NeuralRuntime::~NeuralRuntime()
{
    if (api)
    {
        if (session) api->ReleaseSession(session);
        if (memInfo) api->ReleaseMemoryInfo(memInfo);
        if (env) api->ReleaseEnv(env);
    }
    // the library itself stays loaded: other instances in the same process may still use it
}

bool NeuralRuntime::load(const std::string& libraryPath, const std::string& modelPath, std::string& error)
{
    if (session) return true;
    typedef const OrtApiBase* (ORT_API_CALL *GetBaseFn)(void);
    GetBaseFn getBase = nullptr;
#if defined(_WIN32)
    HMODULE h = LoadLibraryExW(widen(libraryPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { error = "could not load " + libraryPath; return false; }
    library = (void*)h;
    getBase = (GetBaseFn)(void*)GetProcAddress(h, "OrtGetApiBase");
#else
    void* h = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { error = "could not load " + libraryPath; return false; }
    library = h;
    getBase = (GetBaseFn)dlsym(h, "OrtGetApiBase");
#endif
    if (!getBase) { error = "not an ONNX Runtime library: " + libraryPath; return false; }
    const OrtApiBase* base = getBase();
    api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) { error = "the ONNX Runtime library is too old"; return false; }

    if (failed(api, api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "alfred", &env), error, "environment")) return false;
    OrtSessionOptions* so = nullptr;
    if (failed(api, api->CreateSessionOptions(&so), error, "options")) return false;
    // one thread, and it must not spin while waiting: the network runs inside the host's audio thread
    api->ReleaseStatus(api->SetIntraOpNumThreads(so, 1));
    api->ReleaseStatus(api->SetInterOpNumThreads(so, 1));
    api->ReleaseStatus(api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL));
    api->ReleaseStatus(api->AddSessionConfigEntry(so, "session.intra_op.allow_spinning", "0"));
#if defined(_WIN32)
    OrtStatus* st = api->CreateSession(env, widen(modelPath).c_str(), so, &session);
#else
    OrtStatus* st = api->CreateSession(env, modelPath.c_str(), so, &session);
#endif
    api->ReleaseSessionOptions(so);
    if (failed(api, st, error, "model")) { session = nullptr; return false; }
    if (failed(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memInfo), error, "memory")) return false;

    // the model file carries the size of its state and the start values of its two level trackers
    OrtAllocator* alloc = nullptr;
    OrtModelMetadata* meta = nullptr;
    if (failed(api, api->GetAllocatorWithDefaultOptions(&alloc), error, "allocator")) return false;
    if (failed(api, api->SessionGetModelMetadata(session, &meta), error, "metadata")) return false;
    auto lookup = [&](const char* key) -> std::string
    {
        char* value = nullptr;
        OrtStatus* s2 = api->ModelMetadataLookupCustomMetadataMap(meta, alloc, key, &value);
        if (s2) { api->ReleaseStatus(s2); return {}; }
        std::string r = value ? value : "";
        if (value) api->ReleaseStatus(api->AllocatorFree(alloc, value));
        return r;
    };
    const std::string sz = lookup("state_size"), erbInit = lookup("erb_norm_init"), specInit = lookup("spec_norm_init");
    api->ReleaseModelMetadata(meta);
    stateSize = std::atoi(sz.c_str());
    const std::vector<float> a = parseFloats(erbInit.c_str()), b = parseFloats(specInit.c_str());
    if (stateSize <= 0 || a.empty() || b.empty() || (int)(a.size() + b.size()) > stateSize)
    {
        error = "this is not the expected DPDFNet model file";
        api->ReleaseSession(session); session = nullptr;
        return false;
    }
    initialState.assign((size_t)stateSize, 0.0f);
    std::copy(a.begin(), a.end(), initialState.begin());
    std::copy(b.begin(), b.end(), initialState.begin() + (std::ptrdiff_t)a.size());
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// streaming engine

NeuralVoice::NeuralVoice(NeuralRuntime& r) : rt(r)
{
    window.resize(kN);
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < kN; ++i)
    {
        const double s = std::sin(0.5 * pi * (i + 0.5) / (kN / 2));
        window[(size_t)i] = (float)std::sin(0.5 * pi * s * s);          // Vorbis window, as the model was trained with
    }
    roots.resize(kN);
    for (int i = 0; i < kN; ++i) roots[(size_t)i] = std::complex<float>((float)std::cos(2 * pi * i / kN), (float)-std::sin(2 * pi * i / kN));
    work.resize(kN); workOut.resize(kN);
    prepare(1);
}

NeuralVoice::~NeuralVoice()
{
    if (rt.api)
        for (OrtValue* v : { tSpecIn, tSpecOut, tStateA, tStateB }) if (v) rt.api->ReleaseValue(v);
}

void NeuralVoice::prepare(int numChannels)
{
    channels = std::max(1, std::min(2, numChannels));
    ch.assign((size_t)channels, Channel());
    for (auto& c : ch)
    {
        c.hist.assign(kN, 0.0f); c.ola.assign(kHop, 0.0f);
        c.inFifo.assign(kHop, 0.0f); c.speechFifo.assign(kHop, 0.0f);
        c.dryDelay.assign((size_t)kLatency, 0.0f); c.dryPos = 0;
        c.X.assign(kBins, {});
    }
    specDelay.assign((size_t)channels + 1, std::vector<std::complex<float>>((size_t)kBins * 5));
    Xm.assign(kBins, {}); Ym.assign(kBins, {}); workSpec.assign(kBins, {});
    frame.assign(kN, 0.0f);
    specIn.assign((size_t)kBins * 2, 0.0f); specOut.assign((size_t)kBins * 2, 0.0f);
    if (rt.ready() && tSpecIn == nullptr)
    {
        stateA = rt.initialState; stateB = rt.initialState;
        const int64_t specShape[4] = { 1, 1, kBins, 2 };
        const int64_t stateShape[1] = { rt.stateSize };
        const OrtApi* a = rt.api;
        a->ReleaseStatus(a->CreateTensorWithDataAsOrtValue(rt.memInfo, specIn.data(), specIn.size() * sizeof(float), specShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tSpecIn));
        a->ReleaseStatus(a->CreateTensorWithDataAsOrtValue(rt.memInfo, specOut.data(), specOut.size() * sizeof(float), specShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tSpecOut));
        a->ReleaseStatus(a->CreateTensorWithDataAsOrtValue(rt.memInfo, stateA.data(), stateA.size() * sizeof(float), stateShape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tStateA));
        a->ReleaseStatus(a->CreateTensorWithDataAsOrtValue(rt.memInfo, stateB.data(), stateB.size() * sizeof(float), stateShape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tStateB));
    }
    reset();
}

void NeuralVoice::reset()
{
    for (auto& c : ch)
    {
        std::fill(c.hist.begin(), c.hist.end(), 0.0f); std::fill(c.ola.begin(), c.ola.end(), 0.0f);
        std::fill(c.inFifo.begin(), c.inFifo.end(), 0.0f); std::fill(c.speechFifo.begin(), c.speechFifo.end(), 0.0f);
        std::fill(c.dryDelay.begin(), c.dryDelay.end(), 0.0f); c.dryPos = 0;
    }
    for (auto& d : specDelay) std::fill(d.begin(), d.end(), std::complex<float>());
    specPos = 0; fifoFill = 0; warm = 0;
    if (rt.ready() && !stateA.empty()) { stateA = rt.initialState; stateB = rt.initialState; stateInA = true; }
}

// mixed-radix FFT (960 = 2^6 * 3 * 5), decimation in time. sign -1: forward, +1: inverse (not scaled)
void NeuralVoice::fft(std::complex<float>* x, int n, int stride, std::complex<float>* out, int sign) const
{
    if (n == 1) { out[0] = x[0]; return; }
    const int p = (n % 2 == 0) ? 2 : (n % 3 == 0) ? 3 : 5;
    const int m = n / p;
    for (int r = 0; r < p; ++r) fft(x + r * stride, m, stride * p, out + r * m, sign);
    const int step = kN / n;
    std::complex<float> t[5];
    for (int k = 0; k < m; ++k)
    {
        for (int r = 0; r < p; ++r) t[r] = out[r * m + k];
        for (int q = 0; q < p; ++q)
        {
            const int kk = k + q * m;
            std::complex<float> acc = t[0];
            for (int r = 1; r < p; ++r)
            {
                std::complex<float> w = roots[(size_t)(((long)r * kk * step) % kN)];
                if (sign > 0) w = std::conj(w);
                acc += t[r] * w;
            }
            out[kk] = acc;
        }
    }
}

void NeuralVoice::forward(const float* f, std::complex<float>* spec)
{
    for (int i = 0; i < kN; ++i) work[(size_t)i] = std::complex<float>(f[i] * window[(size_t)i], 0.0f);
    fft(work.data(), kN, 1, workOut.data(), -1);
    for (int k = 0; k < kBins; ++k) spec[k] = workOut[(size_t)k];
}

void NeuralVoice::inverse(const std::complex<float>* spec, float* f)
{
    work[0] = spec[0];
    for (int k = 1; k < kBins - 1; ++k) { work[(size_t)k] = spec[k]; work[(size_t)(kN - k)] = std::conj(spec[k]); }
    work[(size_t)(kBins - 1)] = spec[kBins - 1];
    fft(work.data(), kN, 1, workOut.data(), +1);
    const float sc = 1.0f / (float)kN;
    for (int i = 0; i < kN; ++i) f[i] = workOut[(size_t)i].real() * sc * window[(size_t)i];
}

void NeuralVoice::hop(bool active)
{
    for (auto& c : ch)
    {
        std::memmove(c.hist.data(), c.hist.data() + kHop, sizeof(float) * kHop);
        std::memcpy(c.hist.data() + kHop, c.inFifo.data(), sizeof(float) * kHop);
    }
    if (!active || !rt.ready() || tSpecIn == nullptr)
    {
        if (warm != 0)
        {
            // switched off: forget the network's memory, so it starts fresh next time
            warm = 0;
            for (auto& c : ch) std::fill(c.ola.begin(), c.ola.end(), 0.0f);
            for (auto& d : specDelay) std::fill(d.begin(), d.end(), std::complex<float>());
            stateA = rt.initialState; stateB = rt.initialState; stateInA = true;
        }
        return;
    }

    // spectra of this frame; the network listens to the single channel, or to the middle of a stereo pair
    for (int c = 0; c < channels; ++c) forward(ch[(size_t)c].hist.data(), ch[(size_t)c].X.data());
    if (channels == 1) std::copy(ch[0].X.begin(), ch[0].X.end(), Xm.begin());
    else for (int k = 0; k < kBins; ++k) Xm[(size_t)k] = 0.5f * (ch[0].X[(size_t)k] + ch[1].X[(size_t)k]);

    for (int k = 0; k < kBins; ++k) { specIn[(size_t)k * 2] = Xm[(size_t)k].real(); specIn[(size_t)k * 2 + 1] = Xm[(size_t)k].imag(); }
    const char* inNames[2] = { "spec", "state_in" };
    const char* outNames[2] = { "spec_e", "state_out" };
    const OrtValue* ins[2] = { tSpecIn, stateInA ? tStateA : tStateB };
    OrtValue* outs[2] = { tSpecOut, stateInA ? tStateB : tStateA };
    OrtStatus* st = rt.api->Run(rt.session, nullptr, inNames, ins, 2, outNames, 2, outs);
    if (st) { rt.api->ReleaseStatus(st); return; }
    stateInA = !stateInA;
    for (int k = 0; k < kBins; ++k) Ym[(size_t)k] = std::complex<float>(specOut[(size_t)k * 2], specOut[(size_t)k * 2 + 1]);

    // the network answers four frames late: its output belongs to the spectra of four frames ago
    const int old = (specPos + 1) % 5;
    for (int c = 0; c <= channels; ++c)
    {
        const std::complex<float>* src = (c < channels) ? ch[(size_t)c].X.data() : Xm.data();
        std::copy(src, src + kBins, specDelay[(size_t)c].begin() + (std::ptrdiff_t)specPos * kBins);
    }
    for (int c = 0; c < channels; ++c)
    {
        Channel& cc = ch[(size_t)c];
        if (channels == 1) inverse(Ym.data(), frame.data());
        else
        {
            // stereo: what the network did to the middle is applied to left and right alike (a complex gain per
            // frequency), so the stereo picture stays where it was
            const std::complex<float>* xm = specDelay[(size_t)channels].data() + (std::ptrdiff_t)old * kBins;
            const std::complex<float>* xc = specDelay[(size_t)c].data() + (std::ptrdiff_t)old * kBins;
            for (int k = 0; k < kBins; ++k)
            {
                const float p = std::norm(xm[k]);
                std::complex<float> g = p > 1.0e-18f ? Ym[(size_t)k] * std::conj(xm[k]) / p : std::complex<float>();
                const float ga = std::abs(g);
                if (ga > 2.0f) g *= 2.0f / ga;
                workSpec[(size_t)k] = g * xc[k];
            }
            inverse(workSpec.data(), frame.data());
        }
        for (int i = 0; i < kHop; ++i) { cc.speechFifo[(size_t)i] = cc.ola[(size_t)i] + frame[(size_t)i]; cc.ola[(size_t)i] = frame[(size_t)(i + kHop)]; }
    }
    specPos = (specPos + 1) % 5;
    if (warm < 1000) ++warm;
}

void NeuralVoice::process(const float* const* in, float* const* speech, float* const* dry, int n, bool active)
{
    for (int i = 0; i < n; ++i)
    {
        // the first frames after switching on are the network finding its feet: until then the dry signal stands
        // in for the voice, then one frame fades over
        const bool useNet = active && warm >= kWarmHops;
        const float fade = (warm == kWarmHops) ? (float)fifoFill / (float)kHop : 1.0f;
        for (int c = 0; c < channels; ++c)
        {
            Channel& cc = ch[(size_t)c];
            const float x = in[c][i];
            cc.inFifo[(size_t)fifoFill] = x;
            const float d = cc.dryDelay[(size_t)cc.dryPos];
            cc.dryDelay[(size_t)cc.dryPos] = x;
            if (++cc.dryPos >= kLatency) cc.dryPos = 0;
            dry[c][i] = d;
            speech[c][i] = useNet ? d + fade * (cc.speechFifo[(size_t)fifoFill] - d) : d;
        }
        if (++fifoFill >= kHop) { fifoFill = 0; hop(active); }
    }
}

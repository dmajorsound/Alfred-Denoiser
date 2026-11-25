// End-to-end test of the neural voice engine in front of the classic engine, without a host.
// fronttest <onnxruntime library> <model.onnx> <in.f32 mono> <out.f32> [name=value ...]
//   names: sliderN (JSFX slider), e.g. slider2=-21 slider19=2
#include "AlfredCore.h"
#include "../src/neural/VoiceFront.h"
#include "../src/neural/Enhance.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
int main(int argc, char** argv)
{
    if (argc < 5) return 1;
    NeuralRuntime rt; std::string err;
    if (!rt.load(argv[1], argv[2], err)) { std::fprintf(stderr, "load failed: %s\n", err.c_str()); return 2; }
    NeuralVoice nv(rt); VoiceFront front(nv);
    AlfredCore fx; fx.reserveMemory(32 * 65536);
    *fx.var("srate") = 48000; *fx.var("play_state") = 1; fx.sec_init();
    for (int i = 5; i < argc; ++i)
    {
        char* eq = std::strchr(argv[i], '='); if (!eq) continue; *eq = 0;
        if (double* v = fx.var(argv[i])) *v = std::atof(eq + 1);
    }
    FILE* f = std::fopen(argv[3], "rb"); std::vector<float> x; float buf[4096]; size_t r;
    while ((r = std::fread(buf, 4, 4096, f)) > 0) x.insert(x.end(), buf, buf + r);
    std::fclose(f);
    // bs=N sets the block size (0: changing sizes), passes=N renders the file N times in the same instance, with a
    // stop and a start of playback in between (the output of the last pass is written)
    int bsArg = 512, passes = 1;
    for (int i = 5; i < argc; ++i) { if (!std::strcmp(argv[i], "bs")) bsArg = std::atoi(argv[i] + 3); if (!std::strcmp(argv[i], "passes")) passes = std::atoi(argv[i] + 7); }
    const int n = (int)x.size(), cap = 4096;
    front.prepare(48000.0, 1, cap);
    std::vector<float> out((size_t)n); std::vector<double> core((size_t)cap);
    double* spl0 = fx.var("spl0"); double* spl1 = fx.var("spl1");
    const int sizes[6] = { 64, 1024, 333, 2048, 7, 480 };
    // Enhance as the plug-in runs it: set ALFRED_ENH_DIR to the folder with the models. ALFRED_ENH_PLAYBACK: as in
    // playback (windows are prepared in the background; a pass waits for them at its end), else as in a render.
    std::shared_ptr<EnhanceModels> em; EnhanceStage enh; std::vector<double> restBuf(8192, 0.0);
    if (const char* dir = std::getenv("ALFRED_ENH_DIR")) { em = EnhanceModels::get(argv[1], dir); enh.setModels(em); enh.prepare(1, 4096); }
    const bool enhOffline = std::getenv("ALFRED_ENH_PLAYBACK") == nullptr;
    const long long startPos = std::getenv("ALFRED_POS") ? std::atoll(std::getenv("ALFRED_POS")) : 0;

    for (int pass = 0; pass < passes; ++pass)
    {
        if (pass > 0)
        {
            // playback stops (a little silence runs through), then starts again
            *fx.var("play_state") = 0;
            for (int b = 0; b < 20; ++b) { fx.sec_block(); for (int i = 0; i < 256; ++i) { *spl0 = 0; *spl1 = 0; fx.sec_sample(); } }
            *fx.var("play_state") = 1;
            front.reset();
        }
        if (em) { enh.reset(); enh.align(startPos); }
        for (int p = 0, k = 0; p < n; ++k)
        {
            const int want_bs = bsArg > 0 ? bsArg : sizes[k % 6];
            const int m = n - p < want_bs ? n - p : want_bs;
            if (double* v = fx.var("samplesblock")) *v = m;
            if (double* v = fx.var("ext_neural")) *v = 1;
            // as the plug-in does it: the classic engine is told whether the neural engine delivers the voice
            const bool power = *fx.var("slider4") >= 0.5;
            const bool want = power && *fx.var("slider20") < 0.5 && *fx.var("slider24") >= 0.5;
            if (double* v = fx.var("ext_voice")) *v = want ? 1 : 0;
            fx.sec_block();
            double hi, lo; VoiceFront::levels(*fx.var("ga"), (int)(*fx.var("slider19") + 0.5), power, hi, lo);
            const float* ip[2] = { x.data() + p, x.data() + p }; double* cp[2] = { core.data(), core.data() };
            front.pre(ip, cp, m, want);
            for (int i = 0; i < m; ++i) { *spl0 = core[(size_t)i]; *spl1 = core[(size_t)i]; fx.sec_sample(); core[(size_t)i] = *spl0; }
            const int model = em && fx.var("slider25") ? (int)(*fx.var("slider25") + 0.5) : 0;
            if (double* v = fx.var("ext_enh_on")) *v = model != 0 ? 1 : 0;
            if (model != 0)
            {
                std::string err; em->load(model, err);
                std::fill(restBuf.begin(), restBuf.end(), 0.0);
                double* rp[2] = { restBuf.data(), restBuf.data() };
                front.post(rp, m, hi, lo, (int)*fx.var("pdc_delay"));
                const double* rc[2] = { restBuf.data(), restBuf.data() };
                const bool plain = power && (int)(*fx.var("slider19") + 0.5) == 0;
                const double amount = plain ? *fx.var("slider17") * 0.01 : 0.0;
                enh.process(cp, rc, m, model, amount, enhOffline);
            }
            else
            front.post(cp, m, hi, lo, (int)*fx.var("pdc_delay"));
            for (int i = 0; i < m; ++i) out[(size_t)(p + i)] = (float)core[(size_t)i];
            p += m;
        }
        // as a user would: let the background finish before the passage plays again
        while (em && enh.waiting() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (em) std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::fprintf(stderr, "latency %d\n", (int)*fx.var("pdc_delay") + NeuralVoice::kLatency);
    FILE* o = std::fopen(argv[4], "wb"); std::fwrite(out.data(), 4, out.size(), o); std::fclose(o);
    return 0;
}

// End-to-end test of the neural voice engine in front of the classic engine, without a host.
// fronttest <onnxruntime library> <model.onnx> <in.f32 mono> <out.f32> [name=value ...]
//   names: sliderN (JSFX slider), e.g. slider2=-21 slider19=2
#include "AlfredCore.h"
#include "../src/neural/VoiceFront.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    const int n = (int)x.size(), bs = 512;
    front.prepare(48000.0, 1, bs);
    std::vector<float> out((size_t)n); std::vector<double> core((size_t)bs);
    double* spl0 = fx.var("spl0"); double* spl1 = fx.var("spl1");
    for (int p = 0; p < n; p += bs)
    {
        const int m = n - p < bs ? n - p : bs;
        if (double* v = fx.var("samplesblock")) *v = m;
        if (double* v = fx.var("ext_neural")) *v = 1;
        fx.sec_block();
        const bool power = *fx.var("slider4") >= 0.5;
        const bool want = power && *fx.var("slider20") < 0.5 && *fx.var("slider24") >= 0.5;
        double hi, lo; VoiceFront::levels(*fx.var("ga"), (int)(*fx.var("slider19") + 0.5), power, hi, lo);
        const float* ip[2] = { x.data() + p, x.data() + p }; double* cp[2] = { core.data(), core.data() };
        front.pre(ip, cp, m, want);
        for (int i = 0; i < m; ++i) { *spl0 = core[(size_t)i]; *spl1 = core[(size_t)i]; fx.sec_sample(); core[(size_t)i] = *spl0; }
        front.post(cp, m, hi, lo, (int)*fx.var("pdc_delay"));
        for (int i = 0; i < m; ++i) out[(size_t)(p + i)] = (float)core[(size_t)i];
    }
    std::fprintf(stderr, "latency %d\n", (int)*fx.var("pdc_delay") + NeuralVoice::kLatency);
    FILE* o = std::fopen(argv[4], "wb"); std::fwrite(out.data(), 4, out.size(), o); std::fclose(o);
    return 0;
}

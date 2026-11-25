// Enhance stage on a file: enhancetest <ort lib> <model dir> <in.f32 mono 48k> <out.f32> <model 1|2> [block] [run 0|1] [amount]
#include "../src/neural/Enhance.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char** argv)
{
    if (argc < 6) return 2;
    auto models = EnhanceModels::get(argv[1], argv[2]);
    const int model = std::atoi(argv[5]), block = argc > 6 ? std::atoi(argv[6]) : 512;
    const bool run = argc > 7 ? std::atoi(argv[7]) != 0 : true;
    const double amount = argc > 8 ? std::atof(argv[8]) : 1.0;
    std::string err;
    if (run && !models->load(model, err)) { std::fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
    FILE* f = std::fopen(argv[3], "rb"); if (!f) return 1;
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f) / 4; std::fseek(f, 0, SEEK_SET);
    std::vector<float> x((size_t)n + EnhanceStage::kLatency, 0.0f);
    if (std::fread(x.data(), 4, (size_t)n, f) != (size_t)n) return 1;
    std::fclose(f);
    EnhanceStage st; st.setModels(models); st.prepare(1, block);
    std::vector<double> buf((size_t)block);
    std::vector<float> y(x.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < x.size(); pos += (size_t)block)
    {
        const int len = (int)std::min<size_t>((size_t)block, x.size() - pos);
        for (int i = 0; i < len; ++i) buf[(size_t)i] = x[pos + (size_t)i];
        double* p[1] = { buf.data() };
        st.process(p, nullptr, len, model, amount, run);
        for (int i = 0; i < len; ++i) y[pos + (size_t)i] = (float)buf[(size_t)i];
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    f = std::fopen(argv[4], "wb");
    std::fwrite(y.data() + EnhanceStage::kLatency, 4, (size_t)n, f);      // latency removed
    std::fclose(f);
    std::fprintf(stderr, "%.1f s of audio in %.1f s (%.2fx real time), enhanced %.0f s\n", n / 48000.0, dt, n / 48000.0 / dt, st.secondsEnhanced());
    return 0;
}

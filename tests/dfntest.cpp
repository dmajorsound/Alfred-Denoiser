// DeepFilterNet3 voice engine on a file: dfntest <weights.bin> <in.f32> <out_speech.f32> [channels 1|2] [block]
// in/out: interleaved float32 at 48 kHz. The output is shifted back by the latency, so it lines up with the input.
#include "../src/neural/DfnVoice.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char** argv)
{
    if (argc < 4) return 2;
    std::string err;
    auto m = DfnModel::load(argv[1], err);
    if (!m) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const int C = argc > 4 ? std::atoi(argv[4]) : 1, B = argc > 5 ? std::atoi(argv[5]) : 512;
    FILE* f = std::fopen(argv[2], "rb"); std::fseek(f, 0, SEEK_END); const long total = std::ftell(f) / 4; std::fseek(f, 0, SEEK_SET);
    const long n = total / C;
    std::vector<float> x((size_t)total); if (std::fread(x.data(), 4, (size_t)total, f) != (size_t)total) return 1; std::fclose(f);
    DfnVoice v(m); v.prepare(C);
    const long L = DfnVoice::kLatency, N = n + L;
    std::vector<float> in[2], sp[2], dr[2], out((size_t)(n * C));
    for (int c = 0; c < 2; ++c) { in[c].assign((size_t)B, 0.0f); sp[c].assign((size_t)B, 0.0f); dr[c].assign((size_t)B, 0.0f); }
    const auto t0 = std::chrono::steady_clock::now();
    for (long p = 0; p < N; p += B)
    {
        const int len = (int)std::min<long>(B, N - p);
        for (int i = 0; i < len; ++i) for (int c = 0; c < C; ++c) in[c][(size_t)i] = p + i < n ? x[(size_t)((p + i) * C + c)] : 0.0f;
        const float* ip[2] = { in[0].data(), in[1].data() }; float* spp[2] = { sp[0].data(), sp[1].data() }; float* dp[2] = { dr[0].data(), dr[1].data() };
        v.process(ip, spp, dp, len, true);
        for (int i = 0; i < len; ++i) { const long q = p + i - L; if (q >= 0 && q < n) for (int c = 0; c < C; ++c) out[(size_t)(q * C + c)] = sp[c][(size_t)i]; }
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    f = std::fopen(argv[3], "wb"); std::fwrite(out.data(), 4, out.size(), f); std::fclose(f);
    std::fprintf(stderr, "%.1f s of audio in %.2f s: %.1fx real time\n", n / 48000.0, dt, n / 48000.0 / dt);
    return 0;
}

// Runs the neural voice engine over a raw float32 file in blocks of changing size.
// neuraltest <onnxruntime library> <model.onnx> <in.f32> <speech.f32> <dry.f32> <channels>
#include "../src/neural/NeuralVoice.h"
#include <chrono>
#include <cstdio>
#include <vector>
int main(int argc, char** argv)
{
    if (argc < 7) return 1;
    NeuralRuntime rt; std::string err;
    if (!rt.load(argv[1], argv[2], err)) { std::fprintf(stderr, "load failed: %s\n", err.c_str()); return 2; }
    const int nch = std::atoi(argv[6]);
    NeuralVoice nv(rt); nv.prepare(nch);
    FILE* f = std::fopen(argv[3], "rb"); std::vector<float> x; float buf[4096]; size_t r;
    while ((r = std::fread(buf, sizeof(float), 4096, f)) > 0) x.insert(x.end(), buf, buf + r);
    std::fclose(f);
    const int n = (int)x.size() / nch;
    std::vector<std::vector<float>> in((size_t)nch, std::vector<float>((size_t)n)), sp = in, dr = in;
    for (int i = 0; i < n; ++i) for (int c = 0; c < nch; ++c) in[(size_t)c][(size_t)i] = x[(size_t)(i * nch + c)];
    const int sizes[5] = { 64, 512, 333, 1024, 7 };
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0, k = 0; pos < n; ++k)
    {
        const int bs = std::min(sizes[k % 5], n - pos);
        const float* ip[2]; float* s[2]; float* d[2];
        for (int c = 0; c < nch; ++c) { ip[c] = in[(size_t)c].data() + pos; s[c] = sp[(size_t)c].data() + pos; d[c] = dr[(size_t)c].data() + pos; }
        nv.process(ip, s, d, bs, true);
        pos += bs;
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "%.1f s of audio in %.2f s (%.1fx real time, one thread)\n", n / 48000.0, dt, n / 48000.0 / dt);
    auto wr = [&](const char* p, std::vector<std::vector<float>>& v) { FILE* o = std::fopen(p, "wb"); for (int i = 0; i < n; ++i) for (int c = 0; c < nch; ++c) std::fwrite(&v[(size_t)c][(size_t)i], sizeof(float), 1, o); std::fclose(o); };
    wr(argv[4], sp); wr(argv[5], dr);
    return 0;
}

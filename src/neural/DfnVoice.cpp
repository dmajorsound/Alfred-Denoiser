#include "DfnVoice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

namespace
{
const double kPi = 3.14159265358979323846;
inline float sig(float x) { return 1.0f / (1.0f + std::exp(-x)); }
inline float relu(float x) { return x > 0.0f ? x : 0.0f; }

// grouped linear: x [g * i] with w [g][i][h] -> y [g * h]
void glin(const float* x, const std::vector<float>& w, int g, int i, int h, float* y)
{
    for (int a = 0; a < g; ++a)
    {
        float* o = y + a * h;
        for (int k = 0; k < h; ++k) o[k] = 0.0f;
        const float* xa = x + a * i;
        const float* wa = w.data() + (size_t)a * i * h;
        for (int j = 0; j < i; ++j)
        {
            const float v = xa[j];
            const float* wr = wa + (size_t)j * h;
            for (int k = 0; k < h; ++k) o[k] += v * wr[k];
        }
    }
}

// GRU with ONNX gate order z, r, h (linear_before_reset). Wt [in][3H] and Rt [H][3H] are the transposed weights,
// so both products run along memory (the compiler can vectorise them without changing the sum order), B [6H]
void gru(const float* x, int in, float* h, const std::vector<float>& Wt, const std::vector<float>& Rt, const std::vector<float>& B, float* tmp)
{
    const int H = 256, G = 3 * H;
    float* a = tmp; float* b = tmp + G;
    for (int r = 0; r < G; ++r) { a[r] = B[(size_t)r]; b[r] = B[(size_t)(G + r)]; }
    for (int k = 0; k < in; ++k)
    {
        const float v = x[k]; const float* w = Wt.data() + (size_t)k * G;
        for (int r = 0; r < G; ++r) a[r] += w[r] * v;
    }
    for (int k = 0; k < H; ++k)
    {
        const float v = h[k]; const float* w = Rt.data() + (size_t)k * G;
        for (int r = 0; r < G; ++r) b[r] += w[r] * v;
    }
    for (int k = 0; k < H; ++k)
    {
        const float z = sig(a[k] + b[k]), rg = sig(a[H + k] + b[H + k]);
        const float n = std::tanh(a[2 * H + k] + rg * b[2 * H + k]);
        h[k] = (1.0f - z) * n + z * h[k];
    }
}

// depthwise 1x3 over frequency, padding 1: x [C][F] -> y [C][Fo]
int dw3(const float* x, int C, int F, const std::vector<float>& w, int stride, float* y)
{
    const int Fo = (F + 2 - 3) / stride + 1;
    for (int c = 0; c < C; ++c)
    {
        const float k0 = w[(size_t)c * 3], k1 = w[(size_t)c * 3 + 1], k2 = w[(size_t)c * 3 + 2];
        const float* xc = x + c * F;
        for (int f = 0; f < Fo; ++f)
        {
            const int s = f * stride - 1;
            float v = 0.0f;
            if (s >= 0) v += k0 * xc[s];
            if (s + 1 >= 0 && s + 1 < F) v += k1 * xc[s + 1];
            if (s + 2 < F) v += k2 * xc[s + 2];
            y[c * Fo + f] = v;
        }
    }
    return Fo;
}

// depthwise transposed 1x3, stride 2, padding 1, output padding 1: [C][F] -> [C][2F]
void dwt3(const float* x, int C, int F, const std::vector<float>& w, float* y)
{
    for (int c = 0; c < C; ++c)
    {
        const float* k = w.data() + (size_t)c * 3;
        float* yc = y + c * 2 * F;
        for (int f = 0; f < 2 * F; ++f) yc[f] = 0.0f;
        for (int f = 0; f < F; ++f)
            for (int j = 0; j < 3; ++j)
            {
                const int o = 2 * f + j - 1;
                if (o >= 0 && o < 2 * F) yc[o] += x[c * F + f] * k[j];
            }
    }
}

// pointwise: w [O][C], b [O]: x [C][F] -> y [O][F], with relu
void pw(const float* x, int C, int F, const std::vector<float>& w, const std::vector<float>& b, int O, float* y, bool doRelu)
{
    for (int o = 0; o < O; ++o)
    {
        float* yo = y + o * F;
        const float bo = b.empty() ? 0.0f : b[(size_t)o];
        for (int f = 0; f < F; ++f) yo[f] = bo;
        for (int c = 0; c < C; ++c)
        {
            const float wc = w[(size_t)o * C + c];
            const float* xc = x + c * F;
            for (int f = 0; f < F; ++f) yo[f] += wc * xc[f];
        }
        if (doRelu) for (int f = 0; f < F; ++f) yo[f] = relu(yo[f]);
    }
}
}

// ------------------------------------------------------------------------------------------------- model
const std::vector<float> DfnModel::none;

std::shared_ptr<DfnModel> DfnModel::load(const std::string& path, std::string& error)
{
#if defined(_WIN32)
    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0);
    std::wstring wp((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), &wp[0], wn);
    FILE* f = _wfopen(wp.c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) { error = "cannot open " + path; return nullptr; }
    auto m = std::shared_ptr<DfnModel>(new DfnModel());
    char magic[8];
    unsigned count = 0;
    bool ok = std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, "DFN3W001", 8) == 0 && std::fread(&count, 4, 1, f) == 1;
    for (unsigned i = 0; ok && i < count; ++i)
    {
        unsigned len = 0, rank = 0;
        ok = std::fread(&len, 4, 1, f) == 1 && len < 256;
        std::string name(len, '\0');
        ok = ok && std::fread(&name[0], 1, len, f) == len && std::fread(&rank, 4, 1, f) == 1 && rank <= 4;
        size_t n = 1;
        for (unsigned r = 0; ok && r < rank; ++r) { unsigned d = 0; ok = std::fread(&d, 4, 1, f) == 1; n *= d; }
        if (!ok) break;
        std::vector<float> v(n);
        ok = std::fread(v.data(), 4, n, f) == n;
        m->t[name] = std::move(v);
    }
    std::fclose(f);
    if (ok)
        for (auto& kv : m->t)
        {
            const std::string& k = kv.first;
            if (k.size() < 2 || (k.compare(k.size() - 2, 2, ".W") != 0 && k.compare(k.size() - 2, 2, ".R") != 0) || kv.second.size() != 768u * 256u) continue;
            std::vector<float> tr(kv.second.size());
            for (int r = 0; r < 768; ++r) for (int c = 0; c < 256; ++c) tr[(size_t)c * 768 + (size_t)r] = kv.second[(size_t)r * 256 + (size_t)c];
            kv.second.swap(tr);         // from here on [in][3H]
        }
    if (!ok || m->t.size() != 62 || (*m)["enc.gru.W"].size() != 768u * 256u) { error = "not the expected DeepFilterNet3 weight file"; return nullptr; }
    return m;
}

const std::vector<float>& DfnModel::operator[](const char* name) const
{
    const auto it = t.find(name);
    return it == t.end() ? none : it->second;
}

// --------------------------------------------------------------------------------------------------- net
DfnNet::DfnNet(std::shared_ptr<DfnModel> m) : M(std::move(m))
{
    pe.assign(2 * 32, 0.0f); ps.assign(2 * 2 * 96, 0.0f); pc.assign(4 * 64 * 96, 0.0f);
    for (auto& v : h) v.assign(256, 0.0f);
    e0.resize(64 * 32); e1.resize(64 * 16); e2.resize(64 * 8); e3.resize(64 * 8);
    t0.resize(64 * 96); t1.resize(64 * 96); d.resize(64 * 96); c0.resize(64 * 96); c1.resize(64 * 48);
    emb.resize(512); x.resize(3072); y.resize(1024); tmp.resize(6 * 256); co.resize(960); dd.resize(10 * 96);
}

void DfnNet::reset()
{
    std::fill(pe.begin(), pe.end(), 0.0f); std::fill(ps.begin(), ps.end(), 0.0f); std::fill(pc.begin(), pc.end(), 0.0f);
    for (auto& v : h) std::fill(v.begin(), v.end(), 0.0f);
}

void DfnNet::step(const float* fe, const std::complex<float>* fs, float* mask, std::complex<float>* coef)
{
    const DfnModel& W = *M;
    // ---- encoder, ERB path: 3 frames x 32 bands, the newest last
    float xin[3][34] = {};
    for (int k = 0; k < 32; ++k) { xin[0][k + 1] = pe[(size_t)k]; xin[1][k + 1] = pe[(size_t)(32 + k)]; xin[2][k + 1] = fe[k]; }
    std::memcpy(pe.data(), pe.data() + 32, sizeof(float) * 32); std::memcpy(pe.data() + 32, fe, sizeof(float) * 32);
    {
        const auto& w = W["enc.erb_conv0.w"]; const auto& b = W["enc.erb_conv0.b"];
        for (int o = 0; o < 64; ++o)
            for (int f = 0; f < 32; ++f)
            {
                float s = b[(size_t)o];
                for (int t = 0; t < 3; ++t) for (int j = 0; j < 3; ++j) s += w[(size_t)(o * 9 + t * 3 + j)] * xin[t][f + j];
                e0[(size_t)(o * 32 + f)] = relu(s);
            }
    }
    int F = dw3(e0.data(), 64, 32, W["enc.erb_conv1_dw.w"], 2, t0.data());
    pw(t0.data(), 64, F, W["enc.erb_conv1_pw.w"], W["enc.erb_conv1_pw.b"], 64, e1.data(), true);      // [64][16]
    F = dw3(e1.data(), 64, 16, W["enc.erb_conv2_dw.w"], 2, t0.data());
    pw(t0.data(), 64, F, W["enc.erb_conv2_pw.w"], W["enc.erb_conv2_pw.b"], 64, e2.data(), true);      // [64][8]
    F = dw3(e2.data(), 64, 8, W["enc.erb_conv3_dw.w"], 1, t0.data());
    pw(t0.data(), 64, F, W["enc.erb_conv3_pw.w"], W["enc.erb_conv3_pw.b"], 64, e3.data(), true);      // [64][8]

    // ---- encoder, complex path: 3 frames x (re, im) x 96 bins
    float sp[3][2][98];
    for (int t = 0; t < 3; ++t) for (int c = 0; c < 2; ++c) { sp[t][c][0] = 0.0f; sp[t][c][97] = 0.0f; }
    for (int k = 0; k < 96; ++k)
    {
        sp[0][0][k + 1] = ps[(size_t)k]; sp[0][1][k + 1] = ps[(size_t)(96 + k)];
        sp[1][0][k + 1] = ps[(size_t)(192 + k)]; sp[1][1][k + 1] = ps[(size_t)(288 + k)];
        sp[2][0][k + 1] = fs[k].real(); sp[2][1][k + 1] = fs[k].imag();
    }
    std::memcpy(ps.data(), ps.data() + 192, sizeof(float) * 192);
    for (int k = 0; k < 96; ++k) { ps[(size_t)(192 + k)] = fs[k].real(); ps[(size_t)(288 + k)] = fs[k].imag(); }
    {
        const auto& w = W["enc.df_conv0_dw.w"];          // [64][1][3][3], two groups of 32
        for (int o = 0; o < 64; ++o)
        {
            const int ci = o / 32;
            for (int f = 0; f < 96; ++f)
            {
                float s = 0.0f;
                for (int t = 0; t < 3; ++t) for (int j = 0; j < 3; ++j) s += w[(size_t)(o * 9 + t * 3 + j)] * sp[t][ci][f + j];
                d[(size_t)(o * 96 + f)] = s;
            }
        }
    }
    pw(d.data(), 64, 96, W["enc.df_conv0_pw.w"], W["enc.df_conv0_pw.b"], 64, c0.data(), true);        // [64][96]
    F = dw3(c0.data(), 64, 96, W["enc.df_conv1_dw.w"], 2, t0.data());
    pw(t0.data(), 64, F, W["enc.df_conv1_pw.w"], W["enc.df_conv1_pw.b"], 64, c1.data(), true);        // [64][48]
    for (int f = 0; f < 48; ++f) for (int c = 0; c < 64; ++c) x[(size_t)(f * 64 + c)] = c1[(size_t)(c * 48 + f)];
    glin(x.data(), W["enc.df_fc_emb"], 32, 96, 16, y.data());                                          // 512
    for (int f = 0; f < 8; ++f) for (int c = 0; c < 64; ++c) emb[(size_t)(f * 64 + c)] = e3[(size_t)(c * 8 + f)] + relu(y[(size_t)(f * 64 + c)]);
    glin(emb.data(), W["enc.lin_in"], 16, 32, 16, y.data());
    for (int k = 0; k < 256; ++k) y[(size_t)k] = relu(y[(size_t)k]);
    gru(y.data(), 256, h[0].data(), W["enc.gru.W"], W["enc.gru.R"], W["enc.gru.B"], tmp.data());
    glin(h[0].data(), W["enc.lin_out"], 16, 16, 32, emb.data());
    for (int k = 0; k < 512; ++k) emb[(size_t)k] = relu(emb[(size_t)k]);

    // ---- ERB decoder -> mask
    glin(emb.data(), W["erb.lin_in"], 16, 32, 16, y.data());
    for (int k = 0; k < 256; ++k) y[(size_t)k] = relu(y[(size_t)k]);
    gru(y.data(), 256, h[1].data(), W["erb.gru0.W"], W["erb.gru0.R"], W["erb.gru0.B"], tmp.data());
    gru(h[1].data(), 256, h[2].data(), W["erb.gru1.W"], W["erb.gru1.R"], W["erb.gru1.B"], tmp.data());
    glin(h[2].data(), W["erb.lin_out"], 16, 16, 32, y.data());                                          // [8][64] -> de [64][8]
    auto skip = [&](const float* e, int Fs, const char* n, float* out)
    {
        const auto& w = W[(std::string("erb.") + n + ".w").c_str()]; const auto& b = W[(std::string("erb.") + n + ".b").c_str()];
        for (int c = 0; c < 64; ++c) for (int f = 0; f < Fs; ++f) out[c * Fs + f] = relu(e[c * Fs + f] * w[(size_t)c] + b[(size_t)c]);
    };
    skip(e3.data(), 8, "conv3p", t0.data());
    for (int c = 0; c < 64; ++c) for (int f = 0; f < 8; ++f) t0[(size_t)(c * 8 + f)] += relu(y[(size_t)(f * 64 + c)]);
    dw3(t0.data(), 64, 8, W["erb.convt3_dw.w"], 1, t1.data());
    pw(t1.data(), 64, 8, W["erb.convt3_pw.w"], W["erb.convt3_pw.b"], 64, d.data(), true);              // [64][8]
    skip(e2.data(), 8, "conv2p", t0.data());
    for (int i = 0; i < 64 * 8; ++i) t0[(size_t)i] += d[(size_t)i];
    dwt3(t0.data(), 64, 8, W["erb.convt2_dw.w"], t1.data());
    pw(t1.data(), 64, 16, W["erb.convt2_pw.w"], W["erb.convt2_pw.b"], 64, d.data(), true);             // [64][16]
    skip(e1.data(), 16, "conv1p", t0.data());
    for (int i = 0; i < 64 * 16; ++i) t0[(size_t)i] += d[(size_t)i];
    dwt3(t0.data(), 64, 16, W["erb.convt1_dw.w"], t1.data());
    pw(t1.data(), 64, 32, W["erb.convt1_pw.w"], W["erb.convt1_pw.b"], 64, d.data(), true);             // [64][32]
    skip(e0.data(), 32, "conv0p", t0.data());
    for (int i = 0; i < 64 * 32; ++i) t0[(size_t)i] += d[(size_t)i];
    {
        const auto& w = W["erb.conv0_out.w"]; const float b = W["erb.conv0_out.b"][0];   // [1][64][1][3]
        for (int f = 0; f < 32; ++f)
        {
            float s = b;
            for (int c = 0; c < 64; ++c)
                for (int j = 0; j < 3; ++j)
                {
                    const int q = f + j - 1;
                    if (q >= 0 && q < 32) s += w[(size_t)(c * 3 + j)] * t0[(size_t)(c * 32 + q)];
                }
            mask[f] = sig(s);
        }
    }

    // ---- deep-filter decoder -> coefficients
    glin(emb.data(), W["df.lin_in"], 8, 64, 32, y.data());
    for (int k = 0; k < 256; ++k) y[(size_t)k] = relu(y[(size_t)k]);
    gru(y.data(), 256, h[3].data(), W["df.gru0.W"], W["df.gru0.R"], W["df.gru0.B"], tmp.data());
    gru(h[3].data(), 256, h[4].data(), W["df.gru1.W"], W["df.gru1.R"], W["df.gru1.B"], tmp.data());
    glin(emb.data(), W["df.skip"], 16, 32, 16, y.data());
    for (int k = 0; k < 256; ++k) y[(size_t)k] += h[4][(size_t)k];
    glin(y.data(), W["df.out"], 16, 16, 60, co.data());                                                  // [96][10]
    for (int k = 0; k < 960; ++k) co[(size_t)k] = std::tanh(co[(size_t)k]);
    {
        // a convolution over the last five frames of c0 (the newest last)
        const auto& w = W["df.convp_dw.w"];                                                              // [10][32][5][1]
        for (int o = 0; o < 10; ++o)
        {
            const int ci = (o / 5) * 32;
            float* r = d.data() + o * 96;
            for (int f = 0; f < 96; ++f) r[f] = 0.0f;
            for (int c = 0; c < 32; ++c)
                for (int t = 0; t < 5; ++t)
                {
                    const float wt = w[(size_t)((o * 32 + c) * 5 + t)];
                    const float* src = t < 4 ? pc.data() + (size_t)(t * 64 + ci + c) * 96 : c0.data() + (size_t)(ci + c) * 96;
                    for (int f = 0; f < 96; ++f) r[f] += wt * src[f];
                }
        }
        std::memmove(pc.data(), pc.data() + 64 * 96, sizeof(float) * 3 * 64 * 96);
        std::memcpy(pc.data() + 3 * 64 * 96, c0.data(), sizeof(float) * 64 * 96);
        pw(d.data(), 10, 96, W["df.convp_pw.w"], W["df.convp_pw.b"], 10, dd.data(), true);
    }
    for (int f = 0; f < 96; ++f)
        for (int n = 0; n < 5; ++n)
            coef[f * 5 + n] = std::complex<float>(co[(size_t)(f * 10 + 2 * n)] + dd[(size_t)((2 * n) * 96 + f)],
                                                  co[(size_t)(f * 10 + 2 * n + 1)] + dd[(size_t)((2 * n + 1) * 96 + f)]);
}

// ------------------------------------------------------------------------------------------------ voice
DfnVoice::DfnVoice(std::shared_ptr<DfnModel> m) : model(m), net(m)
{
    window.resize(kN);
    for (int i = 0; i < kN; ++i)
    {
        const double s = std::sin(0.5 * kPi * (i + 0.5) / (kN / 2));
        window[(size_t)i] = (float)std::sin(0.5 * kPi * s * s);
    }
    roots.resize(kN);
    for (int i = 0; i < kN; ++i) roots[(size_t)i] = std::complex<float>((float)std::cos(2 * kPi * i / kN), (float)-std::sin(2 * kPi * i / kN));
    work.resize(kN); workOut.resize(kN);
    // the ERB bands of libDF: 32 bands over 481 bins, at least two bins each
    auto f2e = [](double f) { return 9.265 * std::log1p(f / (24.7 * 9.265)); };
    auto e2f = [](double e) { return 24.7 * 9.265 * (std::exp(e / 9.265) - 1.0); };
    const double step = (f2e(24000.0) - f2e(0.0)) / kErb;
    std::vector<int> erb; int prev = 0, over = 0;
    for (int i = 1; i <= kErb; ++i)
    {
        const int fb = (int)std::floor(e2f(i * step) / (48000.0 / kN) + 0.5);
        int nf = fb - prev - over;
        if (nf < 2) { over = 2 - nf; nf = 2; } else over = 0;
        erb.push_back(nf); prev = fb;
    }
    erb.back() += 1;
    int sum = 0; for (int v : erb) sum += v;
    erb.back() -= sum - kBins;
    bandEdge.assign(1, 0);
    for (int v : erb) bandEdge.push_back(bandEdge.back() + v);
    bandOf.resize(kBins);
    for (int b = 0; b < kErb; ++b) for (int k = bandEdge[(size_t)b]; k < bandEdge[(size_t)b + 1]; ++k) bandOf[(size_t)k] = b;
    prepare(1);
}

void DfnVoice::prepare(int numChannels)
{
    channels = std::max(1, std::min(2, numChannels));
    ch.assign((size_t)channels, Channel());
    for (auto& c : ch)
    {
        c.hist.assign(kN, 0.0f); c.ola.assign(kHop, 0.0f); c.inFifo.assign(kHop, 0.0f); c.speechFifo.assign(kHop, 0.0f);
        c.dryDelay.assign((size_t)kLatency, 0.0f); c.X.assign(kBins, {});
    }
    past.assign((size_t)channels + 1, std::vector<std::complex<float>>((size_t)kBins * kOrder));
    Xm.assign(kBins, {}); Ym.assign(kBins, {}); Yc.assign(kBins, {}); coef.assign((size_t)kDf * kOrder, {});
    fe.assign(kErb, 0.0f); mask.assign(kErb, 1.0f); frame.assign(kN, 0.0f); fs.assign(kDf, {});
    erbMean.resize(kErb); dfUnit.resize(kDf);
    reset();
}

void DfnVoice::reset()
{
    for (auto& c : ch)
    {
        std::fill(c.hist.begin(), c.hist.end(), 0.0f); std::fill(c.ola.begin(), c.ola.end(), 0.0f);
        std::fill(c.inFifo.begin(), c.inFifo.end(), 0.0f); std::fill(c.speechFifo.begin(), c.speechFifo.end(), 0.0f);
        std::fill(c.dryDelay.begin(), c.dryDelay.end(), 0.0f); c.dryPos = 0;
    }
    for (auto& p : past) std::fill(p.begin(), p.end(), std::complex<float>());
    pastPos = 0; fifoFill = 0; warm = 0;
    for (int b = 0; b < kErb; ++b) erbMean[(size_t)b] = (float)(-60.0 - 30.0 * b / (kErb - 1));
    for (int k = 0; k < kDf; ++k) dfUnit[(size_t)k] = (float)(0.001 - 0.0009 * k / (kDf - 1));
    net.reset();
}

// mixed-radix FFT (960 = 2^6 * 3 * 5), decimation in time. sign -1: forward, +1: inverse (not scaled).
// Products written out by hand: std::complex multiplication checks for infinities and is several times slower.
void DfnVoice::fft(std::complex<float>* x, int n, int stride, std::complex<float>* out, int sign) const
{
    if (n == 1) { out[0] = x[0]; return; }
    const int p = (n % 2 == 0) ? 2 : (n % 3 == 0) ? 3 : 5;
    const int m = n / p;
    for (int r = 0; r < p; ++r) fft(x + r * stride, m, stride * p, out + r * m, sign);
    const int st = kN / n;
    float tr[5], ti[5];
    for (int k = 0; k < m; ++k)
    {
        for (int r = 0; r < p; ++r) { tr[r] = out[r * m + k].real(); ti[r] = out[r * m + k].imag(); }
        for (int q = 0; q < p; ++q)
        {
            const int kk = k + q * m;
            float ar = tr[0], ai = ti[0];
            int idx = 0; const int inc = kk * st;
            for (int r = 1; r < p; ++r)
            {
                idx += inc; if (idx >= kN) idx -= kN * (idx / kN);
                const float wr = roots[(size_t)idx].real(), wi = sign > 0 ? -roots[(size_t)idx].imag() : roots[(size_t)idx].imag();
                ar += tr[r] * wr - ti[r] * wi; ai += tr[r] * wi + ti[r] * wr;
            }
            out[kk] = std::complex<float>(ar, ai);
        }
    }
}

void DfnVoice::forward(const float* f, std::complex<float>* spec)
{
    // libDF scales the analysis by 1 / (N^2 / (2 hop)) = 1 / 960
    const float sc = 2.0f * kHop / ((float)kN * (float)kN);
    for (int i = 0; i < kN; ++i) work[(size_t)i] = std::complex<float>(f[i] * window[(size_t)i], 0.0f);
    fft(work.data(), kN, 1, workOut.data(), -1);
    for (int k = 0; k < kBins; ++k) spec[k] = workOut[(size_t)k] * sc;
}

void DfnVoice::inverse(const std::complex<float>* spec, float* f)
{
    work[0] = spec[0];
    for (int k = 1; k < kBins - 1; ++k) { work[(size_t)k] = spec[k]; work[(size_t)(kN - k)] = std::conj(spec[k]); }
    work[(size_t)(kBins - 1)] = spec[kBins - 1];
    fft(work.data(), kN, 1, workOut.data(), +1);
    // irfft (1/N) times N, times the window
    for (int i = 0; i < kN; ++i) f[i] = workOut[(size_t)i].real() * window[(size_t)i];
}

void DfnVoice::hop(bool active)
{
    for (auto& c : ch)
    {
        std::memmove(c.hist.data(), c.hist.data() + kHop, sizeof(float) * kHop);
        std::memcpy(c.hist.data() + kHop, c.inFifo.data(), sizeof(float) * kHop);
    }
    if (!active)
    {
        if (warm != 0)
        {
            warm = 0;
            for (auto& c : ch) std::fill(c.ola.begin(), c.ola.end(), 0.0f);
            for (auto& p : past) std::fill(p.begin(), p.end(), std::complex<float>());
            for (int b = 0; b < kErb; ++b) erbMean[(size_t)b] = (float)(-60.0 - 30.0 * b / (kErb - 1));
            for (int k = 0; k < kDf; ++k) dfUnit[(size_t)k] = (float)(0.001 - 0.0009 * k / (kDf - 1));
            net.reset();
        }
        return;
    }
    for (int c = 0; c < channels; ++c) forward(ch[(size_t)c].hist.data(), ch[(size_t)c].X.data());
    if (channels == 1) std::copy(ch[0].X.begin(), ch[0].X.end(), Xm.begin());
    else for (int k = 0; k < kBins; ++k) Xm[(size_t)k] = 0.5f * (ch[0].X[(size_t)k] + ch[1].X[(size_t)k]);

    // features of the newest frame
    const float a = 0.99f;
    for (int b = 0; b < kErb; ++b)
    {
        double p = 0.0;
        for (int k = bandEdge[(size_t)b]; k < bandEdge[(size_t)b + 1]; ++k) p += std::norm(Xm[(size_t)k]);
        const float e = (float)(10.0 * std::log10(p / (bandEdge[(size_t)b + 1] - bandEdge[(size_t)b]) + 1.0e-10));
        erbMean[(size_t)b] = e * (1.0f - a) + erbMean[(size_t)b] * a;
        fe[(size_t)b] = (e - erbMean[(size_t)b]) / 40.0f;
    }
    for (int k = 0; k < kDf; ++k)
    {
        dfUnit[(size_t)k] = std::abs(Xm[(size_t)k]) * (1.0f - a) + dfUnit[(size_t)k] * a;
        fs[(size_t)k] = Xm[(size_t)k] / std::sqrt(dfUnit[(size_t)k]);
    }
    // keep the last five spectra; the network's answer belongs to the frame two hops back
    for (int c = 0; c <= channels; ++c)
    {
        const std::complex<float>* src = c < channels ? ch[(size_t)c].X.data() : Xm.data();
        std::copy(src, src + kBins, past[(size_t)c].begin() + (std::ptrdiff_t)pastPos * kBins);
    }
    net.step(fe.data(), fs.data(), mask.data(), coef.data());
    auto at = [&](int c, int age) { return past[(size_t)c].data() + (std::ptrdiff_t)(((pastPos - age) % kOrder + kOrder) % kOrder) * kBins; };
    // Y = X(t) * mask above the deep-filter range, the deep filter over X(t-2 .. t+2) below it (t = newest - 2)
    const std::complex<float>* xt = at(channels, kAhead);
    for (int k = kDf; k < kBins; ++k) Ym[(size_t)k] = xt[k] * mask[(size_t)bandOf[(size_t)k]];
    for (int k = 0; k < kDf; ++k)
    {
        float sr = 0.0f, si = 0.0f;
        for (int n = 0; n < kOrder; ++n)
        {
            const std::complex<float> a = at(channels, kOrder - 1 - n)[k], b = coef[(size_t)(k * kOrder + n)];
            sr += a.real() * b.real() - a.imag() * b.imag(); si += a.real() * b.imag() + a.imag() * b.real();
        }
        Ym[(size_t)k] = std::complex<float>(sr, si);
    }
    for (int c = 0; c < channels; ++c)
    {
        Channel& cc = ch[(size_t)c];
        if (channels == 1) inverse(Ym.data(), frame.data());
        else
        {
            // stereo: the change the network made to the middle, applied to left and right alike
            const std::complex<float>* xc = at(c, kAhead);
            for (int k = 0; k < kBins; ++k)
            {
                const float p = std::norm(xt[k]);
                std::complex<float> g = p > 1.0e-18f ? Ym[(size_t)k] * std::conj(xt[k]) / p : std::complex<float>();
                const float ga = std::abs(g);
                if (ga > 2.0f) g *= 2.0f / ga;
                Yc[(size_t)k] = g * xc[k];
            }
            inverse(Yc.data(), frame.data());
        }
        for (int i = 0; i < kHop; ++i) { cc.speechFifo[(size_t)i] = cc.ola[(size_t)i] + frame[(size_t)i]; cc.ola[(size_t)i] = frame[(size_t)(i + kHop)]; }
    }
    pastPos = (pastPos + 1) % kOrder;
    if (warm < 1000) ++warm;
}

void DfnVoice::process(const float* const* in, float* const* speech, float* const* dry, int n, bool active)
{
    for (int i = 0; i < n; ++i)
    {
        const bool useNet = active && warm >= kWarmHops;
        const float fade = (warm == kWarmHops) ? (float)fifoFill / (float)kHop : 1.0f;
        for (int c = 0; c < channels; ++c)
        {
            Channel& cc = ch[(size_t)c];
            const float x = in[c][i];
            cc.inFifo[(size_t)fifoFill] = x;
            const float dv = cc.dryDelay[(size_t)cc.dryPos];
            cc.dryDelay[(size_t)cc.dryPos] = x;
            if (++cc.dryPos >= kLatency) cc.dryPos = 0;
            dry[c][i] = dv;
            speech[c][i] = useNet ? dv + fade * (cc.speechFifo[(size_t)fifoFill] - dv) : dv;
        }
        if (++fifoFill >= kHop) { fifoFill = 0; hop(active); }
    }
}

#pragma once
// Puts the neural voice engine in front of the classic engine.
//   pre():  input -> the voice (goes on to the classic engine); what the network took away ("the rest") is kept
//   post(): adds the rest to the classic engine's output, at the level of the Noise fader,
//           delayed by the classic engine's latency so both line up
// Below 350 Hz the rest is turned down more gently than above, which keeps the lows of a voice full.
#include "NeuralVoice.h"
#include <algorithm>
#include <cmath>
#include <vector>

class VoiceFront
{
public:
    explicit VoiceFront(NeuralVoice& n) : nv(n) {}

    void prepare(double sampleRate, int numChannels, int maxBlock)
    {
        sr = sampleRate; nch = numChannels < 2 ? 1 : 2;
        nv.prepare(nch);
        cap = maxBlock < 4096 ? 4096 : maxBlock;
        for (int c = 0; c < 2; ++c)
        {
            in[c].assign((size_t)cap, 0.0f); speech[c].assign((size_t)cap, 0.0f); dry[c].assign((size_t)cap, 0.0f);
            ch[c].lo.assign((size_t)kLen, 0.0f); ch[c].hi.assign((size_t)kLen, 0.0f); ch[c].pos = 0; ch[c].z1 = ch[c].z2 = 0;
        }
        const double pi = 3.14159265358979323846;
        const double w0 = 2.0 * pi * 350.0 / sr, cs = std::cos(w0), al = std::sin(w0) / (2.0 * 0.7071), a0 = 1.0 + al;
        b0 = (1.0 - cs) * 0.5 / a0; b1 = (1.0 - cs) / a0; b2 = b0; a1 = -2.0 * cs / a0; a2 = (1.0 - al) / a0;
        kMix = 1.0 - std::exp(-1.0 / (0.005 * sr)); kGain = 1.0 - std::exp(-1.0 / (0.010 * sr));
        mixOn = 0; gHi = 1; gLo = 1; rdPos = 0;
    }

    int capacity() const { return cap; }

    // back to the state right after prepare(): called when playback starts, so every render begins the same way
    void reset()
    {
        nv.reset();
        for (int c = 0; c < 2; ++c)
        {
            std::fill(ch[c].lo.begin(), ch[c].lo.end(), 0.0f); std::fill(ch[c].hi.begin(), ch[c].hi.end(), 0.0f);
            ch[c].pos = 0; ch[c].z1 = ch[c].z2 = 0;
        }
        mixOn = 0; gHi = 1; gLo = 1;
    }

    // Levels for the rest, from the classic engine's own noise gain (which already knows solo, mute and delta).
    static void levels(double noiseGain, int delta, bool powerOn, double& hi, double& lo)
    {
        const double g = noiseGain < 0.0 ? 0.0 : noiseGain;
        if (!powerOn) { hi = 1.0; lo = 1.0; return; }                                   // voice + rest = the input again
        if (delta == 2) { hi = g; lo = 1.0 - lows(g > 1.0 ? 0.0 : 1.0 - g); return; }   // delta: what the Noise fader removes
        hi = g; lo = lows(g);
    }

    // n <= capacity(). x: [channels][n] input; coreIn: [channels][n] receives the voice.
    void pre(const float* const* x, double* const* coreIn, int n, bool want)
    {
        float* sp[2] = { speech[0].data(), speech[1].data() };
        float* dp[2] = { dry[0].data(), dry[1].data() };
        nv.process(x, sp, dp, n, want || mixOn > 1.0e-3);        // the network keeps running until its part has faded out
        for (int i = 0; i < n; ++i)
        {
            mixOn += ((want ? 1.0 : 0.0) - mixOn) * kMix;
            for (int c = 0; c < nch; ++c)
            {
                Channel& k = ch[c];
                const double d = (double)dry[c][(size_t)i];
                const double rest = (d - (double)speech[c][(size_t)i]) * mixOn;
                coreIn[c][i] = d - rest;
                const double lo = b0 * rest + k.z1;
                k.z1 = b1 * rest - a1 * lo + k.z2;
                k.z2 = b2 * rest - a2 * lo;
                k.lo[(size_t)k.pos] = (float)lo; k.hi[(size_t)k.pos] = (float)(rest - lo);
                k.pos = (k.pos + 1) & (kLen - 1);
            }
        }
    }

    // io: [channels][n], the classic engine's output for the same n samples; coreLatency in samples
    void post(double* const* io, int n, double levelHi, double levelLo, int coreLatency)
    {
        const int lat = coreLatency < 0 ? 0 : (coreLatency > kLen - cap - 1 ? kLen - cap - 1 : coreLatency);
        for (int i = 0; i < n; ++i)
        {
            gHi += (levelHi - gHi) * kGain; gLo += (levelLo - gLo) * kGain;
            for (int c = 0; c < nch; ++c)
            {
                const Channel& k = ch[c];
                const int rd = (k.pos - n + i - lat) & (kLen - 1);
                io[c][i] += gHi * (double)k.hi[(size_t)rd] + gLo * (double)k.lo[(size_t)rd];
            }
        }
    }

private:
    static double lows(double g) { return g < 1.0 ? std::pow(g, 0.4) : g; }
    static constexpr int kLen = 1 << 16;
    struct Channel { double z1 = 0, z2 = 0; std::vector<float> lo, hi; int pos = 0; };

    NeuralVoice& nv;
    double sr = 48000.0; int nch = 1, cap = 4096, rdPos = 0;
    std::vector<float> in[2], speech[2], dry[2];
    Channel ch[2];
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, kMix = 0, kGain = 0, mixOn = 0, gHi = 1, gLo = 1;
};

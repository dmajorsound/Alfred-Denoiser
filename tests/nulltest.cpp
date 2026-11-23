#include <string>
// Runs the translated core on raw stereo float32 audio, the same way the reference host (ysfx) runs the JSFX:
// @init at the given rate, then the slider values, then per block @block and @sample per frame.
// usage: nulltest in.f32 out.f32 samplerate blocksize [slider values...]
// env:   LEARN=a,b   sets test_lt at those sample positions (test hook in the JSFX)
//        STATE_OUT=f / STATE_IN=f   write / read the @serialize stream after / before processing
#include CORE_HEADER
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage\n"); return 1; }
    CORE_CLASS fx;
    fx.reserveMemory(64 * 65536);
    const double sr = std::atof(argv[3]);
    const int bs = std::atoi(argv[4]);
    *fx.var("srate") = sr;
    if (double* v = fx.var("samplesblock")) *v = bs;
    if (double* v = fx.var("num_ch")) *v = 2;
    *fx.var("play_state") = 1;
    fx.sec_init();
    int nsl = 0; const JsfxSliderInfo* si = CORE_CLASS::sliderInfo(nsl);
    for (int i = 5; i < argc && i - 5 < nsl; ++i)
    {
        char nm[32]; std::snprintf(nm, sizeof nm, "slider%d", si[i - 5].index);
        if (double* v = fx.var(nm)) *v = std::atof(argv[i]);
    }
    if (const char* fi = std::getenv("STATE_IN"))
    {
        FILE* f = std::fopen(fi, "rb");
        if (f)
        {
            std::vector<float> d; float x;
            while (std::fread(&x, 4, 1, f) == 1) d.push_back(x);
            std::fclose(f);
            fx.serializeRead(d.data(), d.size());
            std::fprintf(stderr, "state loaded: %zu values\n", d.size());
        }
    }
    FILE* f = std::fopen(argv[1], "rb");
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f) / 8; std::fseek(f, 0, SEEK_SET);
    std::vector<float> in((size_t)n * 2), out((size_t)n * 2);
    if (std::fread(in.data(), 4, (size_t)n * 2, f) != (size_t)n * 2) return 2;
    std::fclose(f);
    long l0 = -1, l1 = -1;
    if (const char* le = std::getenv("LEARN")) std::sscanf(le, "%ld,%ld", &l0, &l1);
    double* spl0 = fx.var("spl0"); double* spl1 = fx.var("spl1");
    double* tlt = fx.var("test_lt");
    for (long p = 0; p < n; p += bs)
    {
        if (tlt && l0 >= 0 && p >= l0) { *tlt = 1; l0 = -1; }
        if (tlt && l1 >= 0 && p >= l1) { *tlt = 1; l1 = -1; }
        const int m = (int)((n - p < bs) ? (n - p) : bs);
        if (double* v = fx.var("samplesblock")) *v = m;
        fx.sec_block();
        for (int i = 0; i < m; ++i)
        {
            *spl0 = (double)in[(size_t)(p + i) * 2];
            *spl1 = (double)in[(size_t)(p + i) * 2 + 1];
            fx.sec_sample();
            out[(size_t)(p + i) * 2] = (float)*spl0;
            out[(size_t)(p + i) * 2 + 1] = (float)*spl1;
        }
    }
    f = std::fopen(argv[2], "wb"); std::fwrite(out.data(), 4, (size_t)n * 2, f); std::fclose(f);
    std::fprintf(stderr, "pdc=%g\n", *fx.var("pdc_delay"));
    // DUMP=name,name: prints a few entries of these arrays (debugging)
    if (const char* du = std::getenv("DUMP"))
    {
        std::string names(du); size_t st = 0;
        while (st < names.size())
        {
            size_t en = names.find(',', st); if (en == std::string::npos) en = names.size();
            std::string nm = names.substr(st, en - st); st = en + 1;
            if (double* v = fx.var(nm.c_str()))
            {
                std::fprintf(stderr, "%s=%g:", nm.c_str(), *v);
                for (int k = 20; k <= 400; k += 38) std::fprintf(stderr, " %.4g", fx.M(*v + k));
                std::fprintf(stderr, "\n");
            }
        }
    }
    if (const char* fo = std::getenv("STATE_OUT"))
    {
        std::vector<float> d; fx.serializeWrite(d);
        FILE* g = std::fopen(fo, "wb"); std::fwrite(d.data(), 4, d.size(), g); std::fclose(g);
        std::fprintf(stderr, "state saved: %zu values\n", d.size());
    }
    return 0;
}

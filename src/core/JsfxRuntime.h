// Runtime for JSFX code translated to C++ by tools/eel2cpp.py.
// Reproduces what REAPER's EEL2 provides: memory (blocks of 65536 doubles), the FFT, strings,
// the @serialize stream, slider automation and the gfx drawing calls (through GfxBackend).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

struct JsfxSliderInfo
{
    int index;          // slider number (1-based, as in the JSFX)
    double def, lo, hi, step;
    const char* label;
    const char* enumNames; // "A|B|C" or nullptr
    bool hidden;
};

// drawing target of the @gfx code
class GfxBackend
{
public:
    virtual ~GfxBackend() = default;
    // colors and positions come from the script variables (gfx_r, gfx_g, gfx_b, gfx_a, gfx_x, gfx_y)
    virtual void rect(double x, double y, double w, double h, bool fill) = 0;
    virtual void line(double x1, double y1, double x2, double y2, bool aa) = 0;
    virtual void circle(double x, double y, double r, bool fill, bool aa) = 0;
    virtual void arc(double x, double y, double r, double a1, double a2, bool aa) = 0;
    virtual void roundrect(double x, double y, double w, double h, double r, bool aa) = 0;
    virtual void triangle(const double* xy, int npts) = 0;
    virtual void drawstr(const std::string& s, double& w, double& h) = 0; // draws at gfx_x/gfx_y, returns size
    virtual void setfont(int idx, const std::string& face, double size, int flags) = 0;
    virtual void measurestr(const std::string& s, double& w, double& h) = 0;
    virtual int getchar() = 0;
};

// slider writes from the script (gfx side): the plug-in forwards them to the host
class SliderListener
{
public:
    virtual ~SliderListener() = default;
    virtual void sliderAutomate(uint64_t mask, bool endTouch) = 0;
};

class JsfxRuntime
{
public:
    static constexpr int kBlockItems = 65536;
    static constexpr int kBlocks = 128;               // 8M items, as REAPER's default memory size

    JsfxRuntime();
    virtual ~JsfxRuntime();

    virtual void sec_init() = 0;
    virtual void sec_serialize() = 0;
    virtual void sec_block() = 0;
    virtual void sec_sample() = 0;
    virtual void sec_gfx() = 0;
    virtual double* var(const char* name) = 0;

    // memory
    double& M(double addr)
    {
        const double a = addr + 0.00001;
        if (!(a >= 0.0 && a < (double)(kBlocks * kBlockItems))) { dummy = 0.0; return dummy; }
        const unsigned idx = (unsigned)a;
        double* b = blocks[idx >> 16].get();
        if (!b) b = allocBlock((int)(idx >> 16));
        return b[idx & 65535];
    }
    void reserveMemory(int items);   // allocate ahead (never allocate on the audio thread)

    // serialize stream: float32 values like REAPER
    void serializeWrite(std::vector<float>& out);
    void serializeRead(const float* data, size_t n);

    // hooks
    GfxBackend* gfx = nullptr;
    SliderListener* sliderListener = nullptr;

    // strings
    std::string& str(double id);
    void str_define(int id, const char* s);

protected:
    void bindVars();

    // ---- EEL2 semantics
    static inline bool T(double x) { return std::fabs(x) >= 0.00001; }
    static inline double EQ(double a, double b) { return std::fabs(a - b) < 0.00001 ? 1.0 : 0.0; }
    static inline double NE(double a, double b) { return std::fabs(a - b) >= 0.00001 ? 1.0 : 0.0; }
    static inline double DF(double a)
    {
        // stores drop denormals (and inf/nan), as REAPER's x86-64 code does
        uint64_t w; std::memcpy(&w, &a, 8);
        const uint32_t hw = (uint32_t)(w >> 32);
        return (((hw + 0x00100000u) & 0x7ff00000u) > 0x00200000u) ? a : 0.0;
    }
    static inline double MOD(double a, double b) { const int bi = (int)b; return bi ? (double)((int)a % bi) : 0.0; }
    static inline double SHL(double a, double b) { return (double)((int)a << (int)b); }
    static inline double SHR(double a, double b) { return (double)((int)a >> (int)b); }
    static inline double BAND(double a, double b) { return (double)((int64_t)a & (int64_t)b); }
    static inline double BOR(double a, double b) { return (double)((int64_t)a | (int64_t)b); }
    static inline double BXOR(double a, double b) { return (double)((int64_t)a ^ (int64_t)b); }
    static inline double SIGN(double a) { return a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0; }
    static inline int loopcount(double v) { int n = (int)v; return n < 1 ? 0 : (n > 1048576 ? 1048576 : n); }

    // ---- built-in functions
    double rt_memset(double dst, double val, double n);
    double rt_memcpy(double dst, double src, double n);
    double rt_fft(int dir, double start, double len);
    double rt_slider_automate(double mask, double end);
    double rt_file_avail(double h);
    double rt_file_var(double h, double* v);
    double rt_file_mem(double h, double start, double n);
    double rt_time_precise();

    double str_copy(double dst, double src);
    double str_cat(double dst, double src);
    double str_len(double s);
    double str_getchar(double s, double i);
    double str_copy_substr(double dst, double src, double offs, double len);
    double str_printf(double dst, double fmt, std::initializer_list<double> args);
    double str_match(double fmt, double s, std::initializer_list<double*> outs);

    double g_rect(double x, double y, double w, double h, double fill);
    double g_line(double x1, double y1, double x2, double y2, double aa);
    double g_circle(double x, double y, double r, double fill, double aa);
    double g_arc(double x, double y, double r, double a1, double a2, double aa);
    double g_roundrect(double x, double y, double w, double h, double r, double aa);
    double g_triangle(double x1, double y1, double x2, double y2, double x3, double y3);
    double g_drawstr(double s);
    double g_setfont(double idx, double face, double size, double flags);
    double g_measurestr(double s, double* w, double* h);
    double g_getchar();

    double* gx = nullptr;   // gfx_x
    double* gy = nullptr;   // gfx_y

private:
    double* allocBlock(int b);
    std::unique_ptr<double[]> blocks[kBlocks];
    double dummy = 0.0;

    std::vector<std::string> strings;    // indexed by id - 80000 (named) / 90000 (literal)
    std::string scratch;

    // serialize
    bool serWriting = false;
    std::vector<float>* serOut = nullptr;
    const float* serIn = nullptr;
    size_t serN = 0, serPos = 0;
};

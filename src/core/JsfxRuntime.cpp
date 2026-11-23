#include "JsfxRuntime.h"
#include "fft.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace
{
    struct FftInit { FftInit() { WDL_fft_init(); } };
    FftInit fftInit;

    constexpr int kNamedBase = 80000;
    constexpr int kLiteralBase = 90000;
    constexpr int kMaxStrings = 20000;
}

JsfxRuntime::JsfxRuntime()
{
    strings.resize(kMaxStrings);
}

JsfxRuntime::~JsfxRuntime() = default;

double* JsfxRuntime::allocBlock(int b)
{
    blocks[b].reset(new double[kBlockItems]());
    return blocks[b].get();
}

void JsfxRuntime::reserveMemory(int items)
{
    const int nb = (items + kBlockItems - 1) / kBlockItems;
    for (int b = 0; b < nb && b < kBlocks; ++b)
        if (!blocks[b]) allocBlock(b);
}

void JsfxRuntime::bindVars()
{
    gx = var("gfx_x");
    gy = var("gfx_y");
}

// ------------------------------------------------------------------------------------------- memory
double JsfxRuntime::rt_memset(double dst, double val, double n)
{
    int cnt = (int)n;
    double a = dst;
    for (int i = 0; i < cnt; ++i) M(a + i) = val;
    return dst;
}

double JsfxRuntime::rt_memcpy(double dst, double src, double n)
{
    const int cnt = (int)n;
    if (cnt <= 0) return dst;
    const int d = (int)(dst + 0.00001), s = (int)(src + 0.00001);
    if (d == s) return dst;
    if (d < s) for (int i = 0; i < cnt; ++i) M(d + i) = M(s + i);
    else for (int i = cnt - 1; i >= 0; --i) M(d + i) = M(s + i);
    return dst;
}

// ------------------------------------------------------------------------------------------- fft (as EEL2's fft_func)
double JsfxRuntime::rt_fft(int dir, double start, double length)
{
    const int offs = (int)(start + 0.0001);
    const int itemSizeShift = (dir & 2) ? 0 : 1;
    int l = (int)(length + 0.0001);
    int bitl = 0;
    while (l > 1 && bitl < 15) { bitl++; l >>= 1; }
    if (bitl < ((dir & 4) ? 3 : 4)) return start;
    const int ilen = 1 << bitl;
    if (offs / kBlockItems != (offs + (ilen << itemSizeShift) - 1) / kBlockItems) return start;
    if (offs < 0 || offs >= kBlocks * kBlockItems) return start;
    double* ptr = &M(offs);

    if (dir == 4 || dir == 5)
    {
        std::vector<double> tmp(ptr, ptr + ilen * 2);
        for (int x = 0; x < ilen * 2; x += 2)
        {
            const int y = WDL_fft_permute(ilen, x / 2) * 2;
            if (dir == 4) { ptr[x] = tmp[y]; ptr[x + 1] = tmp[y + 1]; }
            else { ptr[y] = tmp[x]; ptr[y + 1] = tmp[x + 1]; }
        }
    }
    else if (dir == 0 || dir == 1)
    {
        WDL_fft((WDL_FFT_COMPLEX*)ptr, ilen, dir & 1);
    }
    return start;
}

// ------------------------------------------------------------------------------------------- sliders, time
double JsfxRuntime::rt_slider_automate(double mask, double end)
{
    if (sliderListener) sliderListener->sliderAutomate((uint64_t)(int64_t)mask, T(end));
    return mask;
}

double JsfxRuntime::rt_time_precise()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------------------------------- serialize
void JsfxRuntime::serializeWrite(std::vector<float>& out)
{
    out.clear();
    serWriting = true; serOut = &out;
    sec_serialize();
    serWriting = false; serOut = nullptr;
}

void JsfxRuntime::serializeRead(const float* data, size_t n)
{
    serWriting = false; serIn = data; serN = n; serPos = 0;
    sec_serialize();
    serIn = nullptr; serN = 0; serPos = 0;
}

double JsfxRuntime::rt_file_avail(double)
{
    if (serWriting) return -1.0;
    return (double)(serN > serPos ? 0 : 0);   // like the reference host: 0 while reading
}

double JsfxRuntime::rt_file_var(double, double* v)
{
    if (serWriting) { if (serOut) serOut->push_back((float)*v); return 1.0; }
    if (serIn && serPos < serN) { *v = (double)serIn[serPos++]; return 1.0; }
    *v = 0.0;
    return 0.0;
}

double JsfxRuntime::rt_file_mem(double, double start, double n)
{
    const int cnt = (int)n;
    int done = 0;
    for (int i = 0; i < cnt; ++i)
    {
        double& m = M(start + i);
        if (serWriting) { if (serOut) serOut->push_back((float)m); }
        else
        {
            if (!(serIn && serPos < serN)) break;
            m = (double)serIn[serPos++];
        }
        ++done;
    }
    return (double)done;
}

// ------------------------------------------------------------------------------------------- strings
std::string& JsfxRuntime::str(double id)
{
    const int i = (int)(id + 0.5);
    if (i >= kNamedBase && i < kNamedBase + kMaxStrings / 2) return strings[i - kNamedBase];
    if (i >= kLiteralBase && i < kLiteralBase + kMaxStrings / 2) return strings[kMaxStrings / 2 + i - kLiteralBase];
    scratch.clear();
    return scratch;
}

void JsfxRuntime::str_define(int id, const char* s) { str((double)id) = s; }

double JsfxRuntime::str_copy(double dst, double src)
{
    if ((int)(dst + 0.5) != (int)(src + 0.5)) str(dst) = std::string(str(src));
    return dst;
}

double JsfxRuntime::str_cat(double dst, double src)
{
    const std::string add = str(src);
    str(dst) += add;
    return dst;
}

double JsfxRuntime::str_len(double s) { return (double)str(s).size(); }

double JsfxRuntime::str_getchar(double s, double i)
{
    const std::string& t = str(s);
    int k = (int)i;
    if (k < 0) k += (int)t.size();
    if (k < 0 || k >= (int)t.size()) return 0.0;
    return (double)(unsigned char)t[k];
}

double JsfxRuntime::str_copy_substr(double dst, double src, double offs, double len)
{
    const std::string t = str(src);
    int o = (int)offs, n = (int)len;
    if (o < 0) o += (int)t.size();
    if (o < 0) o = 0;
    if (o > (int)t.size()) o = (int)t.size();
    if (n < 0) n += (int)t.size() - o;
    if (n < 0) n = 0;
    str(dst) = t.substr(o, n);
    return dst;
}

double JsfxRuntime::str_printf(double dst, double fmtId, std::initializer_list<double> args)
{
    const std::string fmt = str(fmtId);
    std::string out;
    auto it = args.begin();
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') { out += '%'; ++i; continue; }
        size_t j = i + 1;
        while (j < fmt.size() && std::strchr("-+ #0123456789.", fmt[j])) ++j;
        if (j >= fmt.size()) break;
        const char conv = fmt[j];
        std::string spec = fmt.substr(i, j - i + 1);
        const double v = it != args.end() ? *it++ : 0.0;
        char buf[512];
        if (conv == 's') { std::snprintf(buf, sizeof buf, spec.c_str(), str(v).c_str()); }
        else if (conv == 'd' || conv == 'i') { spec.back() = 'd'; std::snprintf(buf, sizeof buf, spec.c_str(), (int)v); }
        else if (conv == 'x' || conv == 'X' || conv == 'c') { std::snprintf(buf, sizeof buf, spec.c_str(), (int)v); }
        else { std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        out += buf;
        i = j;
    }
    str(dst) = out;
    return dst;
}

double JsfxRuntime::str_match(double fmtId, double sId, std::initializer_list<double*> outs)
{
    // only the form the plug-in uses: "%f" against a whole string with a number in it
    const std::string fmt = str(fmtId);
    const std::string s = str(sId);
    if (fmt != "%f" || outs.size() < 1) return 0.0;
    const char* c = s.c_str();
    char* end = nullptr;
    const double v = std::strtod(c, &end);
    if (end == c) return 0.0;
    while (*end == ' ') ++end;
    if (*end) return 0.0;
    **outs.begin() = v;
    return 1.0;
}

// ------------------------------------------------------------------------------------------- gfx
double JsfxRuntime::g_rect(double x, double y, double w, double h, double fill)
{
    if (gfx) gfx->rect(x, y, w, h, T(fill));
    return 0.0;
}
double JsfxRuntime::g_line(double x1, double y1, double x2, double y2, double aa)
{
    if (gfx) gfx->line(x1, y1, x2, y2, T(aa));
    return 0.0;
}
double JsfxRuntime::g_circle(double x, double y, double r, double fill, double aa)
{
    if (gfx) gfx->circle(x, y, r, T(fill), T(aa));
    return 0.0;
}
double JsfxRuntime::g_arc(double x, double y, double r, double a1, double a2, double aa)
{
    if (gfx) gfx->arc(x, y, r, a1, a2, T(aa));
    return 0.0;
}
double JsfxRuntime::g_roundrect(double x, double y, double w, double h, double r, double aa)
{
    if (gfx) gfx->roundrect(x, y, w, h, r, T(aa));
    return 0.0;
}
double JsfxRuntime::g_triangle(double x1, double y1, double x2, double y2, double x3, double y3)
{
    const double xy[6] = { x1, y1, x2, y2, x3, y3 };
    if (gfx) gfx->triangle(xy, 3);
    return 0.0;
}
double JsfxRuntime::g_drawstr(double s)
{
    if (gfx && gx)
    {
        double w = 0, h = 0;
        gfx->drawstr(str(s), w, h);
        *gx += w;
    }
    return 0.0;
}
double JsfxRuntime::g_setfont(double idx, double face, double size, double flags)
{
    if (gfx) gfx->setfont((int)idx, face < 0 ? std::string() : str(face), size, (int)flags);
    return 1.0;
}
double JsfxRuntime::g_measurestr(double s, double* w, double* h)
{
    double ww = 0, hh = 0;
    if (gfx) gfx->measurestr(str(s), ww, hh);
    *w = ww; *h = hh;
    return 0.0;
}
double JsfxRuntime::g_getchar()
{
    return gfx ? (double)gfx->getchar() : 0.0;
}

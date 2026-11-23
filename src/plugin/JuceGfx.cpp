#include "JuceGfx.h"
#include "BinaryData.h"

// The script asks for "Arial". Instead of the system's font, the plug-in uses its own built-in
// Liberation Sans (same letter widths as Arial), so text never runs wider or sits differently on
// Windows than on macOS, and a missing or substituted system font cannot break the layout.
static juce::Typeface::Ptr builtInTypeface(bool bold, bool italic)
{
    static const juce::Typeface::Ptr faces[4] = {
        juce::Typeface::createSystemTypefaceFor(BinaryData::LiberationSansRegular_ttf, (size_t)BinaryData::LiberationSansRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::LiberationSansBold_ttf, (size_t)BinaryData::LiberationSansBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::LiberationSansItalic_ttf, (size_t)BinaryData::LiberationSansItalic_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::LiberationSansBoldItalic_ttf, (size_t)BinaryData::LiberationSansBoldItalic_ttfSize) };
    return faces[(bold ? 1 : 0) + (italic ? 2 : 0)];
}

static juce::Font builtInFont(float height, bool bold, bool italic, bool underline)
{
    return juce::Font(juce::FontOptions().withTypeface(builtInTypeface(bold, italic))
                                         .withHeight(juce::jmax(1.0f, height))
                                         .withUnderline(underline));
}

// ------------------------------------------------------------------------------------------- JuceGfx
JuceGfx::JuceGfx(JsfxRuntime& runtime) : rt(runtime)
{
    r = rt.var("gfx_r"); g = rt.var("gfx_g"); b = rt.var("gfx_b"); a = rt.var("gfx_a");
    x = rt.var("gfx_x"); y = rt.var("gfx_y");
}

void JuceGfx::begin(juce::Image& img)
{
    gr = std::make_unique<juce::Graphics>(img);
    bits = std::make_unique<juce::Image::BitmapData>(img, juce::Image::BitmapData::readWrite);
    // the direct path needs 32-bit pixels in the order blue, green, red, alpha (JUCE's ARGB on little-endian machines)
   #if JUCE_LITTLE_ENDIAN
    direct = img.getFormat() == juce::Image::ARGB && bits->pixelStride == 4;
   #else
    direct = false;
   #endif
}

void JuceGfx::end() { bits.reset(); gr.reset(); }

// ---- direct drawing. The image is opaque (alpha 255 everywhere), so blending only touches the three colours.
inline void JuceGfx::blendSpan(int px, int py, int n, juce::uint32 c8, int a256)
{
    auto* p = (juce::uint32*)(void*)bits->getPixelPointer(px, py);
    if (a256 >= 256) { for (int i = 0; i < n; ++i) p[i] = c8; return; }
    const juce::uint32 srb = (c8 & 0x00ff00ffu) * (juce::uint32)a256, sg = (c8 & 0x0000ff00u) * (juce::uint32)a256;
    const juce::uint32 ia = (juce::uint32)(256 - a256);
    for (int i = 0; i < n; ++i)
    {
        const juce::uint32 d = p[i];
        const juce::uint32 rb = (((d & 0x00ff00ffu) * ia + srb) >> 8) & 0x00ff00ffu;
        const juce::uint32 gg = (((d & 0x0000ff00u) * ia + sg) >> 8) & 0x0000ff00u;
        p[i] = 0xff000000u | rb | gg;
    }
}

inline void JuceGfx::blendPixel(int px, int py, juce::uint32 c8, int a256)
{
    if (px < 0 || py < 0 || px >= bits->width || py >= bits->height || a256 <= 0) return;
    blendSpan(px, py, 1, c8, a256);
}

void JuceGfx::fillRectDirect(int x0, int y0, int w, int h)
{
    int x1 = x0 + w, y1 = y0 + h;
    x0 = juce::jmax(0, x0); y0 = juce::jmax(0, y0); x1 = juce::jmin(bits->width, x1); y1 = juce::jmin(bits->height, y1);
    if (x1 <= x0 || y1 <= y0) return;
    const juce::Colour c = colour();
    const juce::uint32 c8 = 0xff000000u | ((juce::uint32)c.getRed() << 16) | ((juce::uint32)c.getGreen() << 8) | (juce::uint32)c.getBlue();
    const int a256 = (int)std::lround(c.getFloatAlpha() * 256.0f);
    if (a256 <= 0) return;
    for (int yy = y0; yy < y1; ++yy) blendSpan(x0, yy, x1 - x0, c8, a256);
}

// anti-aliased line, one pixel wide (Xiaolin Wu), coordinates are pixel centres
void JuceGfx::lineDirect(double x1, double y1, double x2, double y2)
{
    const juce::Colour c = colour();
    const juce::uint32 c8 = 0xff000000u | ((juce::uint32)c.getRed() << 16) | ((juce::uint32)c.getGreen() << 8) | (juce::uint32)c.getBlue();
    const double al = (double)c.getFloatAlpha() * 256.0;
    if (al <= 0) return;
    const double lim = 1.0e6;
    if (!(std::abs(x1) < lim && std::abs(y1) < lim && std::abs(x2) < lim && std::abs(y2) < lim)) return;
    const bool steep = std::abs(y2 - y1) > std::abs(x2 - x1);
    if (steep) { std::swap(x1, y1); std::swap(x2, y2); }
    if (x1 > x2) { std::swap(x1, x2); std::swap(y1, y2); }
    const double dx = x2 - x1, dy = y2 - y1;
    const double grad = dx < 1.0e-9 ? 0.0 : dy / dx;
    const int maxMajor = steep ? bits->height : bits->width;
    int xa = (int)std::floor(x1 + 0.5), xb = (int)std::floor(x2 + 0.5);
    double yv = y1 + grad * ((double)xa - x1);
    if (xa < 0) { yv += grad * (double)(-xa); xa = 0; }
    if (xb >= maxMajor) xb = maxMajor - 1;
    for (int xx = xa; xx <= xb; ++xx, yv += grad)
    {
        const double fl = std::floor(yv); const int yi = (int)fl; const double fr = yv - fl;
        const int a0 = (int)(al * (1.0 - fr) + 0.5), a1 = (int)(al * fr + 0.5);
        if (steep) { blendPixel(yi, xx, c8, a0); blendPixel(yi + 1, xx, c8, a1); }
        else       { blendPixel(xx, yi, c8, a0); blendPixel(xx, yi + 1, c8, a1); }
    }
}

juce::Colour JuceGfx::colour() const
{
    auto c = [](double* v) { return (juce::uint8)juce::jlimit(0, 255, (int)std::lround((v ? *v : 1.0) * 255.0)); };
    const float al = (float)juce::jlimit(0.0, 1.0, a ? *a : 1.0);
    return juce::Colour(c(r), c(g), c(b), al);
}

void JuceGfx::rect(double rx, double ry, double w, double h, bool fill)
{
    if (!gr || w <= 0 || h <= 0) return;
    const juce::Rectangle<int> rc((int)std::floor(rx), (int)std::floor(ry), (int)w, (int)h);
    if (fill && direct) { fillRectDirect(rc.getX(), rc.getY(), rc.getWidth(), rc.getHeight()); return; }
    gr->setColour(colour());
    if (fill) gr->fillRect(rc); else gr->drawRect(rc, 1);
}

void JuceGfx::line(double x1, double y1, double x2, double y2, bool)
{
    if (!gr) return;
    if (direct) { lineDirect(x1, y1, x2, y2); return; }
    gr->setColour(colour());
    gr->drawLine((float)x1 + 0.5f, (float)y1 + 0.5f, (float)x2 + 0.5f, (float)y2 + 0.5f, 1.0f);
}

void JuceGfx::circle(double cx, double cy, double rad, bool fill, bool)
{
    if (!gr || rad <= 0) return;
    gr->setColour(colour());
    const float fx = (float)cx + 0.5f, fy = (float)cy + 0.5f, fr = (float)rad;
    if (fill) gr->fillEllipse(fx - fr, fy - fr, fr * 2, fr * 2);
    else gr->drawEllipse(fx - fr, fy - fr, fr * 2, fr * 2, 1.0f);
}

void JuceGfx::arc(double cx, double cy, double rad, double a1, double a2, bool)
{
    if (!gr || rad <= 0) return;
    gr->setColour(colour());
    juce::Path p;
    p.addCentredArc((float)cx + 0.5f, (float)cy + 0.5f, (float)rad, (float)rad, 0.0f, (float)a1, (float)a2, true);
    gr->strokePath(p, juce::PathStrokeType(1.0f));
}

void JuceGfx::roundrect(double rx, double ry, double w, double h, double rad, bool)
{
    if (!gr || w <= 0 || h <= 0) return;
    gr->setColour(colour());
    gr->drawRoundedRectangle((float)rx + 0.5f, (float)ry + 0.5f, (float)w, (float)h, (float)rad, 1.0f);
}

void JuceGfx::triangle(const double* xy, int npts)
{
    if (!gr || npts < 3) return;
    gr->setColour(colour());
    juce::Path p;
    p.startNewSubPath((float)xy[0], (float)xy[1]);
    for (int i = 1; i < npts; ++i) p.lineTo((float)xy[i * 2], (float)xy[i * 2 + 1]);
    p.closeSubPath();
    gr->fillPath(p);
}

float JuceGfx::ascent(const juce::Font& f)
{
    // some font set-ups report no ascent (NaN); then Arial's share of the line height
    const float a = f.getAscent();
    return std::isfinite(a) && a > 0.0f ? a : f.getHeight() * 0.7877f;
}

const juce::Font& JuceGfx::font()
{
    if (cur > 0 && cur < 32 && fonts[cur].valid) return fonts[cur].font;
    if (!defaultFont) defaultFont = std::make_unique<juce::Font>(builtInFont(13.0f, false, false, false));
    return *defaultFont;
}

void JuceGfx::setfont(int idx, const std::string& face, double size, int flags)
{
    cur = juce::jlimit(0, 31, idx);
    if (cur == 0 || face.empty()) return;          // gfx_setfont(n): select a font set before
    auto& f = fonts[cur];
    const juce::String fc(face);
    if (f.valid && f.face == fc && juce::exactlyEqual(f.size, size) && f.flags == flags) return;
    bool bold = false, italic = false, underline = false;
    for (int k = 0; k < 4; ++k)
    {
        const int ch = (flags >> (k * 8)) & 255;
        if (ch == 'b') bold = true;
        if (ch == 'i') italic = true;
        if (ch == 'u') underline = true;
    }
    f.face = fc; f.size = size; f.flags = flags;
    f.generation = ++fontGeneration;
    // the JSFX font size is the line height (ascent + descent), as gfx_texth reports it in REAPER
    f.font = builtInFont((float)size, bold, italic, underline);
    f.valid = true;
}

// Text is cached per font and string: its width, its glyph outlines, and (on first draw) a coverage mask that
// is then blended straight into the image. Measuring and drawing a string that was seen before costs almost nothing.
JuceGfx::TextEntry& JuceGfx::textEntry(const std::string& s)
{
    std::string key;
    key.reserve(s.size() + 8);
    key.push_back((char)(cur + 1));
    const juce::uint32 gen = (cur > 0 && cur < 32) ? fonts[cur].generation : 0u;
    key.append((const char*)&gen, sizeof(gen));
    key.append(s);
    auto it = textCache.find(key);
    if (it == textCache.end())
    {
        if (textCache.size() > 4000) textCache.clear();
        const auto& f = font();
        TextEntry e;
        const juce::String text = juce::String::fromUTF8(s.c_str());
        e.w = (double)juce::GlyphArrangement::getStringWidth(f, text);
        e.h = (double)f.getHeight();
        e.asc = ascent(f);
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, text, 0.0f, 0.0f);
        ga.createPath(e.path);
        it = textCache.emplace(std::move(key), std::move(e)).first;
    }
    return it->second;
}

void JuceGfx::measurestr(const std::string& s, double& w, double& h)
{
    const auto& e = textEntry(s);
    w = e.w; h = e.h;
}

void JuceGfx::drawstr(const std::string& s, double& w, double& h)
{
    auto& e = textEntry(s);
    w = e.w; h = e.h;
    if (!gr || !x || !y || s.empty()) return;
    const int ox = (int)std::floor(*x), oy = (int)std::floor(*y + (double)e.asc);
    if (!direct)
    {
        gr->setColour(colour());
        gr->fillPath(e.path, juce::AffineTransform::translation((float)ox, (float)oy));
        return;
    }
    if (!e.masked)
    {
        e.masked = true;
        const auto bounds = e.path.getBounds();
        if (!bounds.isEmpty())
        {
            e.mx = (int)std::floor(bounds.getX()) - 1; e.my = (int)std::floor(bounds.getY()) - 1;
            e.mw = (int)std::ceil(bounds.getRight()) + 1 - e.mx; e.mh = (int)std::ceil(bounds.getBottom()) + 1 - e.my;
            juce::Image m(juce::Image::SingleChannel, e.mw, e.mh, true, juce::SoftwareImageType());
            {
                juce::Graphics mg(m);
                mg.setColour(juce::Colours::white);
                mg.fillPath(e.path, juce::AffineTransform::translation((float)-e.mx, (float)-e.my));
            }
            juce::Image::BitmapData md(m, juce::Image::BitmapData::readOnly);
            e.cov.resize((size_t)e.mw * (size_t)e.mh);
            for (int j = 0; j < e.mh; ++j)
                for (int i = 0; i < e.mw; ++i) e.cov[(size_t)j * (size_t)e.mw + (size_t)i] = *md.getPixelPointer(i, j);
        }
    }
    if (e.cov.empty()) return;
    const juce::Colour c = colour();
    const juce::uint32 c8 = 0xff000000u | ((juce::uint32)c.getRed() << 16) | ((juce::uint32)c.getGreen() << 8) | (juce::uint32)c.getBlue();
    const int al = (int)std::lround(c.getFloatAlpha() * 256.0f);
    if (al <= 0) return;
    const int x0 = ox + e.mx, y0 = oy + e.my;
    const int i0 = juce::jmax(0, -x0), i1 = juce::jmin(e.mw, bits->width - x0);
    const int j0 = juce::jmax(0, -y0), j1 = juce::jmin(e.mh, bits->height - y0);
    for (int j = j0; j < j1; ++j)
    {
        const juce::uint8* cv = e.cov.data() + (size_t)j * (size_t)e.mw;
        for (int i = i0; i < i1; ++i)
            if (const int cvv = cv[i]) blendSpan(x0 + i, y0 + j, 1, c8, (al * (cvv + (cvv >> 7))) >> 8);
    }
}

int JuceGfx::getchar()
{
    if (keys.empty()) return 0;
    const int k = keys.front(); keys.pop_front();
    return k;
}



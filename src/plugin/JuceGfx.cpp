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
}

void JuceGfx::end() { gr.reset(); }

juce::Colour JuceGfx::colour() const
{
    auto c = [](double* v) { return (juce::uint8)juce::jlimit(0, 255, (int)std::lround((v ? *v : 1.0) * 255.0)); };
    const float al = (float)juce::jlimit(0.0, 1.0, a ? *a : 1.0);
    return juce::Colour(c(r), c(g), c(b), al);
}

void JuceGfx::rect(double rx, double ry, double w, double h, bool fill)
{
    if (!gr || w <= 0 || h <= 0) return;
    gr->setColour(colour());
    const juce::Rectangle<int> rc((int)std::floor(rx), (int)std::floor(ry), (int)w, (int)h);
    if (fill) gr->fillRect(rc); else gr->drawRect(rc, 1);
}

void JuceGfx::line(double x1, double y1, double x2, double y2, bool)
{
    if (!gr) return;
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
    // the JSFX font size is the line height (ascent + descent), as gfx_texth reports it in REAPER
    f.font = builtInFont((float)size, bold, italic, underline);
    f.valid = true;
}

void JuceGfx::measurestr(const std::string& s, double& w, double& h)
{
    const auto& f = font();
    w = (double)juce::GlyphArrangement::getStringWidth(f, juce::String::fromUTF8(s.c_str()));
    h = (double)f.getHeight();
}

void JuceGfx::drawstr(const std::string& s, double& w, double& h)
{
    measurestr(s, w, h);
    if (!gr || !x || !y || s.empty()) return;
    // text is drawn as its glyph outlines (cached per font and string): the same look on every system,
    // and it avoids the software renderer's glyph cache
    const auto& f = font();
    const juce::String key = juce::String(cur) + "|" + f.getTypefaceName() + "|" + juce::String(f.getHeight(), 3) + "|"
                           + juce::String(f.getStyleFlags()) + "|" + juce::String::fromUTF8(s.c_str());
    auto it = textCache.find(key);
    if (it == textCache.end())
    {
        if (textCache.size() > 4000) textCache.clear();
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, juce::String::fromUTF8(s.c_str()), 0.0f, 0.0f);
        juce::Path p;
        ga.createPath(p);
        it = textCache.emplace(key, std::move(p)).first;
    }
    gr->setColour(colour());
    gr->fillPath(it->second, juce::AffineTransform::translation((float)std::floor(*x), (float)std::floor(*y + ascent(f))));
}

int JuceGfx::getchar()
{
    if (keys.empty()) return 0;
    const int k = keys.front(); keys.pop_front();
    return k;
}



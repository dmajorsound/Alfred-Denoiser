#pragma once
#include <juce_graphics/juce_graphics.h>
#include "JsfxRuntime.h"
#include <deque>
#include <unordered_map>
#include <memory>

// Draws the JSFX's @gfx calls with JUCE, into an image at the screen's pixel density.
class JuceGfx : public GfxBackend
{
public:
    JuceGfx(JsfxRuntime& rt);
    void begin(juce::Image& img);
    void end();

    void rect(double x, double y, double w, double h, bool fill) override;
    void line(double x1, double y1, double x2, double y2, bool aa) override;
    void circle(double x, double y, double r, bool fill, bool aa) override;
    void arc(double x, double y, double r, double a1, double a2, bool aa) override;
    void roundrect(double x, double y, double w, double h, double r, bool aa) override;
    void triangle(const double* xy, int npts) override;
    void drawstr(const std::string& s, double& w, double& h) override;
    void setfont(int idx, const std::string& face, double size, int flags) override;
    void measurestr(const std::string& s, double& w, double& h) override;
    int getchar() override;

    std::deque<int> keys;

private:
    juce::Colour colour() const;
    const juce::Font& font();
    static float ascent(const juce::Font& f);

    JsfxRuntime& rt;
    double *r = nullptr, *g = nullptr, *b = nullptr, *a = nullptr, *x = nullptr, *y = nullptr;
    std::unique_ptr<juce::Graphics> gr;
    struct FontSlot { juce::String face; double size = 0; int flags = 0; juce::Font font { juce::FontOptions() }; bool valid = false; };
    FontSlot fonts[32];
    int cur = 0;
    juce::Font defaultFont { juce::FontOptions(13.0f) };
    std::unordered_map<juce::String, juce::Path> textCache;
};


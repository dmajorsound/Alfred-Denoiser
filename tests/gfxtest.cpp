// Headless test of the interface: runs the translated @gfx code through JuceGfx into an image,
// driven by a small script on stdin (same commands as the JSFX test host):
//   m X Y    move the mouse          d / u    left button down / up      s / S   shift down / up
//   k CODE   key press (character code)       f N      run N frames
//   w FILE   write the image (PNG)            p LABEL  print the slider values
// usage: gfxtest WIDTH HEIGHT [slider=value ...]
#include "AlfredCore.h"
#include "JuceGfx.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

struct Listener : SliderListener
{
    void sliderAutomate(uint64_t, bool) override {}
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int w = argc > 1 ? std::atoi(argv[1]) : 664, h = argc > 2 ? std::atoi(argv[2]) : 652;
    AlfredCore core;
    Listener ls; core.sliderListener = &ls;
    core.reserveMemory(32 * 65536);
    *core.var("srate") = 48000; *core.var("play_state") = 1;
    core.sec_init();
    for (int i = 3; i < argc; ++i)
    {
        int idx = 0; double v = 0;
        if (std::sscanf(argv[i], "%d=%lf", &idx, &v) == 2) { char nm[32]; std::snprintf(nm, sizeof nm, "slider%d", idx); *core.var(nm) = v; }
    }
    // some audio so the spectrum has something to show
    double* s0 = core.var("spl0"); double* s1 = core.var("spl1");
    juce::Random rnd(1);
    for (int b = 0; b < 200; ++b)
    {
        core.sec_block();
        for (int i = 0; i < 512; ++i)
        {
            const double t = (b * 512 + i) / 48000.0;
            const double v = 0.05 * (rnd.nextDouble() * 2 - 1) + 0.2 * std::sin(2 * 3.14159265 * 220 * t) * (std::fmod(t, 0.6) < 0.35);
            *s0 = v; *s1 = v; core.sec_sample();
        }
    }
    JuceGfx gfx(core);
    core.gfx = &gfx;
    juce::Image img(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    double* mx = core.var("mouse_x"); double* my = core.var("mouse_y"); double* mc = core.var("mouse_cap");
    *core.var("gfx_w") = w; *core.var("gfx_h") = h;
    int nsl = 0; const JsfxSliderInfo* si = AlfredCore::sliderInfo(nsl);
    auto frame = [&]() { img.clear(img.getBounds(), juce::Colours::black); gfx.begin(img); core.sec_gfx(); gfx.end(); };
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::istringstream in(line); std::string c; in >> c;
        if (c == "m") { in >> *mx >> *my; }
        else if (c == "d") { *mc = (double)((int)*mc | 1); }
        else if (c == "u") { *mc = (double)((int)*mc & ~1); }
        else if (c == "s") { *mc = (double)((int)*mc | 8); }
        else if (c == "S") { *mc = (double)((int)*mc & ~8); }
        else if (c == "k") { int k; in >> k; gfx.keys.push_back(k); }
        else if (c == "v") { std::string nm; double val; in >> nm >> val; if (double* p = core.var(nm.c_str())) *p = val; }
        else if (c == "f") { int n; in >> n; for (int i = 0; i < n; ++i) frame(); }
        else if (c == "w")
        {
            std::string fn; in >> fn;
            juce::File f(fn); f.deleteFile();
            juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os);
        }
        else if (c == "p")
        {
            std::string lab; std::getline(in, lab);
            std::printf("%s:", lab.c_str());
            for (int i = 0; i < nsl; ++i) { char nm[32]; std::snprintf(nm, sizeof nm, "slider%d", si[i].index); std::printf(" %g", *core.var(nm)); }
            std::printf("\n");
        }
    }
    core.gfx = nullptr;
    return 0;
}

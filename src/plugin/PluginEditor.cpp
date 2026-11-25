#include "PluginEditor.h"
#include <cstring>

// ------------------------------------------------------------------------------------------- editor
AlfredEditor::AlfredEditor(AlfredProcessor& p) : AudioProcessorEditor(&p), proc(p), gfx(p.core)
{
    auto& c = proc.core;
    mx = c.var("mouse_x"); my = c.var("mouse_y"); mcap = c.var("mouse_cap"); mwheel = c.var("mouse_wheel");
    gw = c.var("gfx_w"); gh = c.var("gfx_h"); gretina = c.var("gfx_ext_retina");
    c.gfx = &gfx;
    setWantsKeyboardFocus(true);
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(400, 200, 2400, 2400);
    wantKey = proc.core.var("uwantk"); wantH = proc.core.var("uwanth");
    updFlag = proc.core.var("ext_update"); updA = proc.core.var("ext_upd_a"); updB = proc.core.var("ext_upd_b"); updOpen = proc.core.var("ext_upd_open");
    updateCheck->start();
    setSize(AlfredCore::gfxWidth(), AlfredCore::gfxHeight());
    startTimerHz(60);
}

AlfredEditor::~AlfredEditor()
{
    stopTimer();
    proc.core.gfx = nullptr;
}

int AlfredEditor::capFromMods(const juce::ModifierKeys& m) const
{
    int cap = buttons;
    if (m.isCommandDown()) cap |= 4;
    if (m.isShiftDown()) cap |= 8;
    if (m.isAltDown()) cap |= 16;
#if JUCE_MAC
    if (m.isCtrlDown()) cap |= 32;
#endif
    return cap;
}

void AlfredEditor::updateMouse(const juce::MouseEvent& e, bool)
{
    if (mx) *mx = std::floor(e.position.x * lastScale);
    if (my) *my = std::floor(e.position.y * lastScale);
    if (mcap) *mcap = capFromMods(e.mods);
}

void AlfredEditor::mouseMove(const juce::MouseEvent& e) { updateMouse(e, false); }
void AlfredEditor::mouseDrag(const juce::MouseEvent& e) { updateMouse(e, true); timerCallback(); }   // a drag is drawn at once
void AlfredEditor::mouseExit(const juce::MouseEvent& e) { updateMouse(e, false); }

void AlfredEditor::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    buttons = (e.mods.isLeftButtonDown() ? 1 : 0) | (e.mods.isRightButtonDown() ? 2 : 0) | (e.mods.isMiddleButtonDown() ? 64 : 0);
    updateMouse(e, true);
    timerCallback();            // a click between two frames is seen by the next frame
}

void AlfredEditor::mouseUp(const juce::MouseEvent& e)
{
    timerCallback();
    buttons = 0;
    updateMouse(e, false);
}

void AlfredEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    updateMouse(e, false);
    if (mwheel) *mwheel += (double)w.deltaY * 960.0;
}

void AlfredEditor::modifierKeysChanged(const juce::ModifierKeys& m)
{
    if (mcap) *mcap = capFromMods(m);
}

bool AlfredEditor::keyPressed(const juce::KeyPress& k)
{
    int code = 0;
    if (k == juce::KeyPress::returnKey) code = 13;
    else if (k == juce::KeyPress::backspaceKey) code = 8;
    else if (k == juce::KeyPress::escapeKey) code = 27;
    else if (k == juce::KeyPress::deleteKey) code = 6579564;
    else if (k == juce::KeyPress::leftKey) code = 1818584692;
    else if (k == juce::KeyPress::rightKey) code = 1919379572;
    else if (k == juce::KeyPress::upKey) code = 30064;
    else if (k == juce::KeyPress::downKey) code = 1685026670;
    else if (k.getTextCharacter() >= 32) code = (int)k.getTextCharacter();
    if (code == 0) return false;
    gfx.keys.push_back(code);
    return true;
}

void AlfredEditor::timerCallback()
{
    // nothing is drawn while the window is not on screen
    if (!isShowing()) return;
    // a newer release on GitHub? The script shows an UPDATE button in the footer; a click on it opens the page
    if (updFlag && *updFlag < 0.5)
    {
        int a = 0, b = 0;
        if (UpdateCheck::state(a, b, updPage) == 1) { if (updA) *updA = a; if (updB) *updB = b; *updFlag = 1.0; }
    }
    if (updOpen && *updOpen >= 0.5) { *updOpen = 0.0; UpdateCheck::openPage(updPage); }
    const int pw = juce::jmax(1, juce::roundToInt(getWidth() * lastScale));
    const int ph = juce::jmax(1, juce::roundToInt(getHeight() * lastScale));
    bool all = false;
    if (!frame.isValid() || frame.getWidth() != pw || frame.getHeight() != ph)
    {
        frame = juce::Image(juce::Image::ARGB, pw, ph, false, juce::SoftwareImageType());
        shown = juce::Image(juce::Image::ARGB, pw, ph, false, juce::SoftwareImageType());
        frame.clear(frame.getBounds(), juce::Colours::black);
        all = true;
    }
    if (gw) *gw = pw;
    if (gh) *gh = ph;
    if (gretina) *gretina = lastScale;
    gfx.begin(frame);
    proc.core.sec_gfx();
    gfx.end();

    // the script says how tall its layout wants to be (spectrum folded or open, Basic or Expert tab);
    // when that changes, the window takes that height at its current width
    if (wantKey && wantH && *wantKey != lastWantKey && *wantH > 0)
    {
        lastWantKey = *wantKey;
        const int w = getWidth();
        const int h = juce::jlimit(200, 2400, juce::roundToInt(*wantH * (double)w / 664.0));
        if (h != getHeight()) { setSize(w, h); return; }
    }

    // Only what changed since the last frame goes to the screen: the new frame is compared with the one that is
    // showing, row by row. A still picture costs no repaint at all, moving meters only their own area.
    int x0 = pw, x1 = -1, y0 = ph, y1 = -1;
    {
        juce::Image::BitmapData a(frame, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData b(shown, juce::Image::BitmapData::readWrite);
        const size_t rowBytes = (size_t)pw * (size_t)a.pixelStride;
        for (int yy = 0; yy < ph; ++yy)
        {
            const juce::uint8* ra = a.getLinePointer(yy); juce::uint8* rb = b.getLinePointer(yy);
            if (!all && std::memcmp(ra, rb, rowBytes) == 0) continue;
            if (!all && a.pixelStride == 4)
            {
                const auto* pa = (const juce::uint32*)(const void*)ra; const auto* pb = (const juce::uint32*)(const void*)rb;
                int l = 0, r = pw - 1;
                while (l < pw && pa[l] == pb[l]) ++l;
                while (r > l && pa[r] == pb[r]) --r;
                x0 = juce::jmin(x0, l); x1 = juce::jmax(x1, r);
            }
            else { x0 = 0; x1 = pw - 1; }
            y0 = juce::jmin(y0, yy); y1 = yy;
            std::memcpy(rb, ra, rowBytes);
        }
    }
    if (y1 < 0) return;
    const float inv = 1.0f / lastScale;
    repaint(juce::Rectangle<int>::leftTopRightBottom((int)std::floor((float)x0 * inv) - 1, (int)std::floor((float)y0 * inv) - 1,
                                                     (int)std::ceil((float)(x1 + 1) * inv) + 1, (int)std::ceil((float)(y1 + 1) * inv) + 1)
                .getIntersection(getLocalBounds()));
}

void AlfredEditor::paint(juce::Graphics& g)
{
    const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (s > 0.5f && std::abs(s - lastScale) > 0.01f) lastScale = s;
    // the image has exactly the screen's pixels: no filtering needed when it is drawn
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    if (shown.isValid()) g.drawImage(shown, getLocalBounds().toFloat());
    else g.fillAll(juce::Colours::black);
}

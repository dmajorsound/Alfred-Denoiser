#include "PluginEditor.h"

// ------------------------------------------------------------------------------------------- editor
AlfredEditor::AlfredEditor(AlfredProcessor& p) : AudioProcessorEditor(&p), proc(p), gfx(p.core)
{
    auto& c = proc.core;
    mx = c.var("mouse_x"); my = c.var("mouse_y"); mcap = c.var("mouse_cap"); mwheel = c.var("mouse_wheel");
    gw = c.var("gfx_w"); gh = c.var("gfx_h"); gretina = c.var("gfx_ext_retina");
    c.gfx = &gfx;
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(400, 300, 2400, 2400);
    setSize(AlfredCore::gfxWidth(), AlfredCore::gfxHeight());
    startTimerHz(30);
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
void AlfredEditor::mouseDrag(const juce::MouseEvent& e) { updateMouse(e, true); }
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
    const int pw = juce::jmax(1, juce::roundToInt(getWidth() * lastScale));
    const int ph = juce::jmax(1, juce::roundToInt(getHeight() * lastScale));
    if (!frame.isValid() || frame.getWidth() != pw || frame.getHeight() != ph)
        frame = juce::Image(juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType());
    frame.clear(frame.getBounds(), juce::Colours::black);
    if (gw) *gw = pw;
    if (gh) *gh = ph;
    if (gretina) *gretina = lastScale;
    gfx.begin(frame);
    proc.core.sec_gfx();
    gfx.end();
    repaint();
}

void AlfredEditor::paint(juce::Graphics& g)
{
    const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (s > 0.5f && std::abs(s - lastScale) > 0.01f) lastScale = s;
    if (frame.isValid()) g.drawImage(frame, getLocalBounds().toFloat());
}

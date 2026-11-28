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
    updFlag = proc.core.var("ext_update"); updA = proc.core.var("ext_upd_a"); updB = proc.core.var("ext_upd_b"); updOpen = proc.core.var("ext_upd_open"); manualOpen = proc.core.var("ext_manual_open");
    updDl = proc.core.var("ext_upd_dl"); updProg = proc.core.var("ext_upd_prog"); updMenu = proc.core.var("ext_upd_menu"); updTipId = proc.core.var("ext_upd_tipid");
    // a downloaded update that should install opens on the message thread (the download ends on its own thread)
    static bool hooked = false;
    if (!hooked)
    {
        hooked = true;
        UpdateDownload::get().onReadyToInstall = [](const juce::File& f)
        { juce::MessageManager::callAsync([f] { UpdateDownload::openInstaller(f); }); };
    }
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

void AlfredEditor::mouseMove(const juce::MouseEvent& e) { updateMouse(e, false); lastInputAt = juce::Time::getMillisecondCounterHiRes(); }
void AlfredEditor::mouseDrag(const juce::MouseEvent& e) { updateMouse(e, true); lastInputAt = juce::Time::getMillisecondCounterHiRes(); }   // drawn with the next frame
void AlfredEditor::mouseExit(const juce::MouseEvent& e) { updateMouse(e, false); }

void AlfredEditor::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    buttons = (e.mods.isLeftButtonDown() ? 1 : 0) | (e.mods.isRightButtonDown() ? 2 : 0) | (e.mods.isMiddleButtonDown() ? 64 : 0);
    updateMouse(e, true);
    lastInputAt = juce::Time::getMillisecondCounterHiRes();
    frame_(true);               // a click is seen at once, even between two frames
}

void AlfredEditor::mouseUp(const juce::MouseEvent& e)
{
    frame_(true);
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

void AlfredEditor::frame_(bool forced)
{
    // nothing is drawn while the window is not on screen
    if (!isShowing()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!forced)
    {
        const bool busy = now - lastInputAt < 600.0;
        const double gap = juce::jmax(busy ? 15.0 : 31.0, lastFrameMs * 4.0);
        if (now - lastFrameAt < gap) return;
    }
    lastFrameAt = now;
    struct Cost { AlfredEditor& e; double t0; ~Cost() { e.lastFrameMs = juce::Time::getMillisecondCounterHiRes() - t0; } } cost { *this, now };
    // a newer release on GitHub? The script shows an UPDATE button in the footer; a click on it opens the page
    if (updFlag && *updFlag < 0.5)
    {
        int a = 0, b = 0;
        if (UpdateCheck::state(a, b, updPage) == 1) { if (updA) *updA = a; if (updB) *updB = b; *updFlag = 1.0; }
    }
    if (updFlag && *updFlag >= 0.5)
    {
        double prog = 0.0;
        const int dl = UpdateDownload::get().status(&prog);
        if (updDl) *updDl = dl;
        if (updProg) *updProg = prog;
        updateTip();
    }
    if (updOpen && *updOpen >= 0.5) { *updOpen = 0.0; updateClick(); }
    if (updMenu && *updMenu >= 0.5) { *updMenu = 0.0; updateMenu(); }
    // READ MANUAL in the help panel: the manual on dmajorsound.com
    if (manualOpen && *manualOpen >= 0.5) { *manualOpen = 0.0; juce::URL("https://dmajorsound.com/software/alfred/manual.html").launchInDefaultBrowser(); }
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

// ---- update straight from GitHub (as in OLAAF)
static juce::String updVersion(double* a, double* b)
{
    return juce::String(a ? (int)*a : 0) + "." + juce::String(b ? (int)*b : 0);
}

void AlfredEditor::updateTip()
{
    if (updTipId == nullptr || *updTipId < 80000.0) return;
    double prog = 0.0; juce::File path; juce::String err;
    const int dl = UpdateDownload::get().status(&prog, &path, &err);
    const juce::String v = updVersion(updA, updB);
    const juce::String dir = UpdateDownload::get().folder().getFileName();
    const bool direct = UpdateCheck::asset().isNotEmpty();
    juce::String t;
    if (dl == UpdateDownload::kLoading) t = "Alfred " + v + " is coming from GitHub into your " + dir + " folder.";
    else if (dl == UpdateDownload::kThere) t = "Alfred " + v + " is in " + dir + ". Click to install it (quit your DAW before you click Install).";
    else if (dl == UpdateDownload::kFailed) t = "Download failed (" + err + "). Click to try again, right-click for the page.";
    else t = direct ? "Alfred " + v + " is out. Click to download and install it. Right-click: more choices."
                    : "Alfred " + v + " is out. Click to open the download page.";
    if (t.length() > 104) t = t.substring(0, 101) + "...";
    proc.core.str(*updTipId) = t.toStdString();
    if (auto* ts = proc.core.var("ext_upd_tipset")) *ts = 1.0;
}

void AlfredEditor::updateClick()
{
    auto& d = UpdateDownload::get();
    const juce::String asset = UpdateCheck::asset();
    juce::File path;
    const int dl = d.status(nullptr, &path);
    if (asset.isEmpty()) UpdateCheck::openPage(updPage);
    else if (dl == UpdateDownload::kThere) UpdateDownload::openInstaller(path);
    else if (dl != UpdateDownload::kLoading) d.start(asset, true);
}

void AlfredEditor::updateMenu()
{
    auto& d = UpdateDownload::get();
    const juce::String asset = UpdateCheck::asset();
    juce::File path;
    const int dl = d.status(nullptr, &path);
    const bool direct = asset.isNotEmpty();
    const juce::File dir = d.folder();
    juce::PopupMenu m;
    if (dl == UpdateDownload::kThere)
    {
        m.addItem(3, "Install now");
       #if JUCE_MAC
        m.addItem(4, "Show in Finder");
       #else
        m.addItem(4, "Show in Explorer");
       #endif
    }
    else
    {
        m.addItem(1, "Download and install", direct && dl != UpdateDownload::kLoading);
        m.addItem(2, "Download only (install later)", direct && dl != UpdateDownload::kLoading);
    }
    m.addSeparator();
    m.addItem(5, "Download to: " + dir.getFileName() + " ...", dl != UpdateDownload::kLoading);
    m.addItem(6, "What's new (release page)");
    juce::Component::SafePointer<AlfredEditor> self(this);
    const juce::String page = updPage;
    m.showMenuAsync(juce::PopupMenu::Options().withMousePosition(), [self, asset, path, dir, page](int cmd)
    {
        auto& dd = UpdateDownload::get();
        if (cmd == 1 || cmd == 2) dd.start(asset, cmd == 1);
        else if (cmd == 3) UpdateDownload::openInstaller(path);
        else if (cmd == 4) UpdateDownload::reveal(path);
        else if (cmd == 6) UpdateCheck::openPage(page);
        else if (cmd == 5 && self != nullptr)
        {
            self->folderChooser = std::make_unique<juce::FileChooser>("Alfred: choose where updates are downloaded to", dir);
            self->folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                             [](const juce::FileChooser& fc)
                                             {
                                                 const juce::File r = fc.getResult();
                                                 if (r.isDirectory()) UpdateDownload::get().setFolder(r);
                                             });
        }
    });
}

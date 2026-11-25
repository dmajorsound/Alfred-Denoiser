#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "JuceGfx.h"
#include "UpdateCheck.h"

class AlfredEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AlfredEditor(AlfredProcessor&);
    ~AlfredEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override {}

    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;

private:
    void timerCallback() override;
    void updateMouse(const juce::MouseEvent& e, bool down);
    int capFromMods(const juce::ModifierKeys& m) const;

    AlfredProcessor& proc;
    JuceGfx gfx;
    juce::Image frame;      // the frame being drawn
    juce::Image shown;      // the frame that is on screen
    double* wantKey = nullptr; double* wantH = nullptr; double lastWantKey = 0;   // layout height asked for by the script
    double* mx; double* my; double* mcap; double* mwheel;
    double* gw; double* gh; double* gretina;
    juce::SharedResourcePointer<UpdateCheck> updateCheck;   // one for all open Alfred windows
    double* updFlag = nullptr; double* updA = nullptr; double* updB = nullptr; double* updOpen = nullptr;
    juce::String updPage;
    int buttons = 0;
    float lastScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlfredEditor)
};

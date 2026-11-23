#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "JuceGfx.h"

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
    double* mx; double* my; double* mcap; double* mwheel;
    double* gw; double* gh; double* gretina;
    int buttons = 0;
    float lastScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlfredEditor)
};

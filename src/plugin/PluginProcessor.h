#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AlfredCore.h"
#include <array>
#include <atomic>

// Alfred as a native plug-in. The DSP and the interface code are the JSFX, translated to C++
// (src/gen/AlfredCore.*); this class connects it to the host the way REAPER runs a JSFX:
// sliders = host parameters, @block/@sample per audio block, @serialize = project state, pdc_delay = latency.
class AlfredProcessor : public juce::AudioProcessor,
                        private SliderListener,
                        private juce::AsyncUpdater
{
public:
    AlfredProcessor();
    ~AlfredProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Alfred"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // the translated JSFX; the editor runs its @gfx section on the message thread (as REAPER does, without a lock
    // against the audio side: the script only talks to it through sliders and request variables)
    AlfredCore core;
    juce::SpinLock stateLock;     // held by the audio thread; state save/load takes it too

private:
    template <typename T> void process(juce::AudioBuffer<T>& buffer, bool bypassed);
    void syncParamsToSliders();
    void sliderAutomate(uint64_t mask, bool endTouch) override;   // from the script (interface side)
    void handleAsyncUpdate() override;

    double sliderToNorm(int i, double v) const;
    double normToSlider(int i, double n) const;

    int numSliders = 0;
    const JsfxSliderInfo* sliders = nullptr;
    std::vector<double*> sliderVars;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> lastParam;
    std::vector<char> inGesture;
    juce::AudioParameterBool* bypassParam = nullptr;
    double* vSrate = nullptr; double* vBlock = nullptr; double* vPlay = nullptr; double* vPdc = nullptr;
    double* vSpl0 = nullptr; double* vSpl1 = nullptr; double* vPower = nullptr;
    std::atomic<int> pendingLatency { -1 };
    int reportedLatency = -1;
    bool initDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlfredProcessor)
};

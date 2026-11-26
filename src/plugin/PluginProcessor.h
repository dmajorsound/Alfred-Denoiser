#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AlfredCore.h"
#include "../neural/VoiceFront.h"
#include "../neural/Enhance.h"
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
    // The power button and the host's bypass are one switch: whichever is changed, the other follows.
    struct LinkListener : juce::AudioProcessorParameter::Listener
    {
        LinkListener(AlfredProcessor& p, int w) : owner(p), which(w) {}
        void parameterValueChanged(int, float) override { if (!owner.linking.load()) { owner.pendingLink.store(which); owner.triggerAsyncUpdate(); } }
        void parameterGestureChanged(int, bool) override {}
        AlfredProcessor& owner; int which;
    };
    LinkListener powerListener { *this, 1 }, bypassListener { *this, 2 };
    juce::RangedAudioParameter* powerParam = nullptr;
    std::atomic<int> pendingLink { 0 };
    std::atomic<bool> linking { false };
    double* vSrate = nullptr; double* vBlock = nullptr; double* vPlay = nullptr; double* vPdc = nullptr;
    double* vSpl0 = nullptr; double* vSpl1 = nullptr; double* vPower = nullptr;
    // ---- neural voice engine (48 kHz, Voice mode). It splits the input into the voice and the rest; the voice
    // goes through the classic engine, the rest comes back in at the level of the Noise fader.
    void loadNeural();
    // the network sits in the signal path only while the engine switch is on Neural (at 48 kHz): with the classic
    // engine Alfred has no extra delay and spends nothing on the network
    bool frontInPath() const { return neural != nullptr && front != nullptr && vSrate != nullptr && std::abs(*vSrate - 48000.0) < 1.0
                                      && vEngine != nullptr && *vEngine >= 0.5; }
    int neuralLatency() const { return frontInPath() ? NeuralVoice::kLatency : 0; }
    bool frontWasInPath = false;
    NeuralRuntime neuralRuntime;
    std::unique_ptr<NeuralVoice> neural;
    bool neuralTried = false;
    bool wasPlaying = false;
    double* vExtVoice = nullptr;
    double* vGa = nullptr; double* vDelta = nullptr; double* vMode = nullptr; double* vEngine = nullptr; double* vExtNeural = nullptr;
    std::unique_ptr<VoiceFront> front;
    std::vector<float> nIn[2];
    std::vector<double> nCore[2];

    // ---- Enhance: studio restoration of the voice (Restore or Sidon), worked out while the host renders offline
    std::shared_ptr<EnhanceModels> enhModels;
    EnhanceStage enh;
    int enhModel() const;                 // the armed model (0 = none): chosen, installed, 48 kHz
    int enhLatency() const { return enhModel() != 0 ? EnhanceStage::kLatency : 0; }
    void enhRun(double* const* voice, const double* const* rest, int n, int nch);
    std::vector<double> eVoice[2], eRest[2];
    double* vEnhModel = nullptr; double* vEnhAmt = nullptr; double* vSolo = nullptr; double* vMuteSig = nullptr;
    double* vExtEnh = nullptr; double* vExtEnhOn = nullptr; double* vExtEnhSec = nullptr; double* vExtEnhLoad = nullptr;
    std::atomic<int> pendingEnhLoad { 0 };
    double* vExtEnhPend = nullptr; double* vExtEnhState = nullptr;
    bool enhLive = false;
    juce::int64 expectedPos = -1; int jumpBudget = 8;

    std::atomic<int> pendingLatency { -1 };
    int reportedLatency = -1;
    bool initDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlfredProcessor)
};

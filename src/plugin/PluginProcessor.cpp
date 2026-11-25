#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::StringArray splitEnum(const char* e)
    {
        juce::StringArray a;
        if (e) a.addTokens(e, "|", "");
        return a;
    }
    constexpr uint32_t kStateMagic = 0x41464c44; // 'AFLD'
    constexpr int kStateVersion = 1;
}

AlfredProcessor::AlfredProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    core.reserveMemory(32 * JsfxRuntime::kBlockItems);
    sliders = AlfredCore::sliderInfo(numSliders);

    for (int i = 0; i < numSliders; ++i)
    {
        const auto& s = sliders[i];
        char nm[32]; std::snprintf(nm, sizeof nm, "slider%d", s.index);
        sliderVars.push_back(core.var(nm));
        const juce::String id = "s" + juce::String(s.index);
        juce::RangedAudioParameter* p = nullptr;
        if (s.enumNames)
        {
            p = new juce::AudioParameterChoice(juce::ParameterID { id, 1 }, s.label, splitEnum(s.enumNames), (int)s.def);
        }
        else
        {
            juce::NormalisableRange<float> r((float)s.lo, (float)s.hi, (float)s.step);
            p = new juce::AudioParameterFloat(juce::ParameterID { id, 1 }, s.label, r, (float)s.def);
        }
        addParameter(p);
        params.push_back(p);
        lastParam.push_back(p->getValue());
        inGesture.push_back(0);
    }
    bypassParam = new juce::AudioParameterBool(juce::ParameterID { "bypass", 1 }, "Bypass", false);
    addParameter(bypassParam);
    for (int i = 0; i < numSliders; ++i)
        if (sliders[i].index == 4) powerParam = params[(size_t)i];
    if (powerParam) powerParam->addListener(&powerListener);
    bypassParam->addListener(&bypassListener);

    vSrate = core.var("srate"); vBlock = core.var("samplesblock"); vPlay = core.var("play_state");
    vPdc = core.var("pdc_delay"); vSpl0 = core.var("spl0"); vSpl1 = core.var("spl1"); vPower = core.var("slider4");
    vGa = core.var("ga"); vDelta = core.var("slider19"); vMode = core.var("slider20"); vEngine = core.var("slider24");
    vExtNeural = core.var("ext_neural"); vExtVoice = core.var("ext_voice");
    if (vExtNeural) *vExtNeural = 2.0;      // the interface shows the engine switch; "not available" until it is loaded

    // @init at a default rate; the real rate arrives with prepareToPlay (the script follows srate in @block)
    *vSrate = 48000.0;
    if (vBlock) *vBlock = 512;
    if (vPlay) *vPlay = 0;
    core.sec_init();
    initDone = true;
    core.sliderListener = this;
    setLatencySamples((int)*vPdc);
    reportedLatency = (int)*vPdc;      // the neural engine adds its own latency once it is loaded (prepareToPlay)
}

AlfredProcessor::~AlfredProcessor()
{
    if (powerParam) powerParam->removeListener(&powerListener);
    if (bypassParam) bypassParam->removeListener(&bypassListener);
    cancelPendingUpdate();
    core.sliderListener = nullptr;
}

bool AlfredProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// The neural engine's two files are installed once for all plug-in formats:
//   macOS    /Library/Application Support/dmajorsound/Alfred
//   Windows  C:\ProgramData\dmajorsound\Alfred
// (or the folder named by the environment variable ALFRED_NEURAL_DIR). Without them Alfred runs its classic engine.
void AlfredProcessor::loadNeural()
{
    if (neuralTried) return;
    neuralTried = true;
    juce::Array<juce::File> dirs;
    const juce::String env = juce::SystemStats::getEnvironmentVariable("ALFRED_NEURAL_DIR", {});
    if (env.isNotEmpty()) dirs.add(juce::File(env));
    const juce::File common = juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    const juce::File user = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dirs.add(common.getChildFile("Application Support/dmajorsound/Alfred"));
    dirs.add(user.getChildFile("Application Support/dmajorsound/Alfred"));
    const char* libName = "libonnxruntime.dylib";
   #elif JUCE_WINDOWS
    dirs.add(common.getChildFile("dmajorsound/Alfred"));
    dirs.add(user.getChildFile("dmajorsound/Alfred"));
    const char* libName = "onnxruntime.dll";
   #else
    dirs.add(common.getChildFile("dmajorsound/Alfred"));
    dirs.add(user.getChildFile("dmajorsound/Alfred"));
    const char* libName = "libonnxruntime.so";
   #endif
    for (const auto& d : dirs)
    {
        const juce::File lib = d.getChildFile(libName), model = d.getChildFile("dpdfnet2_48khz_hr.onnx");
        if (!lib.existsAsFile() || !model.existsAsFile()) continue;
        std::string error;
        if (neuralRuntime.load(lib.getFullPathName().toStdString(), model.getFullPathName().toStdString(), error))
        {
            neural = std::make_unique<NeuralVoice>(neuralRuntime);
            break;
        }
    }
}

void AlfredProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    loadNeural();       // before the lock: loading the model takes a moment
    const juce::SpinLock::ScopedLockType lock(stateLock);
    *vSrate = sampleRate;
    if (vBlock) *vBlock = samplesPerBlock;
    syncParamsToSliders();
    core.sec_block();                       // runs setup() for the new rate, which sets pdc_delay
    if (neural)
    {
        if (!front) front = std::make_unique<VoiceFront>(*neural);
        front->prepare(sampleRate, juce::jmin(2, juce::jmax(1, getTotalNumInputChannels())), samplesPerBlock);
        for (int c = 0; c < 2; ++c) { nIn[c].assign((size_t)front->capacity(), 0.0f); nCore[c].assign((size_t)front->capacity(), 0.0); }
    }
    const int lat = (int)*vPdc + neuralLatency();
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples(lat); }
}

// ------------------------------------------------------------------------------------------- parameters
double AlfredProcessor::sliderToNorm(int i, double v) const
{
    return params[(size_t)i]->convertTo0to1((float)v);
}

double AlfredProcessor::normToSlider(int i, double n) const
{
    return (double)params[(size_t)i]->convertFrom0to1((float)n);
}

void AlfredProcessor::syncParamsToSliders()
{
    // a host change of a parameter goes to its slider; the slider's own (double) value is kept as long as
    // the host only echoes what the script wrote
    for (int i = 0; i < numSliders; ++i)
    {
        const float p = params[(size_t)i]->getValue();
        if (! juce::exactlyEqual(p, lastParam[(size_t)i]))
        {
            lastParam[(size_t)i] = p;
            if (sliderVars[(size_t)i]) *sliderVars[(size_t)i] = normToSlider(i, p);
        }
    }
}

void AlfredProcessor::sliderAutomate(uint64_t mask, bool endTouch)
{
    // called by the script's interface code (message thread)
    for (int i = 0; i < numSliders; ++i)
    {
        const int bit = sliders[i].index - 1;
        if (bit < 0 || bit > 63 || !((mask >> bit) & 1)) continue;
        auto* p = params[(size_t)i];
        const float n = (float)sliderToNorm(i, *sliderVars[(size_t)i]);
        lastParam[(size_t)i] = n;
        if (!inGesture[(size_t)i]) { p->beginChangeGesture(); inGesture[(size_t)i] = 1; }
        p->setValueNotifyingHost(n);
        lastParam[(size_t)i] = p->getValue();
        if (endTouch) { p->endChangeGesture(); inGesture[(size_t)i] = 0; }
    }
}

void AlfredProcessor::handleAsyncUpdate()
{
    const int lat = pendingLatency.exchange(-1);
    if (lat >= 0 && lat != getLatencySamples()) setLatencySamples(lat);

    // power button <-> host bypass
    const int link = pendingLink.exchange(0);
    if (link != 0 && powerParam != nullptr)
    {
        linking.store(true);
        const bool powerOn = powerParam->getValue() >= 0.5f, bypassOn = bypassParam->get();
        if (link == 1 && bypassOn == powerOn)          // the power button moved: the host's bypass follows
        {
            bypassParam->beginChangeGesture(); bypassParam->setValueNotifyingHost(powerOn ? 0.0f : 1.0f); bypassParam->endChangeGesture();
        }
        else if (link == 2 && bypassOn == powerOn)     // the host's bypass moved: the power button follows
        {
            powerParam->beginChangeGesture(); powerParam->setValueNotifyingHost(bypassOn ? 0.0f : 1.0f); powerParam->endChangeGesture();
        }
        linking.store(false);
    }
}

// ------------------------------------------------------------------------------------------- audio
template <typename T>
void AlfredProcessor::process(juce::AudioBuffer<T>& buffer, bool bypassed)
{
    const juce::SpinLock::ScopedLockType lock(stateLock);
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin(2, getTotalNumInputChannels());
    syncParamsToSliders();

    // the host's bypass (and the bypass parameter) runs the plug-in with its power off: the input comes out
    // delayed by the latency, so switching causes no jump in timing
    const bool hostBypass = bypassed;      // (the bypass parameter itself is linked to the power button)
    const double powerSave = *vPower;
    if (hostBypass) *vPower = 0.0;

    if (vBlock) *vBlock = n;
    if (vPlay)
    {
        bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) playing = pos->getIsPlaying();
        *vPlay = playing ? 1.0 : 0.0;
        // playback starts: the neural engine forgets what it heard before, as the classic engine does in its
        // @block, so a render always begins from the same state
        if (playing && !wasPlaying && front) front->reset();
        wasPlaying = playing;
    }
    const bool neuralUsable = neural != nullptr && std::abs(*vSrate - 48000.0) < 1.0;
    if (vExtNeural) *vExtNeural = neuralUsable ? 1.0 : 2.0;
    // when the neural engine delivers the voice, the classic engine must not look for noise in it a second time
    const bool neuralOn = neural != nullptr && front != nullptr && neuralUsable && *vPower >= 0.5
                          && (vMode == nullptr || *vMode < 0.5) && (vEngine == nullptr || *vEngine >= 0.5);
    if (vExtVoice) *vExtVoice = neuralOn ? 1.0 : 0.0;
    core.sec_block();

    T* l = buffer.getWritePointer(0);
    T* r = nch > 1 ? buffer.getWritePointer(1) : nullptr;
    if (neural == nullptr || front == nullptr)
    {
        for (int i = 0; i < n; ++i)
        {
            *vSpl0 = (double)l[i];
            *vSpl1 = r ? (double)r[i] : (double)l[i];
            core.sec_sample();
            l[i] = (T)*vSpl0;
            if (r) r[i] = (T)*vSpl1;
        }
    }
    else
    {
        // ---- with the neural voice engine in front: the network's voice goes through the classic engine, and what
        // the network took away comes back at the level of the Noise fader
        const bool want = neuralOn;
        double levelHi, levelLo;
        VoiceFront::levels(vGa ? *vGa : 1.0, vDelta ? (int)(*vDelta + 0.5) : 0, *vPower >= 0.5, levelHi, levelLo);
        const int cap = front->capacity();
        for (int start = 0; start < n; start += cap)
        {
            const int len = juce::jmin(cap, n - start);
            for (int i = 0; i < len; ++i) { nIn[0][(size_t)i] = (float)l[start + i]; if (r) nIn[1][(size_t)i] = (float)r[start + i]; }
            const float* ip[2] = { nIn[0].data(), nIn[1].data() };
            double* cp[2] = { nCore[0].data(), nCore[1].data() };
            front->pre(ip, cp, len, want);
            for (int i = 0; i < len; ++i)
            {
                *vSpl0 = cp[0][i];
                *vSpl1 = r ? cp[1][i] : cp[0][i];
                core.sec_sample();
                cp[0][i] = *vSpl0;
                if (r) cp[1][i] = *vSpl1;
            }
            front->post(cp, len, levelHi, levelLo, (int)*vPdc);
            for (int i = 0; i < len; ++i) { l[start + i] = (T)cp[0][i]; if (r) r[start + i] = (T)cp[1][i]; }
        }
    }
    for (int ch = nch; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    if (hostBypass) *vPower = powerSave;

    const int lat = (int)*vPdc + neuralLatency();
    if (lat != reportedLatency) { reportedLatency = lat; pendingLatency = lat; triggerAsyncUpdate(); }
}

void AlfredProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process(b, false); }
void AlfredProcessor::processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process(b, false); }
void AlfredProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process(b, true); }
void AlfredProcessor::processBlockBypassed(juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process(b, true); }

// ------------------------------------------------------------------------------------------- state
void AlfredProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out(dest, false);
    std::vector<float> ser;
    std::vector<double> vals;
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        for (int i = 0; i < numSliders; ++i) vals.push_back(sliderVars[(size_t)i] ? *sliderVars[(size_t)i] : 0.0);
        core.serializeWrite(ser);
    }
    out.writeInt((int)kStateMagic);
    out.writeInt(kStateVersion);
    out.writeInt(numSliders);
    for (int i = 0; i < numSliders; ++i) { out.writeInt(sliders[i].index); out.writeDouble(vals[(size_t)i]); }
    out.writeInt((int)ser.size());
    for (float f : ser) out.writeFloat(f);
}

void AlfredProcessor::setStateInformation(const void* data, int size)
{
    juce::MemoryInputStream in(data, (size_t)size, false);
    if (size < 12 || (uint32_t)in.readInt() != kStateMagic) return;
    const int ver = in.readInt();
    if (ver < 1) return;
    const int ns = in.readInt();
    std::vector<std::pair<int, double>> vals;
    for (int i = 0; i < ns && !in.isExhausted(); ++i) { const int idx = in.readInt(); vals.push_back({ idx, in.readDouble() }); }
    const int nser = in.readInt();
    std::vector<float> ser;
    for (int i = 0; i < nser && !in.isExhausted(); ++i) ser.push_back(in.readFloat());

    std::vector<int> touched;
    {
        const juce::SpinLock::ScopedLockType lock(stateLock);
        for (auto& [idx, v] : vals)
            for (int i = 0; i < numSliders; ++i)
                if (sliders[i].index == idx && sliderVars[(size_t)i])
                {
                    *sliderVars[(size_t)i] = v;
                    lastParam[(size_t)i] = (float)sliderToNorm(i, v);
                    touched.push_back(i);
                }
        core.serializeRead(ser.data(), ser.size());
    }
    // tell the host the loaded values (the sliders keep their exact values: lastParam already matches)
    for (int i : touched) params[(size_t)i]->setValueNotifyingHost(lastParam[(size_t)i]);
}

juce::AudioProcessorEditor* AlfredProcessor::createEditor() { return new AlfredEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AlfredProcessor(); }

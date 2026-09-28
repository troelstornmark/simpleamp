#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "NAM/activations.h"

// The resampler comes from iPlug2 and expects these two symbols from it.
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#ifndef DEFAULT_BLOCK_SIZE
  #define DEFAULT_BLOCK_SIZE 512
#endif
#include "dsp/ResamplingContainer/ResamplingContainer.h"

namespace
{
constexpr int kMaxIRSamples = 2048;         // ~43 ms at 48 kHz: plenty for a cab, cheap to convolve
constexpr int kMaxReverbIRSamples = 144000; // 3 s at 48 kHz
constexpr float kTargetLoudnessDb = -18.0f;
constexpr double kMaxDelaySeconds = 2.0;
constexpr double kTailSeconds = 4.0;         // delay/reverb keep ringing this long after switching off

const char* kGeqIds[sa::GraphicEQ::kBands] = { "geq_100", "geq_200", "geq_400", "geq_800", "geq_1k6", "geq_3k2", "geq_6k4" };

struct Preset
{
    const char* name;
    float input, gate, boost, drive, bass, mid, treble, cab, output;
};

// Starting points. Tweak Input first: it sets how hard you hit the amp.
const Preset kPresets[] = {
    { "Default 5150",            0.0f, -60.0f, 1.0f, 0.20f,  0.0f,  0.0f,  0.0f, 1.0f, -6.0f },
    { "Pantera (scooped)",       3.0f, -64.0f, 0.0f, 0.00f,  3.0f, -6.0f,  3.0f, 1.0f, -6.0f },
    { "Gojira (boosted, flat)",  0.0f, -60.0f, 1.0f, 0.15f,  0.0f,  1.0f,  1.0f, 1.0f, -6.0f },
    { "Meshuggah (tight/djent)", 2.0f, -52.0f, 1.0f, 0.35f, -2.0f,  3.0f,  2.0f, 1.0f, -6.0f },
};

inline float peakOf (const float* x, int n)
{
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (x[i]));
    return pk;
}

inline void storeMax (std::atomic<float>& a, float v)
{
    if (v > a.load (std::memory_order_relaxed)) a.store (v, std::memory_order_relaxed);
}
} // namespace

//==============================================================================
AmpEngine::AmpEngine() = default;
AmpEngine::~AmpEngine() = default;

void AmpEngine::prepare (double sampleRate, int maxBlock)
{
    hostRate = sampleRate;
    hostBlock = maxBlock;

    if (std::abs (sampleRate - modelRate) > 0.5)
    {
        resampler = std::make_unique<Resampler> (modelRate);
        resampler->Reset (sampleRate, maxBlock);
        modelBlock = (int) std::ceil (maxBlock * modelRate / sampleRate) + 4;
    }
    else
    {
        resampler.reset();
        modelBlock = maxBlock;
    }

    model->Reset (modelRate, modelBlock); // also prewarms
}

void AmpEngine::process (float* in, float* out, int n)
{
    auto runModel = [this] (float** ins, float** outs, int num)
    {
        // Resampler may hand us slightly more than planned; chunk to be safe.
        for (int pos = 0; pos < num;)
        {
            const int len = std::min (num - pos, modelBlock);
            float* i = ins[0] + pos;
            float* o = outs[0] + pos;
            model->process (&i, &o, len);
            pos += len;
        }
    };

    if (resampler != nullptr)
        resampler->ProcessBlock (&in, &out, n, runModel);
    else
        runModel (&in, &out, n);

    for (int i = 0; i < n; ++i)
        out[i] *= levelGain;
}

int AmpEngine::latency() const { return resampler != nullptr ? resampler->GetLatency() : 0; }

void EngineSlot::swapIfReady (double rate, int block)
{
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || ! hasPending || retired != nullptr) return;

    if (pending == nullptr) // "clear"
    {
        retired = std::move (active);
        hasPending = false;
    }
    else if (pending->hostRate == rate && pending->hostBlock == block)
    {
        retired = std::move (active);
        active = std::move (pending);
        hasPending = false;
    }
}

//==============================================================================
SimpleAmpProcessor::SimpleAmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      convolution (juce::dsp::Convolution::Latency { 0 }),
      revConvolution (juce::dsp::Convolution::NonUniform { 512 })
{
    nam::activations::Activation::enable_fast_tanh();

    pInput = param ("input"); pGate = param ("gate"); pBoost = param ("boost"); pDrive = param ("drive");
    pBass = param ("bass"); pMid = param ("mid"); pTreble = param ("treble"); pCab = param ("cab");
    pOutput = param ("output"); pEco = param ("eco"); pInputCh = param ("inputch");
    pCompOn = param ("comp_on"); pCompSustain = param ("comp_sustain"); pCompLevel = param ("comp_level");
    pOctOn = param ("oct_on"); pOctSub = param ("oct_sub"); pOctUp = param ("oct_up"); pOctDry = param ("oct_dry");
    pWahOn = param ("wah_on"); pWahSens = param ("wah_sens"); pWahReso = param ("wah_reso"); pWahMix = param ("wah_mix");
    pPedalOn = param ("pedal_on"); pPedalLevel = param ("pedal_level");
    pChorusOn = param ("chorus_on"); pChorusRate = param ("chorus_rate"); pChorusDepth = param ("chorus_depth"); pChorusMix = param ("chorus_mix");
    pFlangerOn = param ("flanger_on"); pFlangerRate = param ("flanger_rate"); pFlangerDepth = param ("flanger_depth"); pFlangerRegen = param ("flanger_regen");
    pLimitOn = param ("limit_on"); pLimitCeiling = param ("limit_ceiling"); pLimitLevel = param ("limit_level");
    pGeqOn = param ("geq_on"); pGeqLevel = param ("geq_level");
    for (int i = 0; i < sa::GraphicEQ::kBands; ++i) pGeq[i] = param (kGeqIds[i]);
    pDelayOn = param ("delay_on"); pDelayTime = param ("delay_time"); pDelayFb = param ("delay_fb"); pDelayMix = param ("delay_mix");
    pRevOn = param ("rev_on"); pRevSize = param ("rev_size"); pRevTone = param ("rev_tone"); pRevMix = param ("rev_mix"); pRevIR = param ("rev_ir");
    lastEco = pEco->load() > 0.5f;

    // Out of the box: pick up the first model / cab IR in ~/Music/SimpleAmp.
    // A saved session state (setStateInformation) overrides this.
    auto folder = defaultFolder();
    auto models = folder.findChildFiles (juce::File::findFiles, false, "*.nam");
    auto irs = findLibraryFiles ("cabs");
    models.sort();
    if (! models.isEmpty()) loadModel (models.getFirst());
    if (! irs.isEmpty()) loadImpulseResponse (irs.getFirst());

    startTimerHz (10);
}

SimpleAmpProcessor::~SimpleAmpProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 10000);
}

//==============================================================================
juce::File SimpleAmpProcessor::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SimpleAmp");
}

juce::Array<juce::File> SimpleAmpProcessor::libraryRoots()
{
    // 1) tones bundled in the running app, 2) tones bundled in SimpleAmp Live (so the plugin in LUNA
    // sees them too), 3) the user's own folder.
    juce::Array<juce::File> roots;
    for (auto dir : { juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/tones"),
                      juce::File ("/Applications/SimpleAmp Live.app/Contents/Resources/tones"),
                      defaultFolder() })
        if (dir.isDirectory() && ! roots.contains (dir)) roots.add (dir);
    return roots;
}

bool SimpleAmpProcessor::isReservedFolder (const juce::String& name)
{
    return name.equalsIgnoreCase ("pedals") || name.equalsIgnoreCase ("cabs") || name.equalsIgnoreCase ("reverbs");
}

juce::Array<juce::File> SimpleAmpProcessor::findLibraryFiles (const juce::String& kind)
{
    const juce::String pattern = kind == "pedals" ? "*.nam" : "*.wav";
    juce::Array<juce::File> result;
    for (auto& root : libraryRoots())
    {
        auto files = root.getChildFile (kind).findChildFiles (juce::File::findFiles, true, pattern);
        if (kind == "cabs") files.addArray (root.findChildFiles (juce::File::findFiles, false, "*.wav")); // older layout
        files.sort();
        result.addArray (files);
    }
    return result;
}

juce::Array<juce::File> SimpleAmpProcessor::findAmpFiles()
{
    juce::Array<juce::File> result;
    for (auto& root : libraryRoots())
    {
        auto files = root.findChildFiles (juce::File::findFiles, true, "*.nam");
        files.sort();
        for (auto& f : files)
        {
            bool reserved = false; // pedals/, cabs/ and reverbs/ hold other things
            for (auto dir = f.getParentDirectory(); dir != root && dir != juce::File(); dir = dir.getParentDirectory())
                if (dir.getParentDirectory() == root && isReservedFolder (dir.getFileName())) reserved = true;
            if (! reserved) result.add (f);
        }
    }
    return result;
}

juce::String SimpleAmpProcessor::ampGroup (const juce::File& amp)
{
    const auto parent = amp.getParentDirectory();
    for (auto& root : libraryRoots())
        if (parent == root || (parent.getParentDirectory() == root && parent.getFileName().equalsIgnoreCase ("amps")))
            return "My amps";
    return parent.getFileName();
}

// "Full Rig Peavey 5150 Maxon Mesa OS SM57 - jp_is_out_of_tune" -> "5150 Maxon Mesa OS SM57"
juce::String SimpleAmpProcessor::displayName (const juce::File& f)
{
    auto n = f.getFileNameWithoutExtension();
    if (n.contains (" - ")) n = n.upToLastOccurrenceOf (" - ", false, false);
    for (auto prefix : { "Full Rig Peavey ", "FR " })
        if (n.startsWith (prefix)) n = n.substring ((int) std::strlen (prefix));
    return n.trim();
}

void SimpleAmpProcessor::selectAmp (const juce::File& file)
{
    loadModel (file);
    if (! file.existsAsFile()) return;

    const auto pack = file.getParentDirectory().getFileName();
    const auto text = " " + (pack + " " + file.getFileNameWithoutExtension()).toLowerCase() + " ";
    const bool fullRig = text.contains ("full rig") || file.getFileName().startsWith ("FR ");
    bool boosted = false;
    if (! text.contains ("no boost"))
        for (auto word : { "boosted", "maxon", "mxr", "od808", "ts808", "ts9", "sd1", "sd-1", " rat ", "ocd", "dodbs", "tmb", "klon", "precision drive" })
            if (text.contains (word)) boosted = true;

    if (fullRig) if (auto* cab = apvts.getParameter ("cab")) cab->setValueNotifyingHost (0.0f);
    if (boosted) if (auto* b = apvts.getParameter ("boost")) b->setValueNotifyingHost (0.0f);
}

void SimpleAmpProcessor::createLibraryFolders()
{
    const auto root = defaultFolder();
    const std::pair<const char*, const char*> folders[] = {
        { "amps",    "Amp captures (.nam), e.g. a 5150 head or a full rig. Subfolders become groups in the menu." },
        { "pedals",  "Pedal captures (.nam) for the NAM PEDAL slot: boosts, overdrives, distortions, fuzzes, compressors." },
        { "cabs",    "Cabinet impulse responses (.wav), used when the amp model is a head without a cab." },
        { "reverbs", "Reverb impulse responses (.wav, mono or stereo, up to 3 s are used) for the REVERB pedal." },
    };
    for (auto& [name, text] : folders)
    {
        auto dir = root.getChildFile (name);
        dir.createDirectory();
        auto readme = dir.getChildFile ("README.txt");
        if (! readme.existsAsFile()) readme.replaceWithText (juce::String (text) + "\n");
    }

    auto guide = root.getChildFile ("HOW TO ADD TONES.txt");
    if (! guide.existsAsFile())
        guide.replaceWithText (
            "SimpleAmp tone library\n"
            "======================\n\n"
            "Drop files into these folders. SimpleAmp picks them up within a few seconds; no restart needed.\n\n"
            "amps/     .nam  Amp captures (head or full rig). Subfolders become groups in the tone menu.\n"
            "                Full-rig captures (name contains 'Full Rig' or starts with 'FR ') switch the Cab off.\n"
            "pedals/   .nam  Pedal captures for the NAM PEDAL slot (pre-amp): boosts, overdrives, distortions,\n"
            "                fuzzes, and (roughly) compressors. Tone3000: filter Gear = Pedal.\n"
            "cabs/     .wav  Cabinet impulse responses. Used when the amp is a head-only capture and Cab is on.\n"
            "reverbs/  .wav  Reverb impulse responses (mono or stereo, the first 3 s are used). Pick one in the\n"
            "                REVERB pedal's menu; 'Built-in room' switches back.\n\n"
            "Why chorus, flanger, autowah, octaver, delay and EQ are built in instead of files:\n"
            "a NAM capture can only learn effects that react the same way every time (drive, fuzz, tone).\n"
            "Anything with an LFO (chorus, flanger), an envelope (autowah), pitch tracking (octaver) or long\n"
            "memory (delay, reverb) can't be captured, so SimpleAmp has its own versions of those.\n");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SimpleAmpProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = [&] (const String& id, const String& name, float lo, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
    };
    auto amount = [&] (const String& id, const String& name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f, 0.01f), def));
    };
    auto range = [&] (const String& id, const String& name, float lo, float hi, float def, float skewCentre, const String& label)
    {
        NormalisableRange<float> r (lo, hi, 0.01f);
        r.setSkewForCentre (skewCentre);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                           AudioParameterFloatAttributes().withLabel (label)));
    };
    auto toggle = [&] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    // Amp
    db ("input", "Input", -24.0f, 24.0f, 0.0f);
    db ("gate", "Gate", -96.0f, -20.0f, -60.0f);
    toggle ("boost", "Boost", true);
    amount ("drive", "Drive", 0.2f);
    db ("bass", "Bass", -12.0f, 12.0f, 0.0f);
    db ("mid", "Mid", -12.0f, 12.0f, 0.0f);
    db ("treble", "Treble", -12.0f, 12.0f, 0.0f);
    toggle ("cab", "Cab", true);
    db ("output", "Output", -40.0f, 12.0f, -6.0f);
    toggle ("eco", "Eco", false);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "inputch", 1 }, "Input Channel", StringArray { "In 1", "In 2" }, 0));

    // Pre-amp pedals
    toggle ("comp_on", "Comp On", false);        amount ("comp_sustain", "Comp Sustain", 0.5f); db ("comp_level", "Comp Level", -12.0f, 18.0f, 6.0f);
    toggle ("oct_on", "Octave On", false);       amount ("oct_sub", "Octave Sub", 0.6f); amount ("oct_up", "Octave Up", 0.0f); amount ("oct_dry", "Octave Dry", 0.8f);
    toggle ("wah_on", "Wah On", false);          amount ("wah_sens", "Wah Sens", 0.5f); amount ("wah_reso", "Wah Reso", 0.5f); amount ("wah_mix", "Wah Mix", 1.0f);
    toggle ("pedal_on", "NAM Pedal On", false);  db ("pedal_level", "NAM Pedal Level", -18.0f, 18.0f, 0.0f);
    toggle ("chorus_on", "Chorus On", false);    range ("chorus_rate", "Chorus Rate", 0.1f, 5.0f, 0.8f, 1.0f, "Hz");
                                                 amount ("chorus_depth", "Chorus Depth", 0.5f); amount ("chorus_mix", "Chorus Mix", 0.5f);
    toggle ("flanger_on", "Flanger On", false);  range ("flanger_rate", "Flanger Rate", 0.05f, 3.0f, 0.25f, 0.5f, "Hz");
                                                 amount ("flanger_depth", "Flanger Depth", 0.7f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "flanger_regen", 1 }, "Flanger Regen", NormalisableRange<float> (-0.9f, 0.9f, 0.01f), 0.5f));
    toggle ("limit_on", "Limiter On", false);    db ("limit_ceiling", "Limiter Ceiling", -30.0f, 0.0f, -12.0f); db ("limit_level", "Limiter Level", -12.0f, 18.0f, 6.0f);

    // Post-amp pedals
    toggle ("geq_on", "EQ On", false);
    const char* geqNames[] = { "EQ 100", "EQ 200", "EQ 400", "EQ 800", "EQ 1.6k", "EQ 3.2k", "EQ 6.4k" };
    for (int i = 0; i < sa::GraphicEQ::kBands; ++i) db (kGeqIds[i], geqNames[i], -15.0f, 15.0f, 0.0f);
    db ("geq_level", "EQ Level", -15.0f, 15.0f, 0.0f);
    toggle ("delay_on", "Delay On", false);      range ("delay_time", "Delay Time", 20.0f, 1500.0f, 380.0f, 350.0f, "ms");
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "delay_fb", 1 }, "Delay Feedback", NormalisableRange<float> (0.0f, 0.9f, 0.01f), 0.35f));
    amount ("delay_mix", "Delay Mix", 0.3f);
    toggle ("rev_on", "Reverb On", false);       amount ("rev_size", "Reverb Size", 0.5f); amount ("rev_tone", "Reverb Tone", 0.5f);
    amount ("rev_mix", "Reverb Mix", 0.25f);     toggle ("rev_ir", "Reverb Uses IR", false);
    return layout;
}

bool SimpleAmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

//==============================================================================
void SimpleAmpProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    samplesPerBlock = std::max (samplesPerBlock, 16);
    sampleRate = sr;
    hostRate = sr;
    hostBlock = samplesPerBlock;

    for (auto* v : { &monoBuf, &ampBuf, &pedalBuf, &leftBuf, &rightBuf, &revL, &revR })
        v->assign ((size_t) samplesPerBlock, 0.0f);

    auto on = [] (std::atomic<float>* p) { return p->load() > 0.5f; };
    gate.prepare (sr);
    comp.prepare (sr);          compF.prepare (sr, on (pCompOn));
    octaver.prepare (sr);       octF.prepare (sr, on (pOctOn));
    wah.prepare (sr);           wahF.prepare (sr, on (pWahOn));
    boost.prepare (sr);         boostF.prepare (sr, on (pBoost));
    pedalF.prepare (sr, on (pPedalOn));
    chorus.prepare (sr, 0.03);  chorusF.prepare (sr, on (pChorusOn));
    flanger.prepare (sr, 0.02); flangerF.prepare (sr, on (pFlangerOn));
    limiter.prepare (sr);       limitF.prepare (sr, on (pLimitOn));
    lastDrive = -1.0f;
    fallbackAmp.prepare (sr);
    fallbackCab.prepare (sr);
    eq.prepare (sr);
    geq.prepare (sr);           geqF.prepare (sr, on (pGeqOn));
    delay.prepare (sr, kMaxDelaySeconds); delayF.prepare (sr, on (pDelayOn));
    reverb.setSampleRate (sr);  reverb.reset(); revF.prepare (sr, on (pRevOn));
    revParams.roomSize = -1.0f; // force an update
    delayTail = revTail = 0;

    convolution.prepare ({ sr, (juce::uint32) samplesPerBlock, 1 });
    convolution.reset();
    revConvolution.prepare ({ sr, (juce::uint32) samplesPerBlock, 2 });
    revConvolution.reset();

    inGain.reset (sr, 0.02);
    outGain.reset (sr, 0.02);
    inGain.setCurrentAndTargetValue (sa::dbToGain (pInput->load()));
    outGain.setCurrentAndTargetValue (sa::dbToGain (pOutput->load()));

    loadMeasurer.reset (sr, samplesPerBlock);
    tunerGain.reset (sr, 0.02);
    tunerGain.setCurrentAndTargetValue (tunerActive.load() && tunerMute.load() ? 0.0f : 1.0f);

    // Audio is stopped here, so it is safe to re-prepare engines in place.
    for (auto* slot : { &amp, &pedal })
    {
        {
            const juce::SpinLock::ScopedLockType sl (slot->lock);
            if (slot->pending != nullptr) slot->pending->prepare (sr, samplesPerBlock);
        }
        if (slot->active != nullptr) slot->active->prepare (sr, samplesPerBlock);
        slot->latency = slot->active != nullptr ? slot->active->latency() : 0;
    }
    setLatencySamples (amp.latency + pedal.latency);
}

void SimpleAmpProcessor::processChunk (float* mono, float* left, float* right, int n)
{
    auto on = [] (std::atomic<float>* p) { return p->load (std::memory_order_relaxed) > 0.5f; };

    // ---- PRE, part 1: input, gate, comp, octave, wah, TS boost (per sample) ----
    const float gateDb = pGate->load();
    const bool gateOn = gateDb > -95.0f;
    const float openT = sa::dbToGain (gateDb), closeT = sa::dbToGain (gateDb - 6.0f);
    const float drive = pDrive->load();
    if (drive != lastDrive) { boost.setDrive (drive); lastDrive = drive; }
    inGain.setTargetValue (sa::dbToGain (pInput->load()));

    compF.set (on (pCompOn));    comp.set (pCompSustain->load(), pCompLevel->load());
    octF.set (on (pOctOn));      octaver.set (pOctSub->load(), pOctUp->load(), pOctDry->load());
    wahF.set (on (pWahOn));      wah.set (pWahSens->load(), pWahReso->load(), pWahMix->load());
    boostF.set (on (pBoost));

    for (int i = 0; i < n; ++i)
    {
        float x = mono[i] * inGain.getNextValue();
        if (gateOn) x = gate.process (x, openT, closeT);
        if (compF.active())  { const float g = compF.next();  x += g * (comp.process (x) - x); }
        if (octF.active())   { const float g = octF.next();   x += g * (octaver.process (x) - x); }
        if (wahF.active())   { const float g = wahF.next();   x += g * (wah.process (x) - x); }
        if (boostF.active()) { const float g = boostF.next(); x += g * (boost.process (x) - x); }
        mono[i] = x;
    }

    // ---- PRE, part 2: NAM pedal capture (block) ----
    pedalF.set (on (pPedalOn) && pedal.active != nullptr);
    if (pedalF.active() && pedal.active != nullptr)
    {
        float* pb = pedalBuf.data();
        pedal.active->process (mono, pb, n);
        const float lvl = sa::dbToGain (pPedalLevel->load());
        for (int i = 0; i < n; ++i) { const float g = pedalF.next(); mono[i] += g * (pb[i] * lvl - mono[i]); }
    }

    // ---- PRE, part 3: chorus, flanger, limiter (per sample) ----
    chorusF.set (on (pChorusOn));
    chorus.set (pChorusRate->load(), 7.0f, 1.0f + 4.0f * pChorusDepth->load(), 0.0f, pChorusMix->load());
    flangerF.set (on (pFlangerOn));
    flanger.set (pFlangerRate->load(), 0.3f, 0.3f + 4.5f * pFlangerDepth->load(), pFlangerRegen->load(), 0.5f);
    limitF.set (on (pLimitOn));  limiter.set (pLimitCeiling->load(), pLimitLevel->load());
    if (chorusF.active() || flangerF.active() || limitF.active())
        for (int i = 0; i < n; ++i)
        {
            float x = mono[i];
            if (chorusF.active())  { const float g = chorusF.next();  x += g * (chorus.process (x) - x); }
            if (flangerF.active()) { const float g = flangerF.next(); x += g * (flanger.process (x) - x); }
            if (limitF.active())   { const float g = limitF.next();   x += g * (limiter.process (x) - x); }
            mono[i] = x;
        }

    // ---- AMP + CAB ----
    float* ampOut = ampBuf.data();
    if (amp.active != nullptr)
        amp.active->process (mono, ampOut, n);
    else
        for (int i = 0; i < n; ++i) ampOut[i] = fallbackAmp.process (mono[i]);

    if (on (pCab))
    {
        if (cabUseIR.load (std::memory_order_relaxed) && convolution.getCurrentIRSize() > 0)
        {
            juce::dsp::AudioBlock<float> block (&ampOut, 1, (size_t) n);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else
        {
            for (int i = 0; i < n; ++i) ampOut[i] = fallbackCab.process (ampOut[i]);
        }
    }

    // ---- POST: amp EQ, graphic EQ (mono) ----
    eq.update (pBass->load(), pMid->load(), pTreble->load());
    geqF.set (on (pGeqOn));
    float geqGains[sa::GraphicEQ::kBands];
    for (int b = 0; b < sa::GraphicEQ::kBands; ++b) geqGains[b] = pGeq[b]->load();
    geq.set (geqGains, pGeqLevel->load());
    for (int i = 0; i < n; ++i)
    {
        float x = eq.process (ampOut[i]);
        if (geqF.active()) { const float g = geqF.next(); x += g * (geq.process (x) - x); }
        left[i] = right[i] = x;
    }

    // ---- POST: delay (mono echo into both sides). Repeats ring out after switching off. ----
    const bool delayOn = on (pDelayOn);
    delayF.set (delayOn);
    if (delayOn) delayTail = (int) (kTailSeconds * sampleRate);
    if (delayOn || delayF.active() || delayTail > 0)
    {
        delay.set ((float) (pDelayTime->load() * 0.001 * sampleRate), pDelayFb->load());
        const float mix = pDelayMix->load();
        for (int i = 0; i < n; ++i)
        {
            const float wet = delay.process (left[i] * delayF.next()) * mix;
            left[i] += wet; right[i] += wet;
        }
        if (! delayOn) delayTail = std::max (0, delayTail - n);
        if (delayTail == 0 && ! delayF.active()) delay.clear();
    }

    // ---- POST: reverb (stereo). Built-in room, or an impulse response from reverbs/. ----
    const bool revOn = on (pRevOn);
    revF.set (revOn);
    if (revOn) revTail = (int) (kTailSeconds * sampleRate);
    if (revOn || revF.active() || revTail > 0)
    {
        float* rl = revL.data();
        float* rr = revR.data();
        for (int i = 0; i < n; ++i) { const float g = revF.next(); rl[i] = left[i] * g; rr[i] = right[i] * g; }

        const bool useIR = on (pRevIR) && revConvolution.getCurrentIRSize() > 0;
        if (useIR)
        {
            float* chans[] = { rl, rr };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            revConvolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else
        {
            const float size = pRevSize->load(), tone = pRevTone->load();
            if (size != revParams.roomSize || 1.0f - tone != revParams.damping)
            {
                revParams.roomSize = size; revParams.damping = 1.0f - tone;
                revParams.wetLevel = 1.0f; revParams.dryLevel = 0.0f; revParams.width = 1.0f;
                reverb.setParameters (revParams);
            }
            reverb.processStereo (rl, rr, n);
        }

        const float mix = pRevMix->load() * (useIR ? 1.0f : 0.5f);
        for (int i = 0; i < n; ++i) { left[i] += rl[i] * mix; right[i] += rr[i] * mix; }
        if (! revOn) revTail = std::max (0, revTail - n);
    }

    // ---- Output ----
    outGain.setTargetValue (sa::dbToGain (pOutput->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        left[i] *= g; right[i] *= g;
    }
}

void SimpleAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    // Pick up freshly loaded models.
    amp.swapIfReady (hostRate.load(), hostBlock.load());
    pedal.swapIfReady (hostRate.load(), hostBlock.load());

    const int numIn = getTotalNumInputChannels();
    const int numOut = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int srcCh = (pInputCh->load() > 0.5f && numIn > 1) ? 1 : 0;

    for (int start = 0; start < numSamples;)
    {
        const int n = std::min (numSamples - start, (int) monoBuf.size());
        float* mono = monoBuf.data();
        float* left = leftBuf.data();
        float* right = rightBuf.data();
        if (numIn > 0 && ! inputMuted.load (std::memory_order_relaxed))
            std::copy_n (buffer.getReadPointer (srcCh, start), n, mono);
        else
            std::fill_n (mono, n, 0.0f);

        storeMax (inPeak, peakOf (mono, n));

        const bool tuning = tunerActive.load (std::memory_order_relaxed);
        if (tuning)
        {
            int w = tunerWrite.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) { tunerRing[(size_t) w] = mono[i]; w = (w + 1) & (kTunerRing - 1); }
            tunerWrite.store (w, std::memory_order_release);
        }

        processChunk (mono, left, right, n);

        // Silent while tuning (fades over 20 ms so it doesn't click).
        tunerGain.setTargetValue (tuning && tunerMute.load (std::memory_order_relaxed) ? 0.0f : 1.0f);
        if (tunerGain.isSmoothing() || tunerGain.getTargetValue() < 0.5f)
            for (int i = 0; i < n; ++i) { const float g = tunerGain.getNextValue(); left[i] *= g; right[i] *= g; }
        storeMax (outPeak, std::max (peakOf (left, n), peakOf (right, n)));

        if (numOut >= 2)
        {
            std::copy_n (left, n, buffer.getWritePointer (0, start));
            std::copy_n (right, n, buffer.getWritePointer (1, start));
            for (int ch = 2; ch < numOut; ++ch) std::copy_n (left, n, buffer.getWritePointer (ch, start));
        }
        else if (numOut == 1)
        {
            float* out = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i) out[i] = 0.5f * (left[i] + right[i]);
        }
        start += n;
    }
}

//==============================================================================
void SimpleAmpProcessor::startModelJob (EngineSlot& slot, juce::File file, bool normaliseLoudness)
{
    const bool eco = pEco->load() > 0.5f;
    loaderPool.addJob ([this, &slot, file, eco, normaliseLoudness]
    {
        std::unique_ptr<AmpEngine> engine;
        juce::String error;

        if (file != juce::File())
        {
            try
            {
                engine = std::make_unique<AmpEngine>();
                engine->model = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()),
                                              nam::DspLoadOptions { false });
                if (engine->model == nullptr)
                    throw std::runtime_error ("could not read model");
                if (engine->model->NumInputChannels() != 1)
                    throw std::runtime_error ("model is not mono-in");

                const double sr = engine->model->GetExpectedSampleRate();
                engine->modelRate = sr > 0 ? sr : 48000.0;
                // Amps are levelled to a common loudness. Pedals keep their real output level:
                // how hard a boost hits the amp *is* the sound.
                engine->levelGain = normaliseLoudness && engine->model->HasLoudness()
                                      ? sa::dbToGain (kTargetLoudnessDb - (float) engine->model->GetLoudness())
                                      : 1.0f;

                if (auto* slim = dynamic_cast<nam::SlimmableModel*> (engine->model.get()))
                {
                    engine->slimmable = true;
                    slim->SetSlimmableSize (eco ? 0.0 : 1.0);
                }

                const double rate = hostRate.load();
                const int block = hostBlock.load();
                engine->prepare (rate > 0 ? rate : 48000.0, block > 0 ? block : 512);
            }
            catch (const std::exception& e)
            {
                engine.reset();
                error = file.getFileName() + ": " + e.what();
            }
        }

        {
            const juce::ScopedLock il (infoLock);
            slot.error = error;
            if (error.isNotEmpty()) return; // keep the current model
            slot.file = file;
        }

        slot.slimmable = engine != nullptr && engine->slimmable;
        slot.latency = engine != nullptr ? engine->latency() : 0;

        // Hand over. If the audio thread hasn't taken the last one yet, this replaces it.
        std::unique_ptr<AmpEngine> toFree;
        {
            const juce::SpinLock::ScopedLockType sl (slot.lock);
            toFree = std::move (slot.pending);
            slot.pending = std::move (engine);
            slot.hasPending = true;
        }
    });
}

void SimpleAmpProcessor::loadModel (const juce::File& file) { startModelJob (amp, file, true); }
void SimpleAmpProcessor::loadPedal (const juce::File& file) { startModelJob (pedal, file, false); }

void SimpleAmpProcessor::loadImpulseResponse (const juce::File& file)
{
    const juce::ScopedLock il (infoLock);
    if (file.existsAsFile())
    {
        convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                         kMaxIRSamples, juce::dsp::Convolution::Normalise::yes);
        irFile = file;
        cabUseIR = true;
    }
    else
    {
        irFile = juce::File();
        cabUseIR = false; // built-in cab
    }
}

void SimpleAmpProcessor::loadReverbIR (const juce::File& file)
{
    const juce::ScopedLock il (infoLock);
    if (file.existsAsFile())
    {
        revConvolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes,
                                            kMaxReverbIRSamples, juce::dsp::Convolution::Normalise::yes);
        revFile = file;
    }
    else
    {
        revFile = juce::File();
    }
    if (auto* p = apvts.getParameter ("rev_ir")) p->setValueNotifyingHost (file.existsAsFile() ? 1.0f : 0.0f);
}

void SimpleAmpProcessor::updateLatency()
{
    const int lat = amp.latency.load() + pedal.latency.load();
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void SimpleAmpProcessor::timerCallback()
{
    for (auto* slot : { &amp, &pedal })
    {
        // Free engines the audio thread swapped out (never delete on the audio thread).
        std::unique_ptr<AmpEngine> toFree, staleEngine;
        bool stale = false;
        {
            const juce::SpinLock::ScopedLockType sl (slot->lock);
            toFree = std::move (slot->retired);
            // A load that raced with a sample-rate change was prepared for the old rate.
            stale = slot->hasPending && slot->pending != nullptr && hostRate.load() > 0
                    && (slot->pending->hostRate != hostRate.load() || slot->pending->hostBlock != hostBlock.load());
            if (stale) { staleEngine = std::move (slot->pending); slot->hasPending = false; }
        }
        if (stale)
        {
            juce::File f;
            { const juce::ScopedLock il (infoLock); f = slot->file; }
            startModelJob (*slot, f, slot == &amp);
        }
    }

    updateLatency();

    // Eco toggled: reload slimmable models at the new size.
    const bool eco = pEco->load() > 0.5f;
    if (eco != lastEco)
    {
        lastEco = eco;
        for (auto* slot : { &amp, &pedal })
        {
            juce::File f;
            { const juce::ScopedLock il (infoLock); f = slot->file; }
            if (slot->slimmable.load() && f.existsAsFile()) startModelJob (*slot, f, slot == &amp);
        }
    }
}

juce::File SimpleAmpProcessor::getModelFile() const  { const juce::ScopedLock il (infoLock); return amp.file; }
juce::File SimpleAmpProcessor::getPedalFile() const  { const juce::ScopedLock il (infoLock); return pedal.file; }
juce::File SimpleAmpProcessor::getCabFile() const    { const juce::ScopedLock il (infoLock); return irFile; }
juce::File SimpleAmpProcessor::getReverbFile() const { const juce::ScopedLock il (infoLock); return revFile; }

juce::String SimpleAmpProcessor::getModelName() const
{
    const auto f = getModelFile();
    return f == juce::File() ? juce::String ("Built-in (no .nam loaded)") : f.getFileNameWithoutExtension();
}

juce::String SimpleAmpProcessor::getIRName() const
{
    const auto f = getCabFile();
    return f == juce::File() ? juce::String ("Built-in cab") : f.getFileNameWithoutExtension();
}

juce::String SimpleAmpProcessor::getStatus() const
{
    const juce::ScopedLock il (infoLock);
    if (amp.error.isNotEmpty()) return "Amp error: " + amp.error;
    if (pedal.error.isNotEmpty()) return "Pedal error: " + pedal.error;
    return {};
}

//==============================================================================
int SimpleAmpProcessor::getNumPrograms() { return (int) std::size (kPresets); }

const juce::String SimpleAmpProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (kPresets[index].name) : juce::String();
}

void SimpleAmpProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    userPreset = juce::File();
    const auto& p = kPresets[index];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    // Cab and the pedalboard are left alone: they depend on the model and on your taste.
    set ("input", p.input); set ("gate", p.gate); set ("boost", p.boost); set ("drive", p.drive);
    set ("bass", p.bass); set ("mid", p.mid); set ("treble", p.treble); set ("output", p.output);
}

int SimpleAmpProcessor::copyTunerInput (float* dest, int n) const
{
    n = std::min (n, kTunerRing);
    const int end = tunerWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dest[i] = tunerRing[(size_t) ((end - n + i) & (kTunerRing - 1))];
    return n;
}

juce::File SimpleAmpProcessor::presetFolder() { return defaultFolder().getChildFile ("presets"); }

juce::Array<juce::File> SimpleAmpProcessor::findUserPresets()
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.simpleamp");
    files.sort();
    return files;
}

bool SimpleAmpProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    presetFolder().createDirectory();
    auto xml = createStateXml();
    xml->setAttribute ("presetName", name.trim());
    const auto file = presetFolder().getChildFile (clean + ".simpleamp");
    if (! xml->writeTo (file)) return false;
    userPreset = file;
    return true;
}

bool SimpleAmpProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    applyStateXml (*xml);
    userPreset = file;
    return true;
}

void SimpleAmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateXml())
    {
        xml->setAttribute ("userPreset", userPreset.getFullPathName());
        copyXmlToBinary (*xml, destData);
    }
}

std::unique_ptr<juce::XmlElement> SimpleAmpProcessor::createStateXml()
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock il (infoLock);
        state.setProperty ("modelPath", amp.file.getFullPathName(), nullptr);
        state.setProperty ("pedalPath", pedal.file.getFullPathName(), nullptr);
        state.setProperty ("irPath", irFile.getFullPathName(), nullptr);
        state.setProperty ("reverbPath", revFile.getFullPathName(), nullptr);
    }
    state.setProperty ("program", currentProgram, nullptr);
    state.removeProperty ("userPreset", nullptr);
    state.removeProperty ("presetName", nullptr);
    return state.createXml();
}

void SimpleAmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    applyStateXml (*xml);
    const juce::File preset (xml->getStringAttribute ("userPreset"));
    userPreset = preset.existsAsFile() ? preset : juce::File();
}

void SimpleAmpProcessor::applyStateXml (const juce::XmlElement& xml)
{
    auto state = juce::ValueTree::fromXml (xml);
    const juce::File modelPath (state.getProperty ("modelPath").toString());
    const juce::File pedalPath (state.getProperty ("pedalPath").toString());
    const juce::File irPath (state.getProperty ("irPath").toString());
    const juce::File revPath (state.getProperty ("reverbPath").toString());
    const bool revIR = (bool) state.getChildWithProperty ("id", "rev_ir").getProperty ("value", 0.0f);
    currentProgram = state.getProperty ("program", 0);
    apvts.replaceState (state);
    // A new amp file loads at the new Eco size anyway; the same file with a changed Eco is reloaded by the timer.
    if (modelPath != getModelFile()) lastEco = pEco->load() > 0.5f;

    auto reload = [] (const juce::File& wanted, const juce::File& current, auto&& load)
    {
        if (wanted == current) return;
        if (wanted.existsAsFile() || wanted == juce::File()) load (wanted.existsAsFile() ? wanted : juce::File());
    };
    reload (modelPath, getModelFile(), [this] (const juce::File& f) { loadModel (f); });
    reload (pedalPath, getPedalFile(), [this] (const juce::File& f) { loadPedal (f); });
    reload (irPath, getCabFile(), [this] (const juce::File& f) { loadImpulseResponse (f); });
    reload (revPath, getReverbFile(), [this] (const juce::File& f) { loadReverbIR (f); });
    if (auto* p = apvts.getParameter ("rev_ir")) p->setValueNotifyingHost (revIR && revPath.existsAsFile() ? 1.0f : 0.0f);
}

juce::AudioProcessorEditor* SimpleAmpProcessor::createEditor() { return new SimpleAmpEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SimpleAmpProcessor(); }

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP.h"
#include "Pedals.h"

namespace nam { class DSP; }
namespace dsp { template <typename T, int NCHANS, size_t A> class ResamplingContainer; }

// A loaded NAM model plus the resampler needed when the host rate differs
// from the rate the model was trained at. Built off the audio thread.
struct AmpEngine
{
    using Resampler = dsp::ResamplingContainer<float, 1, 12>;

    AmpEngine();
    ~AmpEngine();

    std::unique_ptr<nam::DSP> model;
    std::unique_ptr<Resampler> resampler;
    float levelGain = 1.0f;
    double modelRate = 48000.0, hostRate = 0.0;
    int hostBlock = 0, modelBlock = 0;
    bool slimmable = false;

    void prepare (double sampleRate, int maxBlock); // allocates; never on audio thread
    void process (float* in, float* out, int n);   // audio thread
    int latency() const;
};

// Hands NAM models from the loader thread to the audio thread without locking the audio thread.
struct EngineSlot
{
    std::unique_ptr<AmpEngine> active;   // audio thread only
    std::unique_ptr<AmpEngine> pending;  // guarded by lock
    std::unique_ptr<AmpEngine> retired;  // guarded by lock, freed on the message thread
    bool hasPending = false;             // guarded by lock
    juce::SpinLock lock;

    juce::File file;                     // guarded by the processor's infoLock
    juce::String error;                  // guarded by the processor's infoLock
    std::atomic<bool> slimmable { false };
    std::atomic<int> latency { 0 };

    void swapIfReady (double rate, int block); // audio thread
};

class SimpleAmpProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SimpleAmpProcessor();
    ~SimpleAmpProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SimpleAmp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the UI (message thread). An empty File means "none / built-in".
    void loadModel (const juce::File& file);
    void clearModel() { loadModel ({}); }
    void loadPedal (const juce::File& file);
    void loadImpulseResponse (const juce::File& file);
    void loadReverbIR (const juce::File& file);

    juce::AudioProcessorValueTreeState apvts;

    // For the editor.
    juce::String getModelName() const;
    juce::String getIRName() const;
    juce::String getStatus() const;
    juce::File getModelFile() const;
    juce::File getPedalFile() const;
    juce::File getCabFile() const;
    juce::File getReverbFile() const;
    bool isModelSlimmable() const { return amp.slimmable.load(); }
    double getCpuPercent() const { return loadMeasurer.getLoadAsProportion() * 100.0; }
    double getHostRate() const { return hostRate.load(); }
    int getHostBlock() const { return hostBlock.load(); }
    bool isIRLoaded() { return cabUseIR.load() && convolution.getCurrentIRSize() > 0; }

    // Peak levels since the last call (for meters); read from the message thread.
    float takeInputPeak() { return inPeak.exchange (0.0f); }
    float takeOutputPeak() { return outPeak.exchange (0.0f); }
    void setInputMuted (bool m) { inputMuted = m; } // standalone feedback guard

    // Tuner: the UI reads the most recent raw input; the output can be muted while tuning.
    void setTunerActive (bool on) { tunerActive = on; }
    void setTunerMute (bool on) { tunerMute = on; }
    bool isTunerMuted() const { return tunerMute.load(); }
    int copyTunerInput (float* dest, int n) const; // most recent n samples (n <= kTunerRing)
    static constexpr int kTunerRing = 8192;

    // User presets: the whole setup (knobs, pedals, amp/pedal/cab/reverb files) in ~/Music/SimpleAmp/presets.
    static juce::File presetFolder();
    static juce::Array<juce::File> findUserPresets();
    bool saveUserPreset (const juce::String& name); // message thread
    bool loadUserPreset (const juce::File& file);   // message thread
    juce::File getUserPreset() const { return userPreset; }
    void forgetUserPreset() { userPreset = juce::File(); }

    bool isModelActiveForTest() const { return amp.active != nullptr; } // only valid on the processing thread
    bool isPedalActiveForTest() const { return pedal.active != nullptr; }

    // Tone library: <root>/amps (or any other folder), <root>/pedals, <root>/cabs, <root>/reverbs.
    // Roots are the app bundle's Resources/tones (SimpleAmp Live) and ~/Music/SimpleAmp.
    static juce::File defaultFolder();
    static juce::Array<juce::File> libraryRoots();
    static juce::Array<juce::File> findLibraryFiles (const juce::String& kind); // "pedals", "cabs", "reverbs"
    static juce::Array<juce::File> findAmpFiles();          // every .nam outside pedals/ cabs/ reverbs/, sorted by pack
    static juce::String ampGroup (const juce::File& amp);   // pack (folder) name, or "My amps"
    static juce::String displayName (const juce::File& f);  // short name for menus
    // Loads an amp and sets Cab / TS Boost to suit it (full rigs include the cab; boosted captures already have a boost).
    void selectAmp (const juce::File& file);
    static bool isReservedFolder (const juce::String& name);
    static void createLibraryFolders();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void startModelJob (EngineSlot& slot, juce::File file, bool normaliseLoudness);
    void processChunk (float* mono, float* left, float* right, int n);
    void updateLatency();
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement& xml);
    std::atomic<float>* param (const char* id) { return apvts.getRawParameterValue (id); }

    // Parameters (cached raw pointers; read on the audio thread).
    std::atomic<float>* pInput = nullptr; std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pBoost = nullptr; std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pBass = nullptr;  std::atomic<float>* pMid = nullptr;
    std::atomic<float>* pTreble = nullptr; std::atomic<float>* pCab = nullptr;
    std::atomic<float>* pOutput = nullptr; std::atomic<float>* pEco = nullptr;
    std::atomic<float>* pInputCh = nullptr;
    std::atomic<float>* pCompOn = nullptr; std::atomic<float>* pCompSustain = nullptr; std::atomic<float>* pCompLevel = nullptr;
    std::atomic<float>* pOctOn = nullptr; std::atomic<float>* pOctSub = nullptr; std::atomic<float>* pOctUp = nullptr; std::atomic<float>* pOctDry = nullptr;
    std::atomic<float>* pWahOn = nullptr; std::atomic<float>* pWahSens = nullptr; std::atomic<float>* pWahReso = nullptr; std::atomic<float>* pWahMix = nullptr;
    std::atomic<float>* pPedalOn = nullptr; std::atomic<float>* pPedalLevel = nullptr;
    std::atomic<float>* pChorusOn = nullptr; std::atomic<float>* pChorusRate = nullptr; std::atomic<float>* pChorusDepth = nullptr; std::atomic<float>* pChorusMix = nullptr;
    std::atomic<float>* pFlangerOn = nullptr; std::atomic<float>* pFlangerRate = nullptr; std::atomic<float>* pFlangerDepth = nullptr; std::atomic<float>* pFlangerRegen = nullptr;
    std::atomic<float>* pLimitOn = nullptr; std::atomic<float>* pLimitCeiling = nullptr; std::atomic<float>* pLimitLevel = nullptr;
    std::atomic<float>* pGeqOn = nullptr; std::atomic<float>* pGeq[sa::GraphicEQ::kBands] {}; std::atomic<float>* pGeqLevel = nullptr;
    std::atomic<float>* pDelayOn = nullptr; std::atomic<float>* pDelayTime = nullptr; std::atomic<float>* pDelayFb = nullptr; std::atomic<float>* pDelayMix = nullptr;
    std::atomic<float>* pRevOn = nullptr; std::atomic<float>* pRevSize = nullptr; std::atomic<float>* pRevTone = nullptr; std::atomic<float>* pRevMix = nullptr; std::atomic<float>* pRevIR = nullptr;

    // DSP chain
    sa::Gate gate;
    sa::Compressor comp;   sa::Fader compF;
    sa::Octaver octaver;   sa::Fader octF;
    sa::AutoWah wah;       sa::Fader wahF;
    sa::TightBoost boost;  sa::Fader boostF;
    sa::Fader pedalF;
    sa::ModDelay chorus;   sa::Fader chorusF;
    sa::ModDelay flanger;  sa::Fader flangerF;
    sa::Limiter limiter;   sa::Fader limitF;
    sa::FallbackAmp fallbackAmp;
    sa::FallbackCab fallbackCab;
    sa::ThreeBandEQ eq;
    sa::GraphicEQ geq;     sa::Fader geqF;
    sa::Delay delay;       sa::Fader delayF;
    juce::Reverb reverb;   sa::Fader revF;
    juce::Reverb::Parameters revParams;
    juce::dsp::Convolution convolution;   // cab IR
    juce::dsp::Convolution revConvolution; // reverb IR
    juce::SmoothedValue<float> inGain, outGain;
    std::vector<float> monoBuf, ampBuf, pedalBuf, leftBuf, rightBuf, revL, revR;
    float lastDrive = -1.0f;
    int delayTail = 0, revTail = 0;
    double sampleRate = 48000.0;
    juce::AudioProcessLoadMeasurer loadMeasurer;

    EngineSlot amp, pedal;

    std::atomic<double> hostRate { 0.0 };
    std::atomic<int> hostBlock { 0 };
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<bool> inputMuted { false };
    std::atomic<bool> cabUseIR { false };
    std::vector<float> tunerRing = std::vector<float> ((size_t) kTunerRing, 0.0f);
    std::atomic<int> tunerWrite { 0 };
    std::atomic<bool> tunerActive { false }, tunerMute { true };
    juce::SmoothedValue<float> tunerGain;
    juce::File userPreset; // message thread
    bool lastEco = false;

    mutable juce::CriticalSection infoLock;
    juce::File irFile, revFile;

    int currentProgram = 0;

    juce::ThreadPool loaderPool { 1 }; // declared last: joined first on destruction

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleAmpProcessor)
};

// Headless checks for SimpleAmp:
//  1. DSP with real NAM models at 48k and 44.1k: finite output, gate works, CPU cost.
//  2. The built VST3 and AU binaries load in a host and produce sound.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Pedals.h"
#include "../Source/Tuner.h"
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s\n", msg); } } while (0)

// Plucked-ish low E: decaying saw, 150 ms note then 150 ms of near-silence.
static void fillGuitar (float* x, int n, double sr, int64_t& t)
{
    juce::Random rng (1234 + (int) t);
    for (int i = 0; i < n; ++i, ++t)
    {
        const double s = (double) t / sr;
        const double inCycle = std::fmod (s, 0.30);
        const double env = inCycle < 0.15 ? std::exp (-inCycle * 12.0) : 0.0;
        const double saw = 2.0 * std::fmod (s * 82.41, 1.0) - 1.0;
        x[i] = (float) (0.25 * env * saw + 0.0003 * (rng.nextFloat() * 2 - 1)); // ~ -70 dB hum/noise floor
    }
}

struct Result { double cpuPct, noteRms, gapRms; bool finite; };

static Result run (juce::AudioProcessor& p, double sr, int block, double seconds)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    int64_t t = 0;
    const int total = (int) (sr * seconds);
    double noteSum = 0, gapSum = 0; int noteN = 0, gapN = 0; bool finite = true;
    double cpuSec = 0;

    for (int done = 0; done < total; done += block)
    {
        fillGuitar (buf.getWritePointer (0), block, sr, t);
        buf.copyFrom (1, 0, buf, 0, 0, block);
        const auto t0 = juce::Time::getHighResolutionTicks();
        p.processBlock (buf, midi);
        cpuSec += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);

        const float* y = buf.getReadPointer (0);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (y[i])) finite = false;
            const double pos = std::fmod ((double) (t - block + i) / sr, 0.30);
            if (done < sr * 0.5) continue; // skip warm-up
            if (pos > 0.01 && pos < 0.12) { noteSum += y[i] * y[i]; ++noteN; }
            else if (pos > 0.26)          { gapSum += y[i] * y[i]; ++gapN; }
        }
    }
    return { 100.0 * cpuSec / seconds, std::sqrt (noteSum / std::max (1, noteN)), std::sqrt (gapSum / std::max (1, gapN)), finite };
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void testDsp (const juce::String& modelName, double sr, bool eco, bool synthetic = false)
{
    SimpleAmpProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 64);
    p.prepareToPlay (sr, 64);
    if (auto* e = p.apvts.getParameter ("eco")) e->setValueNotifyingHost (eco ? 1.0f : 0.0f);
    pump (200);

    const juce::File model = juce::File::isAbsolutePath (modelName) ? juce::File (modelName)
                                                                    : juce::File (SIMPLEAMP_TEST_MODELS).getChildFile (modelName);
    if (modelName.isNotEmpty())
    {
        p.loadModel (model);
        juce::AudioBuffer<float> silence (2, 64); juce::MidiBuffer m;
        for (int i = 0; i < 400 && ! p.isModelActiveForTest(); ++i) { silence.clear(); p.processBlock (silence, m); pump (10); }
        CHECK (p.isModelActiveForTest(), "model did not become active");
        pump (250); // let the processor's timer publish latency
        CHECK (p.getStatus().isEmpty(), p.getStatus().toRawUTF8());
    }

    const auto r = run (p, sr, 64, 6.0);

    // What the model outputs for digital silence: a pre-amp gate cannot remove this.
    double idle = 0;
    {
        juce::AudioBuffer<float> z (2, 64); juce::MidiBuffer m; double sum = 0; int cnt = 0;
        for (int b = 0; b < (int) (sr / 64); ++b)
        {
            z.clear(); p.processBlock (z, m);
            if (b > (int) (sr / 128)) for (int i = 0; i < 64; ++i) { sum += z.getSample (0, i) * z.getSample (0, i); ++cnt; }
        }
        idle = std::sqrt (sum / std::max (1, cnt));
    }
    std::printf ("  %-28s %5.1f kHz eco=%d  CPU %5.1f%% of one core  note %6.1f dBFS  gap %6.1f dBFS  idle %6.1f  latency %d  slim=%d\n",
                 modelName.isEmpty() ? "(built-in amp)" : juce::File::isAbsolutePath (modelName) ? juce::File (modelName).getFileNameWithoutExtension().substring (0, 28).toRawUTF8() : modelName.toRawUTF8(), sr / 1000.0, (int) eco, r.cpuPct,
                 juce::Decibels::gainToDecibels (r.noteRms, -150.0), juce::Decibels::gainToDecibels (r.gapRms, -150.0),
                 juce::Decibels::gainToDecibels (idle, -150.0), p.getLatencySamples(), (int) p.isModelSlimmable());
    CHECK (r.finite, "non-finite output");
    CHECK (r.noteRms > (eco ? 0.001 : 0.01), "output too quiet while playing");
    // Untrained example models make sound from silence, so the gate can't be judged on them.
    if (! synthetic) CHECK (r.gapRms < std::max (r.noteRms * 0.0178, idle * 2.0) /* >= 35 dB, or down at the model idle floor */, "gate did not close between notes");
    CHECK (r.cpuPct < 25.0, "CPU too high");
    CHECK (std::abs (sr - 48000.0) < 1 || modelName.isEmpty() || p.getLatencySamples() > 0, "resampling latency not reported");
    p.releaseResources();
    pump (200);
}

static void testPlugin (juce::AudioPluginFormat& format, const juce::PluginDescription& desc)
{
    juce::String err;
    auto inst = format.createInstanceFromDescription (desc, 48000.0, 128, err);
    CHECK (inst != nullptr, ("could not instantiate " + desc.pluginFormatName + ": " + err).toRawUTF8());
    if (inst == nullptr) return;

    inst->enableAllBuses();
    inst->setPlayConfigDetails (2, 2, 48000.0, 128);
    inst->prepareToPlay (48000.0, 128);
    const auto r = run (*inst, 48000.0, 128, 3.0);
    std::printf ("  %-10s '%s'  params=%d programs=%d  CPU %.1f%%  note %.1f dBFS\n", desc.pluginFormatName.toRawUTF8(),
                 inst->getName().toRawUTF8(), (int) inst->getParameters().size(), inst->getNumPrograms(), r.cpuPct,
                 juce::Decibels::gainToDecibels (r.noteRms, -150.0));
    CHECK (r.finite && r.noteRms > 0.01, "plugin produced no sound");
    CHECK (inst->hasEditor(), "no editor");
    if (auto* ed = inst->createEditorIfNeeded()) { pump (300); delete ed; }

    // State round trip
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (state.getSize() > 0, "empty state");
    inst->releaseResources();
}


//==============================================================================
static double rms (const std::vector<float>& v, size_t from = 0)
{
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return std::sqrt (s / std::max<size_t> (1, v.size() - from));
}
static std::vector<float> sine (double freq, double amp, int n, double sr = 48000.0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * juce::MathConstants<double>::pi * freq * i / sr));
    return v;
}
static double zeroCrossingFreq (const std::vector<float>& v, size_t from, double sr = 48000.0)
{
    int crossings = 0;
    for (size_t i = from + 1; i < v.size(); ++i) if (v[i - 1] <= 0 && v[i] > 0) ++crossings;
    return crossings * sr / (double) (v.size() - from);
}
template <typename P> static std::vector<float> runPedal (P& p, std::vector<float> x) { for (auto& s : x) s = p.process (s); return x; }

static void testPedalUnits()
{
    const double sr = 48000.0;
    std::printf ("Pedal DSP:\n");
    {
        sa::Octaver o; o.prepare (sr); o.set (1.0f, 0.0f, 0.0f);
        auto y = runPedal (o, sine (110.0, 0.3, 48000));
        const double f = zeroCrossingFreq (y, 4800);
        std::printf ("  octaver: 110 Hz in -> %.1f Hz out (sub only)\n", f);
        CHECK (std::abs (f - 55.0) < 3.0, "octaver sub is not an octave down");
    }
    {
        sa::Delay d; d.prepare (sr, 2.0); d.set (4800.0f, 0.0f);
        std::vector<float> x (20000, 0.0f); x[0] = 1.0f;
        auto y = runPedal (d, x);
        const auto peakAt = std::max_element (y.begin(), y.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - y.begin();
        std::printf ("  delay: 100 ms setting -> echo at %.1f ms\n", peakAt * 1000.0 / sr);
        CHECK (std::abs ((double) peakAt - 4800.0) <= 2.0, "delay echo at wrong time");
    }
    {
        sa::Compressor c; c.prepare (sr); c.set (0.8f, 0.0f);
        const double loud = rms (runPedal (c, sine (220, 0.5, 24000)), 12000);
        c.prepare (sr);
        const double quiet = rms (runPedal (c, sine (220, 0.05, 24000)), 12000);
        const double ratioDb = 20 * std::log10 (loud / quiet);
        std::printf ("  compressor: 20 dB input difference -> %.1f dB output difference\n", ratioDb);
        CHECK (ratioDb < 10.0, "compressor is not compressing");
    }
    {
        sa::Limiter l; l.prepare (sr); l.set (-12.0f, 0.0f);
        auto y = runPedal (l, sine (220, 1.0, 24000));
        float pk = 0; for (size_t i = 2400; i < y.size(); ++i) pk = std::max (pk, std::abs (y[i]));
        std::printf ("  limiter: 0 dBFS in, -12 dB ceiling -> %.1f dBFS peak out\n", juce::Decibels::gainToDecibels (pk));
        CHECK (juce::Decibels::gainToDecibels (pk) < -11.0f, "limiter exceeds ceiling");
    }
    {
        sa::GraphicEQ e; e.prepare (sr);
        float gains[7] = { 0, 0, 0, 12, 0, 0, 0 };
        e.set (gains, 0.0f);
        const double g = 20 * std::log10 (rms (runPedal (e, sine (800, 0.1, 24000)), 4800) / rms (sine (800, 0.1, 24000), 4800));
        std::printf ("  graphic EQ: +12 dB at 800 Hz -> %.1f dB\n", g);
        CHECK (std::abs (g - 12.0) < 1.0, "graphic EQ band gain wrong");
    }
    for (int kind = 0; kind < 3; ++kind)
    {
        auto x = sine (220, 0.3, 48000);
        std::vector<float> y;
        const char* name = kind == 0 ? "chorus" : kind == 1 ? "flanger" : "autowah";
        if (kind < 2) { sa::ModDelay m; m.prepare (sr, 0.03); m.set (kind == 0 ? 0.8f : 0.25f, kind == 0 ? 7.0f : 0.3f, 3.0f, kind == 0 ? 0.0f : 0.5f, 0.5f); y = runPedal (m, x); }
        else { sa::AutoWah w; w.prepare (sr); w.set (0.5f, 0.5f, 1.0f); y = runPedal (w, x); }
        double diff = 0; bool finite = true;
        for (size_t i = 0; i < y.size(); ++i) { diff += std::abs (y[i] - x[i]); finite &= std::isfinite (y[i]); }
        std::printf ("  %s: changes the signal (mean |diff| %.3f), finite=%d\n", name, diff / y.size(), (int) finite);
        CHECK (finite && diff / y.size() > 0.01, "modulation pedal has no effect");
    }
}

static void setParam (SimpleAmpProcessor& p, const char* id, float value)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

static juce::File writeTestReverbIR()
{
    // 1.5 s stereo exponentially decaying noise.
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("simpleamp_test_reverb.wav");
    const int n = 72000;
    juce::AudioBuffer<float> ir (2, n);
    juce::Random r (7);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) ir.setSample (ch, i, (r.nextFloat() * 2 - 1) * std::exp (-4.0f * i / (float) n));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0)) { os.release(); w->writeFromAudioSampleBuffer (ir, 0, n); delete w; }
    return f;
}

static void testBoard (const juce::File& ampModel)
{
    std::printf ("Pedalboard through the processor (48 kHz / 64):\n");

    // Tails: reverb and delay must add sound in the gaps between notes.
    for (int mode = 0; mode < 3; ++mode)
    {
        SimpleAmpProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 64);
        p.prepareToPlay (48000.0, 64);
        const char* name = mode == 0 ? "dry" : mode == 1 ? "delay" : "reverb";
        if (mode == 1) setParam (p, "delay_on", 1.0f);
        if (mode == 2) setParam (p, "rev_on", 1.0f);
        const auto r = run (p, 48000.0, 64, 4.0);
        std::printf ("  %-7s note %6.1f dBFS  gap %6.1f dBFS\n", name, juce::Decibels::gainToDecibels (r.noteRms, -150.0),
                     juce::Decibels::gainToDecibels (r.gapRms, -150.0));
        CHECK (r.finite, "non-finite output");
        if (mode > 0) CHECK (r.gapRms > 0.003, "no delay/reverb tail in the gaps");

        // Switch it off and check that the tail rings on, then dies.
        if (mode > 0)
        {
            setParam (p, mode == 1 ? "delay_on" : "rev_on", 0.0f);
            juce::AudioBuffer<float> z (2, 64); juce::MidiBuffer m;
            double first = 0, late = 0;
            for (int b = 0; b < 48000 * 6 / 64; ++b)
            {
                z.clear(); p.processBlock (z, m);
                const double e = z.getRMSLevel (0, 0, 64);
                if (b < 48000 / 4 / 64) first = std::max (first, e);
                if (b > 48000 * 5 / 64) late = std::max (late, e);
            }
            std::printf ("          after switching off: tail %.1f dBFS, 5 s later %.1f dBFS\n",
                         juce::Decibels::gainToDecibels (first, -150.0), juce::Decibels::gainToDecibels (late, -150.0));
            CHECK (first > 0.001, "tail was cut when switching off");
            CHECK (late < 0.0005, "tail never stops");
        }
    }

    // Everything on, real amp + NAM pedal + reverb IR: CPU budget.
    SimpleAmpProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 64);
    p.prepareToPlay (48000.0, 64);
    p.loadModel (ampModel);
    p.loadPedal (juce::File (SIMPLEAMP_TEST_MODELS).getChildFile ("lstm.nam"));
    p.loadReverbIR (writeTestReverbIR());
    for (auto id : { "comp_on", "oct_on", "wah_on", "boost", "pedal_on", "chorus_on", "flanger_on", "limit_on", "geq_on", "delay_on", "rev_on" })
        setParam (p, id, 1.0f);
    juce::AudioBuffer<float> silence (2, 64); juce::MidiBuffer m;
    for (int i = 0; i < 400 && ! (p.isModelActiveForTest() && p.isPedalActiveForTest()); ++i) { silence.clear(); p.processBlock (silence, m); pump (10); }
    pump (300);
    CHECK (p.isModelActiveForTest() && p.isPedalActiveForTest(), "amp or pedal model not active");
    CHECK (p.getReverbFile().existsAsFile() && p.apvts.getRawParameterValue ("rev_ir")->load() > 0.5f, "reverb IR not selected");
    CHECK (p.getStatus().isEmpty(), p.getStatus().toRawUTF8());

    for (bool eco : { false, true })
    {
        setParam (p, "eco", eco ? 1.0f : 0.0f);
        for (int i = 0; i < 50; ++i) { silence.clear(); p.processBlock (silence, m); pump (10); }
        const auto r = run (p, 48000.0, 64, 5.0);
        std::printf ("  ALL pedals + %s amp (eco=%d) + NAM pedal + IR reverb: CPU %.1f%% of one core, note %.1f dBFS, finite=%d\n",
                     ampModel.getFileNameWithoutExtension().substring (0, 20).toRawUTF8(), (int) eco, r.cpuPct,
                     juce::Decibels::gainToDecibels (r.noteRms, -150.0), (int) r.finite);
        CHECK (r.finite && r.noteRms > 0.01, "full board produced no sound");
        CHECK (r.cpuPct < 35.0, "full board CPU too high");
    }

    // State round trip keeps pedal settings and files.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SimpleAmpProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    pump (300);
    CHECK (q.getPedalFile() == p.getPedalFile(), "pedal file not restored");
    CHECK (q.getReverbFile() == p.getReverbFile(), "reverb IR not restored");
    CHECK (q.apvts.getRawParameterValue ("flanger_on")->load() > 0.5f, "pedal on/off not restored");
    CHECK (q.apvts.getRawParameterValue ("rev_ir")->load() > 0.5f, "reverb IR mode not restored");
}

//==============================================================================
// Plucked-string-like test tone: saw-ish harmonics that decay, plus a little noise.
static std::vector<float> pluck (double f0, int n, double sr, unsigned seed = 1)
{
    std::vector<float> v ((size_t) n);
    juce::Random r ((juce::int64) seed);
    for (int i = 0; i < n; ++i)
    {
        double x = 0;
        for (int h = 1; h <= 12; ++h)
            x += std::sin (2 * juce::MathConstants<double>::pi * f0 * h * i / sr) / h * std::exp (-0.8 * h * i / sr);
        v[(size_t) i] = (float) (0.3 * x + 0.002 * (r.nextFloat() * 2 - 1));
    }
    return v;
}

static void testTuner()
{
    std::printf ("Tuner:\n");
    sa::PitchDetector det;
    const std::pair<const char*, double> strings[] = {
        { "F#1 (8-string)", 46.25 }, { "A1 (drop A)", 55.00 }, { "B1 (7-string)", 61.74 }, { "D2 (drop D)", 73.42 },
        { "E2", 82.41 }, { "A2", 110.00 }, { "D3", 146.83 }, { "G3", 196.00 }, { "B3", 246.94 }, { "E4", 329.63 }, { "E6 (24th fret)", 1318.5 } };
    for (double sr : { 48000.0, 44100.0 })
    {
        float worst = 0;
        for (auto& [name, f] : strings)
        {
            const auto x = pluck (f, 4096 + 2000, sr);
            const float hz = det.detect (x.data() + 2000, 4096, sr);
            const float cents = hz > 0 ? 1200.0f * std::log2 (hz / (float) f) : 9999.0f;
            worst = std::max (worst, std::abs (cents));
            if (std::abs (cents) > 2.0f) std::printf ("  %s at %.0f Hz rate: got %.2f Hz (%.1f cents off)\n", name, sr, hz, cents);
            CHECK (std::abs (cents) < 2.0f, "tuner off by more than 2 cents");
        }
        std::printf ("  %.1f kHz: 11 notes F#1 (46 Hz) .. E6 (1319 Hz), worst error %.2f cents\n", sr / 1000.0, worst);
    }
    {
        std::vector<float> silence (4096, 0.0f), noise (4096);
        juce::Random r (3);
        for (auto& v : noise) v = 0.1f * (r.nextFloat() * 2 - 1);
        const float a = det.detect (silence.data(), 4096, 48000.0), b = det.detect (noise.data(), 4096, 48000.0);
        std::printf ("  silence -> %.1f Hz, white noise -> %.1f Hz (0 = no note shown)\n", a, b);
        CHECK (a == 0.0f && b == 0.0f, "tuner shows a note for silence/noise");
    }
    {
        const auto n = sa::PitchDetector::toNote (82.41f), m = sa::PitchDetector::toNote (440.0f * std::pow (2.0f, 10.0f / 1200.0f));
        std::printf ("  82.41 Hz -> %s%d %+.1f c; 440 Hz +10 c -> %s%d %+.1f c\n", n.name.c_str(), n.octave, n.cents, m.name.c_str(), m.octave, m.cents);
        CHECK (n.name == "E" && n.octave == 2 && std::abs (m.cents - 10.0f) < 0.1f && m.name == "A" && m.octave == 4, "note naming wrong");
    }

    // Through the processor: raw input reaches the tuner, output is muted while tuning.
    SimpleAmpProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 64);
    p.prepareToPlay (48000.0, 64);
    p.setTunerActive (true);
    const auto x = pluck (82.41, 48000, 48000.0);
    juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer m;
    double outEnergy = 0;
    for (int b = 0; b + 64 <= (int) x.size(); b += 64)
    {
        buf.copyFrom (0, 0, x.data() + b, 64); buf.copyFrom (1, 0, x.data() + b, 64);
        p.processBlock (buf, m);
        if (b > 4800) outEnergy += buf.getRMSLevel (0, 0, 64);
    }
    std::vector<float> ring (4096);
    const float hz = det.detect (ring.data(), p.copyTunerInput (ring.data(), 4096), 48000.0);
    std::printf ("  processor: low E through the tuner -> %.2f Hz, output while tuning %s\n", hz, outEnergy < 1e-6 ? "muted" : "NOT muted");
    CHECK (std::abs (1200.0f * std::log2 (hz / 82.41f)) < 2.0f, "tuner via processor wrong");
    CHECK (outEnergy < 1e-6, "output not muted while tuning");
}

static void testPresets (const juce::File& ampModel)
{
    std::printf ("Presets:\n");
    const juce::String name = "__simpleamp_test_preset__";
    SimpleAmpProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 64);
    p.prepareToPlay (48000.0, 64);
    p.loadModel (ampModel);
    p.loadPedal (juce::File (SIMPLEAMP_TEST_MODELS).getChildFile ("lstm.nam"));
    setParam (p, "gate", -48.0f); setParam (p, "delay_on", 1.0f); setParam (p, "delay_time", 437.0f);
    setParam (p, "geq_400", -6.0f); setParam (p, "chorus_on", 1.0f);
    pump (1500);
    CHECK (p.saveUserPreset (name), "save failed");
    const auto file = SimpleAmpProcessor::presetFolder().getChildFile (name + ".simpleamp");
    CHECK (file.existsAsFile() && SimpleAmpProcessor::findUserPresets().contains (file), "preset file not listed");

    SimpleAmpProcessor q; // a fresh instance, e.g. another LUNA session
    q.setPlayConfigDetails (2, 2, 48000.0, 64);
    q.prepareToPlay (48000.0, 64);
    CHECK (q.loadUserPreset (file), "load failed");
    pump (1500);
    auto v = [&] (const char* id) { return q.apvts.getRawParameterValue (id)->load(); };
    std::printf ("  saved + loaded in a new instance: gate %.0f, delay %s %.0f ms, EQ 400 %.0f dB, chorus %s, amp %s, pedal %s\n",
                 v ("gate"), v ("delay_on") > 0.5f ? "on" : "off", v ("delay_time"), v ("geq_400"), v ("chorus_on") > 0.5f ? "on" : "off",
                 q.getModelFile() == ampModel ? "ok" : "WRONG", q.getPedalFile().getFileName().toRawUTF8());
    CHECK (std::abs (v ("gate") + 48.0f) < 0.2f && v ("delay_on") > 0.5f && std::abs (v ("delay_time") - 437.0f) < 1.0f
           && std::abs (v ("geq_400") + 6.0f) < 0.2f && v ("chorus_on") > 0.5f, "preset parameters not restored");
    CHECK (q.getModelFile() == ampModel && q.getPedalFile().getFileName() == "lstm.nam", "preset files not restored");
    CHECK (q.getUserPreset() == file, "current preset not tracked");

    // The session state remembers which preset is selected.
    juce::MemoryBlock st; q.getStateInformation (st);
    SimpleAmpProcessor r; r.setStateInformation (st.getData(), (int) st.getSize());
    CHECK (r.getUserPreset() == file, "selected preset not restored with the session");
    pump (500);
    file.deleteFile();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    std::printf ("DSP (64-sample blocks):\n");
    for (double sr : { 48000.0, 44100.0 })
    {
        testDsp ({}, sr, false);
        testDsp ("A2.nam", sr, false);
        testDsp ("wavenet_a1_standard.nam", sr, false, true);
        testDsp ("lstm.nam", sr, false);
    }
    testDsp ("slimmable_container.nam", 48000.0, false);
    testDsp ("slimmable_container.nam", 48000.0, true);

    std::printf ("Your tones (first of each pack):\n");
    for (auto& pack : juce::File (SIMPLEAMP_TONES_DIR).findChildFiles (juce::File::findDirectories, false))
    {
        auto files = pack.findChildFiles (juce::File::findFiles, false, "*.nam");
        files.sort();
        if (files.isEmpty()) continue;
        testDsp (files[0].getFullPathName(), 48000.0, false);
        testDsp (files[0].getFullPathName(), 48000.0, true);
    }

    {
        // Plugin context (not inside SimpleAmp Live): must still see the tones bundled in the installed app.
        const auto amps = SimpleAmpProcessor::findAmpFiles();
        juce::StringArray groups;
        for (auto& f : amps) groups.addIfNotAlreadyThere (SimpleAmpProcessor::ampGroup (f));
        std::printf ("Amp library as the plugin sees it: %d amps in %d groups (%s)\n", amps.size(), groups.size(),
                     groups.joinIntoString (" | ").toRawUTF8());
        CHECK (amps.size() >= 47, "plugin does not see the bundled tones");
        for (auto& f : amps) CHECK (! f.getFullPathName().contains ("/pedals/"), "pedal listed as amp");

        SimpleAmpProcessor p;
        for (auto& f : amps)
            if (f.getFileName().contains ("Maxon"))
            {
                setParam (p, "cab", 1.0f); setParam (p, "boost", 1.0f);
                p.selectAmp (f);
                std::printf ("  selectAmp(%s): cab=%d boost=%d (expect 0 0: full rig, boosted)\n",
                             SimpleAmpProcessor::displayName (f).toRawUTF8(),
                             (int) p.apvts.getRawParameterValue ("cab")->load(), (int) p.apvts.getRawParameterValue ("boost")->load());
                CHECK (p.apvts.getRawParameterValue ("cab")->load() < 0.5f && p.apvts.getRawParameterValue ("boost")->load() < 0.5f,
                       "selectAmp did not set cab/boost for a boosted full rig");
                break;
            }
        pump (300);
    }
    testPedalUnits();
    {
        auto packs = juce::File (SIMPLEAMP_TONES_DIR).findChildFiles (juce::File::findFiles, true, "*.nam");
        packs.sort();
        testBoard (packs.isEmpty() ? juce::File (SIMPLEAMP_TEST_MODELS).getChildFile ("A2.nam") : packs[0]);
    }

    testTuner();
    {
        auto packs = juce::File (SIMPLEAMP_TONES_DIR).findChildFiles (juce::File::findFiles, true, "*.nam");
        packs.sort();
        testPresets (packs.isEmpty() ? juce::File (SIMPLEAMP_TEST_MODELS).getChildFile ("A2.nam") : packs[0]);
    }

    std::printf ("Plugin binaries:\n");
    {
        juce::VST3PluginFormat vst3;
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, home.getChildFile ("Library/Audio/Plug-Ins/VST3/SimpleAmp.vst3").getFullPathName());
        CHECK (found.size() == 1, "VST3 not found");
        if (! found.isEmpty()) testPlugin (vst3, *found[0]);
    }
    {
        // Register the installed AU in-process so we don't depend on the system AU cache.
        const auto exe = home.getChildFile ("Library/Audio/Plug-Ins/Components/SimpleAmp.component/Contents/MacOS/SimpleAmp");
        void* lib = dlopen (exe.getFullPathName().toRawUTF8(), RTLD_NOW);
        auto factory = lib != nullptr ? (AudioComponentFactoryFunction) dlsym (lib, "SimpleAmpAUFactory") : nullptr;
        CHECK (factory != nullptr, "AU factory symbol missing");
        if (factory != nullptr)
        {
            AudioComponentDescription d { kAudioUnitType_Effect, 'SAmp', 'Trnm', 0, 0 };
            AudioComponentRegister (&d, CFSTR ("Tornmark: SimpleAmp"), 0x10000, factory);
            juce::AudioUnitPluginFormat au;
            juce::OwnedArray<juce::PluginDescription> found;
            au.findAllTypesForFile (found, "AudioUnit:Effects/aufx,SAmp,Trnm");
            CHECK (found.size() == 1, "AU not found");
            if (! found.isEmpty()) testPlugin (au, *found[0]);
        }
    }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

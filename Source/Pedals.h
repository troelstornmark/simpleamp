// Built-in pedals for SimpleAmp. All allocation happens in prepare(); process() is real-time safe.
// These are the effects a NAM capture can't reproduce (anything with an LFO, envelope, pitch or
// time dependence), plus dynamics and EQ that are cheaper to do directly.
#pragma once

#include "DSP.h"
#include <vector>

namespace sa
{
// Click-free on/off: ramps between dry (0) and effect (1) over 10 ms.
struct Fader
{
    float g = 0, target = 0, step = 0.001f;
    void prepare (double fs, bool on) { step = (float) (1.0 / (0.010 * fs)); g = target = on ? 1.0f : 0.0f; }
    void set (bool on) { target = on ? 1.0f : 0.0f; }
    bool active() const { return g > 0.0f || target > 0.0f; }
    inline float next()
    {
        if (g < target) g = std::min (target, g + step);
        else if (g > target) g = std::max (target, g - step);
        return g;
    }
};

inline float onePoleCoef (double fs, double seconds) { return (float) std::exp (-1.0 / (seconds * fs)); }

//==============================================================================
// Compressor (Dyna-Comp style): one knob for how much, one for level.
struct Compressor
{
    float env = 0, att = 0, rel = 0, thrDb = -30, slope = 0.75f, makeup = 1;
    void prepare (double fs) { att = onePoleCoef (fs, 0.005); rel = onePoleCoef (fs, 0.15); env = 0; }
    void set (float sustain01, float levelDb)
    {
        thrDb = -12.0f - 36.0f * sustain01;
        const float ratio = 3.0f + 7.0f * sustain01;
        slope = 1.0f - 1.0f / ratio;
        makeup = dbToGain (levelDb);
    }
    inline float process (float x)
    {
        const float a = std::abs (x);
        env = a + (env - a) * (a > env ? att : rel);
        const float lvl = 20.0f * std::log10 (env + 1e-9f);
        const float grDb = lvl > thrDb ? (lvl - thrDb) * slope : 0.0f;
        return x * dbToGain (-grDb) * makeup;
    }
};

// Brick-wall-ish peak limiter.
struct Limiter
{
    float env = 0, att = 0, rel = 0, ceiling = 0.25f, makeup = 1;
    void prepare (double fs) { att = onePoleCoef (fs, 0.0003); rel = onePoleCoef (fs, 0.06); env = 0; }
    void set (float ceilingDb, float levelDb) { ceiling = dbToGain (ceilingDb); makeup = dbToGain (levelDb); }
    inline float process (float x)
    {
        const float a = std::abs (x);
        env = a + (env - a) * (a > env ? att : rel);
        const float g = env > ceiling ? ceiling / env : 1.0f;
        return x * g * makeup;
    }
};

//==============================================================================
// Analog-style octaver (OC-2 idea): a flip-flop toggled at each zero crossing of the
// low-passed signal gives an octave down; full-wave rectification gives an octave up.
struct Octaver
{
    Biquad lp1, lp2, subLp, upHp, upLp, dcBlock;
    float env = 0, envCoef = 0, flip = 1;
    bool positive = false;
    float sub = 0.6f, up = 0.0f, dry = 0.8f;

    void prepare (double fs)
    {
        lp1.lowpass (fs, 500.0, 0.7); lp2.lowpass (fs, 500.0, 0.7);
        subLp.lowpass (fs, 900.0, 0.7);
        upHp.highpass (fs, 150.0); upLp.lowpass (fs, 3000.0); dcBlock.highpass (fs, 80.0);
        for (auto* b : { &lp1, &lp2, &subLp, &upHp, &upLp, &dcBlock }) b->reset();
        envCoef = onePoleCoef (fs, 0.02); env = 0; flip = 1; positive = false;
    }
    void set (float subLevel, float upLevel, float dryLevel) { sub = subLevel; up = upLevel; dry = dryLevel; }

    inline float process (float x)
    {
        const float f = lp2.process (lp1.process (x));
        const float a = std::abs (f);
        env = a > env ? a : env * envCoef;
        const float thr = 0.1f * env + 1e-5f; // hysteresis keeps noise from toggling it
        if (! positive && f > thr) { positive = true; flip = -flip; }
        else if (positive && f < -thr) positive = false;

        const float down = subLp.process (flip * f) * 2.0f;
        const float octUp = dcBlock.process (std::abs (upLp.process (upHp.process (x)))) * 2.0f;
        return dry * x + sub * down + up * octUp;
    }
};

//==============================================================================
// Envelope-controlled resonant band-pass (auto-wah / envelope filter).
struct AutoWah
{
    float env = 0, att = 0, rel = 0, ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 0.5f;
    float sens = 0.5f, mix = 1.0f;
    double fs_ = 48000;
    int counter = 0;

    void prepare (double fs) { fs_ = fs; att = onePoleCoef (fs, 0.003); rel = onePoleCoef (fs, 0.12); env = ic1 = ic2 = 0; counter = 0; updateCoefs (400.0f); }
    void set (float sensitivity, float resonance, float mixAmount)
    {
        sens = sensitivity; mix = mixAmount;
        k = 1.0f / (1.0f + resonance * 9.0f); // Q from 1 to 10
    }
    void updateCoefs (float freq)
    {
        const float g = (float) std::tan (kPi * freq / fs_);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline float process (float x)
    {
        const float a = std::abs (x);
        env = a + (env - a) * (a > env ? att : rel);
        if (--counter <= 0)
        {
            counter = 16;
            const float e = std::min (1.0f, env * (1.0f + sens * 30.0f));
            updateCoefs (350.0f * std::pow (2500.0f / 350.0f, e));
        }
        // Topology-preserving state-variable filter (Simper).
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        const float wah = v1 * k * 2.0f; // unity-peak band-pass, +6 dB
        return x + mix * (wah - x);
    }
};

//==============================================================================
// Modulated delay line shared by chorus and flanger.
struct ModDelay
{
    std::vector<float> buf;
    int mask = 0, write = 0;
    float phase = 0, phaseInc = 0, baseSamples = 0, depthSamples = 0, feedback = 0, mix = 0.5f, last = 0;
    double fs_ = 48000;

    void prepare (double fs, double maxSeconds)
    {
        fs_ = fs;
        int size = 1;
        while (size < (int) (maxSeconds * fs) + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; write = 0; phase = 0; last = 0;
    }
    void set (float rateHz, float baseMs, float depthMs, float fb, float mixAmount)
    {
        phaseInc = (float) (rateHz / fs_);
        baseSamples = (float) (baseMs * 0.001 * fs_);
        depthSamples = (float) (depthMs * 0.001 * fs_);
        feedback = fb; mix = mixAmount;
    }
    inline float process (float x)
    {
        phase += phaseInc; if (phase >= 1.0f) phase -= 1.0f;
        const float lfo = 0.5f + 0.5f * std::sin (2.0f * (float) kPi * phase);
        const float d = baseSamples + depthSamples * lfo;
        const int di = (int) d; const float frac = d - (float) di;
        const float a = buf[(size_t) ((write - di) & mask)], b = buf[(size_t) ((write - di - 1) & mask)];
        const float wet = a + (b - a) * frac;
        buf[(size_t) write] = x + wet * feedback;
        write = (write + 1) & mask;
        return x * (1.0f - mix) + wet * mix;
    }
};

//==============================================================================
// Echo with darkening repeats; time changes glide like a tape delay.
struct Delay
{
    std::vector<float> buf;
    int size = 0, write = 0;
    float current = 0, target = 0, glide = 0, feedback = 0.35f;
    bool fresh = true; // first time setting after prepare() jumps instead of gliding
    Biquad loopLp, loopHp;

    void prepare (double fs, double maxSeconds)
    {
        size = (int) (maxSeconds * fs) + 4;
        buf.assign ((size_t) size, 0.0f);
        write = 0;
        glide = onePoleCoef (fs, 0.08);
        loopLp.lowpass (fs, 3500.0); loopHp.highpass (fs, 80.0);
        loopLp.reset(); loopHp.reset();
        fresh = true;
    }
    void set (float samples, float fb)
    {
        target = std::min (samples, (float) size - 4); feedback = fb;
        if (fresh) { current = target; fresh = false; }
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    // Returns the wet signal only.
    inline float process (float x)
    {
        current = target + (current - target) * glide;
        float r = (float) write - current; if (r < 0) r += (float) size;
        const int i0 = (int) r; const float frac = r - (float) i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float wet = buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
        buf[(size_t) write] = x + loopHp.process (loopLp.process (wet)) * feedback;
        if (++write >= size) write = 0;
        return wet;
    }
};

//==============================================================================
// 7-band graphic EQ (GE-7 bands) with level.
struct GraphicEQ
{
    static constexpr int kBands = 7;
    static constexpr float kFreqs[kBands] = { 100, 200, 400, 800, 1600, 3200, 6400 };
    Biquad band[kBands];
    float last[kBands] {};
    float level = 1;
    double fs_ = 48000;

    void prepare (double fs)
    {
        fs_ = fs;
        for (int i = 0; i < kBands; ++i) { last[i] = 1e9f; band[i].reset(); }
    }
    void set (const float* gainsDb, float levelDb)
    {
        for (int i = 0; i < kBands; ++i)
            if (gainsDb[i] != last[i]) { band[i].peak (fs_, kFreqs[i], 1.4, gainsDb[i]); last[i] = gainsDb[i]; }
        level = dbToGain (levelDb);
    }
    inline float process (float x)
    {
        for (auto& b : band) x = b.process (x);
        return x * level;
    }
};
} // namespace sa

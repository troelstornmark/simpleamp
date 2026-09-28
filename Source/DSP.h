// Small, allocation-free DSP building blocks used by SimpleAmp.
#pragma once

#include <cmath>
#include <algorithm>

namespace sa
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// RBJ cookbook biquad, transposed direct form II. Coefficients are computed in
// place, so it is safe to retune from the audio thread.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }

    void lowpass (double fs, double f, double q = 0.7071)
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void highpass (double fs, double f, double q = 0.7071)
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void peak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * f / fs;
        const double c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void lowShelf (double fs, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * f / fs;
        const double c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
             (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }

    void highShelf (double fs, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * f / fs;
        const double c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
};

// Noise gate with hysteresis, hold and smooth release. Threshold at or below
// -95 dB disables it.
struct Gate
{
    float envelope = 0, gain = 1, envCoef = 0, attackCoef = 0, releaseCoef = 0;
    int holdSamples = 0, holdCounter = 0;
    bool open = false;

    void prepare (double fs)
    {
        envCoef     = (float) std::exp (-1.0 / (0.002 * fs)); // 2 ms level detector
        attackCoef  = (float) std::exp (-1.0 / (0.0005 * fs));
        releaseCoef = (float) std::exp (-1.0 / (0.020 * fs));
        holdSamples = (int) (0.015 * fs);
        envelope = 0; gain = 1; open = false; holdCounter = 0;
    }

    inline float process (float x, float openThresh, float closeThresh)
    {
        const float a = std::abs (x);
        envelope = a > envelope ? a : envelope * envCoef + a * (1 - envCoef);

        if (envelope > openThresh)       { open = true; holdCounter = holdSamples; }
        else if (envelope < closeThresh) { if (holdCounter > 0) --holdCounter; else open = false; }

        const float target = open ? 1.0f : 0.0f;
        const float coef = target > gain ? attackCoef : releaseCoef;
        gain = target + (gain - target) * coef;
        if (! open && gain < 0.003f) gain = 0.0f; // snap shut (-50 dB): a high-gain amp would amplify the leftover
        return x * gain;
    }
};

// Tube-Screamer-style "tight" boost: bass cut, 720 Hz hump, mild clipping,
// top-end roll-off. This is what goes in front of a 5150 for modern metal.
struct TightBoost
{
    Biquad hp, hump, lp;
    float driveGain = 1, makeup = 1;

    void prepare (double fs) { hp.reset(); hump.reset(); lp.reset(); fs_ = fs; lp.lowpass (fs, 6000); setDrive (0.0f); }

    void setDrive (float drive01)
    {
        // Low drive = mostly a filter (classic "drive 0, level max").
        hp.highpass (fs_, 180.0 + 420.0 * drive01, 0.6);
        hump.peak (fs_, 720.0, 0.9, 5.0);
        driveGain = dbToGain (drive01 * 24.0f);
        makeup = 1.0f / std::tanh (std::min (driveGain, 3.0f)) * 1.4f; // level up into the amp
    }

    inline float process (float x)
    {
        x = hump.process (hp.process (x));
        x = std::tanh (x * driveGain) * makeup;
        return lp.process (x);
    }

private:
    double fs_ = 48000;
};

// Fallback amp used until a .nam model is loaded: two clipping stages with
// interstage filtering, loosely voiced like a 5150 lead channel.
struct FallbackAmp
{
    Biquad preHp, inter, post;
    void prepare (double fs)
    {
        preHp.highpass (fs, 120.0); inter.peak (fs, 900.0, 0.8, 4.0); post.lowpass (fs, 7500.0);
        preHp.reset(); inter.reset(); post.reset();
    }
    inline float process (float x)
    {
        x = std::tanh (preHp.process (x) * 30.0f);
        x = std::tanh (inter.process (x) * 6.0f);
        return post.process (x) * 0.25f;
    }
};

// Fallback 4x12 V30-ish cabinet (filters only) used when no IR is loaded.
struct FallbackCab
{
    Biquad hp, thump, mud, presence, lp1, lp2;
    void prepare (double fs)
    {
        hp.highpass (fs, 70.0, 0.8);
        thump.peak (fs, 110.0, 1.2, 4.0);
        mud.peak (fs, 400.0, 1.0, -4.0);
        presence.peak (fs, 2500.0, 1.2, 3.0);
        lp1.lowpass (fs, 5000.0, 0.7);
        lp2.lowpass (fs, 5500.0, 0.9);
        for (auto* b : { &hp, &thump, &mud, &presence, &lp1, &lp2 }) b->reset();
    }
    inline float process (float x)
    {
        return lp2.process (lp1.process (presence.process (mud.process (thump.process (hp.process (x))))));
    }
};

// Post-amp EQ: bass shelf, mid peak, treble shelf.
struct ThreeBandEQ
{
    Biquad bass, mid, treble;
    float lastB = 1e9f, lastM = 1e9f, lastT = 1e9f;
    double fs_ = 48000;

    void prepare (double fs) { fs_ = fs; lastB = lastM = lastT = 1e9f; bass.reset(); mid.reset(); treble.reset(); }

    void update (float b, float m, float t)
    {
        if (b != lastB) { bass.lowShelf (fs_, 110.0, b); lastB = b; }
        if (m != lastM) { mid.peak (fs_, 750.0, 0.7, m); lastM = m; }
        if (t != lastT) { treble.highShelf (fs_, 3500.0, t); lastT = t; }
    }

    inline float process (float x) { return treble.process (mid.process (bass.process (x))); }
};
} // namespace sa

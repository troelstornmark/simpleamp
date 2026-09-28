// Guitar tuner pitch detection (YIN), run on the UI thread from a copy of the input.
// Down-samples to ~12 kHz first, so it's cheap and still resolves down to 30 Hz
// (drop tunings, 7- and 8-string low strings).
#pragma once

#include "DSP.h"
#include <cmath>
#include <vector>
#include <string>

namespace sa
{
struct PitchDetector
{
    static constexpr float kMinHz = 30.0f, kMaxHz = 1400.0f;

    // Returns the detected frequency in Hz, or 0 when there's no clear pitch.
    float detect (const float* x, int n, double sampleRate)
    {
        const int factor = std::max (1, (int) std::round (sampleRate / 12000.0));
        const double rate = sampleRate / factor;

        // Anti-alias (4th-order Butterworth at 0.4 x the new Nyquist) and decimate.
        Biquad lp1, lp2;
        lp1.lowpass (sampleRate, rate * 0.2, 0.54);
        lp2.lowpass (sampleRate, rate * 0.2, 1.31);
        buf.clear();
        double energy = 0;
        for (int i = 0; i < n; ++i)
        {
            const float y = lp2.process (lp1.process (x[i]));
            if (i % factor == 0 && i >= 64) { buf.push_back (y); energy += (double) y * y; }
        }
        const int m = (int) buf.size();
        if (m < 64 || energy / m < 1e-8) return 0.0f; // below about -80 dBFS: nothing to tune

        const int tauMin = std::max (2, (int) (rate / kMaxHz));
        const int tauMax = std::min (m / 2, (int) (rate / kMinHz));
        const int w = m - tauMax - 1; // j + tau stays inside buf for tau up to tauMax + 1
        if (tauMax <= tauMin + 2 || w < 32) return 0.0f;

        // Difference function and cumulative-mean normalisation.
        d.assign ((size_t) tauMax + 2, 1.0f);
        double running = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double sum = 0;
            for (int j = 0; j < w; ++j)
            {
                const double diff = buf[(size_t) j] - buf[(size_t) (j + tau)];
                sum += diff * diff;
            }
            running += sum;
            d[(size_t) tau] = running > 0 ? (float) (sum * tau / running) : 1.0f;
        }

        // First dip below the threshold, then walk to its minimum.
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0) return 0.0f;

        // Parabolic interpolation for sub-sample accuracy.
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float denom = a - 2 * b + c;
        const float shift = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
        const double coarsePeriod = (best + clampUnit (shift)) * factor; // in full-rate samples

        return (float) (sampleRate / refine (x, n, coarsePeriod));
    }

    // Note name, octave and cents offset from the nearest note (A4 = reference).
    struct Note { std::string name; int octave = 0; float cents = 0; };
    static Note toNote (float hz, float referenceA4 = 440.0f)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (hz / referenceA4);
        const int nearest = (int) std::lround (midi);
        return { names[((nearest % 12) + 12) % 12], nearest / 12 - 1, (midi - (float) nearest) * 100.0f };
    }

private:
    static float clampUnit (float v) { return std::max (-1.0f, std::min (1.0f, v)); }

    // Re-measures the period at the full sample rate, only around the coarse estimate.
    // High notes have periods of just a few samples after down-sampling; this keeps them in tune too.
    static double refine (const float* x, int n, double period)
    {
        const int lo = std::max (2, (int) std::floor (period * 0.97) - 1);
        const int hi = (int) std::ceil (period * 1.03) + 1;
        const int w = std::min (n - hi - 2, 2048);
        if (w < 64) return period;

        std::vector<double> diff ((size_t) (hi - lo + 1));
        for (int tau = lo; tau <= hi; ++tau)
        {
            double sum = 0;
            for (int j = 0; j < w; ++j) { const double dd = x[j] - x[j + tau]; sum += dd * dd; }
            diff[(size_t) (tau - lo)] = sum;
        }
        int best = 0;
        for (int i = 1; i < (int) diff.size(); ++i) if (diff[(size_t) i] < diff[(size_t) best]) best = i;
        if (best == 0 || best == (int) diff.size() - 1) return period; // edge: keep the coarse value

        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double denom = a - 2 * b + c;
        const double shift = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
        return lo + best + std::max (-1.0, std::min (1.0, shift));
    }
    std::vector<float> buf, d;
};
} // namespace sa

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include <array>
#include <cmath>

namespace scrr::dsp {
inline constexpr int spectralEqBands = 8;
inline constexpr std::array<double, spectralEqBands> spectralEqFrequencies { 40, 100, 250, 630, 1600, 4000, 10000, 16000 };
inline juce::String spectralEqParameter (int band, const char* suffix)
{ return "band" + juce::String (juce::jlimit (0, spectralEqBands - 1, band) + 1) + suffix; }

struct SpectralEqBand
{
    enum Type { Bell, LowShelf, HighShelf, LowCut, HighCut };
    bool enabled { true };
    int type { Bell };
    double frequency { 1000 }, gain {}, q { 0.7071067811865476 };
    bool operator== (const SpectralEqBand& b) const noexcept
    { return enabled == b.enabled && type == b.type && std::abs (frequency - b.frequency) < 1.0e-10
          && std::abs (gain - b.gain) < 1.0e-10 && std::abs (q - b.q) < 1.0e-10; }
};
inline std::array<SpectralEqBand, spectralEqBands> readSpectralEqBands (const juce::ValueTree& state)
{
    std::array<SpectralEqBand, spectralEqBands> bands;
    for (int i = 0; i < spectralEqBands; ++i)
    {
        auto& b = bands[(size_t) i];
        auto number = [&state, i] (const char* suffix, double fallback, double lo, double hi)
        {
            const double value = (double) state.getProperty (spectralEqParameter (i, suffix), fallback);
            return std::isfinite (value) ? juce::jlimit (lo, hi, value) : fallback;
        };
        b.enabled = (bool) state.getProperty (spectralEqParameter (i, "Enabled"), true);
        b.type = (int) number ("Type", 0, 0, 4);
        b.frequency = number ("Freq", spectralEqFrequencies[(size_t) i], 20, 20000);
        b.gain = number ("Gain", 0, -24, 24);
        b.q = number ("Q", 0.7071067811865476, 0.1, 18);
    }
    return bands;
}

// Magnitude responses of the RBJ/W3C biquad prototypes. Only the positive real
// gain is applied to FFT bins: these filters add no phase rotation or latency.
// Formula reference: https://www.w3.org/TR/audio-eq-cookbook/
class SpectralEqResponse
{
public:
    void configure (const std::array<SpectralEqBand, spectralEqBands>& bands, double rate)
    {
        sampleRate = std::isfinite (rate) && rate >= 1000 ? rate : 44100;
        for (size_t i = 0; i < bands.size(); ++i) filters[i] = coefficients (bands[i], sampleRate);
    }
    double gainAt (double frequency) const noexcept
    {
        const double omega = juce::MathConstants<double>::twoPi * juce::jlimit (0.0, sampleRate * 0.5, frequency) / sampleRate;
        // Evaluate in Cartesian form to avoid cancellation near DC with high Q.
        const double c = std::cos (omega), s = std::sin (omega), c2 = 2 * c * c - 1, s2 = 2 * s * c;
        double gain = 1;
        for (const auto& f : filters)
        {
            if (f.identity) continue;
            const double nr = f.b0 + f.b1 * c + f.b2 * c2, ni = f.b1 * s + f.b2 * s2;
            const double dr = f.a0 + f.a1 * c + f.a2 * c2, di = f.a1 * s + f.a2 * s2;
            const double denominator = dr * dr + di * di;
            gain *= std::sqrt (juce::jmax (0.0, (nr * nr + ni * ni) / juce::jmax (1.0e-40, denominator)));
        }
        // Eight simultaneous boosts are bounded to +96 dB; exact cut zeros stay zero.
        return std::isfinite (gain) ? juce::jlimit (0.0, 63095.7344480193, gain) : 0.0;
    }
    double decibelsAt (double frequency) const noexcept
    { return 20 * std::log10 (juce::jmax (1.0e-12, gainAt (frequency))); }
private:
    struct Coefficients
    { double b0 { 1 }, b1 {}, b2 {}, a0 { 1 }, a1 {}, a2 {}; bool identity { true }; };
    static Coefficients coefficients (const SpectralEqBand& b, double sr)
    {
        Coefficients f;
        if (! b.enabled || (b.type <= SpectralEqBand::HighShelf && std::abs (b.gain) < 1.0e-12)) return f;
        const double w = juce::MathConstants<double>::twoPi * juce::jmin (b.frequency, sr * 0.499) / sr;
        const double c = std::cos (w), alpha = std::sin (w) / (2 * b.q), a = std::pow (10.0, b.gain / 40.0);
        f.identity = false;
        if (b.type == SpectralEqBand::Bell)
        {
            f.b0 = 1 + alpha * a; f.b1 = -2 * c; f.b2 = 1 - alpha * a;
            f.a0 = 1 + alpha / a; f.a1 = -2 * c; f.a2 = 1 - alpha / a;
        }
        else if (b.type == SpectralEqBand::LowShelf)
        {
            const double beta = 2 * std::sqrt (a) * alpha;
            f.b0 = a * ((a + 1) - (a - 1) * c + beta);
            f.b1 = 2 * a * ((a - 1) - (a + 1) * c);
            f.b2 = a * ((a + 1) - (a - 1) * c - beta);
            f.a0 = (a + 1) + (a - 1) * c + beta;
            f.a1 = -2 * ((a - 1) + (a + 1) * c);
            f.a2 = (a + 1) + (a - 1) * c - beta;
        }
        else if (b.type == SpectralEqBand::HighShelf)
        {
            const double beta = 2 * std::sqrt (a) * alpha;
            f.b0 = a * ((a + 1) + (a - 1) * c + beta);
            f.b1 = -2 * a * ((a - 1) + (a + 1) * c);
            f.b2 = a * ((a + 1) + (a - 1) * c - beta);
            f.a0 = (a + 1) - (a - 1) * c + beta;
            f.a1 = 2 * ((a - 1) - (a + 1) * c);
            f.a2 = (a + 1) - (a - 1) * c - beta;
        }
        else
        {
            const bool lowCut = b.type == SpectralEqBand::LowCut;
            f.b0 = f.b2 = (1 + (lowCut ? c : -c)) * 0.5;
            f.b1 = (lowCut ? -2 : 2) * f.b0;
            f.a0 = 1 + alpha; f.a1 = -2 * c; f.a2 = 1 - alpha;
        }
        return f;
    }
    double sampleRate { 44100 };
    std::array<Coefficients, spectralEqBands> filters;
};

class SpectralEqualizer final : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralEqualizer"; }
    void prepare (double rate, int bins) override
    {
        sampleRate = rate;
        gains.assign ((size_t) juce::jmax (0, bins), 1.0f);
        dirty = true;
    }
    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = (bool) state.getProperty ("enabled", false);
        const auto next = readSpectralEqBands (state);
        if (next != bands) { bands = next; dirty = true; }
    }
    void processSpectrum (std::complex<float>* spectrum, int bins, double rate) override
    {
        if (! enabled || bins < 2 || rate < 1000 || ! std::isfinite (rate) || (size_t) bins != gains.size()) return;
        if (dirty || std::abs (sampleRate - rate) > 1.0e-9)
        {
            sampleRate = rate; response.configure (bands, rate);
            for (int b = 0; b < bins; ++b) gains[(size_t) b] = (float) response.gainAt (rate * 0.5 * b / (bins - 1));
            dirty = false;
        }
        for (int b = 0; b < bins; ++b) spectrum[b] *= gains[(size_t) b];
    }
private:
    bool enabled {}, dirty { true };
    double sampleRate { 44100 };
    std::array<SpectralEqBand, spectralEqBands> bands;
    SpectralEqResponse response;
    std::vector<float> gains;
};
} // namespace scrr::dsp

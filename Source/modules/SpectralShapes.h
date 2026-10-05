// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace scrr::dsp {
// All work buffers are prepared with the engine, never grown per frame.
class SpectralMirror : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralMirror"; }
    void prepare (double, int n) override { original.resize ((size_t) n); reset(); }
    void reset() override { phase.fill (0); }
    void beginChannel (int c) override { channel = juce::jlimit (0, 1, c); }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        amount = juce::jlimit (0.0f, 1.0f, (float) s.getProperty ("amount", 0.0) / 100);
        pivot = juce::jlimit (20.0f, 10000.0f, (float) s.getProperty ("pivot", 1000.0));
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || amount <= 0 || n < 3 || sr <= 0 || (size_t) n > original.size()) return;
        const double twicePivotBin = 2.0 * pivot * (2.0 * (n - 1)) / sr;
        const auto rotation = std::polar (1.0f, (float) phase[(size_t) channel]);
        std::copy (x, x + n, original.begin());
        for (int b = 1; b < n - 1; ++b)
        {
            const double source = twicePivotBin - b;
            std::complex<float> reflected {};
            if (source >= 1 && source <= n - 2)
            {
                const int i = (int) source; const float t = (float) (source - i);
                reflected = std::conj (original[(size_t) i] * (1 - t) + original[(size_t) std::min (n - 2, i + 1)] * t) * rotation;
            }
            x[b] = original[(size_t) b] * (1 - amount) + reflected * amount;
        }
        // Fixed N/4 hop, matching FFTEngine. Conjugation reverses the incoming
        // phase progression; this carrier restores 2*pivot - source frequency.
        phase[(size_t) channel] = std::remainder (phase[(size_t) channel] + juce::MathConstants<double>::twoPi * twicePivotBin / 4,
                                                 juce::MathConstants<double>::twoPi);
    }
private:
    bool enabled {}; float amount {}, pivot { 1000 }; int channel {};
    std::array<double, 2> phase {}; std::vector<std::complex<float>> original;
};

class SpectralBloom : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralBloom"; }
    void prepare (double, int n) override { prefix.resize ((size_t) n + 1); blurred.resize ((size_t) n); reset(); }
    void reset() override { frame.fill (0); }
    void beginChannel (int c) override { channel = juce::jlimit (0, 1, c); }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        amount = juce::jlimit (0.0f, 1.0f, (float) s.getProperty ("amount", 0.0) / 100);
        spread = juce::jlimit (10.0f, 3000.0f, (float) s.getProperty ("spread", 150.0));
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || amount <= 0 || n < 3 || sr <= 0 || (size_t) n > blurred.size()) return;
        prefix[0] = prefix[1] = 0;
        for (int b = 1; b < n - 1; ++b) prefix[(size_t) b + 1] = prefix[(size_t) b] + (double) std::norm (x[b]);
        const double inputPower = prefix[(size_t) n - 1];
        if (inputPower < 1e-20 || ! std::isfinite (inputPower)) return;
        const int radius = juce::jlimit (1, n - 2, (int) std::round (spread * 2.0 * (n - 1) / sr));
        double outputPower = 0;
        for (int b = 1; b < n - 1; ++b)
        {
            const int lo = std::max (1, b - radius), hi = std::min (n - 2, b + radius);
            const double power = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1);
            blurred[(size_t) b] = std::max (0.0, power); outputPower += blurred[(size_t) b];
        }
        const double normalise = inputPower / std::max (1e-20, outputPower);
        const int clock = frame[(size_t) channel];
        for (int b = 1; b < n - 1; ++b)
        {
            const float power = std::norm (x[b]);
            const float amplitude = (float) std::sqrt (power * (1 - amount) + blurred[(size_t) b] * normalise * amount);
            // Retain occupied-bin phase. Newly lit bins receive coherent bin
            // oscillators, with a deterministic offset instead of random noise.
            const float angle = power > 1e-20f ? std::arg (x[b])
                : (float) ((b * clock) % 4) * juce::MathConstants<float>::halfPi + (float) (b % 7) * .41f;
            x[b] = std::polar (amplitude, angle);
        }
        frame[(size_t) channel] = (clock + 1) % 4;
    }
private:
    bool enabled {}; float amount {}, spread { 150 }; int channel {};
    std::array<int, 2> frame {}; std::vector<double> prefix, blurred;
};

class SpectralComb : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralComb"; }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        amount = juce::jlimit (0.0f, 1.0f, (float) s.getProperty ("amount", 0.0) / 100);
        spacing = juce::jlimit (20.0f, 4000.0f, (float) s.getProperty ("spacing", 220.0));
        offset = juce::jlimit (0.0f, 1.0f, (float) s.getProperty ("offset", 0.0) / 100);
        width = juce::jlimit (.02f, .98f, (float) s.getProperty ("width", 35.0) / 100);
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || amount <= 0 || n < 3 || sr <= 0) return;
        const float binHz = (float) (sr / (2.0 * (n - 1)));
        for (int b = 1; b < n - 1; ++b)
        {
            const float cycle = (float) b * binHz / spacing - offset;
            const float distance = std::abs (cycle - std::round (cycle));
            const float edge = juce::jmax (.01f, width * .12f);
            const float t = juce::jlimit (0.0f, 1.0f, (width * .5f - distance) / edge);
            const float tooth = t * t * (3 - 2 * t);
            x[b] *= (1 - amount) + amount * tooth;
        }
    }
private:
    bool enabled {}; float amount {}, spacing { 220 }, offset {}, width { .35f };
};
} // namespace scrr::dsp

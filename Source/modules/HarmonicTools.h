// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace scrr::dsp {

class SpectralContrast : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralContrast"; }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        amount = juce::jlimit (-100.0f, 100.0f, (float) s.getProperty ("amount", 0.0f)) / 100.0f;
    }
    void processSpectrum (std::complex<float>* x, int n, double) override
    {
        if (! enabled || std::abs (amount) < 1.0e-8f || n < 3) return;
        double power = 0; float peak = 0;
        for (int b = 1; b < n - 1; ++b) { power += std::norm (x[b]); peak = std::max (peak, std::abs (x[b])); }
        if (peak < 1.0e-9f || ! std::isfinite (power)) return;
        const float pivot = (float) std::sqrt (power / (n - 2));
        double outPower = 0;
        for (int b = 1; b < n - 1; ++b)
        {
            const float ratio = std::max (std::abs (x[b]), peak * 1.0e-4f) / std::max (pivot, 1.0e-9f);
            const float gain = juce::jlimit (0.0625f, 16.0f, std::pow (ratio, amount));
            x[b] *= gain; outPower += std::norm (x[b]);
        }
        const float norm = (float) std::sqrt (power / std::max (outPower, 1.0e-20));
        for (int b = 1; b < n - 1; ++b) x[b] *= std::min (norm, 4.0f);
    }
private:
    bool enabled {}; float amount {};
};

// Frequency translation with fractional-bin interpolation and hop-phase advance.
class FrequencyShift : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "FrequencyShift"; }
    void prepare (double, int n) override { scratch.resize ((size_t) n); reset(); }
    void reset() override { phase.fill (0.0); }
    void beginChannel (int c) override { channel = juce::jlimit (0, 1, c); }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        hz = juce::jlimit (-5000.0f, 5000.0f, (float) s.getProperty ("shift", 0.0));
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || std::abs (hz) < 1.0e-8f || n < 3 || sr <= 0) return;
        const double shift = hz * (2.0 * (n - 1)) / sr;
        const auto rotation = std::polar (1.0f, (float) phase[(size_t) channel]);
        std::copy (x, x + n, scratch.begin());
        for (int b = 1; b < n - 1; ++b)
        {
            const double source = b - shift;
            if (source < 1 || source > n - 2) { x[b] = {}; continue; }
            const int a = (int) source, next = std::min (a + 1, n - 2);
            const float fraction = (float) (source - a);
            x[b] = (scratch[(size_t) a] * (1.0f - fraction) + scratch[(size_t) next] * fraction) * rotation;
        }
        phase[(size_t) channel] = std::remainder (phase[(size_t) channel]
            + juce::MathConstants<double>::twoPi * shift / 4.0, juce::MathConstants<double>::twoPi);
    }
private:
    bool enabled {}; float hz {}; int channel {};
    std::array<double, 2> phase {};
    std::vector<std::complex<float>> scratch;
};

class HarmonicTracker
{
public:
    void reset() { previous.fill (0); }
    void beginChannel (int c) { channel = juce::jlimit (0, 1, c); }
    float find (const std::complex<float>* x, int n, double sr, bool automatic, float manual)
    {
        const float binHz = (float) (sr / (2.0 * (n - 1)));
        if (! automatic) return manual / binHz;
        const int lo = juce::jlimit (1, n - 2, (int) std::ceil (40.0f / binHz));
        const int hi = juce::jlimit (lo, n - 2, (int) (std::min (2000.0, sr / 8.0) / binHz));
        int peak = lo;
        for (int b = lo + 1; b <= hi; ++b) if (std::norm (x[b]) > std::norm (x[peak])) peak = b;
        if (std::abs (x[peak]) < 1.0e-7f) return 0;
        const float left = std::abs (x[peak - 1]), centre = std::abs (x[peak]), right = std::abs (x[peak + 1]);
        const float denom = left - 2 * centre + right;
        const float offset = std::abs (denom) > 1.0e-10f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (left - right) / denom) : 0;
        float found = (float) peak + offset;
        auto& prev = previous[(size_t) channel];
        if (prev > 0 && std::abs (found - prev) < 1.0f) found = prev * 0.65f + found * 0.35f;
        prev = found;
        return std::max (1.0f, found);
    }
private:
    int channel {}; std::array<float, 2> previous {};
};

class HarmonicSculpt : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "HarmonicSculpt"; }
    void prepare (double, int) override { reset(); }
    void reset() override { tracker.reset(); }
    void beginChannel (int c) override { tracker.beginChannel (c); }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        automatic = (bool) s.getProperty ("track", true);
        fundamental = (float) s.getProperty ("fundamental", 220.0);
        odd = juce::Decibels::decibelsToGain ((float) s.getProperty ("odd", 0.0));
        even = juce::Decibels::decibelsToGain ((float) s.getProperty ("even", 0.0));
        width = (float) s.getProperty ("width", 30.0) * 0.005f;
        amount = (float) s.getProperty ("amount", 0.0) * 0.01f;
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || amount <= 0 || n < 3) return;
        const float f0 = tracker.find (x, n, sr, automatic, fundamental);
        if (f0 <= 0) return;
        for (int b = 1; b < n - 1; ++b)
        {
            const int h = (int) std::round ((float) b / f0);
            if (h < 1) continue;
            const float distance = std::abs ((float) b - (float) h * f0);
            const float weight = juce::jlimit (0.0f, 1.0f, 1.0f - distance / std::max (1.0f, f0 * width));
            x[b] *= 1.0f + ((h % 2 == 0 ? even : odd) - 1.0f) * amount * weight;
        }
    }
private:
    bool enabled {}, automatic { true }; float fundamental { 220 }, odd { 1 }, even { 1 }, width { .15f }, amount {};
    HarmonicTracker tracker;
};

// Independently designed envelopes inspired by the HarmMatch workflow. This is
// not a port of DtBlkFx's wavetable sequences. Scale retains existing phases;
// Resynth copies the fundamental band to supply missing harmonics.
class HarmonicMatch : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "HarmonicMatch"; }
    void prepare (double, int n) override { original.resize ((size_t) n); reset(); }
    void reset() override { tracker.reset(); }
    void beginChannel (int c) override { tracker.beginChannel (c); }
    void updateParameters (const juce::ValueTree& s) override
    {
        enabled = (bool) s.getProperty ("enabled", false);
        automatic = (bool) s.getProperty ("track", true);
        fundamental = (float) s.getProperty ("fundamental", 220.0);
        shape = juce::jlimit (0, 4, (int) s.getProperty ("shape", 0));
        resynth = (int) s.getProperty ("mode", 0) == 1;
        colour = (float) s.getProperty ("colour", 50.0) * 0.01f;
        amount = (float) s.getProperty ("amount", 0.0) * 0.01f;
        width = (float) s.getProperty ("width", 50.0) * 0.005f;
    }
    static float envelope (int shapeIndex, int harmonic, float colourValue)
    {
        const float h = (float) harmonic;
        const float tilt = std::pow (h, (colourValue - 0.5f) * 1.5f);
        switch (shapeIndex)
        {
            case 0: return harmonic % 2 == 0 ? 0.0f : tilt / (h * h);
            case 1: return harmonic % 2 == 0 ? 0.0f : tilt / h;
            case 2: return tilt / h;
            case 3: return tilt / std::sqrt (h);
            default: { const float centre = 1.0f + colourValue * 31.0f;
                       const float d = (h - centre) / (1.5f + colourValue * 4.0f);
                       return std::exp (-0.5f * d * d) + 0.015f / h; }
        }
    }
    void processSpectrum (std::complex<float>* x, int n, double sr) override
    {
        if (! enabled || amount <= 0 || n < 3) return;
        const float f0 = tracker.find (x, n, sr, automatic, fundamental);
        if (f0 <= 0) return;
        std::copy (x, x + n, original.begin());
        const int harmonics = std::min (128, (int) ((float) (n - 2) / f0));
        const float halfWidth = std::max (0.5f, f0 * width);
        const int radius = std::max (0, (int) std::floor (halfWidth));
        const int root = juce::jlimit (1, n - 2, (int) std::round (f0));
        double inputPower = 0, targetPower = 0;
        std::array<double, 129> bandPower {};
        for (int h = 1; h <= harmonics; ++h)
        {
            const int centre = (int) std::round ((float) h * f0);
            for (int b = std::max (1, centre - radius); b <= std::min (n - 2, centre + radius); ++b)
                bandPower[(size_t) h] += std::norm (original[(size_t) b]);
            inputPower += bandPower[(size_t) h];
            const auto target = envelope (shape, h, colour);
            if (resynth || bandPower[(size_t) h] > 1.0e-16) targetPower += target * target;
        }
        if (inputPower < 1.0e-16 || targetPower < 1.0e-16) return;
        const double scale = inputPower / targetPower;
        for (int h = 1; h <= harmonics; ++h)
        {
            const int centre = (int) std::round ((float) h * f0);
            const float target = envelope (shape, h, colour);
            const double sourcePower = resynth ? bandPower[1] : bandPower[(size_t) h];
            const float gain = (float) std::min (16.0, std::sqrt (scale * target * target / std::max (1.0e-16, sourcePower)));
            for (int b = std::max (1, centre - radius); b <= std::min (n - 2, centre + radius); ++b)
            {
                const int source = resynth ? juce::jlimit (1, n - 2, root + b - centre) : b;
                x[b] = original[(size_t) b] * (1.0f - amount) + original[(size_t) source] * (gain * amount);
            }
        }
    }
private:
    HarmonicTracker tracker;
    std::vector<std::complex<float>> original;
    bool enabled {}, automatic { true }, resynth {};
    int shape {};
    float fundamental { 220 }, colour { .5f }, width { .25f }, amount {};
};
} // namespace scrr::dsp

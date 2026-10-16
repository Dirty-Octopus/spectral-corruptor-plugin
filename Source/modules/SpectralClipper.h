// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include <cmath>

namespace scrr::dsp {
// A phase-preserving ceiling on each FFT bin, not a sample/true-peak limiter.
class SpectralClipper final : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralClipper"; }
    void prepare (double, int bins) override
    { windowSum = juce::jmax (1.0, 2.0 * (bins - 1)); }
    void setSpectrumCalibration (double sum) override
    { if (std::isfinite (sum) && sum > 0.0) windowSum = sum; }
    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = (bool) state.getProperty ("enabled", false);
        threshold = bounded (state, "threshold", -12, -96, 0);
        driveDb = bounded (state, "drive", 0, 0, 48);
        knee = bounded (state, "knee", 0, 0, 24);
        output = std::pow (10.0, bounded (state, "output", 0, -48, 24) / 20.0);
        drive = std::pow (10.0, driveDb / 20.0);
    }
    void processSpectrum (std::complex<float>* spectrum, int bins, double) override
    {
        if (! enabled || bins < 2) return;
        for (int b = 0; b < bins; ++b)
        {
            const double magnitude = std::abs (spectrum[b]);
            if (! std::isfinite (magnitude)) { spectrum[b] = {}; continue; }
            if (magnitude <= 1.0e-30) continue;
            const double scale = (b == 0 || b == bins - 1 ? 1.0 : 2.0) / windowSum;
            const double level = 20.0 * std::log10 (magnitude * scale) + driveDb;
            double reduction = 0;
            if (knee <= 1.0e-8)
                reduction = juce::jmin (0.0, threshold - level);
            else if (level > threshold + knee * 0.5)
                reduction = threshold - level;
            else if (level > threshold - knee * 0.5)
            {
                const double intoKnee = level - threshold + knee * 0.5;
                reduction = -intoKnee * intoKnee / (2.0 * knee);
            }
            spectrum[b] *= (float) (drive * output * std::pow (10.0, reduction / 20.0));
        }
    }
private:
    static double bounded (const juce::ValueTree& s, const char* id, double fallback, double low, double high)
    {
        const double value = (double) s.getProperty (id, fallback);
        return std::isfinite (value) ? juce::jlimit (low, high, value) : fallback;
    }
    bool enabled {};
    double windowSum { 2048 }, threshold { -12 }, driveDb {}, drive { 1 }, knee {}, output { 1 };
};
} // namespace scrr::dsp

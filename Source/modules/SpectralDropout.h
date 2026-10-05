// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <cstdint>
#include <complex>
#include <vector>

namespace scrr::dsp {

/**
    Phase 2 spectral corruption module #3.

    Builds a persistent per-bin hole mask and attenuates those bins while
    applying a mild power compensation to keep the result usable.
*/
class SpectralDropoutModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralDropout"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        preparedBins = numBins;
        holeMask.assign ((size_t) numBins, uint8_t { 0 });
        maskDirty = true;
        rebuildMask();
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        density = juce::jlimit (0.0f, 1.0f,
                                (float) state.getProperty ("density", 0.0f) / 100.0f);

        if (std::fabs (density - lastDensity) > 1.0e-6f
         || std::fabs (amount  - lastAmount)  > 1.0e-6f)
        {
            lastDensity = density;
            lastAmount = amount;
            maskDirty = true;
        }

        if (maskDirty)
            rebuildMask();
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        jassert ((int) holeMask.size() >= numBins);

        const float holeGain = 1.0f - amount;
        const float holeScale = compensation * holeGain;
        const float survivorScale = compensation;

        for (int i = 1; i < numBins - 1; ++i)
            spectrum[(size_t) i] *= (holeMask[(size_t) i] != 0) ? holeScale : survivorScale;
    }

private:
    static uint32_t hash32 (uint32_t x) noexcept
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    void rebuildMask()
    {
        maskDirty = false;

        if (preparedBins <= 2)
        {
            compensation = 1.0f;
            return;
        }

        int holeCount = 0;
        for (int i = 0; i < preparedBins; ++i)
            holeMask[(size_t) i] = uint8_t { 0 };

        for (int i = 1; i < preparedBins - 1; ++i)
        {
            const float noise = (float) hash32 ((uint32_t) i + 1u) / 4294967295.0f;
            const bool hole = noise < density;
            holeMask[(size_t) i] = hole ? uint8_t { 1 } : uint8_t { 0 };
            holeCount += hole ? 1 : 0;
        }

        const int interiorBins = juce::jmax (1, preparedBins - 2);
        const float maskedFraction = (float) holeCount / (float) interiorBins;
        const float holeGain = 1.0f - amount;
        const float expectedPower = (1.0f - maskedFraction) + maskedFraction * holeGain * holeGain;
        compensation = 1.0f / std::sqrt (juce::jmax (0.20f, expectedPower));
    }

    bool enabled { false };
    float amount { 0.0f };
    float density { 0.0f };
    float lastDensity { -1.0f };
    float lastAmount { -1.0f };
    float compensation { 1.0f };
    int preparedBins { 0 };
    bool maskDirty { true };
    std::vector<uint8_t> holeMask;
};

} // namespace scrr::dsp

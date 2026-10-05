// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

/**
    Phase 2 spectral corruption module #5.

    Groups FFT bins into coarse blocks, replaces magnitudes with block RMS, and
    preserves each bin's phase to avoid level collapse. DC and Nyquist are left untouched.
*/
class SpectralPixelationModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralPixelation"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        scratch.resize ((size_t) numBins);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        blockSizeSetting = juce::jlimit (1, 64,
                                         (int) std::round ((float) state.getProperty ("blockSize", 64.0f)));
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        jassert ((int) scratch.size() >= numBins);

        const int mutableBins = numBins - 2;
        const int maxBlock = juce::jmin (blockSizeSetting, mutableBins);
        if (maxBlock <= 1)
            return;

        std::copy_n (spectrum, (size_t) numBins, scratch.data());

        const int effectiveBlockSize = juce::jlimit (1, maxBlock,
            (int) std::round (juce::jmap (amount, 0.0f, 1.0f, 1.0f, (float) maxBlock)));

        for (int start = 1; start < numBins - 1; start += effectiveBlockSize)
        {
            const int end = juce::jmin (start + effectiveBlockSize, numBins - 1);
            const int count = end - start;
            if (count <= 0)
                continue;

            float powerSum = 0.0f;
            for (int i = start; i < end; ++i)
                powerSum += std::norm (scratch[(size_t) i]);

            const float blockMagnitude = std::sqrt (powerSum / (float) count);

            for (int i = start; i < end; ++i)
            {
                const float phase = std::arg (scratch[(size_t) i]);
                spectrum[(size_t) i] = std::polar (blockMagnitude, phase);
            }
        }
    }

private:
    bool enabled { false };
    float amount { 0.0f };
    int blockSizeSetting { 64 };
    std::vector<std::complex<float>> scratch;
};

} // namespace scrr::dsp

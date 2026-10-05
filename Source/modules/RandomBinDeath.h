// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <complex>

namespace scrr::dsp {

class RandomBinDeathModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "RandomBinDeath"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        mask.resize ((size_t) numBins);

        frameCounter = 0;
    }

    void beginChannel (int channelIndex) override
    {
        juce::ignoreUnused (channelIndex);

        frameCounter = 0;
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        compensation = 1.0f / std::sqrt (juce::jmax (0.05f, 1.0f - amount));

        const int newSeed = (int) state.getProperty ("seed", 0);
        if (newSeed != seed)
            seed = newSeed;
    }

    void reset() override
    {
        frameCounter = 0;
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        const uint32_t baseSeed = (uint32_t) seed;
        const uint32_t frameSeed = mixSeed ((int) baseSeed, frameCounter++);
        juce::Random rng ((int) frameSeed);

        jassert ((int) mask.size() >= numBins);

        mask[0] = uint8_t { 0 };
        mask[(size_t) numBins - 1] = uint8_t { 0 };

        for (int i = 1; i < numBins - 1; ++i)
            mask[(size_t) i] = (rng.nextFloat() < amount) ? uint8_t { 1 } : uint8_t { 0 };

        for (int i = 1; i < numBins - 1; ++i)
            if (mask[(size_t) i] != 0)
                spectrum[(size_t) i] = std::complex<float> { 0.0f, 0.0f };

        for (int i = 1; i < numBins - 1; ++i)
            spectrum[(size_t) i] *= compensation;
    }

private:
    static uint32_t mixSeed (int baseSeed, int frameIndex) noexcept
    {
        uint32_t x = (uint32_t) baseSeed;
        x ^= 0x9e3779b9u + ((uint32_t) frameIndex << 6) + ((uint32_t) frameIndex >> 2);
        return x;
    }

    bool enabled { false };
    float amount { 0.0f };
    float compensation { 1.0f };
    int seed { 0 };

    int frameCounter { 0 };

    std::vector<uint8_t> mask;
};

} // namespace scrr::dsp

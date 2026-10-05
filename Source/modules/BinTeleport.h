// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace scrr::dsp {

class BinTeleportModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "BinTeleport"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        temp.resize ((size_t) numBins);

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

        const int newSeed = (int) state.getProperty ("seed", 0);
        if (newSeed != seed)
            seed = newSeed;
    }

    void reset() override { frameCounter = 0; }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        jassert ((int) temp.size() >= numBins);

        for (int i = 0; i < numBins; ++i)
            temp[(size_t) i] = spectrum[(size_t) i];

        const uint32_t baseSeed = (uint32_t) seed;
        juce::Random rng ((int) mixSeed ((int) baseSeed, frameCounter++));
        const int lo = 1;
        const int hi = numBins - 2;

        for (int i = lo; i <= hi; ++i)
        {
            if (rng.nextFloat() >= amount)
                continue;

            const int target = lo + rng.nextInt (hi - lo + 1);
            temp[(size_t) target] = spectrum[(size_t) i];

            if (rng.nextBool())
                temp[(size_t) i] = std::complex<float> { 0.0f, 0.0f };
        }

        for (int i = 1; i < numBins - 1; ++i)
            spectrum[(size_t) i] = temp[(size_t) i];
    }

private:
    static uint32_t mixSeed (int baseSeed, int frameIndex) noexcept
    {
        uint32_t x = (uint32_t) baseSeed;
        x ^= 0x85ebca6bu + ((uint32_t) frameIndex << 7) + ((uint32_t) frameIndex >> 3);
        return x;
    }

    bool enabled { false };
    float amount { 0.0f };
    int seed { 0 };

    int frameCounter { 0 };

    std::vector<std::complex<float>> temp;
};

} // namespace scrr::dsp

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace scrr::dsp {

class BinHoldModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "BinHold"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        preparedBins = numBins;
        heldBins.assign ((size_t) maxChannels * (size_t) numBins, std::complex<float> { 0.0f, 0.0f });
        remaining.assign ((size_t) maxChannels * (size_t) numBins, 0);

        for (int i = 0; i < maxChannels; ++i)
        {
            frameCounters[i] = 0;
            nextTriggers[i] = 0;
        }
    }

    void beginChannel (int channelIndex) override
    {
        currentChannel = juce::jlimit (0, maxChannels - 1, channelIndex);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        rateFrames = juce::jmax (1.0f, (float) state.getProperty ("rate", 8.0f));
        holdLength = juce::jmax (1, (int) std::round ((float) state.getProperty ("length", 8.0f)));

        const int newSeed = (int) state.getProperty ("seed", 0);
        if (newSeed != seed)
            seed = newSeed;
    }

    void reset() override
    {
        std::fill (heldBins.begin(), heldBins.end(), std::complex<float> { 0.0f, 0.0f });
        std::fill (remaining.begin(), remaining.end(), 0);
        for (int i = 0; i < maxChannels; ++i)
        {
            frameCounters[i] = 0;
            nextTriggers[i] = 0;
        }
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double) override
    {
        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        jassert (preparedBins == numBins);

        const int offset = currentChannel * preparedBins;
        int& frameCounter = frameCounters[currentChannel];
        int& nextTrigger = nextTriggers[currentChannel];

        const uint32_t baseSeed = (uint32_t) seed;
        if (frameCounter >= nextTrigger)
        {
            juce::Random rng ((int) FrameRandomSource::hash32 (baseSeed + (uint32_t) frameCounter));
            for (int i = 1; i < numBins - 1; ++i)
            {
                if (rng.nextFloat() < amount)
                {
                    heldBins[(size_t) (offset + i)] = spectrum[(size_t) i];
                    remaining[(size_t) (offset + i)] = holdLength;
                }
            }

            const int baseInterval = juce::jmax (1, (int) std::round (rateFrames));
            nextTrigger = frameCounter + baseInterval;
        }

        for (int i = 1; i < numBins - 1; ++i)
        {
            auto& framesLeft = remaining[(size_t) (offset + i)];
            if (framesLeft > 0)
            {
                spectrum[(size_t) i] = heldBins[(size_t) (offset + i)];
                --framesLeft;
            }
        }

        ++frameCounter;
    }

private:
    static constexpr int maxChannels = 2;

    bool enabled { false };
    float amount { 0.0f };
    float rateFrames { 8.0f };
    int holdLength { 8 };

    int seed { 0 };

    int preparedBins { 0 };
    int currentChannel { 0 };
    int frameCounters[maxChannels] { 0, 0 };
    int nextTriggers[maxChannels] { 0, 0 };

    std::vector<std::complex<float>> heldBins;
    std::vector<int> remaining;
};

} // namespace scrr::dsp

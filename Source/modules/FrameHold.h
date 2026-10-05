// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace scrr::dsp {

class FrameHoldModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "FrameHold"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        preparedBins = numBins;
        heldFrame.assign ((size_t) maxChannels * (size_t) numBins, std::complex<float> { 0.0f, 0.0f });

        for (int i = 0; i < maxChannels; ++i)
        {
            frameCounters[i] = 0;
            nextTriggers[i] = 0;
            remaining[i] = 0;
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
    }

    void reset() override
    {
        for (int i = 0; i < maxChannels; ++i)
        {
            frameCounters[i] = 0;
            nextTriggers[i] = 0;
            remaining[i] = 0;
        }

        std::fill (heldFrame.begin(), heldFrame.end(), std::complex<float> { 0.0f, 0.0f });
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double) override
    {
        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        jassert (preparedBins == numBins);

        const int offset = currentChannel * preparedBins;
        int& frameCounter = frameCounters[currentChannel];
        int& nextTrigger = nextTriggers[currentChannel];
        int& framesLeft = remaining[currentChannel];

        if (frameCounter >= nextTrigger)
        {
            for (int i = 0; i < numBins; ++i)
                heldFrame[(size_t) (offset + i)] = spectrum[(size_t) i];

            framesLeft = holdLength;
            const int baseInterval = juce::jmax (1, (int) std::round (rateFrames));
            nextTrigger = frameCounter + baseInterval;
        }

        if (framesLeft > 0)
        {
            for (int i = 1; i < numBins - 1; ++i)
                spectrum[(size_t) i] = spectrum[(size_t) i] * (1.0f - amount) + heldFrame[(size_t) (offset + i)] * amount;
            --framesLeft;
        }

        ++frameCounter;
    }

private:
    static constexpr int maxChannels = 2;

    bool enabled { false };
    float amount { 0.0f };
    float rateFrames { 8.0f };
    int holdLength { 8 };

    int preparedBins { 0 };
    int currentChannel { 0 };
    int frameCounters[maxChannels] { 0, 0 };
    int nextTriggers[maxChannels] { 0, 0 };
    int remaining[maxChannels] { 0, 0 };

    std::vector<std::complex<float>> heldFrame;
};

} // namespace scrr::dsp

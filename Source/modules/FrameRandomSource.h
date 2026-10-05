// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace scrr::dsp {

class FrameRandomSource
{
public:
    void prepare()
    {
        values.assign (4096, 0u);
        reset();
    }

    void beginChannel (int channelIndex)
    {
        currentChannel = channelIndex;
        channelFrame = 0;
    }

    void reset()
    {
        globalFrame = 0;
        channelFrame = 0;
        std::fill (values.begin(), values.end(), 0u);
    }

    uint32_t next (int baseSeed, bool randomEnabled, float rateHz,
                   int numBins, double sampleRate, uint32_t salt)
    {
        if (values.empty())
            prepare();

        const int slot = channelFrame % (int) values.size();
        uint32_t value = (uint32_t) baseSeed;

        if (randomEnabled)
        {
            if (currentChannel == 0)
            {
                const int interval = intervalFrames (rateHz, numBins, sampleRate);
                const uint32_t step = (uint32_t) (globalFrame / interval);
                value = hash32 ((uint32_t) baseSeed ^ salt ^ (step * 0x9e3779b9u));
                values[(size_t) slot] = value;
                ++globalFrame;
            }
            else
            {
                value = values[(size_t) slot];
            }
        }

        ++channelFrame;
        return value;
    }

    static uint32_t hash32 (uint32_t x) noexcept
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static float toUnitFloat (uint32_t x) noexcept
    {
        return (float) hash32 (x) / 4294967295.0f;
    }

private:
    static int intervalFrames (float rateHz, int numBins, double sampleRate)
    {
        const int fftSize = juce::jmax (2, (numBins - 1) * 2);
        const int hopSize = juce::jmax (1, fftSize / 4);
        const float frameRate = (float) sampleRate / (float) hopSize;
        return juce::jmax (1, (int) std::round (frameRate / juce::jmax (0.01f, rateHz)));
    }

    int currentChannel { 0 };
    int channelFrame { 0 };
    int globalFrame { 0 };
    std::vector<uint32_t> values;
};

} // namespace scrr::dsp

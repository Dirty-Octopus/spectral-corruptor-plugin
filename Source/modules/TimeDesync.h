// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class TimeDesyncModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "TimeDesync"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        preparedBins = numBins;
        history.assign ((size_t) maxFrames * (size_t) numBins, std::complex<float> { 0.0f, 0.0f });
        writeIndex = 0;
        filled = 0;
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        magDelay = juce::jlimit (0, maxFrames - 1, (int) std::round ((float) state.getProperty ("magDelay", 1.0f)));
        phaseDelay = juce::jlimit (0, maxFrames - 1, (int) std::round ((float) state.getProperty ("phaseDelay", 8.0f)));
    }

    void reset() override
    {
        std::fill (history.begin(), history.end(), std::complex<float> { 0.0f, 0.0f });
        writeIndex = 0;
        filled = 0;
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (numBins <= 2 || preparedBins != numBins)
            return;

        for (int i = 0; i < numBins; ++i)
            history[(size_t) writeIndex * (size_t) preparedBins + (size_t) i] = spectrum[(size_t) i];

        filled = juce::jmin (maxFrames, filled + 1);

        if (enabled && amount > 0.0f && filled > 1)
        {
            const int safeMagDelay = juce::jmin (magDelay, filled - 1);
            const int safePhaseDelay = juce::jmin (phaseDelay, filled - 1);
            const int magIndex = (writeIndex + maxFrames - safeMagDelay) % maxFrames;
            const int phaseIndex = (writeIndex + maxFrames - safePhaseDelay) % maxFrames;

            for (int i = 1; i < numBins - 1; ++i)
            {
                const auto magSource = history[(size_t) magIndex * (size_t) preparedBins + (size_t) i];
                const auto phaseSource = history[(size_t) phaseIndex * (size_t) preparedBins + (size_t) i];
                const auto desynced = std::polar (std::abs (magSource), std::arg (phaseSource));
                spectrum[(size_t) i] = spectrum[(size_t) i] * (1.0f - amount) + desynced * amount;
            }
        }

        writeIndex = (writeIndex + 1) % maxFrames;
    }

private:
    static constexpr int maxFrames = 64;

    bool enabled { false };
    float amount { 0.0f };
    int magDelay { 1 };
    int phaseDelay { 8 };
    int preparedBins { 0 };
    int writeIndex { 0 };
    int filled { 0 };
    std::vector<std::complex<float>> history;
};

} // namespace scrr::dsp

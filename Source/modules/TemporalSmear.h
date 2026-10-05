// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class TemporalSmearModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "TemporalSmear"; }

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
        length = juce::jlimit (1, maxFrames, (int) std::round ((float) state.getProperty ("length", 12.0f)));
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

        if (enabled && amount > 0.0f)
        {
            float inputPower = 0.0f;
            for (int i = 1; i < numBins - 1; ++i)
                inputPower += std::norm (spectrum[(size_t) i]);

            const int frames = juce::jmin (length, filled);
            for (int i = 1; i < numBins - 1; ++i)
            {
                std::complex<float> sum { 0.0f, 0.0f };
                for (int f = 0; f < frames; ++f)
                {
                    const int index = (writeIndex + maxFrames - f) % maxFrames;
                    sum += history[(size_t) index * (size_t) preparedBins + (size_t) i];
                }

                const auto average = sum / (float) frames;
                spectrum[(size_t) i] = spectrum[(size_t) i] * (1.0f - amount) + average * amount;
            }

            compensatePower (spectrum, numBins, inputPower);
        }

        writeIndex = (writeIndex + 1) % maxFrames;
    }

private:
    static constexpr int maxFrames = 64;

    static void compensatePower (std::complex<float>* spectrum, int numBins, float inputPower)
    {
        float outputPower = 0.0f;
        for (int i = 1; i < numBins - 1; ++i)
            outputPower += std::norm (spectrum[(size_t) i]);

        if (inputPower > 0.0f && outputPower > 0.0f)
        {
            const float gain = juce::jlimit (0.5f, 2.5f, std::sqrt (inputPower / outputPower));
            for (int i = 1; i < numBins - 1; ++i)
                spectrum[(size_t) i] *= gain;
        }
    }

    bool enabled { false };
    float amount { 0.0f };
    int length { 12 };
    int preparedBins { 0 };
    int writeIndex { 0 };
    int filled { 0 };
    std::vector<std::complex<float>> history;
};

} // namespace scrr::dsp

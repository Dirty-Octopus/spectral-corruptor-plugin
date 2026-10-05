// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class SpectralDispersionModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralDispersion"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        preparedBins = numBins;
        for (int& index : writeIndices)
            index = 0;
        history.assign ((size_t) maxChannels * (size_t) maxDelayFrames * (size_t) numBins,
                        std::complex<float> { 0.0f, 0.0f });
    }

    void beginChannel (int channelIndex) override
    {
        currentChannel = juce::jlimit (0, maxChannels - 1, channelIndex);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        curve = juce::jlimit (0.0f, 200.0f, (float) state.getProperty ("curve", 50.0f));
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);
        if (numBins <= 2)
            return;

        jassert (preparedBins == numBins);

        const int channelOffset = currentChannel * maxDelayFrames * preparedBins;
        const int writeIndex = writeIndices[currentChannel];

        for (int i = 0; i < numBins; ++i)
            history[(size_t) channelOffset + (size_t) writeIndex * (size_t) preparedBins + (size_t) i] = spectrum[(size_t) i];

        if (! enabled || amount <= 0.0f)
        {
            writeIndices[currentChannel] = (writeIndex + 1) % maxDelayFrames;
            return;
        }

        float inputPower = 0.0f;
        for (int i = 1; i < numBins - 1; ++i)
            inputPower += std::norm (spectrum[(size_t) i]);

        const float wet = juce::jlimit (0.0f, 1.0f, amount * 1.35f);
        const int maxDelay = juce::jlimit (1, maxDelayFrames - 1, (int) std::round (wet * (float) (maxDelayFrames - 1)));
        const float norm = curve / 200.0f;
        for (int i = 1; i < numBins - 1; ++i)
        {
            const float x = (float) (i - 1) / (float) juce::jmax (1, numBins - 3);
            float shaped;
            if (norm <= 0.5f)
            {
                const float exponent = 0.4f + norm * 1.2f;
                shaped = std::pow (x, exponent);
            }
            else
            {
                const float exponent = 0.4f + (1.0f - norm) * 1.2f;
                shaped = std::pow (1.0f - x, exponent);
            }
            const int delay = juce::jlimit (0, maxDelay, (int) std::round (shaped * (float) maxDelay));
            const int readIndex = (writeIndex + maxDelayFrames - delay) % maxDelayFrames;
            const float phaseOffset = shaped * wet * juce::MathConstants<float>::twoPi * 3.0f;
            const auto delayed = history[(size_t) channelOffset + (size_t) readIndex * (size_t) preparedBins + (size_t) i]
                               * std::polar (1.0f, phaseOffset);
            spectrum[(size_t) i] = spectrum[(size_t) i] * (1.0f - wet) + delayed * wet;
        }

        compensatePower (spectrum, numBins, inputPower);

        writeIndices[currentChannel] = (writeIndex + 1) % maxDelayFrames;
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr int maxDelayFrames = 96;

    static void compensatePower (std::complex<float>* spectrum, int numBins, float inputPower)
    {
        float outputPower = 0.0f;
        for (int i = 1; i < numBins - 1; ++i)
            outputPower += std::norm (spectrum[(size_t) i]);

        if (inputPower > 0.0f && outputPower > 0.0f)
        {
            const float gain = juce::jlimit (0.5f, 2.0f, std::sqrt (inputPower / outputPower));
            for (int i = 1; i < numBins - 1; ++i)
                spectrum[(size_t) i] *= gain;
        }
    }

    bool enabled { false };
    float amount { 0.0f };
    float curve { 50.0f };
    int preparedBins { 0 };
    int currentChannel { 0 };
    int writeIndices[maxChannels] { 0, 0 };
    std::vector<std::complex<float>> history;
};

} // namespace scrr::dsp

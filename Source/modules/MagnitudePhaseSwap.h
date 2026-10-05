// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class MagnitudePhaseSwapModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "MagnitudePhaseSwap"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        previousPhase.assign ((size_t) numBins, 0.0f);
        currentPhase.assign ((size_t) numBins, 0.0f);
        previousMag.assign ((size_t) numBins, 0.0f);
        currentMag.assign ((size_t) numBins, 0.0f);
        hasPrevious = false;
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
    }

    void reset() override { hasPrevious = false; }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (numBins <= 2)
            return;

        jassert ((int) previousPhase.size() >= numBins);
        jassert ((int) currentPhase.size() >= numBins);

        for (int i = 1; i < numBins - 1; ++i)
        {
            currentPhase[(size_t) i] = std::arg (spectrum[(size_t) i]);
            currentMag[(size_t) i] = std::abs (spectrum[(size_t) i]);
        }

        if (enabled && amount > 0.0f && hasPrevious)
        {
            float inputPower = 0.0f;
            for (int i = 1; i < numBins - 1; ++i)
                inputPower += std::norm (spectrum[(size_t) i]);

            const int offset = juce::jmax (1, (int) std::round (amount * 48.0f));
            const int interior = numBins - 2;
            for (int i = 1; i < numBins - 1; ++i)
            {
                const int wrapped = 1 + ((i - 1 + offset) % interior);
                const float mag = previousMag[(size_t) wrapped];
                const float phase = previousPhase[(size_t) i] + std::sin ((float) i * 0.071f) * amount * juce::MathConstants<float>::pi;
                const auto swapped = std::polar (mag, phase);
                spectrum[(size_t) i] = spectrum[(size_t) i] * (1.0f - amount) + swapped * amount;
            }

            compensatePower (spectrum, numBins, inputPower);
        }

        for (int i = 1; i < numBins - 1; ++i)
        {
            previousPhase[(size_t) i] = currentPhase[(size_t) i];
            previousMag[(size_t) i] = currentMag[(size_t) i];
        }
        hasPrevious = true;
    }

private:
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
    bool hasPrevious { false };
    std::vector<float> previousPhase;
    std::vector<float> currentPhase;
    std::vector<float> previousMag;
    std::vector<float> currentMag;
};

} // namespace scrr::dsp

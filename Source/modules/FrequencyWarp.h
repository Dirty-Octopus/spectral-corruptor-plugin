// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class FrequencyWarpModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "FrequencyWarp"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        scratch.resize ((size_t) numBins);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        shape = juce::jlimit (-1.0f, 1.0f, (float) state.getProperty ("shape", 50.0f) / 100.0f);
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 3)
            return;

        jassert ((int) scratch.size() >= numBins);
        float inputPower = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            scratch[(size_t) i] = spectrum[(size_t) i];
            if (i > 0 && i < numBins - 1)
                inputPower += std::norm (spectrum[(size_t) i]);
        }

        const int interior = numBins - 2;
        const float exponent = std::pow (2.0f, shape * 2.0f);

        for (int i = 1; i < numBins - 1; ++i)
        {
            const float x = (float) (i - 1) / (float) juce::jmax (1, interior - 1);
            const float curved = std::pow (x, exponent);
            const float srcNorm = juce::jlimit (0.0f, 1.0f, x + amount * (curved - x));
            const float srcPos = 1.0f + srcNorm * (float) (interior - 1);
            const int a = juce::jlimit (1, numBins - 2, (int) std::floor (srcPos));
            const int b = juce::jlimit (1, numBins - 2, a + 1);
            const float frac = srcPos - (float) a;
            spectrum[(size_t) i] = scratch[(size_t) a] * (1.0f - frac) + scratch[(size_t) b] * frac;
        }

        compensatePower (spectrum, numBins, inputPower);
        compensateHighShelf (spectrum, numBins, amount);
    }

private:
    static void compensateHighShelf (std::complex<float>* spectrum, int numBins, float amount)
    {
        const int interior = numBins - 2;
        const int shelfStart = 1 + interior / 4;
        for (int i = shelfStart; i < numBins - 1; ++i)
        {
            const float x = (float) (i - shelfStart) / (float) juce::jmax (1, numBins - 2 - shelfStart);
            const float boost = 1.0f + amount * x * 0.8f;
            spectrum[(size_t) i] *= boost;
        }
    }

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
    float shape { 0.5f };
    std::vector<std::complex<float>> scratch;
};

} // namespace scrr::dsp

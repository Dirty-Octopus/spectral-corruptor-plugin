// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <cmath>
#include <complex>

namespace scrr::dsp {

class ComplexRotationModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "ComplexRotation"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate, numBins);
        randomAmountSource.prepare();
    }

    void beginChannel (int channelIndex) override
    {
        randomAmountSource.beginChannel (channelIndex);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        randomAmount = state.getProperty ("randomAmount", false);
        randomRate = (float) state.getProperty ("randomRate", 2.0f);
    }

    void reset() override { randomAmountSource.reset(); }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        float effectiveAmount = amount;
        if (randomAmount)
        {
            const uint32_t random = randomAmountSource.next (91357, true, randomRate,
                                                             numBins, sampleRate, 0x434d504cu);
            const float swing = 0.05f + 1.95f * FrameRandomSource::toUnitFloat (random);
            effectiveAmount = juce::jlimit (0.0f, 1.0f, amount * swing);
        }

        const auto rotation = std::polar (1.0f, effectiveAmount * juce::MathConstants<float>::pi);
        for (int i = 1; i < numBins - 1; ++i)
            spectrum[(size_t) i] *= rotation;
    }

private:
    bool enabled { false };
    float amount { 0.0f };
    bool randomAmount { false };
    float randomRate { 2.0f };
    FrameRandomSource randomAmountSource;
};

} // namespace scrr::dsp

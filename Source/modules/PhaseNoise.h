// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <cmath>
#include <complex>
#include <cstdint>

namespace scrr::dsp {

class PhaseNoiseModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "PhaseNoise"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate, numBins);
        seedRandom.prepare();
    }

    void beginChannel (int channelIndex) override
    {
        seedRandom.beginChannel (channelIndex);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        rateHz = juce::jmax (0.01f, (float) state.getProperty ("rate", 8.0f));
    }

    void reset() override { seedRandom.reset(); }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        const uint32_t baseSeed = seedRandom.next (0x504e4f49u, true, rateHz,
                                                   numBins, sampleRate, 0x50484e32u);
        juce::Random rng ((int) baseSeed);

        const float maxPhase = amount * juce::MathConstants<float>::pi;
        const float magDepth = amount * 0.18f;

        for (int i = 1; i < numBins - 1; ++i)
        {
            const float phaseNoise = (rng.nextFloat() * 2.0f - 1.0f) * maxPhase;
            const float magScale = 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * magDepth;

            const float mag = std::abs (spectrum[(size_t) i]) * magScale;
            const float phase = std::arg (spectrum[(size_t) i]) + phaseNoise;
            spectrum[(size_t) i] = std::polar (mag, phase);
        }
    }

private:
    bool enabled { false };
    float amount { 0.0f };
    float rateHz { 8.0f };
    FrameRandomSource seedRandom;
};

} // namespace scrr::dsp

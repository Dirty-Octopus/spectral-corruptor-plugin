// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "FrameRandomSource.h"

#include <cmath>
#include <complex>
#include <cstdint>

namespace scrr::dsp {

class PhaseCorruptionModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "PhaseCorruption"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate, numBins);
    }

    void beginChannel (int) override
    {
        frameCounter = 0;
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;

        const int newSeed = (int) state.getProperty ("seed", 0);
        if (newSeed != seed)
            seed = newSeed;
    }

    void reset() override { frameCounter = 0; }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        const uint32_t baseSeed = (uint32_t) seed;
        juce::Random rng ((int) mixSeed ((int) baseSeed, frameCounter++));
        const float maxOffset = amount * juce::MathConstants<float>::twoPi;
        const float phaseStep = juce::MathConstants<float>::twoPi / juce::jmap (amount, 0.0f, 1.0f, 64.0f, 6.0f);

        for (int i = 1; i < numBins - 1; ++i)
        {
            const float mag = std::abs (spectrum[(size_t) i]);
            float phase = std::arg (spectrum[(size_t) i]) + (rng.nextFloat() * 2.0f - 1.0f) * maxOffset;
            if (rng.nextFloat() < amount * 0.35f)
                phase += juce::MathConstants<float>::pi;
            phase = std::round (phase / phaseStep) * phaseStep;
            spectrum[(size_t) i] = std::polar (mag, phase);
        }
    }

private:
    static uint32_t mixSeed (int baseSeed, int frameIndex) noexcept
    {
        uint32_t x = (uint32_t) baseSeed;
        x ^= 0x9e3779b9u + ((uint32_t) frameIndex << 6) + ((uint32_t) frameIndex >> 2);
        return x;
    }

    bool enabled { false };
    float amount { 0.0f };
    int seed { 0 };

    int frameCounter { 0 };

};

} // namespace scrr::dsp

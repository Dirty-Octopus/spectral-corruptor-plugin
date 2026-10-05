// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <complex>

namespace scrr::dsp {

class SpectralJPEGModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralJPEG"; }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f) / 100.0f;
        blockSize = juce::jlimit (1, 64, (int) std::round ((float) state.getProperty ("blockSize", 8.0f)));
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);

        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        const int maxBlock = juce::jmin (blockSize, numBins - 2);
        const float levels = juce::jmap (amount, 0.0f, 1.0f, 128.0f, 5.0f);
        const float threshold = amount * 0.45f;
        const float phaseStep = juce::MathConstants<float>::twoPi / juce::jmap (amount, 0.0f, 1.0f, 96.0f, 8.0f);

        for (int start = 1; start < numBins - 1; start += maxBlock)
        {
            const int end = juce::jmin (start + maxBlock, numBins - 1);
            float peak = 0.0f;

            for (int i = start; i < end; ++i)
                peak = juce::jmax (peak, std::abs (spectrum[(size_t) i]));

            if (peak <= 0.0f)
                continue;

            for (int i = start; i < end; ++i)
            {
                const float normalized = std::abs (spectrum[(size_t) i]) / peak;
                float quantized = 0.0f;
                if (normalized >= threshold)
                    quantized = std::floor (normalized * levels + 0.5f) / levels;

                const float phase = std::round (std::arg (spectrum[(size_t) i]) / phaseStep) * phaseStep;
                spectrum[(size_t) i] = std::polar (quantized * peak, phase);
            }
        }
    }

private:
    bool enabled { false };
    float amount { 0.0f };
    int blockSize { 8 };
};

} // namespace scrr::dsp

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <complex>

namespace scrr::dsp {

class SpectralFilterModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "SpectralFilter"; }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = (bool) state.getProperty ("enabled", false);
        filterType = (int) state.getProperty ("filterType", 0);
        freqHz = (float) state.getProperty ("freq", 1000.0f);
        slope = (int) state.getProperty ("slope", 1);
        q = (float) state.getProperty ("q", 0.707f);
        mix = (float) state.getProperty ("mix", 100.0f) / 100.0f;
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        if (! enabled || mix <= 0.0f || numBins <= 2)
            return;

        const float nyquist = (float) (sampleRate * 0.5);
        const float binFreq = nyquist / (float) (numBins - 1);
        const float cutoffBin = juce::jlimit (1.0f, (float) (numBins - 2), freqHz / binFreq);
        const float slopeFactor = (float) (1 << slope);
        const float bandwidthBins = juce::jmax (1.0f, cutoffBin / q);
        const float transitionWidth = juce::jmax (1.0f, cutoffBin * 0.15f / slopeFactor);

        for (int i = 1; i < numBins - 1; ++i)
        {
            const float gain = computeGain ((float) i, cutoffBin, bandwidthBins, transitionWidth, slopeFactor);
            const float wetGain = 1.0f - mix + mix * gain;
            spectrum[(size_t) i] *= wetGain;
        }
    }

private:
    float computeGain (float bin, float cutoff, float bandwidth, float transition, float slopeFactor) const
    {
        switch (filterType)
        {
            case 0: // Low Pass: pass below cutoff, attenuate above
            {
                if (bin <= cutoff) return 1.0f;
                const float t = (bin - cutoff) / transition;
                return 1.0f / (1.0f + t * t * slopeFactor);
            }
            case 1: // High Pass: pass above cutoff, attenuate below
            {
                if (bin >= cutoff) return 1.0f;
                const float t = (cutoff - bin) / transition;
                return 1.0f / (1.0f + t * t * slopeFactor);
            }
            case 2: // Band Pass: pass around cutoff, attenuate outside
            {
                const float dist = (bin - cutoff) / bandwidth;
                return 1.0f / (1.0f + dist * dist * slopeFactor);
            }
            case 3: // Notch: attenuate around cutoff, pass outside
            {
                const float dist = (bin - cutoff) / bandwidth;
                return (dist * dist * slopeFactor) / (1.0f + dist * dist * slopeFactor);
            }
            default:
                return 1.0f;
        }
    }

    int filterType { 0 };
    float freqHz { 1000.0f };
    int slope { 1 };
    float q { 0.707f };
    float mix { 1.0f };
    bool enabled { false };
};

} // namespace scrr::dsp

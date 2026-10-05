// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

#include <cmath>
#include <complex>
#include <vector>

namespace scrr::dsp {

class FrequencyDriftModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "FrequencyDrift"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        scratch.resize ((size_t) numBins);
        framePhases.assign (4096, 0.0f);
        phase = 0.0f;
        frameCounter = 0;
    }

    void beginChannel (int channelIndex) override
    {
        currentChannel = channelIndex;
        frameCounter = 0;
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        rateHz = (float) state.getProperty ("rate", 0.25f);
        depthBins = (float) state.getProperty ("depth", 0.0f);
    }

    void reset() override
    {
        phase = 0.0f;
        frameCounter = 0;
    }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        if (! enabled || depthBins <= 0.0f || numBins <= 3)
            return;

        jassert ((int) scratch.size() >= numBins);
        float inputPower = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            scratch[(size_t) i] = spectrum[(size_t) i];
            if (i > 0 && i < numBins - 1)
                inputPower += std::norm (spectrum[(size_t) i]);
        }

        const int phaseIndex = frameCounter % (int) framePhases.size();
        float framePhase = 0.0f;
        if (currentChannel == 0)
        {
            framePhase = phase;
            framePhases[(size_t) phaseIndex] = framePhase;

            const int fftSize = (numBins - 1) * 2;
            const int hopSize = juce::jmax (1, fftSize / 4);
            const float frameRate = (float) sampleRate / (float) hopSize;
            phase += juce::MathConstants<float>::twoPi * rateHz / juce::jmax (1.0f, frameRate);
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
        }
        else
        {
            framePhase = framePhases[(size_t) phaseIndex];
        }

        ++frameCounter;

        for (int i = 1; i < numBins - 1; ++i)
        {
            const float x = (float) (i - 1) / (float) juce::jmax (1, numBins - 3);
            const float drift = std::sin (framePhase + x * juce::MathConstants<float>::twoPi * 3.0f) * depthBins;
            const float srcPos = juce::jlimit (1.0f, (float) (numBins - 2), (float) i + drift);
            const int a = juce::jlimit (1, numBins - 2, (int) std::floor (srcPos));
            const int b = juce::jlimit (1, numBins - 2, a + 1);
            const float frac = srcPos - (float) a;
            spectrum[(size_t) i] = scratch[(size_t) a] * (1.0f - frac) + scratch[(size_t) b] * frac;
        }

        compensatePower (spectrum, numBins, inputPower);
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
    float rateHz { 0.25f };
    float depthBins { 0.0f };
    float phase { 0.0f };
    int currentChannel { 0 };
    int frameCounter { 0 };
    std::vector<float> framePhases;
    std::vector<std::complex<float>> scratch;
};

} // namespace scrr::dsp

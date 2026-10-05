// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "DryWetMixer.h"

namespace scrr::dsp {

void DryWetMixer::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    delayBuffer.setSize (numChannels, maxBlockSize * 4);
    delayBuffer.clear();
    writePos = 0;

    wetMix.reset (sampleRate, 0.02); // 20ms smoothing
    wetMix.setCurrentAndTargetValue (1.0f);

    wetGainBuffer.resize ((size_t) maxBlockSize);
    dryGainBuffer.resize ((size_t) maxBlockSize);
}

void DryWetMixer::setWetDelay (int delaySamples)
{
    wetDelay = juce::jmax (0, delaySamples);
    if (delayBuffer.getNumSamples() < wetDelay * 2 + 16)
        delayBuffer.setSize (delayBuffer.getNumChannels(), wetDelay * 2 + 1024, true);
    reset();
}

void DryWetMixer::setMix (float m)
{
    wetMix.setTargetValue (juce::jlimit (0.0f, 1.0f, m));
}

void DryWetMixer::reset()
{
    delayBuffer.clear();
    writePos = 0;
    wetMix.setCurrentAndTargetValue (wetMix.getCurrentValue());
}

void DryWetMixer::process (const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& wet,
                            juce::AudioBuffer<float>& output)
{
    const int n = dry.getNumSamples();
    const int ch = dry.getNumChannels();
    const int bufSize = delayBuffer.getNumSamples();

    if (n > (int) wetGainBuffer.size())
    {
        wetGainBuffer.resize ((size_t) n);
        dryGainBuffer.resize ((size_t) n);
    }

    const float halfPi = juce::MathConstants<float>::halfPi;

    if (wetMix.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = wetMix.getNextValue();
            wetGainBuffer[(size_t) i] = std::sin (m * halfPi);
            dryGainBuffer[(size_t) i] = std::sin ((1.0f - m) * halfPi);
        }
    }
    else
    {
        const float m = wetMix.getCurrentValue();
        const float wg = std::sin (m * halfPi);
        const float dg = std::sin ((1.0f - m) * halfPi);
        for (int i = 0; i < n; ++i)
        {
            wetGainBuffer[(size_t) i] = wg;
            dryGainBuffer[(size_t) i] = dg;
        }
    }

    for (int c = 0; c < ch; ++c)
    {
        auto* dly = delayBuffer.getWritePointer (c);
        const auto* d = dry.getReadPointer (c);
        const auto* w = wet.getReadPointer (c);
        auto* o = output.getWritePointer (c);

        // Push dry into delay buffer (to align with delayed wet path).
        for (int i = 0; i < n; ++i)
        {
            dly[(writePos + i) % bufSize] = d[i];
        }

        const int readPos = (writePos - wetDelay + bufSize) % bufSize;
        for (int i = 0; i < n; ++i)
        {
            const float delayedDry = dly[(readPos + i) % bufSize];
            o[i] = dryGainBuffer[(size_t) i] * delayedDry + wetGainBuffer[(size_t) i] * w[i];
        }
    }
    writePos = (writePos + n) % bufSize;
}

} // namespace scrr::dsp

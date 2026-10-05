// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "GainStage.h"

namespace scrr::dsp {

void GainStage::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);
    sr = sampleRate;
    gain.reset (sampleRate, 0.02); // 20ms smoothing
    gain.setCurrentAndTargetValue (1.0f);
    gainBuffer.resize ((size_t) maxBlockSize);
}

void GainStage::reset()
{
    gain.setCurrentAndTargetValue (gain.getCurrentValue());
}

void GainStage::setGainDb (float dB)
{
    gain.setTargetValue (juce::Decibels::decibelsToGain (dB, -48.0f));
}

void GainStage::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();

    if (! gain.isSmoothing())
    {
        const float g = gain.getCurrentValue();
        for (int c = 0; c < ch; ++c)
            buffer.applyGain (c, 0, n, g);
        return;
    }

    // Compute smoothed gains once per sample, apply to all channels.
    if (n > (int) gainBuffer.size())
        gainBuffer.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
        gainBuffer[(size_t) i] = gain.getNextValue();

    for (int c = 0; c < ch; ++c)
    {
        auto* w = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            w[i] *= gainBuffer[(size_t) i];
    }
}

} // namespace scrr::dsp

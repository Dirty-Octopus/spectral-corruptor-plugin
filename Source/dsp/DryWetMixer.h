// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace scrr::dsp {

/** Equal-power dry/wet mixer with delay compensation for the wet path. */
class DryWetMixer
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void setWetDelay (int delaySamples);
    void setMix (float m);
    void reset();

    /** mix is driven by setMix() (smoothed); process performs per-sample crossfade. */
    void process (const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& wet,
                  juce::AudioBuffer<float>& output);

private:
    juce::AudioBuffer<float> delayBuffer;
    int writePos { 0 };
    int wetDelay { 0 };

    juce::SmoothedValue<float> wetMix;
    std::vector<float> wetGainBuffer;
    std::vector<float> dryGainBuffer;
};

} // namespace scrr::dsp

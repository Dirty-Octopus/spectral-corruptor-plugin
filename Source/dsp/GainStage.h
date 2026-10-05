// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace scrr::dsp {

/** Simple smoothed gain stage in dB. */
class GainStage
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setGainDb (float dB);
    void process (juce::AudioBuffer<float>& buffer);

private:
    juce::SmoothedValue<float> gain;
    double sr { 44100.0 };
    std::vector<float> gainBuffer;
};

} // namespace scrr::dsp

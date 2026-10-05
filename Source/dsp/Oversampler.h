// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace scrr::dsp {

/** Wraps juce::dsp::Oversampling with factor switching (1/2/4/8x). */
class Oversampler
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, int factor);
    void reset();

    /** Returns oversampled buffer (length = numSamples * factor). */
    juce::dsp::AudioBlock<float> processUp (const juce::dsp::AudioBlock<const float>& input);

    /** Downsamples from the internal oversampled buffer into `output`. */
    void processDown (juce::dsp::AudioBlock<float>& output);

    int  getFactor() const noexcept { return factor; }
    int  getLatencySamples() const noexcept;

private:
    void recreate();

    int factor { 2 };
    int numChannels { 2 };
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> osBuffer;
    double sampleRate { 44100.0 };
    int maxBlockSize { 0 };
};

} // namespace scrr::dsp

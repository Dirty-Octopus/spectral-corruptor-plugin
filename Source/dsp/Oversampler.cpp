// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "Oversampler.h"

namespace scrr::dsp {

void Oversampler::prepare (double sr, int blockSize, int numCh, int f)
{
    sampleRate = sr;
    numChannels = numCh;
    factor = f;
    this->maxBlockSize = blockSize;
    recreate();
}

void Oversampler::recreate()
{
    size_t stages = 0;
    for (int f = factor; f > 1; f >>= 1) ++stages;
    os = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, stages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing ((size_t) maxBlockSize);
    const int osSize = maxBlockSize * factor;
    osBuffer.setSize (numChannels, osSize);
}

void Oversampler::reset()
{
    if (os) os->reset();
    osBuffer.clear();
}

juce::dsp::AudioBlock<float> Oversampler::processUp (const juce::dsp::AudioBlock<const float>& input)
{
    if (factor == 1)
    {
        const int n = (int) input.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch)
            osBuffer.copyFrom (ch, 0, input.getChannelPointer ((size_t) ch), n);
        return juce::dsp::AudioBlock<float> (osBuffer).getSubBlock (0, (size_t) n);
    }
    return os->processSamplesUp (input);
}

void Oversampler::processDown (juce::dsp::AudioBlock<float>& output)
{
    if (factor == 1)
    {
        const int n = (int) output.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (output.getChannelPointer ((size_t) ch),
                                               osBuffer.getReadPointer (ch), n);
        return;
    }
    os->processSamplesDown (output);
}

int Oversampler::getLatencySamples() const noexcept
{
    return factor == 1 ? 0 : (os ? (int) std::lround (os->getLatencyInSamples()) : 0);
}

} // namespace scrr::dsp

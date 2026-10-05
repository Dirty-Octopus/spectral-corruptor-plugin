// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <complex>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace scrr::dsp {

class SpectralModule
{
public:
    virtual ~SpectralModule() = default;

    virtual const char* getName() const noexcept = 0;

    virtual void prepare (double sampleRate, int numBins) { juce::ignoreUnused(sampleRate, numBins); }
    virtual void beginChannel (int channelIndex) { juce::ignoreUnused (channelIndex); }
    virtual void updateParameters (const juce::ValueTree& state) { juce::ignoreUnused (state); }
    virtual void reset() {}
    virtual bool isTimeDomain() const noexcept { return false; }

    virtual void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) = 0;

    /** Time-domain post-processing hook. Called after the spectral chain, before master dry/wet.
        Default is no-op. Override for utility modules that need full buffer access. */
    virtual void processBuffer (juce::AudioBuffer<float>& buffer, double sampleRate) { juce::ignoreUnused (buffer, sampleRate); }
};

class PassthroughModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "Passthrough"; }
    void processSpectrum (std::complex<float>*, int, double) override {}
};

} // namespace scrr::dsp

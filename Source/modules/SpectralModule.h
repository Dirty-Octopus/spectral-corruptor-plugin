// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <complex>
#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace scrr::dsp {

/** Work buffers shared only by serially processed clones of one rack slot.
    Each processSpectrum call must overwrite the elements it reads. Histories,
    phase accumulators and parameter caches must remain private to each clone. */
struct SpectralScratchWorkspace
{
    std::vector<std::complex<float>> complex;
    std::vector<int> indices;
    std::vector<uint8_t> mask;
    std::array<std::vector<float>, 2> floats;
    std::array<std::vector<double>, 2> doubles;
};

class SpectralModule
{
public:
    virtual ~SpectralModule() = default;

    virtual const char* getName() const noexcept = 0;

    virtual void prepare (double sampleRate, int numBins) { juce::ignoreUnused(sampleRate, numBins); }
    /** The actual STFT frame advance, in samples at the processing sample rate. */
    virtual void setHopSize (int samples) { actualHopSize = juce::jmax (0, samples); }
    /** Bind before prepare(); the owner outlives every clone and never processes
        borrowers concurrently. Standalone modules retain their own buffers. */
    void setSharedScratchWorkspace (SpectralScratchWorkspace* workspace) noexcept { sharedScratch = workspace; }
    /** Sum of the analysis window. A bin-centred sine of peak amplitude A has
        magnitude A * windowSum / 2; DC and Nyquist use A * windowSum. */
    virtual void setSpectrumCalibration (double windowSum) { juce::ignoreUnused (windowSum); }
    virtual void beginChannel (int channelIndex) { juce::ignoreUnused (channelIndex); }
    virtual void updateParameters (const juce::ValueTree& state) { juce::ignoreUnused (state); }
    virtual void reset() {}
    virtual void setTempo (double bpm) noexcept { juce::ignoreUnused (bpm); }
    virtual bool isTimeDomain() const noexcept { return false; }

    virtual void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) = 0;

    /** Time-domain post-processing hook. Called after the spectral chain, before master dry/wet.
        Default is no-op. Override for utility modules that need full buffer access. */
    virtual void processBuffer (juce::AudioBuffer<float>& buffer, double sampleRate) { juce::ignoreUnused (buffer, sampleRate); }

protected:
    template <typename T>
    std::vector<T>& scratchBuffer (std::vector<T>& own, size_t index = 0) const noexcept
    {
        if (sharedScratch == nullptr) return own;
        jassert (index < 2);
        if constexpr (std::is_same_v<T, std::complex<float>>) return sharedScratch->complex;
        else if constexpr (std::is_same_v<T, int>) return sharedScratch->indices;
        else if constexpr (std::is_same_v<T, uint8_t>) return sharedScratch->mask;
        else if constexpr (std::is_same_v<T, float>) return sharedScratch->floats[index];
        else if constexpr (std::is_same_v<T, double>) return sharedScratch->doubles[index];
        else { static_assert (! std::is_same_v<T, T>, "Unsupported spectral scratch type"); }
    }

    int getHopSize (int numBins) const noexcept
    {
        // Preserve the historical N/4 default for independent module callers.
        return actualHopSize > 0 ? actualHopSize : juce::jmax (1, (numBins - 1) / 2);
    }

private:
    int actualHopSize {};
    SpectralScratchWorkspace* sharedScratch {};
};

class PassthroughModule : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "Passthrough"; }
    void processSpectrum (std::complex<float>*, int, double) override {}
};

} // namespace scrr::dsp

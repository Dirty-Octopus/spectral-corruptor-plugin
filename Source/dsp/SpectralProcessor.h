// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "FFTEngine.h"
#include "Oversampler.h"
#include "DryWetMixer.h"
#include "GainStage.h"
#include "modules/SpectralModule.h"
#include "modules/ModuleInstance.h"
#include "../parameters/MacroMapping.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <memory>
#include <array>

namespace scrr::dsp {

class SpectralProcessor
{
public:
    SpectralProcessor();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void applyPendingReconfigure();

    void updateGlobalParameters (juce::AudioProcessorValueTreeState& apvts);
    void updateModuleChain (int channel, const juce::ValueTree& chainState);
    void setMacroMappings (const std::vector<scrr::params::MacroMapping>&);
    void applyMacroValues (const scrr::params::ModulationValues&);
    void applyMacroValues (const scrr::params::MacroValues& values) { scrr::params::ModulationValues all {}; std::copy (values.begin(), values.end(), all.begin()); applyMacroValues (all); }
    std::array<float, 4> getPreBandLevels() const noexcept { return preBandLevels; }

    void process (juce::AudioBuffer<float>& buffer);

    int  getLatencySamples() const noexcept;
    int  getFFTSize()  const noexcept { return fft.getFFTSize(); }
    int  getNumBins()  const noexcept { return fft.getNumBins(); }
    int  getOversampleFactor() const noexcept { return displayOversample.load(); }
    float getChannelLevel (int channel) const noexcept { return channelLevels[(size_t) juce::jlimit (0, 3, channel)].load(); }
    void setSoloChannel (int ch) { soloChannel.store (juce::jlimit (0, 3, ch)); }
    bool copyLastMagnitudes (std::vector<float>& out, double* rate = nullptr, bool input = false) const { return fft.copyLastMagnitudes (out, rate, input); }

    double getSampleRate() const noexcept { return displaySampleRate.load(); }

private:
    FFTEngine     fft;
    Oversampler   oversampler;
    DryWetMixer   dryWet;
    GainStage     inputGain;
    GainStage     outputGain;

    struct ModuleSlot
    {
        std::unique_ptr<SpectralModule> module;
        juce::String typeId;
        juce::ValueTree state, baseState;
        bool enabled { true };
        std::vector<scrr::params::MacroMapping> mappings;
    };

    std::array<std::vector<ModuleSlot>, 5> chains; // 0=All, 1-4=Ch1-4
    PassthroughModule passthrough;
    std::vector<scrr::params::MacroMapping> globalMappings;
    scrr::params::ModulationValues macroValues {};
    float globalValue (const juce::String&, float) const noexcept;

    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> osBuffer;
    std::array<juce::AudioBuffer<float>, 4> routeAudio;
    std::array<float, 4> preBandLevels {};
    std::array<bool, 4> channelEnabled { true, true, true, true };
    std::array<float, 4> channelGain { 1, 1, 1, 1 };
    std::array<std::atomic<float>, 4> channelLevels {};

    // Freq split
    bool soloMode { false };
    bool postPreview { false };
    std::atomic<int> soloChannel { 0 }; // 0-3
    int  numSplits { 16 };
    float crossfadePct { 0.0f };
    std::array<std::vector<float>, 4> splitWeights;
    std::array<std::vector<std::complex<float>>, 4> channelBuffers;
    bool splitWeightsDirty { true };

    int   oversampleFactor { 2 };
    int   fftSize          { 2048 };
    int   hopSize          { 512 };
    WindowType windowType  { WindowType::Hann };
    int   pendingOversampleFactor { 2 };
    int   pendingFftSize          { 2048 };
    WindowType pendingWindowType  { WindowType::Hann };
    float frequencyLimit { 22050.0f };
    float dryWetMix        { 1.0f };
    bool  bypass           { false };
    bool  pendingReconfigure { false };

    std::atomic<double> displaySampleRate { 44100.0 };
    std::atomic<int> displayOversample { 2 };
    std::vector<std::complex<float>> moduleDry;
    void processChain (std::vector<ModuleSlot>&, std::complex<float>*, int, double);
    double sampleRate { 44100.0 };
    int    numChannels { 2 };
    int    maxBlockSize { 512 };

    void reconfigure();
    void rebuildChain (int channel, const juce::ValueTree& chainState);
    bool chainNeedsRebuild (int channel, const juce::ValueTree& chainState) const;
    void recomputeSplitWeights (int numBins);
    std::vector<ModuleSlot>& getChain (int channel) { return chains[(size_t) juce::jlimit (0, 4, channel)]; }
};

} // namespace scrr::dsp

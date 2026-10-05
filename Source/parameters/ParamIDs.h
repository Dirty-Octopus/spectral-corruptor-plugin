// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace scrr::params {

namespace id {
    inline const juce::String frequencyLimit { "global.frequencyLimit" };
    inline const juce::String oversample   { "global.oversample" };
    inline const juce::String fftSize      { "global.fftSize" };
    inline const juce::String fftSizeExtended { "global.fftSizeExtended" };
    inline const std::array<juce::String, 4> channelEnabled { "channel.1.enabled", "channel.2.enabled", "channel.3.enabled", "channel.4.enabled" };
    inline const juce::String windowType   { "global.windowType" };
    inline const juce::String drywet       { "global.drywet" };
    inline const juce::String inputGain    { "global.inputGain" };
    inline const juce::String outputGain   { "global.outputGain" };
    inline const juce::String bypass       { "global.bypass" };

    inline const juce::String freqSplitEnabled   { "freqsplit.enabled" };
    inline const juce::String freqSplitSolo      { "freqsplit.solo" };
    inline const juce::String freqSplitPostPreview { "freqsplit.postPreview" };
    inline const juce::String freqSplitNumSplits { "freqsplit.numSplits" };
    inline const juce::String freqSplitCrossfade { "freqsplit.crossfade" };
    inline const std::array<juce::String, 8> macros { "macro.1", "macro.2", "macro.3", "macro.4", "macro.5", "macro.6", "macro.7", "macro.8" };
} // namespace id

namespace choice {
    inline const juce::StringArray frequencyLimit { "11025 Hz", "16000 Hz", "22050 Hz", "24000 Hz", "44100 Hz", "48000 Hz", "96000 Hz" };
    inline const juce::StringArray oversample { "1x", "2x", "4x" };
    inline const juce::StringArray fftSize    { "1024", "2048", "4096", "8192" };
    inline const juce::StringArray fftSizeExtended { "Legacy automation", "128", "256", "512", "1024", "2048", "4096", "8192", "16384", "32768" };
    inline const juce::StringArray windowType { "Hann", "Hamming", "Blackman", "Rectangular" };
} // namespace choice

inline float frequencyLimitValue (const juce::AudioProcessorValueTreeState& v)
{
    static constexpr std::array<float, 7> limits { 11025, 16000, 22050, 24000, 44100, 48000, 96000 };
    const auto index = juce::jlimit (0, (int) limits.size() - 1, (int) v.getRawParameterValue (id::frequencyLimit)->load());
    return limits[(size_t) index];
}

inline int oversampleFactor (const juce::AudioProcessorValueTreeState& v)
{
    const int idx = (int) v.getRawParameterValue (id::oversample)->load();
    return 1 << idx;
}

inline int oversampleFactorFromChoice (const juce::AudioProcessorValueTreeState& v)
{
    return oversampleFactor (v);
}

inline int fftSizeValue (const juce::AudioProcessorValueTreeState& v)
{
    const int extended = (int) v.getRawParameterValue (id::fftSizeExtended)->load();
    if (extended > 0) return 128 << juce::jlimit (0, 8, extended - 1);
    const int idx = (int) v.getRawParameterValue (id::fftSize)->load();
    return 1024 << idx;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace scrr::params

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "ParamIDs.h"

namespace scrr::params {

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { id::oversample, 1 }, "Oversample", choice::oversample, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { id::fftSize, 1 }, "FFT Size", choice::fftSize, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { id::windowType, 1 }, "Window", choice::windowType, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::drywet, 1 }, "Dry/Wet",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::inputGain, 1 }, "Input Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel (" dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::outputGain, 1 }, "Output Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel (" dB")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::bypass, 1 }, "Bypass", false));

    // Frequency Split
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::freqSplitEnabled, 1 }, "Freq Split Enabled", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::freqSplitSolo, 1 }, "Freq Split Solo", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::freqSplitPostPreview, 1 }, "Freq Split Post Preview", false));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { id::freqSplitNumSplits, 1 }, "Freq Split Num Splits",
        1, 64, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::freqSplitCrossfade, 1 }, "Freq Split Crossfade",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel (" %")));

    for (int i = 0; i < 8; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id::macros[(size_t) i], 2 }, "Macro " + String (i + 1),
            NormalisableRange<float> (0.0f, 100.0f, .01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%")));
    // Keep the original four-choice parameter and all existing host indices intact.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { id::fftSizeExtended, 3 }, "FFT Size Extended", choice::fftSizeExtended, 0));
    for (int i = 0; i < 4; ++i)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id::channelEnabled[(size_t) i], 3 }, "CH " + String (i + 1) + " Enabled", true));
    // Append new parameters so existing DAW automation indices remain stable.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { id::frequencyLimit, 4 }, "Spectral Frequency Limit", choice::frequencyLimit, 2));
    return layout;
}

} // namespace scrr::params

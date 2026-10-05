// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace scrr::gui { class EditorContent; }

class SpectralCrrptEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpectralCrrptEditor (SpectralCrrptProcessor&);
    ~SpectralCrrptEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpectralCrrptProcessor& processorRef;
    std::unique_ptr<scrr::gui::EditorContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralCrrptEditor)
};

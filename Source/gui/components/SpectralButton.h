// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Painter.h"
#include "../DefaultPainter.h"

namespace scrr::gui {

/** Self-drawn parameter button (choice / trigger). */
class SpectralButton : public juce::Button
{
public:
    SpectralButton (juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID,
                    const juce::String& label);

    void setPainter (Painter* p) { painter = p; repaint(); }
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    DefaultPainter defaultPainter;
    Painter* painter { &defaultPainter };
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralButton)
};

} // namespace scrr::gui

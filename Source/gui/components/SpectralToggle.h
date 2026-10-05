// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Painter.h"
#include "../DefaultPainter.h"

namespace scrr::gui {

/** Self-drawn toggle (bool parameter) styled as a switch. */
class SpectralToggle : public juce::Component
{
public:
    SpectralToggle (juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID,
                    const juce::String& label);

    void setPainter (Painter* p) { painter = p; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;

private:
    DefaultPainter defaultPainter;
    Painter* painter { &defaultPainter };
    juce::String label;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    bool pressed { false };

    Painter::State stateFromMouse() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralToggle)
};

} // namespace scrr::gui

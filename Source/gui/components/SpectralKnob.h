// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Painter.h"
#include "../DefaultPainter.h"

namespace scrr::gui {

/**
    Self-drawn knob. Owns a Painter (default = procedural). Logic (parameter
    attachment, interaction, value) is fully separated from rendering.
    Swapping to Aseprite sprites later = inject a different Painter.
*/
class SpectralKnob : public juce::Component
{
public:
    SpectralKnob (juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramID,
                  const juce::String& label);

    void setPainter (Painter* p) { painter = p; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    DefaultPainter defaultPainter;
    Painter* painter { &defaultPainter };
    juce::String label;
    bool pressed { false };

    Painter::State stateFromMouse() const;
    float value01() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralKnob)
};

} // namespace scrr::gui

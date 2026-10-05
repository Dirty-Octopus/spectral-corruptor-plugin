// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectralKnob.h"

namespace scrr::gui {

SpectralKnob::SpectralKnob (juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& paramID,
                            const juce::String& lbl)
    : label (lbl)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setVisible (false);
    addChildComponent (slider);
    slider.onValueChange = [this] { repaint(); };
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
}

void SpectralKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    auto knobArea = b.withTrimmedBottom (14.0f);
    painter->paintKnob (g, knobArea, value01(), stateFromMouse());

    const auto& s = Skin::get();
    g.setColour (s.textDim);
    g.setFont (10.0f);
    g.drawText (label, getLocalBounds().removeFromBottom (14),
                juce::Justification::centred);
}

void SpectralKnob::resized()
{
    slider.setBounds (getLocalBounds());
}

float SpectralKnob::value01() const
{
    return (float) juce::jmap (slider.getValue(),
                               slider.getMinimum(), slider.getMaximum(), 0.0, 1.0);
}

Painter::State SpectralKnob::stateFromMouse() const
{
    if (! isEnabled()) return Painter::State::Disabled;
    if (pressed)                    return Painter::State::Down;
    if (isMouseOver())              return Painter::State::Hover;
    return Painter::State::Normal;
}

void SpectralKnob::mouseDown (const juce::MouseEvent& e)
{
    pressed = true;
    slider.mouseDown (e.getEventRelativeTo (&slider));
    repaint();
}

void SpectralKnob::mouseDrag (const juce::MouseEvent& e)
{
    slider.mouseDrag (e.getEventRelativeTo (&slider));
    repaint();
}

void SpectralKnob::mouseUp (const juce::MouseEvent& e)
{
    slider.mouseUp (e.getEventRelativeTo (&slider));
    pressed = false;
    repaint();
}

void SpectralKnob::mouseEnter (const juce::MouseEvent&)
{
    repaint();
}

void SpectralKnob::mouseExit (const juce::MouseEvent&)
{
    repaint();
}

void SpectralKnob::mouseMove (const juce::MouseEvent&)
{
    repaint();
}

void SpectralKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    slider.mouseWheelMove (e.getEventRelativeTo (&slider), w);
}

} // namespace scrr::gui

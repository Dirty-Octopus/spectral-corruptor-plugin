// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectralToggle.h"

namespace scrr::gui {

SpectralToggle::SpectralToggle (juce::AudioProcessorValueTreeState& apvts,
                                const juce::String& paramID,
                                const juce::String& lbl)
    : label (lbl)
{
    toggle.setButtonText (lbl);
    toggle.setClickingTogglesState (true);
    toggle.setVisible (false);
    addChildComponent (toggle);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, toggle);
}

void SpectralToggle::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().withTrimmedBottom (14.0f);
    painter->paintToggle (g, area, toggle.getToggleState(), stateFromMouse());

    const auto& s = Skin::get();
    g.setColour (s.textDim);
    g.setFont (10.0f);
    g.drawText (label, getLocalBounds().removeFromBottom (14), juce::Justification::centred);
}

void SpectralToggle::resized()
{
    toggle.setBounds (getLocalBounds());
}

Painter::State SpectralToggle::stateFromMouse() const
{
    if (! isEnabled()) return Painter::State::Disabled;
    if (pressed) return Painter::State::Down;
    if (isMouseOver()) return Painter::State::Hover;
    return Painter::State::Normal;
}

void SpectralToggle::mouseDown (const juce::MouseEvent&) { pressed = true; repaint(); }

void SpectralToggle::mouseUp (const juce::MouseEvent& e)
{
    if (pressed && getLocalBounds().toFloat().contains (e.position))
        toggle.triggerClick();

    pressed = false;
    repaint();
}

void SpectralToggle::mouseEnter (const juce::MouseEvent&)
{
    repaint();
}

void SpectralToggle::mouseExit (const juce::MouseEvent&)
{
    repaint();
}

void SpectralToggle::mouseMove (const juce::MouseEvent&)
{
    repaint();
}

} // namespace scrr::gui

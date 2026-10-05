// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectralButton.h"

namespace scrr::gui {

SpectralButton::SpectralButton (juce::AudioProcessorValueTreeState& apvts,
                                const juce::String& paramID,
                                const juce::String& lbl)
    : juce::Button (lbl), label (lbl)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, *this);
}

void SpectralButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    Painter::State st = Painter::State::Normal;
    if (! isEnabled())        st = Painter::State::Disabled;
    else if (isDown)          st = Painter::State::Down;
    else if (isHighlighted)   st = Painter::State::Hover;

    painter->paintButton (g, getLocalBounds().toFloat().reduced (1.0f), label, getToggleState(), st);
}

} // namespace scrr::gui

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace scrr::gui {
class ColourPreviewButton : public juce::Button
{
public:
    ColourPreviewButton() : juce::Button ("Choose colour") {}
    void setPreview (juce::Colour value, bool isValid)
    { colour = value; valid = isValid; repaint(); }
    juce::Colour getPreview() const { return colour; }
    bool hasValidPreview() const { return valid; }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto area = getLocalBounds().toFloat().reduced (2);
        g.setColour (colour); g.fillRect (area);
        g.setColour (over || down ? Theme::yellow() : Theme::white()); g.drawRect (area, over ? 2.0f : 1.0f);
        if (! valid) { g.setColour (Theme::red()); g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 2); }
    }
private:
    juce::Colour colour { juce::Colours::black }; bool valid { true };
};

class ColourPaletteEditor : public juce::Component, private juce::ChangeListener
{
public:
    explicit ColourPaletteEditor (juce::Colour colour)
        : selector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace)
    {
        setComponentID ("colour-palette-editor"); selector.setComponentID ("colour-palette-selector");
        selector.setCurrentColour (colour, juce::dontSendNotification);
        selector.setColour (juce::ColourSelector::backgroundColourId, Theme::panel());
        selector.addChangeListener (this); addAndMakeVisible (selector); setSize (310, 360);
    }
    ~ColourPaletteEditor() override { selector.removeChangeListener (this); }
    void resized() override { selector.setBounds (getLocalBounds()); }
    void setColour (juce::Colour colour) { selector.setCurrentColour (colour, juce::dontSendNotification); }
    std::function<void(juce::Colour)> changed;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { if (changed) changed (selector.getCurrentColour().withAlpha (1.0f)); }
    juce::ColourSelector selector;
};
} // namespace scrr::gui

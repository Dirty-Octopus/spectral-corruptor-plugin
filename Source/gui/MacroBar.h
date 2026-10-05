// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"

namespace scrr::gui {
class MacroKnobLook : public InstrumentLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float value,
                          float start, float end, juce::Slider& slider) override
    {
        auto circle = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre ((float) juce::jmin (w, h) - 8, (float) juce::jmin (w, h) - 8);
        const auto c = circle.getCentre(); const float radius = circle.getWidth() * .5f;
        const float angle = start + value * (end - start);
        juce::Path background, arc;
        background.addCentredArc (c.x, c.y, radius, radius, 0, start, end, true);
        arc.addCentredArc (c.x, c.y, radius, radius, 0, start, angle, true);
        g.setColour (Theme::line()); g.strokePath (background, juce::PathStrokeType (3));
        const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        g.setColour (colour); g.strokePath (arc, juce::PathStrokeType (3));
        g.setColour (slider.isMouseOverOrDragging() ? Theme::panel() : Theme::field()); g.fillEllipse (circle.reduced (5));
        g.setColour (Theme::white());
        g.drawLine (c.x, c.y, c.x + std::sin (angle) * (radius - 6), c.y - std::cos (angle) * (radius - 6), 2);
    }
};
class MacroBar : public juce::Component
{
public:
    explicit MacroBar (SpectralCrrptProcessor& p) : processor (p)
    {
        setLookAndFeel (&look); setComponentID ("macro-bar");
        for (int i = 0; i < 8; ++i)
        {
            auto& knob = knobs[(size_t) i]; knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 49, 20); knob.setTextValueSuffix (" %");
            knob.setRange (0, 100, .01); knob.setDoubleClickReturnValue (true, 0); knob.setScrollWheelEnabled (false);
            knob.setComponentID ("macro-" + juce::String (i + 1)); knob.setName ("Macro " + juce::String (i + 1));
            knob.setColour (juce::Slider::rotarySliderFillColourId, Theme::channelColour (i));
            addAndMakeVisible (knob);
            attachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getAPVTS(), scrr::params::id::macros[(size_t) i], knob);
            setupParameterGestures (knob);
            knob.setTooltip ("Alt/Option-drag: fine adjustment. Cmd/Ctrl-click or double-click: reset.");
            auto& name = names[(size_t) i]; name.setComponentID ("macro-name-" + juce::String (i + 1));
            name.setEditable (false, true); name.setFont (Theme::font (11, true));
            name.setColour (juce::Label::backgroundColourId, Theme::panel()); name.setColour (juce::Label::textColourId, Theme::white());
            name.onEditorShow = [this, i] { if (auto* field = names[(size_t) i].getCurrentTextEditor()) field->setInputRestrictions (32); };
            name.onTextChange = [this, i]
            {
                processor.setMacroName (i, names[(size_t) i].getText());
                names[(size_t) i].setText (processor.getMacroName (i), juce::dontSendNotification);
                refreshNames();
            };
            addAndMakeVisible (name);
            auto& button = edit[(size_t) i]; button.setComponentID ("macro-map-" + juce::String (i + 1));
            button.onClick = [this, i] { if (openMappings) openMappings (i); }; addAndMakeVisible (button);
        }
        refresh();
    }
    ~MacroBar() override { setLookAndFeel (nullptr); }
    std::function<void(int)> openMappings;
    void refreshNames()
    {
        const auto revision = processor.getMacroNameRevision();
        if (revision == shownNameRevision) return;
        shownNameRevision = revision;
        for (int i = 0; i < 8; ++i)
        {
            auto& name = names[(size_t) i];
            if (! name.isBeingEdited()) name.setText (processor.getMacroName (i), juce::dontSendNotification);
            name.setTooltip (processor.getMacroLabel (i) + ": double-click to rename. Clear to restore M" + juce::String (i + 1) + ".");
            knobs[(size_t) i].setName (processor.getMacroLabel (i));
        }
    }
    void refresh()
    {
        std::array<int, 8> counts {};
        for (const auto& m : processor.getMacroMappings()) if (m.sourceUid.isEmpty()) ++counts[(size_t) m.macro];
        for (int i = 0; i < 8; ++i)
        {
            edit[(size_t) i].setButtonText (juce::String (counts[(size_t) i]) + " >");
            edit[(size_t) i].setTooltip (juce::String (counts[(size_t) i]) + " mappings. Click to edit assignments.");
        }
        refreshNames();
    }
    void resized() override
    {
        const int width = getWidth() / 8;
        for (int i = 0; i < 8; ++i)
        {
            auto cell = juce::Rectangle<int> (i * width, 0, width, getHeight()).reduced (4, 0);
            auto header = cell.removeFromTop (22);
            edit[(size_t) i].setBounds (header.removeFromRight (34)); header.removeFromRight (3);
            names[(size_t) i].setBounds (header); knobs[(size_t) i].setBounds (cell);
        }
    }
    void paint (juce::Graphics& g) override { g.fillAll (Theme::black()); }
private:
    SpectralCrrptProcessor& processor;
    MacroKnobLook look;
    std::array<juce::Slider, 8> knobs;
    std::array<juce::TextButton, 8> edit;
    std::array<juce::Label, 8> names;
    uint64_t shownNameRevision { ~uint64_t{} };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> attachments;
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../parameters/MacroMapping.h"
namespace scrr::gui {
inline bool duplicateModifier (const juce::ModifierKeys& mods)
{
   #if JUCE_MAC
    return mods.isCommandDown();
   #else
    return mods.isCtrlDown();
   #endif
}
class AssignableSlider : public juce::Slider
{
    class Strip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Strip (AssignableSlider& parent) : owner (parent)
        { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setTooltip ("Drag up/down: modulation depth and direction. Cmd/Ctrl-click: unipolar / bipolar. Right-click: remove this route."); }
        scrr::params::MacroMapping mapping;
        void paint (juce::Graphics& g) override
        {
            const auto colour = mapping.sourceEnabled ? owner.findColour (juce::Slider::trackColourId) : Theme::inactive();
            auto position = [&] (double v) { return (float) (owner.valueToProportionOfLength (juce::jlimit (owner.getMinimum(), owner.getMaximum(), v)) * (getWidth() - 8) + 4); };
            mapping.centre = owner.getValue();
            const float centre = position (mapping.centre);
            const float a = position (mapping.fromBase (mapping.centre, mapping.startOffset));
            const float b = position (mapping.fromBase (mapping.centre, mapping.endOffset));
            const float low = std::min ({ a, b, centre }), high = std::max ({ a, b, centre });
            g.setColour (colour.withAlpha (isMouseOverOrDragging() ? .65f : .32f));
            g.fillRect (low, 3.0f, juce::jmax (2.0f, high - low), 8.0f);
            g.setColour (colour); g.fillRect (centre - .5f, 1.0f, 1.0f, 12.0f);
            const float live = position (mapping.sourceEnabled ? mapping.value (owner.values[(size_t) mapping.valueIndex()]) : mapping.centre);
            g.setColour (owner.findColour (juce::Slider::thumbColourId)); g.fillRect (live - 2, 1.0f, 4.0f, 12.0f);
            Theme::text (g, mapping.sourceLabel() + (mapping.bipolar() ? " +/-" : " +"),
                         { 2, 0, getWidth() - 4, 14 }, 9, colour, true, juce::Justification::centredRight);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            startDepth = mapping.endOffset; startY = e.getScreenY(); toggled = e.mods.isPopupMenu() || duplicateModifier (e.mods);
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu; menu.addItem (1, "REMOVE " + mapping.sourceLabel());
                const auto safe = juce::Component::SafePointer<Strip> (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
                { if (safe && result == 1 && safe->owner.unmapRoute) safe->owner.unmapRoute (safe->mapping); });
            }
            else if (toggled) { mapping.setDepth (startDepth, ! mapping.bipolar()); submit(); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (toggled) return;
            mapping.setDepth (startDepth + (double) (startY - e.getScreenY()) / (e.mods.isShiftDown() ? 1200.0 : 200.0), mapping.bipolar()); submit();
        }
    private:
        void submit() { mapping.centre = owner.getValue(); mapping.syncRange(); if (owner.changeMapping) owner.changeMapping (mapping); repaint(); }
        AssignableSlider& owner; double startDepth {}; int startY {}; bool toggled {};
    };
public:
    std::function<void()> mappingMenu;
    std::function<void(scrr::params::MacroMapping)> changeMapping;
    std::function<void(scrr::params::MacroMapping)> unmapRoute;
    void setMacroControlled (bool value) { mapped = value; setTextBoxIsEditable (true); }
    bool isMacroControlled() const noexcept { return mapped; }
    int modulationHeight() const noexcept { return (int) strips.size() * 15; }
    void setMappings (const std::vector<scrr::params::MacroMapping>& mappings)
    {
        const bool different = strips.size() != mappings.size();
        if (different)
        {
            strips.clear();
            for (size_t i = 0; i < mappings.size(); ++i) { auto s = std::make_unique<Strip> (*this); addAndMakeVisible (*s); strips.push_back (std::move (s)); }
        }
        for (size_t i = 0; i < mappings.size(); ++i) strips[i]->mapping = mappings[i];
        mapped = ! strips.empty(); getProperties().set ("modulationHeight", modulationHeight());
        if (different) resized();
    }
    void refreshModulation (const scrr::params::ModulationValues& next) { values = next; repaint(); }
    void resized() override
    {
        juce::Slider::resized();
        const auto layout = getLookAndFeel().getSliderLayout (*this);
        int y = 0;
        for (auto& strip : strips) { strip->setBounds (layout.sliderBounds.getX(), y, layout.sliderBounds.getWidth(), 15); y += 15; }
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu()) { if (mappingMenu) mappingMenu(); return; }
        juce::Slider::mouseDown (event);
    }
private:
    bool mapped {};
    scrr::params::ModulationValues values {};
    std::vector<std::unique_ptr<Strip>> strips;
};
}

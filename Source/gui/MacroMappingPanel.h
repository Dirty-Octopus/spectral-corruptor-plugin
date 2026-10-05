// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include <cstdlib>

namespace scrr::gui {
class MacroMappingRow : public juce::Component
{
public:
    explicit MacroMappingRow (scrr::params::MacroMapping value) : mapping (std::move (value))
    {
        setComponentID ("mapping-" + mapping.uid + "-" + mapping.parameter.toString());
        const double values[] { mapping.startOffset * 100, mapping.centre, mapping.endOffset * 100 };
        const char* ids[] { "map-low", "map-centre", "map-high" };
        const int decimals = mapping.step > 0 ? juce::jlimit (0, 6, (int) std::ceil (-std::log10 (mapping.step) - 1.0e-5)) : 3;
        for (int i = 0; i < 3; ++i)
        {
            auto& field = fields[(size_t) i]; field.setText (juce::String (values[i], i == 1 ? decimals : 2)); field.setComponentID (ids[i]);
            field.setInputRestrictions (20, "0123456789.-"); addAndMakeVisible (field);
            field.onReturnKey = field.onFocusLost = [this] { submit(); };
        }
        mode.setComponentID ("map-mode");
        mode.addItemList ({ "UNIPOLAR", "BIPOLAR" }, 1);
        mode.setSelectedItemIndex (mapping.bipolar() ? 1 : 0, juce::dontSendNotification);
        mode.onChange = [this] { submit(); }; addAndMakeVisible (mode);
        remove.setButtonText ("UNMAP"); remove.onClick = [this] { if (change) change (mapping, true); }; addAndMakeVisible (remove);
    }
    std::function<void(scrr::params::MacroMapping, bool)> change;
    void resized() override
    {
        auto controls = getLocalBounds().withTrimmedTop (35).reduced (8, 0);
        remove.setBounds (controls.removeFromRight (68).withHeight (26)); controls.removeFromRight (6);
        mode.setBounds (controls.removeFromRight (230).withHeight (26)); controls.removeFromRight (8);
        for (auto& field : fields) { field.setBounds (controls.removeFromLeft (90).withHeight (26)); controls.removeFromLeft (8); }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel());
        Theme::text (g, mapping.label + mapping.unit, { 8, 1, getWidth() - 16, 18 }, 12, Theme::white(), true);
        const char* labels[] { "START / %", "BASE / CENTRE", "END / %" };
        for (int i = 0; i < 3; ++i) Theme::text (g, labels[i], { 8 + i * 98, 19, 90, 15 }, 9, Theme::dim());
        g.setColour (Theme::line()); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
private:
    void submit()
    {
        auto next = mapping; double* values[] { &next.startOffset, &next.centre, &next.endOffset };
        for (int i = 0; i < 3; ++i)
        {
            const auto text = fields[(size_t) i].getText().trim().toStdString(); char* end {};
            const double value = std::strtod (text.c_str(), &end);
            if (text.empty() || end == text.c_str() || *end != 0 || ! std::isfinite (value)) return;
            *values[i] = value;
        }
        next.startOffset *= .01; next.endOffset *= .01;
        next.mode = mode.getSelectedItemIndex() == 1 ? scrr::params::MacroMapping::bipolarLeftLow : scrr::params::MacroMapping::unipolarRight;
        const double tolerance = juce::jmax (1.0e-6, mapping.step * .001);
        if (next.mode == mapping.mode && std::abs (next.startOffset - mapping.startOffset) < tolerance
            && std::abs (next.centre - mapping.centre) < tolerance && std::abs (next.endOffset - mapping.endOffset) < tolerance) return;
        mapping = next;
        if (change) change (next, false);
    }
    scrr::params::MacroMapping mapping;
    std::array<juce::TextEditor, 3> fields;
    juce::ComboBox mode;
    juce::TextButton remove;
};
class MacroMappingPanel : public juce::Component
{
public:
    explicit MacroMappingPanel (SpectralCrrptProcessor& p) : processor (p)
    {
        setComponentID ("macro-mappings");
        view.setViewedComponent (&list, false); view.setScrollBarsShown (true, false); addAndMakeVisible (view);
        close.onClick = [this] { setVisible (false); }; addAndMakeVisible (close);
    }
    ~MacroMappingPanel() override { view.setViewedComponent (nullptr, false); }
    void showMacro (int index) { sourceUid.clear(); mappingTitle = "MACRO " + juce::String (index + 1); macro = index; refresh(); setVisible (true); toFront (true); }
    void showSource (const juce::String& uid)
    {
        sourceUid = uid; mappingTitle = "MODULATION";
        for (const auto& source : processor.getModulators()) if (source.uid == uid) mappingTitle = source.name;
        refresh(); setVisible (true); toFront (true);
    }
    void refresh()
    {
        rows.clear();
        for (auto mapping : processor.getMacroMappings()) if (sourceUid.isEmpty() ? mapping.sourceUid.isEmpty() && mapping.macro == macro : mapping.sourceUid == sourceUid)
        {
            auto row = std::make_unique<MacroMappingRow> (mapping);
            const auto safe = juce::Component::SafePointer<MacroMappingPanel> (this);
            row->change = [safe] (scrr::params::MacroMapping next, bool remove)
            {
                juce::MessageManager::callAsync ([safe, next, remove]
                {
                    if (! safe) return;
                    if (remove) safe->processor.removeMacroMapping (next.channel, next.uid, next.parameter, next.macro, next.sourceUid);
                    else safe->processor.updateMacroMapping (next);
                    safe->refresh();
                });
            };
            list.addAndMakeVisible (*row); rows.push_back (std::move (row));
        }
        resized(); repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (18); close.setBounds (area.removeFromTop (30).removeFromRight (92));
        area.removeFromTop (48); view.setBounds (area);
        const int width = juce::jmax (640, view.getWidth() - 16); int y = 0;
        for (auto& row : rows) { row->setBounds (0, y, width, 74); y += 74; }
        list.setSize (width, juce::jmax (y, view.getHeight()));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, (sourceUid.isEmpty() ? processor.getMacroLabel (macro) : mappingTitle) + " / MAPPINGS", { 20, 16, getWidth() - 140, 35 }, 25, Theme::yellow(), true);
        Theme::text (g, "Signed offsets add to the base. Drag strips above sliders; Cmd/Ctrl-click switches Uni/Bipolar.", { 20, 58, getWidth() - 40, 28 }, 12, Theme::dim());
        if (rows.empty()) Theme::text (g, "NO PARAMETERS ASSIGNED", getLocalBounds().reduced (30).withTrimmedTop (100), 18, Theme::white(), true, juce::Justification::centred);
    }
private:
    SpectralCrrptProcessor& processor;
    int macro {};
    juce::String sourceUid, mappingTitle;
    juce::Viewport view;
    juce::Component list;
    juce::TextButton close { "CLOSE  x" };
    std::vector<std::unique_ptr<MacroMappingRow>> rows;
};
}

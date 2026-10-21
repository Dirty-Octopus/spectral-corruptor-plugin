// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include "components/TransferCurveEditor.h"
#include "components/CurveCoordinateMenu.h"
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
        remove.setButtonText ("UNMAP"); remove.onClick = [this] { if (change) change (mapping, true, false); }; addAndMakeVisible (remove);
        response.setComponentID ("map-response-curve");
        response.setConstraints (true, mapping.bipolar()); response.setCurve (mapping.responseCurve);
        response.setAxisLabels ("SOURCE VALUE", "PARAMETER RESPONSE");
        response.onChange = [this] (scrr::params::TransferCurve next)
        {
            if (mapping.responseCurve.sameStateAs (next)) return;
            mapping.responseCurve = std::move (next);
            if (change) change (mapping, false, true);
        };
        response.nodeMenu = [this] (juce::Component& target, uint32_t point)
        {
            const bool available = ! scrr::params::isCurveCoordinateParameter (mapping.parameter) && mapping.routeUid.isNotEmpty();
            const auto safe = juce::Component::SafePointer<MacroMappingRow> (this);
            const auto routeUid = mapping.routeUid;
            showCurveCoordinateMenu (target, available && response.canMapCoordinate (point, true),
                available && response.canMapCoordinate (point, false), "Source position", "Response value",
                [safe, routeUid, point] (bool xAxis)
                {
                    if (! safe || safe->mapping.routeUid != routeUid || ! safe->response.canMapCoordinate (point, xAxis)) return;
                    if (safe->mappingMenu) safe->mappingMenu (safe->response, routeUid, scrr::params::curveCoordinateParameter (point, xAxis));
                });
        };
        if (scrr::params::isCurveCoordinateParameter (mapping.parameter))
            response.setTooltip ("This curve shapes a node-coordinate control. Further nested node mappings are unavailable. Double-click to add or remove points.");
        addAndMakeVisible (response);
        reset.setButtonText ("LINEAR"); reset.setComponentID ("map-curve-reset");
        reset.setTooltip ("Reset this connection's response to a straight line.");
        reset.onClick = [this] { response.reset(); }; addAndMakeVisible (reset);
    }
    std::function<void(scrr::params::MacroMapping, bool, bool)> change;
    std::function<void(juce::Component&, const juce::String&, const juce::Identifier&)> mappingMenu;
    void refreshPreview (SpectralCrrptProcessor& processor, const std::vector<scrr::params::MacroMapping>& routes)
    {
        if (response.isEditing()) return;
        for (const auto& current : routes) if (current.routeUid == mapping.routeUid)
        {
            mapping.responseCurve = current.responseCurve;
            response.setCurve (mapping.responseCurve);
            if (scrr::params::isCurveCoordinateParameter (mapping.parameter)) { response.clearPreviewCurve(); return; }
            auto effective = mapping.responseCurve;
            for (auto& point : effective.points)
            {
                point.x = (float) processor.getEffectiveValue (scrr::params::MacroMapping::responseChannel, mapping.routeUid,
                    scrr::params::curveCoordinateParameter (point.id, true), point.x);
                point.y = (float) processor.getEffectiveValue (scrr::params::MacroMapping::responseChannel, mapping.routeUid,
                    scrr::params::curveCoordinateParameter (point.id, false), point.y);
            }
            response.setPreviewCurve (effective); return;
        }
    }
    void resized() override
    {
        auto controls = getLocalBounds().withTrimmedTop (35).reduced (8, 0);
        remove.setBounds (controls.removeFromRight (68).withHeight (26)); controls.removeFromRight (6);
        mode.setBounds (controls.removeFromRight (150).withHeight (26)); controls.removeFromRight (8);
        for (auto& field : fields) { field.setBounds (controls.removeFromLeft (90).withHeight (26)); controls.removeFromLeft (8); }
        reset.setBounds (getWidth() - 92, 74, 84, 23);
        response.setBounds (8, 103, getWidth() - 16, getHeight() - 112);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel());
        Theme::text (g, mapping.label + mapping.unit, { 8, 1, getWidth() - 16, 18 }, 12, Theme::white(), true);
        const char* labels[] { "START / %", "BASE / CENTRE", "END / %" };
        for (int i = 0; i < 3; ++i) Theme::text (g, labels[i], { 8 + i * 98, 19, 90, 15 }, 9, Theme::dim());
        Theme::text (g, mapping.bipolar() ? "RESPONSE CURVE / 50% STAYS AT BASE" : "RESPONSE CURVE / SOURCE > TARGET", { 8, 74, getWidth() - 110, 23 }, 10, Theme::yellow(), true);
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
        next.startOffset = juce::jlimit (-1.0, 1.0, next.startOffset); next.endOffset = juce::jlimit (-1.0, 1.0, next.endOffset);
        next.centre = juce::jlimit (next.minimum, next.maximum, next.centre);
        next.mode = mode.getSelectedItemIndex() == 1 ? scrr::params::MacroMapping::bipolarLeftLow : scrr::params::MacroMapping::unipolarRight;
        next.responseCurve.sanitise (true, next.bipolar());
        const double tolerance = juce::jmax (1.0e-6, mapping.step * .001);
        if (next.mode == mapping.mode && std::abs (next.startOffset - mapping.startOffset) < tolerance
            && std::abs (next.centre - mapping.centre) < tolerance && std::abs (next.endOffset - mapping.endOffset) < tolerance) return;
        mapping = next;
        response.setConstraints (true, mapping.bipolar()); response.setCurve (mapping.responseCurve);
        fields[0].setText (juce::String (mapping.startOffset * 100, 2), false);
        fields[1].setText (juce::String (mapping.centre, 3), false);
        fields[2].setText (juce::String (mapping.endOffset * 100, 2), false);
        repaint();
        if (change) change (next, false, false);
    }
    scrr::params::MacroMapping mapping;
    std::array<juce::TextEditor, 3> fields;
    juce::ComboBox mode;
    juce::TextButton remove;
    juce::TextButton reset;
    TransferCurveEditor response;
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
    std::function<void(juce::Component&, const juce::String&, const juce::Identifier&)> mappingMenu;
    void tick()
    {
        if (! isVisible()) return;
        const auto routes = processor.getMacroMappings();
        for (auto& row : rows) row->refreshPreview (processor, routes);
        repaint();
    }
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
            row->mappingMenu = [safe] (juce::Component& target, const juce::String& routeUid, const juce::Identifier& parameter)
            { if (safe && safe->mappingMenu) safe->mappingMenu (target, routeUid, parameter); };
            row->change = [safe] (scrr::params::MacroMapping next, bool remove, bool responseOnly)
            {
                juce::MessageManager::callAsync ([safe, next, remove, responseOnly]
                {
                    if (! safe) return;
                    if (remove)
                    {
                        safe->processor.removeMacroMapping (next.channel, next.uid, next.parameter, next.macro, next.sourceUid);
                        safe->refresh();
                    }
                    else if (responseOnly)
                    {
                        // The host can automate the editable base while this
                        // panel is open. A curve gesture changes only its curve.
                        for (auto current : safe->processor.getMacroMappings())
                            if (current.sameTarget (next) && current.sameSource (next))
                            {
                                current.responseCurve = next.responseCurve;
                                safe->processor.updateMacroMapping (current); break;
                            }
                    }
                    else { safe->processor.updateMacroMapping (next); safe->refresh(); }
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
        for (auto& row : rows) { row->setBounds (0, y, width, 270); y += 278; }
        list.setSize (width, juce::jmax (y, view.getHeight()));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, (sourceUid.isEmpty() ? processor.getMacroLabel (macro) : mappingTitle) + " / MAPPINGS", { 20, 16, getWidth() - 140, 35 }, 25, Theme::yellow(), true);
        Theme::text (g, "Double-click: add/remove. Right-click: assign X/Y. Yellow: base; blue: modulated response.", { 20, 58, getWidth() - 40, 28 }, 12, Theme::dim());
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

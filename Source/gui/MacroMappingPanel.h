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
    class CurveDisclosure : public juce::Button
    {
    public:
        CurveDisclosure() : juce::Button ("RESPONSE CURVE") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            if (over || down) { g.setColour (Theme::field()); g.fillRect (getLocalBounds()); }
            juce::Path arrow;
            const float y = (float) getHeight() * .5f;
            if (getToggleState()) arrow.addTriangle (8, y - 3, 16, y - 3, 12, y + 3);
            else arrow.addTriangle (9, y - 4, 9, y + 4, 15, y);
            g.setColour (Theme::yellow()); g.fillPath (arrow);
            Theme::text (g, getButtonText(), getLocalBounds().withTrimmedLeft (24), 10, Theme::yellow(), true);
        }
    };
public:
    explicit MacroMappingRow (scrr::params::MacroMapping definition) : mapping (std::move (definition))
    {
        setComponentID ("mapping-" + mapping.uid + "-" + mapping.parameter.toString());
        const char* ids[] { "map-low", "map-centre", "map-high" };
        for (int i = 0; i < 3; ++i)
        {
            auto& field = fields[(size_t) i]; field.setComponentID (ids[i]);
            field.setSliderStyle (juce::Slider::LinearHorizontal);
            field.setTextBoxStyle (juce::Slider::TextBoxRight, false, i == 1 ? 100 : 78, 24);
            field.getProperties().set ("scrThemeParameterSlider", true); refreshSliderColours (field);
            field.setScrollWheelEnabled (false); field.setPopupDisplayEnabled (false, false, nullptr);
            if (i != 1) { field.setRange (-100, 100, .01); field.setTextValueSuffix (" %"); }
            field.valueFromTextFunction = [&field] (const juce::String& text)
            {
                auto input = text.trim(); const auto suffix = field.getTextValueSuffix().trim();
                if (suffix.isNotEmpty() && input.endsWith (suffix)) input = input.dropLastCharacters (suffix.length()).trimEnd();
                const auto raw = input.toStdString(); char* end {}; const auto value = std::strtod (raw.c_str(), &end);
                return raw.empty() || end == raw.c_str() || *end != 0 || ! std::isfinite (value) ? field.getValue() : value;
            };
            field.onValueChange = [this, i] { submit (i); }; addAndMakeVisible (field);
        }
        configureCentreRange(); updateControls();
        mode.setComponentID ("map-mode");
        mode.addItemList ({ "UNIPOLAR", "BIPOLAR" }, 1);
        mode.setSelectedItemIndex (mapping.bipolar() ? 1 : 0, juce::dontSendNotification);
        mode.onChange = [this] { submit (3); }; addAndMakeVisible (mode);
        remove.setButtonText ("UNMAP"); remove.onClick = [this] { if (change) change (mapping, true, false, -1); }; addAndMakeVisible (remove);
        disclosure.setComponentID ("map-curve-disclosure");
        disclosure.setTooltip ("Show or hide this connection's response curve.");
        disclosure.onClick = [this] { setCurveExpanded (disclosure.getToggleState()); }; addAndMakeVisible (disclosure);
        response.setComponentID ("map-response-curve");
        response.setConstraints (true, mapping.bipolar()); response.setCurve (mapping.responseCurve);
        response.setAxisLabels ("SOURCE VALUE", "PARAMETER RESPONSE");
        response.onChange = [this] (scrr::params::TransferCurve next)
        {
            if (mapping.responseCurve.sameStateAs (next)) return;
            mapping.responseCurve = std::move (next);
            if (change) change (mapping, false, true, -1);
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
        addChildComponent (response);
        reset.setButtonText ("LINEAR"); reset.setComponentID ("map-curve-reset");
        reset.setTooltip ("Reset this connection's response to a straight line.");
        reset.onClick = [this] { response.reset(); }; addChildComponent (reset);
        updateDisclosureLabel();
    }
    // The final argument identifies the edited scalar, so a later host base edit
    // cannot be overwritten by an older row's endpoint or mode gesture.
    std::function<void(scrr::params::MacroMapping, bool, bool, int)> change;
    std::function<void(juce::Component&, const juce::String&, const juce::Identifier&)> mappingMenu;
    std::function<void()> layoutChanged;
    int preferredHeight() const noexcept { return curveExpanded ? 288 : 108; }
    bool isCurveExpanded() const noexcept { return curveExpanded; }
    const juce::String& getRouteUid() const noexcept { return mapping.routeUid; }
    void setCurveExpanded (bool expanded)
    {
        if (curveExpanded == expanded) return;
        curveExpanded = expanded; disclosure.setToggleState (expanded, juce::dontSendNotification);
        response.setVisible (expanded); reset.setVisible (expanded); resized(); repaint();
        if (layoutChanged) layoutChanged();
    }
    void refreshPreview (SpectralCrrptProcessor& processor, const std::vector<scrr::params::MacroMapping>& routes)
    {
        if (response.isEditing()) return;
        for (const auto& current : routes) if (current.routeUid == mapping.routeUid)
        {
            const bool newRange = std::abs (mapping.minimum - current.minimum) > 0 || std::abs (mapping.maximum - current.maximum) > 0
                || std::abs (mapping.step - current.step) > 0 || mapping.logarithmic != current.logarithmic;
            mapping = current;
            if (newRange) configureCentreRange();
            updateControls(); updateDisclosureLabel();
            response.setConstraints (true, mapping.bipolar());
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
        auto header = getLocalBounds().reduced (8, 0).withHeight (24);
        remove.setBounds (header.removeFromRight (68)); header.removeFromRight (6);
        mode.setBounds (header.removeFromRight (132));
        const int fieldWidth = (getWidth() - 32) / 3;
        for (int i = 0; i < 3; ++i) fields[(size_t) i].setBounds (8 + i * (fieldWidth + 8), 43, fieldWidth, 28);
        disclosure.setBounds (8, 79, getWidth() - (curveExpanded ? 108 : 16), 23);
        reset.setBounds (getWidth() - 92, 79, 84, 23);
        response.setBounds (8, 110, getWidth() - 16, juce::jmax (0, getHeight() - 118));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel());
        Theme::text (g, mapping.label + mapping.unit, { 8, 1, getWidth() - 230, 22 }, 12, Theme::white(), true);
        const char* labels[] { "START / %", "BASE / CENTRE", "END / %" };
        const int fieldWidth = (getWidth() - 32) / 3;
        for (int i = 0; i < 3; ++i) Theme::text (g, labels[i], { 8 + i * (fieldWidth + 8), 27, fieldWidth, 15 }, 9, Theme::dim());
        g.setColour (Theme::line()); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
private:
    void configureCentreRange()
    {
        // Use the same logarithmic conversion as the route without forming a
        // potentially overflowing maximum/minimum ratio for custom parameters.
        scrr::params::MacroMapping scale;
        scale.minimum = mapping.minimum; scale.maximum = mapping.maximum;
        scale.step = mapping.step; scale.logarithmic = mapping.logarithmic;
        fields[1].setNormalisableRange ({ mapping.minimum, mapping.maximum,
            [scale] (double, double, double value) { return scale.denormalise (value); },
            [scale] (double, double, double value) { return scale.normalise (value); },
            [scale] (double, double, double value) { return scale.denormalise (scale.normalise (value)); } });
        fields[1].setTextValueSuffix (mapping.unit);
        const int decimals = mapping.step > 0 ? juce::jlimit (0, 6, (int) std::ceil (-std::log10 (mapping.step) - 1.0e-5)) : 3;
        const double displayZeroTolerance = mapping.minimum <= 0 && mapping.maximum >= 0 ? mapping.step * .001 : 0;
        fields[1].textFromValueFunction = [decimals, displayZeroTolerance] (double value)
        {
            // Float parameter steps can leave a tiny residual at a true zero
            // after range snapping. Do not display that residual as a value.
            if (std::abs (value) < displayZeroTolerance) value = 0;
            const bool scientific = std::abs (value) > 0 && (std::abs (value) < .0001 || std::abs (value) >= 1.0e8);
            return juce::String (value, scientific ? 8 : decimals, scientific);
        };
        fields[1].updateText();
    }
    void updateControls()
    {
        const double values[] { mapping.startOffset * 100, mapping.centre, mapping.endOffset * 100 };
        for (int i = 0; i < 3; ++i) if (! fields[(size_t) i].isMouseButtonDown (true) && ! fields[(size_t) i].hasKeyboardFocus (true))
            fields[(size_t) i].setValue (values[i], juce::dontSendNotification);
        mode.setSelectedItemIndex (mapping.bipolar() ? 1 : 0, juce::dontSendNotification);
    }
    void updateDisclosureLabel()
    { disclosure.setButtonText (mapping.bipolar() ? "RESPONSE CURVE / 50% STAYS AT BASE" : "RESPONSE CURVE / SOURCE > TARGET"); }
    void submit (int control)
    {
        auto next = mapping;
        if (control == 0) next.startOffset = fields[0].getValue() * .01;
        else if (control == 1) next.centre = fields[1].getValue();
        else if (control == 2) next.endOffset = fields[2].getValue() * .01;
        else
        {
            next.mode = mode.getSelectedItemIndex() == 1 ? scrr::params::MacroMapping::bipolarLeftLow : scrr::params::MacroMapping::unipolarRight;
            next.responseCurve.sanitise (true, next.bipolar());
        }
        if (next.mode == mapping.mode && std::abs (next.startOffset - mapping.startOffset) <= 0
            && std::abs (next.centre - mapping.centre) <= 0 && std::abs (next.endOffset - mapping.endOffset) <= 0) return;
        mapping = next;
        response.setConstraints (true, mapping.bipolar()); response.setCurve (mapping.responseCurve);
        updateDisclosureLabel(); repaint();
        if (change) change (next, false, false, control);
    }
    scrr::params::MacroMapping mapping;
    std::array<juce::Slider, 3> fields;
    CurveDisclosure disclosure;
    juce::ComboBox mode;
    juce::TextButton remove;
    juce::TextButton reset;
    TransferCurveEditor response;
    bool curveExpanded {};
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
            row->layoutChanged = [safe] { if (safe) safe->resized(); };
            row->change = [safe] (scrr::params::MacroMapping next, bool remove, bool responseOnly, int control)
            {
                juce::MessageManager::callAsync ([safe, next, remove, responseOnly, control]
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
                            if (current.routeUid == next.routeUid && current.sameTarget (next) && current.sameSource (next))
                            {
                                current.responseCurve = next.responseCurve;
                                safe->processor.updateMacroMapping (current); break;
                            }
                    }
                    else
                    {
                        for (auto current : safe->processor.getMacroMappings())
                            if (current.routeUid == next.routeUid && current.sameTarget (next) && current.sameSource (next))
                            {
                                if (control == 0) current.startOffset = next.startOffset;
                                else if (control == 1) current.centre = next.centre;
                                else if (control == 2) current.endOffset = next.endOffset;
                                else if (control == 3)
                                { current.mode = next.mode; current.responseCurve.sanitise (true, current.bipolar()); }
                                current.syncRange(); safe->processor.updateMacroMapping (current); break;
                            }
                        // Keep the dragged slider and any open curve editor alive.
                        safe->tick();
                    }
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
        for (auto& row : rows) { row->setBounds (0, y, width, row->preferredHeight()); y += row->preferredHeight() + 8; }
        list.setSize (width, juce::jmax (y, view.getHeight()));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, (sourceUid.isEmpty() ? processor.getMacroLabel (macro) : mappingTitle) + " / MAPPINGS", { 20, 16, getWidth() - 140, 35 }, 25, Theme::yellow(), true);
        Theme::text (g, "Expand RESPONSE CURVE to edit its shape. Drag a slider or type its value.", { 20, 58, getWidth() - 40, 28 }, 12, Theme::dim());
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

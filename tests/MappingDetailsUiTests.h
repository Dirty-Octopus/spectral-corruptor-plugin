// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

namespace mappingDetailsUiTests {
inline std::vector<scrr::gui::MacroMappingRow*> rowsIn (juce::Component& component)
{
    std::vector<scrr::gui::MacroMappingRow*> rows;
    if (auto* row = dynamic_cast<scrr::gui::MacroMappingRow*> (&component)) rows.push_back (row);
    for (auto* child : component.getChildren())
    {
        const auto children = rowsIn (*child); rows.insert (rows.end(), children.begin(), children.end());
    }
    return rows;
}
inline juce::Slider* slider (juce::Component& row, const char* id)
{ return dynamic_cast<juce::Slider*> (findID (row, id)); }
inline juce::Label* valueLabel (juce::Slider& slider)
{
    for (auto* child : slider.getChildren()) if (auto* label = dynamic_cast<juce::Label*> (child)) return label;
    return nullptr;
}
}

TEST_CASE ("Mapping details default to compact rows and disclosure changes layout without changing sound state")
{
    using namespace mappingDetailsUiTests;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "shift");
    p->assignMacro (0, 0, {}, juce::Identifier (scrr::params::id::outputGain));
    const auto state = p->copyPresetState().toXmlString();
    scrr::gui::MacroMappingPanel panel (*p); panel.setSize (800, 520); panel.showMacro (0);
    const auto rows = rowsIn (panel); REQUIRE (rows.size() == 2);
    auto* graph = findID (*rows[0], "map-response-curve"); REQUIRE (graph != nullptr);
    auto* reset = findID (*rows[0], "map-curve-reset"); REQUIRE (reset != nullptr);
    auto* disclosure = dynamic_cast<juce::Button*> (findID (*rows[0], "map-curve-disclosure")); REQUIRE (disclosure != nullptr);
    REQUIRE (! rows[0]->isCurveExpanded()); REQUIRE (! graph->isVisible()); REQUIRE (! reset->isVisible());
    REQUIRE (rows[0]->getHeight() == rows[0]->preferredHeight());
    const int compactSecondY = rows[1]->getY(); const int compactHeight = rows[0]->getHeight();
    disclosure->triggerClick(); pump();
    REQUIRE (rows[0]->isCurveExpanded()); REQUIRE (disclosure->getToggleState());
    REQUIRE (graph->isVisible()); REQUIRE (reset->isVisible());
    REQUIRE (rows[0]->getHeight() > compactHeight);
    REQUIRE (rows[1]->getY() > compactSecondY);
    REQUIRE (rows[0]->getLocalBounds().contains (graph->getBounds()));
    REQUIRE (rows[1]->getY() >= rows[0]->getBottom());
    REQUIRE (! rows[1]->isCurveExpanded());
    disclosure->triggerClick(); pump();
    REQUIRE (! graph->isVisible()); REQUIRE (! reset->isVisible());
    REQUIRE (rows[0]->getHeight() == compactHeight); REQUIRE (rows[1]->getY() == compactSecondY);
    REQUIRE (p->copyPresetState().toXmlString() == state);
}

TEST_CASE ("Macro and modulation mapping details expose editable sliders and retain active rows through value edits")
{
    using namespace mappingDetailsUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 125.0); p->assignMacro (0, 1, uid, "shift");
    auto original = p->getMacroMappings()[0];
    original.startOffset = -.123456789; original.endOffset = .276543219;
    original.mode = scrr::params::MacroMapping::unipolarLeft; p->updateMacroMapping (original);
    scrr::gui::MacroMappingPanel panel (*p); panel.setSize (800, 520); panel.showMacro (0);
    const auto rows = rowsIn (panel); REQUIRE (rows.size() == 1); auto* row = rows[0];
    row->setCurveExpanded (true); juce::Component::SafePointer<scrr::gui::MacroMappingRow> surviving (row);
    for (const auto* id : { "map-low", "map-centre", "map-high" })
    {
        auto* control = slider (*row, id); REQUIRE (control != nullptr);
        REQUIRE (control->getSliderStyle() == juce::Slider::LinearHorizontal);
        REQUIRE (control->isTextBoxEditable()); REQUIRE (control->getTextBoxPosition() == juce::Slider::TextBoxRight);
        REQUIRE (row->getLocalBounds().contains (control->getBounds()));
    }
    auto* start = slider (*row, "map-low"); auto* centre = slider (*row, "map-centre"); auto* end = slider (*row, "map-high");
    REQUIRE (juce::exactlyEqual (start->getMinimum(), -100.0)); REQUIRE (juce::exactlyEqual (end->getMaximum(), 100.0));
    REQUIRE (juce::exactlyEqual (centre->getMinimum(), original.minimum)); REQUIRE (juce::exactlyEqual (centre->getMaximum(), original.maximum));
    // A host edit after the row was opened must survive an endpoint gesture.
    p->setModuleParameter (1, uid, "shift", 350.0);
    end->setValue (-37.25, juce::sendNotificationSync); pump();
    REQUIRE (surviving != nullptr); REQUIRE (row->isCurveExpanded());
    auto current = p->getMacroMappings()[0];
    REQUIRE (std::abs (current.centre - 350) < 1.0e-9);
    REQUIRE (juce::exactlyEqual (current.startOffset, original.startOffset)); REQUIRE (std::abs (current.endOffset + .3725) < 1.0e-12);
    REQUIRE (current.mode == original.mode); REQUIRE (current.sameResponseCurve (original));
    for (const auto value : { -20.0, -10.0, 12.34 }) { start->setValue (value, juce::sendNotificationSync); pump(); REQUIRE (surviving != nullptr); }
    REQUIRE (std::abs (p->getMacroMappings()[0].startOffset - .1234) < 1.0e-12);
    auto* label = valueLabel (*centre); REQUIRE (label != nullptr); label->showEditor();
    auto* input = label->getCurrentTextEditor(); REQUIRE (input != nullptr); input->setText ("420 Hz", false); label->hideEditor (false); pump();
    REQUIRE (std::abs (p->getMacroMappings()[0].centre - 420) < .001);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 420) < .001);
    REQUIRE (surviving != nullptr); REQUIRE (findID (*surviving, "map-response-curve")->isVisible());
    const auto source = p->addModulator (false); p->assignModulator (source, 1, uid, "shift"); panel.showSource (source);
    const auto sourceRows = rowsIn (panel); REQUIRE (sourceRows.size() == 1);
    REQUIRE (! sourceRows[0]->isCurveExpanded());
    auto* sourceEnd = slider (*sourceRows[0], "map-high"); REQUIRE (sourceEnd != nullptr);
    sourceEnd->setValue (45.67, juce::sendNotificationSync); pump();
    const auto routes = p->getMacroMappings(); REQUIRE (routes.size() == 2);
    REQUIRE (std::abs (routes[1].endOffset - .4567) < 1.0e-12);
    REQUIRE (std::abs (routes[0].endOffset + .3725) < 1.0e-12);
}

TEST_CASE ("Coordinate mapping response curves use the same compact disclosure and remain editable")
{
    using namespace scrr::params; using namespace mappingDetailsUiTests;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString(); p->assignMacro (0, 1, uid, "shift");
    auto owner = p->getMacroMappings()[0]; const auto point = owner.responseCurve.allocatePointId();
    owner.responseCurve.points.insert (owner.responseCurve.points.begin() + 1, { .4f, .25f, 0, point }); p->updateMacroMapping (owner);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, curveCoordinateParameter (point, false));
    REQUIRE (p->getMacroMappings().size() == 2);
    scrr::gui::MacroMappingPanel panel (*p); panel.setSize (800, 520); panel.showMacro (1);
    const auto rows = rowsIn (panel); REQUIRE (rows.size() == 1);
    auto* graph = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (*rows[0], "map-response-curve")); REQUIRE (graph != nullptr);
    REQUIRE (! graph->isVisible()); REQUIRE (! rows[0]->isCurveExpanded());
    const auto state = p->copyPresetState().toXmlString(); rows[0]->setCurveExpanded (true);
    REQUIRE (graph->isVisible()); REQUIRE (p->copyPresetState().toXmlString() == state);
    const auto plot = graph->getPlotBounds(); const auto position = juce::Point<float> (plot.getX() + .3f * plot.getWidth(), plot.getBottom() - .15f * plot.getHeight());
    graph->mouseDoubleClick (mouse (*graph, position)); pump();
    const auto routes = p->getMacroMappings(); REQUIRE (routes.size() == 2);
    REQUIRE (routes[0].sameResponseCurve (owner)); REQUIRE (routes[1].responseCurve.points.size() == 3);
    REQUIRE (std::abs (routes[1].responseCurve.evaluate (.3) - .15) < 1.0e-6);
    REQUIRE (rows[0]->isCurveExpanded());
}

TEST_CASE ("Mapping base sliders keep finite logarithmic custom ranges and reject invalid typed numbers")
{
    using namespace mappingDetailsUiTests;
    scrr::params::MacroMapping zero; zero.minimum = -5000; zero.maximum = 5000; zero.step = .1f; zero.centre = 0;
    scrr::gui::MacroMappingRow zeroRow (zero);
    auto* zeroSlider = slider (zeroRow, "map-centre"); REQUIRE (zeroSlider != nullptr);
    REQUIRE (juce::exactlyEqual (zeroSlider->getTextFromValue (zeroSlider->getValue()).getDoubleValue(), 0.0));
    REQUIRE (juce::exactlyEqual (valueLabel (*zeroSlider)->getText().getDoubleValue(), 0.0));
    scrr::params::MacroMapping mapping; mapping.minimum = 1.0e-200; mapping.maximum = 1.0e200;
    mapping.centre = 1; mapping.logarithmic = true; mapping.step = 0;
    scrr::gui::MacroMappingRow row (mapping); row.setSize (700, row.preferredHeight());
    auto* centre = slider (row, "map-centre"); REQUIRE (centre != nullptr);
    REQUIRE (std::isfinite (centre->proportionOfLengthToValue (.5)));
    REQUIRE (std::abs (centre->proportionOfLengthToValue (.5) - 1) < 1.0e-10);
    REQUIRE (std::abs (centre->valueToProportionOfLength (1) - .5) < 1.0e-12);
    REQUIRE (juce::exactlyEqual (centre->getValueFromText ("nan"), centre->getValue()));
    REQUIRE (juce::exactlyEqual (centre->getValueFromText ("3oops"), centre->getValue()));
    REQUIRE (juce::exactlyEqual (centre->getValueFromText ("2.5e100"), 2.5e100));
    int edits = 0; row.change = [&] (auto changed, bool remove, bool responseOnly, int control)
    { REQUIRE (! remove); REQUIRE (! responseOnly); REQUIRE (control == 2); REQUIRE (juce::exactlyEqual (changed.centre, mapping.centre)); ++edits; };
    slider (row, "map-high")->setValue (25, juce::sendNotificationSync); REQUIRE (edits == 1);
}

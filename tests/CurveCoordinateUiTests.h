// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

namespace coordinateUiTests {
inline void chooseMenu (int result, const char* context = "coordinate menu")
{
    // Synthetic mouseDown bypasses native activation. A macOS CLI must also
    // become a foreground-capable app so popup focus timers do not dismiss it.
   #if JUCE_MAC
    juce::Process::setDockIconVisible (true);
   #endif
    juce::Process::makeForegroundProcess();
    const bool opened = waitForUI ([] { return juce::Component::getCurrentlyModalComponent() != nullptr; });
    if (! opened) std::cerr << "Missing popup: " << context << '\n';
    REQUIRE (opened);
    auto* menu = juce::Component::getCurrentlyModalComponent();
    const int item = result == 20 ? 1 : result == 21 ? 2 : result;
    if (item == 1 || item == 2)
    {
        for (int i = 0; i < item; ++i) menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        menu->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    }
    else menu->exitModalState (result);
    pump (60);
}
inline juce::Point<float> position (const scrr::gui::TransferCurveEditor& editor, float x, float y)
{
    const auto area = editor.getPlotBounds();
    return { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y };
}
}

TEST_CASE ("LFO right click preserves custom points and built-in waveform shape")
{
    using namespace scrr::params;
    scrr::gui::ModulationCurve curve; curve.setSize (480, 190);
    curve.source.shape = 0; int changes = 0; uint32_t selected = 0;
    curve.changed = [&] (auto) { ++changes; };
    curve.nodeMenu = [&] (juce::Component& target, uint32_t id) { REQUIRE (&target == &curve); selected = id; };
    curve.mouseDown (mouse (curve, { 240, 95 }, false, juce::ModifierKeys::rightButtonModifier));
    REQUIRE (curve.source.shape == 0); REQUIRE (changes == 0); REQUIRE (selected == 0);
    curve.source.shape = 5;
    curve.source.points = { { 0, 0, 0, 1 }, { .5f, .25f, 0, 7 }, { 1, 1, 0, 2 } };
    const auto area = curve.getLocalBounds().toFloat().reduced (8);
    curve.mouseDown (mouse (curve, { area.getCentreX(), area.getBottom() - area.getHeight() * .25f }, false, juce::ModifierKeys::rightButtonModifier));
    REQUIRE (curve.source.points.size() == 3); REQUIRE (selected == 7); REQUIRE (changes == 0);
}

TEST_CASE ("Frequency Remap coordinate menus independently assign X and Y without deleting a point")
{
    using namespace scrr::params; using namespace coordinateUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    TransferCurve curve; curve.points = { { 0, 0, 0, 1 }, { .4f, .6f, 0, 7 }, { 1, 1, 0, 2 } };
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    const auto source = p->addModulator (false);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    editor->addToDesktop (0); editor->setVisible (true);
    // Give native menus a visible focused peer for the synthetic gesture.
    editor->setWantsKeyboardFocus (true); editor->toFront (true); editor->grabKeyboardFocus(); pump (200);
    auto* graph = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (*editor, "frequency-remap-curve")); REQUIRE (graph != nullptr);
    REQUIRE (graph->getCurve().pointIndex (7) >= 0); REQUIRE (graph->canMapCoordinate (7, true));
    graph->mouseDown (mouse (*graph, position (*graph, .4f, .6f), false, juce::ModifierKeys::rightButtonModifier));
    chooseMenu (1, "Remap X axis"); chooseMenu (1, "Remap X source");
    auto routes = p->getMacroMappings(); REQUIRE (routes.size() == 1);
    REQUIRE (routes[0].channel == 1); REQUIRE (routes[0].uid == uid); REQUIRE (routes[0].parameter == curveCoordinateParameter (7, true));
    REQUIRE (routes[0].sourceUid.isEmpty()); REQUIRE (routes[0].macro == 0);
    graph->mouseDown (mouse (*graph, position (*graph, .4f, .6f), false, juce::ModifierKeys::rightButtonModifier));
    chooseMenu (2, "Remap Y axis"); chooseMenu (100, "Remap Y source");
    routes = p->getMacroMappings(); REQUIRE (routes.size() == 2);
    REQUIRE (routes[1].parameter == curveCoordinateParameter (7, false)); REQUIRE (routes[1].sourceUid == source);
    REQUIRE (TransferCurve::decode (p->getModuleChain (1).getChild (0)["remapCurve"].toString()).points.size() == 3);
}

TEST_CASE ("Mapping response coordinate menu identifies the owning route")
{
    using namespace scrr::params; using namespace coordinateUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    MacroMapping route; route.uid = "module"; route.routeUid = "owning-response-route";
    route.responseCurve.points = { { 0, 0, 0, 1 }, { .4f, .2f, 0, 7 }, { 1, 1, 0, 2 } };
    scrr::gui::MacroMappingRow row (route); row.setSize (700, 270); row.addToDesktop (0); row.setVisible (true);
    juce::String owner; juce::Identifier target;
    row.mappingMenu = [&] (juce::Component&, const juce::String& uid, const juce::Identifier& key) { owner = uid; target = key; };
    auto* graph = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (row, "map-response-curve")); REQUIRE (graph != nullptr);
    graph->mouseDown (mouse (*graph, position (*graph, .4f, .2f), false, juce::ModifierKeys::rightButtonModifier)); chooseMenu (2);
    REQUIRE (owner == route.routeUid); REQUIRE (target == curveCoordinateParameter (7, false)); REQUIRE (graph->getCurve().points.size() == 3);
}

TEST_CASE ("Spectral EQ node menu offers independent frequency and gain assignments")
{
    using namespace coordinateUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    scrr::gui::SpectralEqEditor graph; juce::ValueTree state ("module"); state.setProperty ("uid", "equalizer", nullptr);
    graph.setState (state); graph.setSize (600, 230); graph.addToDesktop (0); graph.setVisible (true);
    juce::String target; graph.mappingMenu = [&] (juce::Component&, const juce::String& key) { target = key; };
    graph.mouseDown (mouse (graph, graph.getBandPosition (0), false, juce::ModifierKeys::rightButtonModifier)); chooseMenu (20);
    REQUIRE (target == "band1Freq");
    graph.mouseDown (mouse (graph, graph.getBandPosition (0), false, juce::ModifierKeys::rightButtonModifier)); chooseMenu (21);
    REQUIRE (target == "band1Gain");
}

TEST_CASE ("Formula mode preserves EDIT data and parameter mappings through preset clipboard and Undo")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    TransferCurve curve; curve.points = { { 0, .1f, 0 }, { .4f, .6f, 0 }, { 1, .9f, 0 } };
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    RemapFormula formula; FormulaParameter parameter; parameter.id = "ratio"; parameter.name = "Ratio";
    parameter.minimum = .25; parameter.maximum = 8; parameter.defaultValue = 2; parameter.step = .01; formula.parameters.push_back (parameter);
    FormulaNode value; value.id = 1; value.op = FormulaOp::Parameter; value.parameterId = parameter.id;
    FormulaNode multiply; multiply.id = 2; multiply.op = FormulaOp::Multiply; multiply.inputs = { 0, 1, -1 };
    formula.nodes.push_back (value); formula.nodes.push_back (multiply); formula.outputNode = 2;
    p->setModuleParameter (1, uid, "remapFormula", formula.encode()); p->setModuleParameter (1, uid, parameter.property(), 4.0);
    p->setModuleParameter (1, uid, "formulaView", "{\"xMin\":20,\"xMax\":10000}");
    p->setModuleParameter (1, uid, "remapMode", 1);
    p->assignMacro (0, 1, uid, parameter.property()); REQUIRE (p->getMacroMappings().size() == 1);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    auto state = restored->getModuleChain (1).getChild (0);
    REQUIRE ((int) state["remapMode"] == 1); REQUIRE (state["remapCurve"].toString() == p->getModuleChain (1).getChild (0)["remapCurve"].toString());
    REQUIRE (state["remapFormula"].toString() == formula.encode()); REQUIRE (std::abs ((double) state[parameter.property()] - 4) < .001);
    REQUIRE (state["formulaView"].toString().contains ("10000")); REQUIRE (restored->getMacroMappings().front().parameter == parameter.property());
    auto pasted = testProcessor(); REQUIRE (pasted->pasteModule (2, p->copyModule (1, uid)).isNotEmpty());
    REQUIRE (pasted->getModuleChain (2).getChild (0)["remapFormula"].toString() == formula.encode());
    REQUIRE (pasted->getMacroMappings().front().parameter == parameter.property());
    p->setModuleParameter (1, uid, "remapMode", 0); REQUIRE ((int) p->getModuleChain (1).getChild (0)["remapMode"] == 0);
    REQUIRE (p->undo()); REQUIRE ((int) p->getModuleChain (1).getChild (0)["remapMode"] == 1);
    REQUIRE (p->redo()); REQUIRE ((int) p->getModuleChain (1).getChild (0)["remapMode"] == 0);
}

TEST_CASE ("Formula workspace sliders use ordinary Macro and modulation routes")
{
    using namespace scrr::params; using namespace coordinateUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString(); const auto source = p->addModulator (false);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780); editor->addToDesktop (0); editor->setVisible (true);
    // Give native menus a visible focused peer for the synthetic gesture.
    editor->setWantsKeyboardFocus (true); editor->toFront (true); editor->grabKeyboardFocus(); pump (200);
    auto* mode = dynamic_cast<juce::Button*> (findID (*editor, "remap-mode-formula")); REQUIRE (mode != nullptr); mode->onClick(); pump (100);
    auto* formula = dynamic_cast<scrr::gui::RemapFormulaEditor*> (findID (*editor, "remap-formula-editor")); REQUIRE (formula != nullptr); REQUIRE (formula->isVisible());
    formulaUiTests::click (*formula, { 360, 280 }, true); formulaUiTests::chooseMenu (500);
    REQUIRE (waitForUI ([&] { return formula->getParameterSliders().size() == 1; }));
    const auto sliders = formula->getParameterSliders(); REQUIRE (sliders.size() == 1);
    sliders.front()->mouseDown (mouse (*sliders.front(), { 10, 10 }, false, juce::ModifierKeys::rightButtonModifier)); chooseMenu (1);
    REQUIRE (p->getMacroMappings().size() == 1); REQUIRE (p->getMacroMappings()[0].parameter.toString() == sliders.front()->getComponentID());
    REQUIRE (sliders.front()->dropModulationSource (source)); REQUIRE (p->getMacroMappings().size() == 2);
    REQUIRE (p->getMacroMappings()[1].uid == uid); REQUIRE (p->getMacroMappings()[1].sourceUid == source);
}

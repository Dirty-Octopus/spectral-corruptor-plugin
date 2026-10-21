// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Source/gui/components/RemapFormulaEditor.h"

TEST_CASE ("Formula canvas connects math nodes to output and reports disconnected inputs")
{
    using namespace scrr::params;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    RemapFormula formula;
    FormulaNode constant; constant.id = 1; constant.op = FormulaOp::Constant; constant.value = 2; constant.canvasX = 240; constant.canvasY = 150;
    FormulaNode multiply; multiply.id = 2; multiply.op = FormulaOp::Multiply; multiply.canvasX = 470; multiply.canvasY = 80;
    formula.nodes.push_back (constant); formula.nodes.push_back (multiply);
    juce::ValueTree state ("module"); state.setProperty ("uid", "formula-test", nullptr); state.setProperty ("remapFormula", formula.encode(), nullptr);
    scrr::gui::RemapFormulaEditor editor; editor.setState (state); editor.setSize (480, editor.getPreferredHeight());
    editor.onChange = [&] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
    auto* canvas = findID (editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    auto click = [&] (juce::Point<float> position) { canvas->mouseDown (mouse (*canvas, position)); canvas->mouseUp (mouse (*canvas, position)); };
    click ({ 182, 100 }); REQUIRE (editor.isEditing()); click ({ 470, 120 }); REQUIRE (! editor.isEditing());
    click ({ 382, 170 }); click ({ 470, 140 });
    click ({ 612, 100 }); click ({ 350, 117 });
    const auto connected = RemapFormula::decode (state["remapFormula"].toString()); REQUIRE (connected.outputNode == 2);
    const auto program = FormulaProgram::compile (connected); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {}; REQUIRE (std::abs (program.evaluate (200, values).value - 400) < 1.0e-9);
    auto* status = dynamic_cast<juce::Label*> (findID (editor, "formula-status")); REQUIRE (status != nullptr);
    REQUIRE (status->getText().contains ("Output y"));
    canvas->mouseDown (mouse (*canvas, { 470, 140 }, false, juce::ModifierKeys::rightButtonModifier));
    REQUIRE (! FormulaProgram::compile (RemapFormula::decode (state["remapFormula"].toString())).valid);
    REQUIRE (status->getText().contains ("connect every input"));
    click ({ 500, 90 }); canvas->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    const auto removed = RemapFormula::decode (state["remapFormula"].toString()); REQUIRE (removed.nodes.size() == 2); REQUIRE (removed.outputNode == -1);
}

TEST_CASE ("Formula custom parameters retain stable mapping IDs through Details edits")
{
    using namespace scrr::params;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    juce::ValueTree state ("module"); state.setProperty ("uid", "formula-parameters", nullptr);
    scrr::gui::RemapFormulaEditor editor; editor.setState (state); editor.setSize (520, editor.getPreferredHeight());
    editor.onChange = [&] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
    int changes = 0; editor.onParametersChanged = [&] { ++changes; };
    auto* add = findButton (editor, "ADD PARAM"); REQUIRE (add != nullptr); add->onClick();
    auto graph = RemapFormula::decode (state["remapFormula"].toString()); REQUIRE (graph.parameters.size() == 1); REQUIRE (graph.nodes.size() == 2);
    const auto property = graph.parameters.front().property(); auto sliders = editor.getParameterSliders(); REQUIRE (sliders.size() == 1);
    REQUIRE (sliders.front()->getComponentID() == property.toString()); REQUIRE (std::abs (sliders.front()->getValue() - .5) < .001);
    juce::Identifier mapped;
    editor.mappingMenu = [&] (juce::Component& target, const juce::Identifier& key) { REQUIRE (&target == sliders.front()); mapped = key; };
    sliders.front()->mouseDown (mouse (*sliders.front(), { 10, 10 }, false, juce::ModifierKeys::rightButtonModifier)); REQUIRE (mapped == property);
    auto edit = [&] (const char* id, const char* value)
    { auto* text = dynamic_cast<juce::TextEditor*> (findID (editor, id)); REQUIRE (text != nullptr); text->setText (value, false); text->onReturnKey(); };
    edit ("formula-param-name", "Offset"); edit ("formula-param-max", "2"); edit ("formula-param-step", "0.25");
    graph = RemapFormula::decode (state["remapFormula"].toString()); REQUIRE (graph.parameters.front().name == "Offset"); REQUIRE (graph.parameters.front().property() == property);
    REQUIRE (std::abs (graph.parameters.front().maximum - 2) < .001); REQUIRE (std::abs (graph.parameters.front().step - .25) < .001);
    sliders.front()->setValue (1.25, juce::sendNotificationSync); REQUIRE (std::abs ((double) state[property] - 1.25) < .001);
    REQUIRE (changes >= 4);
    auto* remove = findButton (editor, "DELETE PARAM"); REQUIRE (remove != nullptr); remove->onClick();
    graph = RemapFormula::decode (state["remapFormula"].toString()); REQUIRE (graph.parameters.empty()); REQUIRE (editor.getParameterSliders().empty());
}

TEST_CASE ("Formula preview Details are separate from graph DSP and normalized coordinates")
{
    using namespace scrr::params;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    juce::ValueTree state ("module"); state.setProperty ("uid", "formula-view", nullptr); state.setProperty ("remapFormula", RemapFormula {}.encode(), nullptr);
    scrr::gui::RemapFormulaEditor editor; editor.setState (state); editor.setSize (480, editor.getPreferredHeight());
    editor.onChange = [&] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
    auto* details = findButton (editor, "DETAILS"); REQUIRE (details != nullptr); details->setToggleState (true, juce::dontSendNotification); details->onClick();
    auto* maximum = dynamic_cast<juce::TextEditor*> (findID (editor, "formula-view-x-max")); REQUIRE (maximum != nullptr);
    const auto graphBefore = state["remapFormula"].toString(); maximum->setText ("10000", false); maximum->onReturnKey();
    REQUIRE (state["remapFormula"].toString() == graphBefore); REQUIRE (state["formulaView"].toString().isNotEmpty());
    const auto view = juce::JSON::parse (state["formulaView"].toString()); REQUIRE (std::abs ((double) view["xMax"] - 10000) < .001);
    auto* units = dynamic_cast<juce::ComboBox*> (findID (editor, "formula-units")); REQUIRE (units != nullptr); units->setSelectedId (2, juce::sendNotificationSync);
    REQUIRE (RemapFormula::decode (state["remapFormula"].toString()).unit == FormulaUnit::Normalized);
    const auto normalized = juce::JSON::parse (state["formulaView"].toString()); REQUIRE (std::abs ((double) normalized["xMax"] - 1) < .001); REQUIRE (! (bool) normalized["logX"]);
    editor.effectiveValue = [] (const juce::Identifier&, double base) { return base; }; editor.refreshPreview();
    REQUIRE (editor.getPreferredHeight() > 478);
}

TEST_CASE ("Formula module switches cancel a pending wire without applying it to the next module")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    juce::ValueTree state ("module"); state.setProperty ("uid", "first-formula", nullptr);
    scrr::gui::RemapFormulaEditor editor; editor.setState (state); editor.setSize (480, editor.getPreferredHeight());
    auto* canvas = findID (editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    canvas->mouseDown (mouse (*canvas, { 182, 100 })); canvas->mouseUp (mouse (*canvas, { 182, 100 })); REQUIRE (editor.isEditing());
    state.setProperty ("uid", "second-formula", nullptr); editor.setState (state);
    REQUIRE (editor.getModuleUid() == "second-formula"); REQUIRE (! editor.isEditing());
}

inline int renderRemapFormulaUi (const juce::File& directory)
{
    using namespace scrr::params;
    if (directory.createDirectory().failed() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return 1;
    RemapFormula formula; formula.nodes.front().canvasX = 24; formula.nodes.front().canvasY = 25;
    FormulaParameter parameter; parameter.id = "ratio"; parameter.name = "Ratio"; parameter.minimum = .25; parameter.maximum = 4; parameter.defaultValue = 1.5; parameter.step = .01; parameter.logarithmic = true;
    formula.parameters.push_back (parameter);
    FormulaNode parameterNode; parameterNode.id = 1; parameterNode.op = FormulaOp::Parameter; parameterNode.parameterId = parameter.id; parameterNode.canvasX = 24; parameterNode.canvasY = 145;
    FormulaNode multiply; multiply.id = 2; multiply.op = FormulaOp::Multiply; multiply.inputs = {{ 0, 1, -1 }}; multiply.canvasX = 220; multiply.canvasY = 60;
    formula.nodes.push_back (parameterNode); formula.nodes.push_back (multiply); formula.outputNode = 2;
    juce::ValueTree state ("module"); state.setProperty ("uid", "formula-render", nullptr); state.setProperty ("remapFormula", formula.encode(), nullptr);
    state.setProperty ("formulaView", "{\"outputX\":380,\"outputY\":60}", nullptr); state.setProperty (parameter.property(), 1.5, nullptr);
    scrr::gui::InstrumentLookAndFeel look;
    scrr::gui::RemapFormulaEditor editor; editor.setLookAndFeel (&look); editor.setState (state); editor.setSize (520, editor.getPreferredHeight());
    editor.layoutChanged = [&] { editor.setSize (520, editor.getPreferredHeight()); };
    editor.onChange = [&] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
    editor.addToDesktop (0); editor.setVisible (true); pump (60);
    const auto writeImage = [] (const juce::Image& image, const juce::File& file)
    {
        juce::MemoryOutputStream bytes;
        return juce::PNGImageFormat().writeImageToStream (image, bytes)
            && file.replaceWithData (bytes.getData(), bytes.getDataSize());
    };
    auto save = [&] (const char* name)
    {
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        return writeImage (image, directory.getChildFile (name));
    };
    bool success = save ("formula-canvas.png");
    if (auto* details = findButton (editor, "DETAILS"))
    {
        auto* parameterDetails = dynamic_cast<juce::Button*> (findID (editor, "formula-param-details-ratio"));
        if (parameterDetails) parameterDetails->onClick(); else { details->setToggleState (true, juce::dontSendNotification); details->onClick(); }
    }
    pump (30); success = save ("formula-details.png") && success;
    formula.nodes.back().inputs[1] = -1; state.setProperty ("remapFormula", formula.encode(), nullptr); editor.setState (state);
    success = save ("formula-error.png") && success; editor.setVisible (false); editor.setLookAndFeel (nullptr);
    // Exercise the real inspector viewport as well as the isolated component.
    formula.nodes.back().inputs[1] = 1;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    processor->setModuleParameter (1, uid, "remapFormula", formula.encode());
    processor->setModuleParameter (1, uid, "formulaView", "{\"outputX\":380,\"outputY\":60}");
    processor->setModuleParameter (1, uid, parameter.property(), 1.5);
    processor->setModuleParameter (1, uid, "remapMode", 1); processor->prepareToPlay (48000, 128);
    auto fullEditor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor()); fullEditor->addToDesktop (0); fullEditor->setVisible (true);
    for (const auto size : { juce::Point<int> (1040, 780), juce::Point<int> (1180, 980) })
    {
        fullEditor->setSize (size.x, size.y);
        success = waitForUI ([&] { auto* card = findID (*fullEditor, "effect-card"); return card != nullptr && card->getAlpha() >= .999f; }) && success;
        if (auto* guide = findID (*fullEditor, "quick-guide")) guide->setVisible (false);
        const auto screenshot = fullEditor->createComponentSnapshot (fullEditor->getLocalBounds(), true, 1.0f);
        success = writeImage (screenshot, directory.getChildFile ("formula-full-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png")) && success;
    }
    return success ? 0 : 1;
}

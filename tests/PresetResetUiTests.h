// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

namespace presetResetUiTests {
inline scrr::gui::AssignableSlider& slider (juce::Component& root, const juce::String& id)
{
    auto* component = findID (root, id); REQUIRE (component != nullptr);
    if (auto* control = dynamic_cast<scrr::gui::ParameterControl*> (component))
    {
        auto* value = control->getAssignableSlider(); REQUIRE (value != nullptr); return *value;
    }
    auto* value = dynamic_cast<scrr::gui::AssignableSlider*> (component); REQUIRE (value != nullptr); return *value;
}
inline void reset (juce::Slider& value)
{ value.mouseDoubleClick (mouse (value, value.getLocalBounds().toFloat().getCentre())); }
}

TEST_CASE ("Preset reset gestures read current saved Master Macro and module bases without a UI refresh")
{
    using namespace presetResetUiTests;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    setParam (*p, scrr::params::id::inputGain, -8); setParam (*p, scrr::params::id::outputGain, -4);
    setParam (*p, scrr::params::id::macros[0], 37); setParam (*p, "global.overlapFactor", 7.25f);
    p->setModuleParameter (1, uid, "shift", 321.0); p->capturePresetResetBaseline();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    const juce::String components[] { "input-gain", "output-gain", "macro-1", "fft-overlap", "shift" };
    const double baseline[] { -8, -4, 37, 7.25, 321 };
    for (size_t i = 0; i < std::size (components); ++i)
    {
        auto& control = slider (*editor, components[i]); control.setValue (control.getMaximum(), juce::sendNotificationSync);
        reset (control); REQUIRE (std::abs (control.getValue() - baseline[i]) < .011);
    }
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::macros[0])->load() - 37) < .01f);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 321) < .01);

    // SAVE changes the reset target immediately; a temporary edit does not.
    auto& gain = slider (*editor, "input-gain"); gain.setValue (-12, juce::sendNotificationSync); p->capturePresetResetBaseline();
    gain.setValue (-2, juce::sendNotificationSync); reset (gain); REQUIRE (std::abs (gain.getValue() + 12) < .01);
    gain.setValue (-1, juce::sendNotificationSync);
    const auto modified = mouse (gain, gain.getLocalBounds().toFloat().getCentre(), false,
                                juce::ModifierKeys::leftButtonModifier | scrr::gui::resetModifier().getRawFlags());
    gain.mouseDown (modified); gain.mouseUp (modified); REQUIRE (std::abs (gain.getValue() + 12) < .01);
}

TEST_CASE ("Preset reset gestures preserve modulation source identity and all numeric source settings")
{
    using namespace presetResetUiTests;
    for (int type = 0; type < 3; ++type)
    {
        auto p = testProcessor(); p->addModulator (type == 1, type == 2);
        auto source = p->getModulators().front();
        source.rate = 3.25f; source.phase = 87; source.seed = 732; source.smooth = 214;
        source.gain = -7; source.attack = 31; source.release = 423;
        p->updateModulator (source); p->capturePresetResetBaseline();
        scrr::gui::ModulatorCard card (*p, source); card.setSize (460, 420);
        const juce::StringArray keys = type == 0 ? juce::StringArray { "rate", "phase" }
            : type == 1 ? juce::StringArray { "gain", "attack", "release" } : juce::StringArray { "rate", "seed", "smooth" };
        for (const auto& key : keys)
        {
            auto& control = slider (card, key == "smooth" ? "random-smooth" : "modulation-" + key);
            control.setValue (control.getMaximum(), juce::sendNotificationSync); reset (control);
            REQUIRE (std::abs (p->getModulators().front().parameterValue (juce::Identifier (key)) - source.parameterValue (juce::Identifier (key))) < .011);
        }
    }
}

TEST_CASE ("Preset reset of Formula parameters preserves node expansion and the custom default fallback")
{
    using namespace scrr::params; using namespace presetResetUiTests;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    RemapFormula graph; FormulaParameter parameter { "ratio", "Ratio", 1, 8, 2, .01, false };
    graph.parameters.push_back (parameter); FormulaNode node; node.id = 1; node.op = FormulaOp::Parameter;
    node.parameterId = parameter.id; node.canvasX = 280; node.canvasY = 40; graph.nodes.push_back (node);
    p->setModuleParameter (1, uid, "remapFormula", graph.encode()); p->setModuleParameter (1, uid, "remapMode", 1);
    p->setModuleParameter (1, uid, parameter.property(), 3.5); p->capturePresetResetBaseline();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    auto* formula = dynamic_cast<scrr::gui::RemapFormulaEditor*> (findID (*editor, "remap-formula-editor")); REQUIRE (formula != nullptr);
    auto& control = slider (*formula, parameter.property().toString()); control.setValue (6, juce::sendNotificationSync);
    auto* canvas = findID (*formula, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto bounds = formula->getNodeBounds (node.id);
    canvas->mouseDoubleClick (mouse (*canvas, { bounds.getCentreX(), bounds.getY() + 12 }));
    REQUIRE (formula->getNodeBounds (node.id).getHeight() > bounds.getHeight());
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter.property()] - 6) < .011);
    reset (control); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter.property()] - 3.5) < .011);
    REQUIRE (p->getModuleChain (1).getChild (0)["remapFormula"].toString() == graph.encode());

    // A parameter absent from the saved baseline still uses its declared default.
    parameter.id = "new_ratio"; parameter.name = "New ratio"; graph.parameters.push_back (parameter); node.id = 2; node.parameterId = parameter.id;
    node.canvasX = 550; graph.nodes.push_back (node); p->setModuleParameter (1, uid, "remapFormula", graph.encode());
    formula->setState (p->getModuleChain (1).getChild (0));
    auto& added = slider (*formula, parameter.property().toString()); added.setValue (7, juce::sendNotificationSync); reset (added);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter.property()] - 2) < .011);
}

TEST_CASE ("Preset reset of an EQ graph node restores its three saved coordinates together")
{
    auto p = testProcessor(); p->addModule (1, "SpectralEqualizer");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "band4Freq", 830.0); p->setModuleParameter (1, uid, "band4Gain", 5.0);
    p->setModuleParameter (1, uid, "band4Q", 2.4); p->capturePresetResetBaseline();
    p->setModuleParameter (1, uid, "band4Freq", 910.0); p->setModuleParameter (1, uid, "band4Gain", -8.0);
    p->setModuleParameter (1, uid, "band4Q", 1.1);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    auto* eq = dynamic_cast<scrr::gui::SpectralEqEditor*> (findID (*editor, "spectral-eq-editor")); REQUIRE (eq != nullptr);
    eq->mouseDoubleClick (mouse (*eq, eq->getBandPosition (3)));
    const auto state = p->getModuleChain (1).getChild (0);
    REQUIRE (std::abs ((double) state["band4Freq"] - 830) < .01);
    REQUIRE (std::abs ((double) state["band4Gain"] - 5) < .01);
    REQUIRE (std::abs ((double) state["band4Q"] - 2.4) < .01);
}

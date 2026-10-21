// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "gui/components/TransferCurveEditor.h"

TEST_CASE ("Response curves preserve legacy routes and anchor the bipolar base")
{
    scrr::params::MacroMapping mapping;
    mapping.minimum = -100; mapping.maximum = 100; mapping.centre = 10;
    mapping.startOffset = -0.1; mapping.endOffset = 0.3;
    for (int i = 0; i <= 100; ++i)
    {
        const float source = (float) i / 100;
        REQUIRE (std::abs (mapping.offset (source) - (-0.1 + 0.4 * source)) < 1.0e-6);
    }
    mapping.responseCurve.points = { { 0, 0, 0 }, { 0.25f, 0.05f, -0.6f }, { 0.75f, 0.95f, 0.4f }, { 1, 1, 0 } };
    mapping.mode = scrr::params::MacroMapping::bipolarLeftLow;
    REQUIRE (std::abs (mapping.offset (0.5f)) < 1.0e-12);
    REQUIRE (std::abs (mapping.value (0.5f) - 10) < 1.0e-6);
    REQUIRE (std::abs (mapping.offset (0.0f) + 0.1) < 1.0e-6);
    REQUIRE (std::abs (mapping.offset (1.0f) - 0.3) < 1.0e-6);
    REQUIRE (std::abs (mapping.offset (0.25f) + 0.09) < 1.0e-6);
    REQUIRE (std::abs (mapping.offset (0.75f) - 0.27) < 1.0e-6);
    REQUIRE (std::abs (mapping.offset (0.499999f)) < 0.00001);
    REQUIRE (std::abs (mapping.offset (0.500001f)) < 0.00001);
    auto independent = mapping; independent.responseCurve.reset();
    REQUIRE (! independent.sameResponseCurve (mapping));
    REQUIRE (std::abs (independent.offset (0.25f) + 0.05) < 1.0e-6);
    juce::ValueTree legacy ("MAP"); mapping.readResponseCurve (legacy);
    REQUIRE (std::abs (mapping.offset (0.25f) - independent.offset (0.25f)) < 1.0e-6);
}

TEST_CASE ("Transfer curves round trip arbitrary shapes and reject malformed state")
{
    using scrr::params::TransferCurve;
    TransferCurve curve;
    curve.points = { { 0, 0.8f, -0.3f }, { 0.3f, 0.1f, 0.7f }, { 0.7f, 0.9f, 0 }, { 1, 0.2f, 0 } };
    const auto restored = TransferCurve::decode (curve.encode());
    REQUIRE (restored.points.size() == 4);
    for (int i = 0; i <= 100; ++i)
        REQUIRE (std::abs (restored.evaluate ((double) i / 100) - curve.evaluate ((double) i / 100)) < 1.0e-6);
    REQUIRE (std::abs (restored.evaluate (1) - 0.2) < 1.0e-6); // No LFO-style wrapping at 100%.
    REQUIRE (std::abs (restored.evaluate (0) - 0.8) < 1.0e-6);
    for (const auto* invalid : { "garbage", "1;0,0,0;1,nan,0", "1;0,0,0;1,0.7bad,0", "2;0,1,0;1,0,0" })
        REQUIRE (std::abs (TransferCurve::decode (invalid).evaluate (0.25) - 0.25) < 1.0e-6);
    auto pinned = curve; pinned.sanitise (true, true);
    REQUIRE (std::abs (pinned.points.front().y) < 1.0e-6); REQUIRE (std::abs (pinned.points.back().y - 1) < 1.0e-6);
    REQUIRE (std::abs (pinned.evaluate (0.5, true) - 0.5) < 1.0e-12);
    for (int i = 0; i <= 100; ++i)
    {
        const double value = pinned.evaluate ((double) i / 100, true);
        REQUIRE (std::isfinite (value)); REQUIRE (value >= 0); REQUIRE (value <= 1);
    }
}

TEST_CASE ("Each macro and modulator route saves its own response through preset copy and Undo")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto source = p->addModulator (false);
    p->assignMacro (0, 1, uid, "shift"); p->assignModulator (source, 1, uid, "shift");
    auto route = p->getMacroMappings()[0];
    route.responseCurve.points = { { 0, 0, -0.7f }, { 0.5f, 0.25f, 0.5f }, { 1, 1, 0 } };
    p->updateMacroMapping (route);
    REQUIRE (p->getMacroMappings()[0].sameResponseCurve (route));
    REQUIRE (! p->getMacroMappings()[1].sameResponseCurve (route));
    REQUIRE (p->undo()); REQUIRE (std::abs (p->getMacroMappings()[0].responseCurve.evaluate (0.5) - 0.5) < 1.0e-6);
    REQUIRE (p->redo()); REQUIRE (p->getMacroMappings()[0].sameResponseCurve (route));
    auto modulationRoute = p->getMacroMappings()[1];
    modulationRoute.responseCurve.points = { { 0, 0, 0.5f }, { 1, 1, 0 } };
    p->updateMacroMapping (modulationRoute);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    REQUIRE (restored->getMacroMappings().size() == 2);
    REQUIRE (restored->getMacroMappings()[0].sameResponseCurve (route));
    REQUIRE (restored->getMacroMappings()[1].sameResponseCurve (modulationRoute));
    auto pasted = testProcessor(); REQUIRE (pasted->pasteModule (2, p->copyModule (1, uid)).isNotEmpty());
    REQUIRE (pasted->getMacroMappings().size() == 2);
    REQUIRE (pasted->getMacroMappings()[0].sameResponseCurve (route));
    REQUIRE (pasted->getMacroMappings()[1].sameResponseCurve (modulationRoute));
}

TEST_CASE ("Response curves affect audio without a Mapping editor or changing the saved base")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::drywet, 0);
    p->assignMacro (0, 0, {}, juce::Identifier (id::outputGain));
    auto route = p->getMacroMappings()[0]; route.setDepth (0.1, false);
    route.responseCurve.points = { { 0, 0, 0 }, { 0.5f, 0.25f, 0 }, { 1, 1, 0 } };
    p->updateMacroMapping (route); setParam (*p, id::macros[0], 50);
    p->prepareToPlay (48000, 128);
    juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi;
    for (int block = 0; block < 96; ++block)
    {
        for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 128; ++i) audio.setSample (channel, i, 0.05f);
        p->processBlock (audio, midi);
    }
    const double expected = 0.05 * juce::Decibels::decibelsToGain (1.5);
    REQUIRE (std::abs (audio.getSample (0, 127) - expected) < expected * 0.001);
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (id::outputGain)->load()) < 1.0e-6);
    REQUIRE (std::abs (p->getEffectiveValue (0, {}, juce::Identifier (id::outputGain), 0) - 1.5) < 1.0e-6);
}

TEST_CASE ("Source parameter modulation uses the same independent response curve")
{
    scrr::params::ModulationSource source; source.uid = "response-source"; source.shape = 2; source.rate = 1;
    scrr::params::MacroMapping route;
    route.channel = scrr::params::MacroMapping::modulationChannel; route.uid = source.uid; route.parameter = "rate";
    route.minimum = 0; route.maximum = 10; route.centre = 1; route.setDepth (0.5, false);
    route.responseCurve.points = { { 0, 0, 0 }, { 0.5f, 0.25f, 0 }, { 1, 1, 0 } };
    scrr::dsp::ModulationEngine engine; engine.configure ({ source }, { route }); engine.prepare (1000);
    juce::AudioBuffer<float> audio (1, 100); audio.clear(); scrr::params::MacroValues macros {}; macros[0] = 0.5f;
    engine.process (audio, 0, 100, {}, 120, false, 0, false, nullptr, macros);
    engine.process (audio, 0, 100, {}, 120, false, 0, false, nullptr, macros);
    REQUIRE (std::abs (engine.value (0) - 0.225) < 1.0e-6);
}

TEST_CASE ("Native response editor adds removes bends and pins the bipolar centre")
{
    scrr::gui::TransferCurveEditor editor; editor.setSize (480, 190); editor.setConstraints (true, false);
    int edits = 0; editor.onChange = [&] (auto) { ++edits; };
    const auto area = editor.getPlotBounds();
    const auto point = [&] (float x, float y) { return juce::Point<float> (area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y); };
    editor.mouseDoubleClick (mouse (editor, point (0.4f, 0.2f)));
    REQUIRE (editor.getCurve().points.size() == 3); REQUIRE (edits == 1);
    editor.mouseDoubleClick (mouse (editor, point (0.4f, 0.2f)));
    REQUIRE (editor.getCurve().points.size() == 2);
    const auto midpoint = point (0.5f, 0.5f);
    editor.mouseDown (mouse (editor, midpoint)); editor.mouseDrag (mouse (editor, midpoint.translated (0, -60), true)); editor.mouseUp (mouse (editor, midpoint));
    REQUIRE (editor.getCurve().points[0].curve > 0.39f); REQUIRE (editor.getCurve().evaluate (0.5) > 0.85);
    editor.reset(); editor.setConstraints (true, true);
    REQUIRE (editor.getCurve().points.size() == 3);
    editor.mouseDown (mouse (editor, midpoint)); editor.mouseDrag (mouse (editor, point (0.6f, 0.8f), true)); editor.mouseUp (mouse (editor, midpoint));
    REQUIRE (std::abs (editor.getCurve().evaluate (0.5, true) - 0.5) < 1.0e-12);
    editor.mouseDoubleClick (mouse (editor, midpoint)); REQUIRE (editor.getCurve().points.size() == 3);
    editor.mouseDoubleClick (mouse (editor, point (0.25f, 0.15f))); REQUIRE (editor.getCurve().points.size() == 4);
    uint32_t menuPoint = 0;
    editor.nodeMenu = [&] (juce::Component& target, uint32_t id) { REQUIRE (&target == &editor); menuPoint = id; };
    editor.mouseDown (mouse (editor, point (0.25f, 0.15f), false, juce::ModifierKeys::rightButtonModifier));
    REQUIRE (editor.getCurve().points.size() == 4);
    REQUIRE (menuPoint == editor.getCurve().points[1].id);
}

TEST_CASE ("Mapping page response edits reach the processor without destroying the active editor")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString(); p->assignMacro (0, 1, uid, "shift");
    scrr::gui::MacroMappingPanel panel (*p); panel.setSize (800, 520); panel.showMacro (0);
    auto* editor = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (panel, "map-response-curve")); REQUIRE (editor != nullptr);
    juce::Component::SafePointer<scrr::gui::TransferCurveEditor> surviving (editor);
    p->setModuleParameter (1, uid, "shift", 125.0); // A later base edit must survive an older panel's curve gesture.
    const auto area = editor->getPlotBounds(); const auto point = juce::Point<float> (area.getCentreX(), area.getBottom() - area.getHeight() * 0.25f);
    editor->mouseDoubleClick (mouse (*editor, point)); pump();
    REQUIRE (surviving != nullptr); REQUIRE (std::abs (p->getMacroMappings()[0].responseCurve.evaluate (0.5) - 0.25) < 1.0e-6);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 125.0) < 1.0e-6);
    auto* reset = dynamic_cast<juce::Button*> (findID (panel, "map-curve-reset")); REQUIRE (reset != nullptr); reset->triggerClick(); pump();
    REQUIRE (std::abs (p->getMacroMappings()[0].responseCurve.evaluate (0.5) - 0.5) < 1.0e-6);
}

inline bool renderMappingCurves (SpectralCrrptProcessor& processor, juce::AudioProcessorEditor& editor, const juce::File& directory)
{
    using namespace scrr::params;
    if (directory.createDirectory().failed()) return false;
    auto* panel = dynamic_cast<scrr::gui::MacroMappingPanel*> (findID (editor, "macro-mappings"));
    if (panel == nullptr) return false;
    processor.assignMacro (0, 0, {}, juce::Identifier (id::outputGain));
    processor.assignMacro (0, 0, {}, juce::Identifier (id::inputGain));
    for (auto route : processor.getMacroMappings())
    {
        if (route.channel != 0 || route.macro != 0 || route.sourceUid.isNotEmpty()) continue;
        const bool bipolar = route.parameter.toString() == id::inputGain;
        route.setDepth (bipolar ? 0.2 : 0.1, bipolar);
        route.responseCurve.points = bipolar
            ? std::vector<TransferCurve::Point> { { 0, 0, -0.25f }, { 0.25f, 0.08f, 0.4f }, { 0.5f, 0.5f, -0.4f }, { 0.8f, 0.94f, 0 }, { 1, 1, 0 } }
            : std::vector<TransferCurve::Point> { { 0, 0, -0.3f }, { 0.3f, 0.12f, 0.5f }, { 0.75f, 0.92f, 0 }, { 1, 1, 0 } };
        processor.updateMacroMapping (route);
    }
    const auto capture = [&] (const char* filename)
    {
        pump (80); juce::MemoryOutputStream bytes;
        const auto pixels = editor.createComponentSnapshot (editor.getLocalBounds());
        const auto file = directory.getChildFile (filename);
        const bool ok = juce::PNGImageFormat().writeImageToStream (pixels, bytes) && file.replaceWithData (bytes.getData(), bytes.getDataSize());
        if (ok) std::cout << file.getFullPathName() << '\n';
        return ok;
    };
    if (auto* guide = findID (editor, "quick-guide")) guide->setVisible (false);
    panel->showMacro (0);
    if (! capture ("mapping-response-curves.png")) return false;
    const auto source = processor.addModulator (false); processor.assignModulator (source, 0, {}, juce::Identifier (id::drywet));
    for (auto route : processor.getMacroMappings()) if (route.sourceUid == source)
    {
        route.setDepth (-0.7, false);
        route.responseCurve.points = { { 0, 0, 0.2f }, { 0.3f, 0.7f, -0.4f }, { 0.6f, 0.2f, 0.3f }, { 1, 1, 0 } };
        processor.updateMacroMapping (route);
    }
    panel->showSource (source); const bool result = capture ("mapping-modulator-response.png"); panel->setVisible (false); return result;
}

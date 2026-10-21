// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "RandomPreset.h"
#include "gui/RandomPresetSettings.h"

namespace randomPresetTests {
inline int audioCount (const SpectralCrrptProcessor& processor)
{
    int count = 0;
    for (int ch = 1; ch <= 4; ++ch) for (auto module : processor.getModuleChain (ch))
        if (module["type"].toString() != "Notes") ++count;
    return count;
}
}

TEST_CASE ("Random preset generation is seeded bounded varied and keeps generic stages last")
{
    juce::StringArray observed;
    for (int slices : { 1, 2, 4, 64 }) for (int seed = 1; seed <= 32; ++seed)
    {
        auto generated = scrr::RandomPreset::generate (12, seed, slices, 22050);
        const auto repeated = scrr::RandomPreset::generate (12, seed, slices, 22050);
        REQUIRE (generated.effectCount == 12);
        int total = 0;
        for (size_t ch = 0; ch < generated.chains.size(); ++ch)
        {
            REQUIRE (generated.chains[ch].isEquivalentTo (repeated.chains[ch]));
            if ((int) ch >= slices) REQUIRE (generated.chains[ch].getNumChildren() == 0);
            bool generic = false;
            for (auto module : generated.chains[ch])
            {
                ++total; const auto type = module["type"].toString(); observed.addIfNotAlreadyThere (type);
                REQUIRE (type != "Notes"); REQUIRE (type != "Utility"); REQUIRE ((bool) module["enabled"]);
                if (slices == 1) REQUIRE (! scrr::dsp::isPartialEffect (type));
                if (generic) REQUIRE (scrr::dsp::isGenericEffect (type));
                generic |= scrr::dsp::isGenericEffect (type);
                const auto* spec = scrr::dsp::findModuleSpec (type); REQUIRE (spec != nullptr);
                for (const auto& parameter : spec->params)
                {
                    const double value = (double) module[parameter.id];
                    REQUIRE (std::isfinite (value)); REQUIRE (value >= parameter.minVal); REQUIRE (value <= parameter.maxVal);
                }
                if (type == "Delay") REQUIRE ((double) module["feedback"] <= 50.01);
                if (type == "Compressor") REQUIRE (std::abs ((double) module["makeup"]) < .001);
                if (type == "Distortion" || type == "SpectralClipper") REQUIRE ((double) module["output"] <= 0);
            }
        }
        REQUIRE (total == 12);
    }
    for (const auto& spec : scrr::dsp::getModuleSpecs())
        if (spec.typeId != "Notes" && spec.typeId != "Utility") REQUIRE (observed.contains (spec.typeId));
    REQUIRE (observed.contains ("FrequencyRemap")); REQUIRE (observed.contains ("PartialStretcher")); REQUIRE (observed.contains ("Delay"));
    REQUIRE (scrr::RandomPreset::generate (-1, 1, 1, 22050).effectCount == 1);
    REQUIRE (scrr::RandomPreset::generate (999, 1, 4, 22050).effectCount == 12);
    REQUIRE (scrr::RandomPreset::generate (4, 1, 1, 22050, { 3, 32, 32, 32 }).effectCount == 0);
}

TEST_CASE ("Random preset preserves every global and modulation source while replacing effect bindings with Undo")
{
    auto p = testProcessor();
    using namespace scrr::params;
    setParam (*p, id::freqSplitNumSplits, 4); setParam (*p, id::inputGain, -8); setParam (*p, id::outputGain, -4);
    setParam (*p, id::drywet, 63); setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 6);
    setParam (*p, id::windowTypeExtended, 5); setParam (*p, id::overlapFactor, 7.25f); setParam (*p, id::macros[0], 43);
    p->setMacroName (0, "Keep this name"); p->setPresetName ("Existing preset");
    p->addModule (1, "SpectralMirror"); p->addModule (1, "Notes");
    const auto oldUid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto notesUid = p->getModuleChain (1).getChild (1)["uid"].toString();
    p->setModuleParameter (1, notesUid, "notes", "Preserve the session notes");
    p->assignMacro (0, 1, oldUid, "amount");
    auto owner = p->getMacroMappings().front();
    owner.responseCurve.points = { { 0, 0, 0, 1 }, { .5f, .5f, 0, 3 }, { 1, 1, 0, 2 } }; owner.responseCurve.nextPointId = 4;
    p->updateMacroMapping (owner);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, curveCoordinateParameter (3, false));
    p->assignMacro (2, 0, {}, id::inputGain);
    const auto source = p->addModulator (false); p->assignMacro (3, MacroMapping::modulationChannel, source, "rate");
    const auto before = p->copyPresetState(); const auto globals = p->getAPVTS().copyState();
    REQUIRE (p->getMacroMappings().size() == 4);
    const auto structure = p->getStructureRevision();
    REQUIRE (p->randomizePreset (8, 71025));
    REQUIRE (randomPresetTests::audioCount (*p) == 8);
    REQUIRE (p->getAPVTS().copyState().isEquivalentTo (globals));
    const auto generated = p->copyPresetState();
    REQUIRE (generated.getChildWithName ("modulators").isEquivalentTo (before.getChildWithName ("modulators")));
    REQUIRE (generated.getChildWithName ("macroNames").isEquivalentTo (before.getChildWithName ("macroNames")));
    REQUIRE (p->getCurrentPresetName() == "Existing preset"); REQUIRE (p->isPresetDirty());
    REQUIRE (p->getStructureRevision() == structure + 1);
    REQUIRE (p->getModuleChain (1).getChildWithProperty ("uid", notesUid)["notes"].toString() == "Preserve the session notes");
    REQUIRE (p->getMacroMappings().size() == 2);
    for (const auto& mapping : p->getMacroMappings()) REQUIRE (mapping.channel == 0 || mapping.channel == MacroMapping::modulationChannel);
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (before));
    REQUIRE (p->redo()); REQUIRE (p->copyPresetState().isEquivalentTo (generated));
    REQUIRE (p->getAPVTS().copyState().isEquivalentTo (globals));
}

TEST_CASE ("Random preset retains ALL and Notes and fails without mutation when Notes fill the routed chain")
{
    auto p = testProcessor(); p->addModule (0, "SpectralBloom");
    const auto all = p->getModuleChain (0); const auto allUid = all.getChild (0)["uid"].toString();
    p->assignMacro (0, 0, allUid, "amount");
    const auto original = p->copyPresetState();
    REQUIRE (p->randomizePreset (6, 44)); REQUIRE (p->getModuleChain (0).isEquivalentTo (all)); REQUIRE (p->getMacroMappings().size() == 1);
    const auto generated = p->copyPresetState();
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (original));
    REQUIRE (p->getModuleChain (1).getNumChildren() == 0); REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (p->getMacroMappings().front().channel == 0); REQUIRE (p->getMacroMappings().front().uid == allUid);
    REQUIRE (p->redo()); REQUIRE (p->copyPresetState().isEquivalentTo (generated));
    REQUIRE (p->getMacroMappings().front().uid == allUid);
    for (int ch = 2; ch <= 4; ++ch) REQUIRE (p->getModuleChain (ch).getNumChildren() == 0);
    auto full = testProcessor(); for (int i = 0; i < 32; ++i) full->addModule (1, "Notes");
    full->setPresetDirty (false); const auto before = full->copyPresetState(); const auto revision = full->getStructureRevision();
    REQUIRE (! full->randomizePreset (1, 1)); REQUIRE (full->copyPresetState().isEquivalentTo (before));
    REQUIRE (! full->isPresetDirty()); REQUIRE (full->getStructureRevision() == revision);
}


TEST_CASE ("Random preset Frequency Remap remains canonical across repeated Undo and Redo")
{
    int seedWithRemap = -1;
    for (int seed = 1; seed <= 128 && seedWithRemap < 0; ++seed)
        for (const auto& chain : scrr::RandomPreset::generate (12, seed, 4, 22050).chains)
            for (auto module : chain) if (module["type"].toString() == "FrequencyRemap") seedWithRemap = seed;
    REQUIRE (seedWithRemap > 0);
    auto p = testProcessor(); setParam (*p, scrr::params::id::freqSplitNumSplits, 4);
    const auto original = p->copyPresetState();
    REQUIRE (p->randomizePreset (12, seedWithRemap));
    const auto generated = p->copyPresetState();
    bool found = false;
    for (int ch = 1; ch <= 4; ++ch) for (auto module : p->getModuleChain (ch))
        if (module["type"].toString() == "FrequencyRemap")
        { found = true; REQUIRE (module.hasProperty ("remapFormula")); REQUIRE (module.hasProperty ("formulaView")); }
    REQUIRE (found);
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (original));
    REQUIRE (p->redo()); REQUIRE (p->copyPresetState().isEquivalentTo (generated));
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (original));
}

TEST_CASE ("Random preset button creates an unsaved sound without overwriting library files")
{
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("random-preset-regression", "");
    struct Cleanup { juce::File path; int count { scrr::gui::RandomPresetSettings::get() }; ~Cleanup() { path.deleteRecursively(); scrr::gui::RandomPresetSettings::set (count); } } cleanup { directory };
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->setPresetName ("Saved original"); p->setPresetDirty (false);
    const auto before = p->copyPresetState(); scrr::PresetManager manager (directory); REQUIRE (manager.saveToUser ("Saved original", before).wasOk());
    auto files = directory.findChildFiles (juce::File::findFiles, false); REQUIRE (files.size() == 1);
    const auto bytes = files[0].loadFileAsString();
    scrr::gui::RandomPresetSettings::set (4);
    scrr::gui::PresetBar bar (*p, directory); bar.setSize (900, 34);
    auto* button = dynamic_cast<juce::Button*> (findID (bar, "random-preset")); REQUIRE (button != nullptr);
    REQUIRE (button->getWidth() >= 100); button->triggerClick(); pump (40);
    REQUIRE (p->isPresetDirty()); REQUIRE (p->getCurrentPresetName() == "Saved original");
    REQUIRE (randomPresetTests::audioCount (*p) == 4);
    REQUIRE (directory.findChildFiles (juce::File::findFiles, false).size() == 1); REQUIRE (files[0].loadFileAsString() == bytes);
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (before));
}

TEST_CASE ("Random preset effect count is a shared local preference independent of sound state")
{
    using Preference = scrr::gui::RandomPresetSettings;
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("random-preset-settings", "");
    struct Cleanup { juce::File path; int count { Preference::get() }; ~Cleanup() { path.deleteRecursively(); Preference::set (count); } } cleanup { directory };
    const auto file = directory.getChildFile ("random-preset-count.txt");
    REQUIRE (Preference::read (file) == 6); REQUIRE (Preference::write (12, file)); REQUIRE (Preference::read (file) == 12);
    REQUIRE (file.replaceWithText ("not a count")); REQUIRE (Preference::read (file) == 6);
    Preference::set (6); auto p = testProcessor(); const auto before = p->copyPresetState();
    scrr::gui::SettingsPanel panel (directory.getChildFile ("language.txt"), &p->getAPVTS()); panel.setSize (600, 620);
    auto* choice = dynamic_cast<juce::ComboBox*> (findID (panel, "random-preset-effect-count")); REQUIRE (choice != nullptr);
    REQUIRE (choice->isVisible()); REQUIRE (choice->getNumItems() == 12);
    choice->setSelectedId (9, juce::sendNotificationSync); REQUIRE (Preference::get() == 9); REQUIRE (Preference::read (file) == 9);
    REQUIRE (p->copyPresetState().isEquivalentTo (before));
    scrr::gui::SettingsPanel second; second.tick();
    auto* shared = dynamic_cast<juce::ComboBox*> (findID (second, "random-preset-effect-count")); REQUIRE (shared != nullptr); REQUIRE (shared->getSelectedId() == 9);
}

TEST_CASE ("Random presets render finite bounded stereo audio with unchanged master gain")
{
    for (int seed : { 2, 17, 81 })
    {
        auto p = testProcessor();
        setParam (*p, scrr::params::id::fftSizeExtended, 4); setParam (*p, scrr::params::id::oversample, 0);
        setParam (*p, scrr::params::id::freqSplitNumSplits, 4);
        REQUIRE (p->randomizePreset (12, seed)); p->prepareToPlay (48000, 128);
        juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi; double power = 0;
        for (int block = 0; block < 96; ++block)
        {
            for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 128; ++sample)
            {
                const double time = (block * 128 + sample) / 48000.0;
                audio.setSample (ch, sample, (float) (.08 * std::sin (juce::MathConstants<double>::twoPi * (ch == 0 ? 440 : 660) * time)
                    + .04 * std::sin (juce::MathConstants<double>::twoPi * 2300 * time)));
            }
            p->processBlock (audio, midi);
            for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 128; ++sample)
            {
                const float value = audio.getSample (ch, sample); REQUIRE (std::isfinite (value)); REQUIRE (std::abs (value) <= 8.0f);
                if (block > 32) power += value * value;
            }
        }
        REQUIRE (power > .00001);
        REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::inputGain)->load()) < .001f);
        REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::outputGain)->load()) < .001f);
    }
}

TEST_CASE ("Preset reset baseline uses physical globals Macros and stable module identities")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 320.0);
    setParam (*p, id::inputGain, -9); setParam (*p, id::outputGain, -3); setParam (*p, id::overlapFactor, 7.25f); setParam (*p, id::macros[0], 41);
    const auto sound = p->copyPresetState(); p->capturePresetResetBaseline();
    REQUIRE (p->copyPresetState().isEquivalentTo (sound));
    REQUIRE (! p->copyPresetState().getChildWithName ("presetResetBaseline").isValid());
    p->setModuleParameter (1, uid, "shift", -110.0); setParam (*p, id::inputGain, -4); setParam (*p, id::macros[0], 73);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 9) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::outputGain, 0) + 3) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::overlapFactor, 4) - 7.25) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::macros[0], 0) - 41) < .001);
    REQUIRE (std::abs (p->getModuleResetValue (1, uid, "shift", 0) - 320) < .001);
    REQUIRE (p->transferModule (1, uid, 3, 0, false) == uid);
    REQUIRE (std::abs (p->getModuleResetValue (3, uid, "shift", 0) - 320) < .001);
    p->addModule (1, "FrequencyShift"); const auto added = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, added, "shift", 100.0);
    REQUIRE (std::abs (p->getModuleResetValue (1, added, "shift", 0)) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue ("unknown.parameter", 123) - 123) < .001);
    // A successful save calls this same explicit boundary; temporary edits do not.
    p->capturePresetResetBaseline();
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 4) < .001);
    REQUIRE (std::abs (p->getModuleResetValue (3, uid, "shift", 0) + 110) < .001);
}

TEST_CASE ("Preset reset baseline switches with loaded and random presets through Undo Redo")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto originalUid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, originalUid, "shift", 320.0); setParam (*p, id::inputGain, -9); p->capturePresetResetBaseline();
    p->setModuleParameter (1, originalUid, "shift", 500.0); setParam (*p, id::inputGain, -4);
    const auto before = p->copyPresetState();
    auto target = testProcessor(); target->addModule (1, "FrequencyShift");
    target->setModuleParameter (1, target->getModuleChain (1).getChild (0)["uid"].toString(), "shift", 80.0);
    setParam (*target, id::inputGain, -2);
    REQUIRE (p->switchPreset (target->copyPresetState(), "Target", true));
    const auto loadedUid = p->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs (p->getModuleResetValue (1, loadedUid, "shift", 0) - 80) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 2) < .001);
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (before));
    REQUIRE (std::abs (p->getModuleResetValue (1, originalUid, "shift", 0) - 320) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 9) < .001);
    REQUIRE (p->redo()); REQUIRE (std::abs (p->getModuleResetValue (1, loadedUid, "shift", 0) - 80) < .001);
    REQUIRE (p->restorePresetRecovery());
    const auto recoveredUid = p->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 500) < .001);
    REQUIRE (std::abs (p->getModuleResetValue (1, recoveredUid, "shift", 0) - 320) < .001);
    const auto recovered = p->copyPresetState();
    REQUIRE (p->randomizePreset (6, 89125));
    const auto generated = p->copyPresetState();
    for (int ch = 1; ch <= 4; ++ch) for (auto module : p->getModuleChain (ch))
        if (const auto* spec = scrr::dsp::findModuleSpec (module["type"].toString()))
            for (const auto& parameter : spec->params)
                REQUIRE (std::abs (p->getModuleResetValue (ch, module["uid"].toString(), parameter.id, -9876) - (double) module[parameter.id]) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 4) < .001);
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (recovered));
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 9) < .001);
    REQUIRE (p->redo()); REQUIRE (p->copyPresetState().isEquivalentTo (generated));
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 4) < .001);
}

TEST_CASE ("Preset reset baseline survives dirty host restore with Formula and source values")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    RemapFormula formula; FormulaParameter parameter; parameter.id = "baseline_ratio"; parameter.name = "Ratio";
    parameter.minimum = 1; parameter.maximum = 8; parameter.defaultValue = 2; formula.parameters.push_back (parameter);
    p->setModuleParameter (1, uid, "remapFormula", formula.encode()); p->setModuleParameter (1, uid, parameter.property(), 3.0);
    const auto sourceId = p->addModulator (false); auto source = p->getModulators().front(); source.rate = 3.5f; source.phase = 83; p->updateModulator (source);
    setParam (*p, id::inputGain, -8); setParam (*p, id::macros[2], 37); p->capturePresetResetBaseline();
    p->setModuleParameter (1, uid, parameter.property(), 6.0); source.rate = 7; source.phase = 190; p->updateModulator (source);
    setParam (*p, id::inputGain, -1); setParam (*p, id::macros[2], 92);
    juce::MemoryBlock host; p->getStateInformation (host);
    auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize()); REQUIRE (xml != nullptr);
    REQUIRE (juce::ValueTree::fromXml (*xml).getChildWithName ("presetResetBaseline").isValid());
    auto restored = testProcessor(); restored->setStateInformation (host.getData(), (int) host.getSize());
    const auto newUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs (restored->getGlobalResetValue (id::inputGain, 0) + 8) < .001);
    REQUIRE (std::abs (restored->getGlobalResetValue (id::macros[2], 0) - 37) < .001);
    REQUIRE (std::abs (restored->getModuleResetValue (1, newUid, parameter.property(), 2) - 3) < .001);
    REQUIRE (std::abs ((double) restored->getModuleChain (1).getChild (0)[parameter.property()] - 6) < .001);
    REQUIRE (std::abs (restored->getModuleResetValue (MacroMapping::modulationChannel, sourceId, "rate", 1) - 3.5) < .001);
    REQUIRE (std::abs (restored->getModuleResetValue (MacroMapping::modulationChannel, sourceId, "phase", 0) - 83) < .001);
    REQUIRE (! restored->copyPresetState().getChildWithName ("presetResetBaseline").isValid());
    // A preset file starts a new baseline from its sound, never from host metadata.
    REQUIRE (restored->applyPreset (juce::ValueTree::fromXml (*xml)));
    const auto importedUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs (restored->getModuleResetValue (1, importedUid, parameter.property(), 2) - 6) < .001);
    REQUIRE (std::abs (restored->getGlobalResetValue (id::inputGain, 0) + 1) < .001);
}

TEST_CASE ("Preset reset baseline missing legacy metadata distinguishes dirty and clean projects")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    p->setModuleParameter (1, p->getModuleChain (1).getChild (0)["uid"].toString(), "shift", 700.0);
    setParam (*p, id::inputGain, -6); setParam (*p, id::macros[0], 70);
    auto legacy = p->copyPresetState(); REQUIRE (! legacy.getChildWithName ("presetResetBaseline").isValid());
    for (bool dirty : { true, false })
    {
        legacy.setProperty ("presetDirty", dirty, nullptr); juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), bytes);
        auto restored = testProcessor(); restored->setStateInformation (bytes.getData(), (int) bytes.getSize());
        const auto uid = restored->getModuleChain (1).getChild (0)["uid"].toString();
        REQUIRE (std::abs (restored->getGlobalResetValue (id::inputGain, 0) - (dirty ? 0 : -6)) < .001);
        REQUIRE (std::abs (restored->getGlobalResetValue (id::macros[0], 0) - (dirty ? 0 : 70)) < .001);
        REQUIRE (std::abs (restored->getModuleResetValue (1, uid, "shift", 0) - (dirty ? 0 : 700)) < .001);
    }
}

TEST_CASE ("Preset reset baseline history retains removed entries without creating extra Undo edits")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->addModule (1, "SpectralBloom");
    const auto shift = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, shift, "shift", 200.0); p->capturePresetResetBaseline();
    p->removeModule (1, 1); const auto removed = p->copyPresetState();
    p->setModuleParameter (1, shift, "shift", 400.0); const auto edited = p->copyPresetState();
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (removed));
    REQUIRE (std::abs (p->getModuleResetValue (1, shift, "shift", 0) - 200) < .001);
    REQUIRE (p->redo()); REQUIRE (p->copyPresetState().isEquivalentTo (edited));
    REQUIRE (p->undo()); REQUIRE (p->copyPresetState().isEquivalentTo (removed));
}

TEST_CASE ("Preset reset baseline stays at the saved value when earlier parameter edits are undone")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::inputGain, 5);
    p->capturePresetResetBaseline();
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) - 5) < .001);
    REQUIRE (p->undo());
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (id::inputGain)->load()) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) - 5) < .001);
    REQUIRE (! p->canUndo()); REQUIRE (p->canRedo());
    REQUIRE (p->redo());
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (id::inputGain)->load() - 5) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) - 5) < .001);
    REQUIRE (! p->canRedo());
}

TEST_CASE ("Preset reset baseline rejects malformed XML numbers instead of resetting them to zero")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto source = p->addModulator (false);
    p->setModuleParameter (1, uid, "shift", 300.0); setParam (*p, id::inputGain, -8); p->capturePresetResetBaseline();
    juce::MemoryBlock host; p->getStateInformation (host);
    auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize()); REQUIRE (xml != nullptr);
    auto state = juce::ValueTree::fromXml (*xml); const auto baseline = state.getChildWithName ("presetResetBaseline");
    REQUIRE (baseline.getChildWithName ("globals")[id::inputGain].isString());
    for (const auto* invalid : { "abc", "12junk", "", "NaN", "1e9999" })
    {
        auto bad = state.createCopy(); auto values = bad.getChildWithName ("presetResetBaseline");
        values.getChildWithName ("globals").setProperty (id::inputGain, invalid, nullptr);
        values.getChildWithProperty ("uid", uid).setProperty ("shift", invalid, nullptr);
        values.getChildWithProperty ("uid", source).setProperty ("rate", invalid, nullptr);
        juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary (*bad.createXml(), bytes);
        auto restored = testProcessor(); restored->setStateInformation (bytes.getData(), (int) bytes.getSize());
        const auto newUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
        REQUIRE (std::abs (restored->getGlobalResetValue (id::inputGain, 4) - 4) < .001);
        REQUIRE (std::abs (restored->getModuleResetValue (1, newUid, "shift", 7) - 7) < .001);
        REQUIRE (std::abs (restored->getModuleResetValue (MacroMapping::modulationChannel, source, "rate", 9) - 9) < .001);
    }
}

TEST_CASE ("Preset reset baseline retains Formula values through temporary range changes and host restore")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    RemapFormula formula; FormulaParameter parameter; parameter.id = "range_baseline"; parameter.name = "Ratio";
    parameter.minimum = 1; parameter.maximum = 8; parameter.defaultValue = 2; formula.parameters.push_back (parameter);
    p->setModuleParameter (1, uid, "remapFormula", formula.encode()); p->setModuleParameter (1, uid, parameter.property(), 4.0);
    p->capturePresetResetBaseline();
    formula.parameters[0].maximum = 3; p->setModuleParameter (1, uid, "remapFormula", formula.encode());
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter.property()] - 3) < .001);
    juce::MemoryBlock state; p->getStateInformation (state);
    auto restored = testProcessor(); restored->setStateInformation (state.getData(), (int) state.getSize());
    const auto newUid = restored->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE (std::abs (restored->getModuleResetValue (1, newUid, parameter.property(), 2) - 4) < .001);
    formula.parameters[0].maximum = 8; restored->setModuleParameter (1, newUid, "remapFormula", formula.encode());
    restored->setModuleParameter (1, newUid, parameter.property(), restored->getModuleResetValue (1, newUid, parameter.property(), 2));
    REQUIRE (std::abs ((double) restored->getModuleChain (1).getChild (0)[parameter.property()] - 4) < .001);
}

TEST_CASE ("Preset reset baseline captures the saved snapshot rather than later automation values")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto sourceId = p->addModulator (false); auto source = p->getModulators().front();
    p->setModuleParameter (1, uid, "shift", 123.0); source.rate = 2.5f; p->updateModulator (source);
    setParam (*p, id::inputGain, -7); setParam (*p, id::macros[1], 26);
    const auto saved = p->copyPresetState();
    p->setModuleParameter (1, uid, "shift", 500.0); source.rate = 12; p->updateModulator (source);
    setParam (*p, id::inputGain, -1); setParam (*p, id::macros[1], 80);
    p->capturePresetResetBaseline (saved);
    REQUIRE (std::abs (p->getGlobalResetValue (id::inputGain, 0) + 7) < .001);
    REQUIRE (std::abs (p->getGlobalResetValue (id::macros[1], 0) - 26) < .001);
    REQUIRE (std::abs (p->getModuleResetValue (1, uid, "shift", 0) - 123) < .001);
    REQUIRE (std::abs (p->getModuleResetValue (MacroMapping::modulationChannel, sourceId, "rate", 1) - 2.5) < .001);
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (id::inputGain)->load() + 1) < .001);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 500) < .001);
}

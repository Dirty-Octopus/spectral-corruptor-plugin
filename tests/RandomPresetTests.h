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

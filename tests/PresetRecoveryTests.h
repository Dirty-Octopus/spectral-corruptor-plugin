// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Shift preset recovery keeps one complete instance state and persists only in host state")
{
    auto p = testProcessor();
    p->addModule (1, "SpectralMirror");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "enabled", false);
    p->setMacroName (0, "Original motion");
    p->addModulator (false);
    p->assignMacro (0, 1, uid, "moduleMix");
    p->setPresetName ("Original");
    p->setPresetDirty (true);
    const auto original = p->copyPresetState();
    auto target = testProcessor(); target->addModule (2, "SpectralBloom");
    REQUIRE (p->switchPreset (target->copyPresetState(), "Second", true));
    REQUIRE (p->hasPresetRecovery());
    REQUIRE (p->getPresetRecoveryName() == "Original");
    REQUIRE (! p->isPresetDirty());
    REQUIRE (! p->copyPresetState().getChildWithName ("presetRecovery").isValid());

    juce::MemoryBlock host; p->getStateInformation (host);
    auto restored = testProcessor(); restored->setStateInformation (host.getData(), (int) host.getSize());
    REQUIRE (restored->hasPresetRecovery());
    REQUIRE (restored->restorePresetRecovery());
    REQUIRE (restored->getCurrentPresetName() == "Original");
    REQUIRE (restored->isPresetDirty());
    // applyPreset deliberately regenerates module UIDs and reconnects routes.
    const auto stripModuleIds = [] (auto&& self, juce::ValueTree tree) -> void
    {
        tree.removeProperty ("uid", nullptr);
        for (auto child : tree) self (self, child);
    };
    auto expected = original.createCopy(); auto actual = restored->copyPresetState();
    stripModuleIds (stripModuleIds, expected); stripModuleIds (stripModuleIds, actual);
    REQUIRE (actual.isEquivalentTo (expected));
    REQUIRE (restored->getModulators().size() == 1);
    REQUIRE (restored->getMacroMappings().size() == 1);

    REQUIRE (p->switchPreset (original, "Third", true));
    REQUIRE (p->getPresetRecoveryName() == "Second");
    REQUIRE (! p->switchPreset ({}, "Invalid", true));
    REQUIRE (p->getPresetRecoveryName() == "Second");
    REQUIRE (p->restorePresetRecovery());
    REQUIRE (p->getModuleChain (2).getChild (0)["type"].toString() == "SpectralBloom");
    REQUIRE (p->getCurrentPresetName() == "Second");
    REQUIRE (! p->isPresetDirty());

    auto isolated = testProcessor();
    REQUIRE (! isolated->hasPresetRecovery());
    REQUIRE (p->switchPreset (original, "Normal switch", false));
    REQUIRE (p->getPresetRecoveryName() == "Second");
    juce::MemoryBlock legacy; isolated->getStateInformation (legacy);
    p->setStateInformation (legacy.getData(), (int) legacy.getSize());
    REQUIRE (! p->hasPresetRecovery());
}

TEST_CASE ("Shift preset arrow avoids the unsaved dialog without writing a preset")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    const auto directory = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile ("recovery-regression", "");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    auto p = testProcessor(); p->addModule (1, "SpectralMirror"); p->setPresetName ("Unsaved original");
    scrr::PresetManager manager (directory);
    auto target = testProcessor(); target->addModule (2, "SpectralBloom");
    REQUIRE (manager.saveToUser ("Target", target->copyPresetState()).wasOk());
    scrr::gui::PresetBar bar (*p, directory); bar.setSize (900, 34);
    auto* next = findButton (bar, ">"); REQUIRE (next != nullptr);
    const int shift = juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier;
    static_cast<juce::Component*> (next)->mouseDown (mouse (*next, { 12, 12 }, false, shift));
    static_cast<juce::Component*> (next)->mouseUp (mouse (*next, { 12, 12 }, false, juce::ModifierKeys::shiftModifier));
    pump (40);
    REQUIRE (juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
    REQUIRE (p->hasPresetRecovery());
    REQUIRE (p->getPresetRecoveryName() == "Unsaved original");
    REQUIRE (directory.findChildFiles (juce::File::findFiles, false).size() == 1);
}

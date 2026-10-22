// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Colour HEX previews update immediately and invalid drafts preserve the last valid swatch")
{
    scrr::gui::SettingsPanel panel; panel.setSize (600, 620); panel.showPage (1);
    auto* field = dynamic_cast<juce::TextEditor*> (findID (panel, "colour-0"));
    auto* preview = dynamic_cast<scrr::gui::ColourPreviewButton*> (findID (panel, "colour-preview-0"));
    REQUIRE (field != nullptr); REQUIRE (preview != nullptr); REQUIRE (preview->isVisible());
    const auto savedPalette = scrr::gui::ThemeProfiles::get().values();
    field->setText ("#15A2F0", true); pump();
    REQUIRE (preview->hasValidPreview()); REQUIRE (preview->getPreview().getARGB() == 0xff15a2f0u);
    field->setText ("#15", true); pump();
    REQUIRE (! preview->hasValidPreview()); REQUIRE (preview->getPreview().getARGB() == 0xff15a2f0u);
    REQUIRE (scrr::gui::ThemeProfiles::get().values() == savedPalette);
    panel.showPage (0); REQUIRE (! preview->isVisible());
}

TEST_CASE ("Colour palette and HEX stay synchronized and stale palette callbacks cannot edit a closed settings page")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    scrr::gui::SettingsPanel panel; panel.setSize (600, 620); panel.showPage (1);
    formulaUiTests::foreground (panel);
    auto* field = dynamic_cast<juce::TextEditor*> (findID (panel, "colour-7"));
    auto* preview = dynamic_cast<scrr::gui::ColourPreviewButton*> (findID (panel, "colour-preview-7"));
    REQUIRE (field != nullptr); REQUIRE (preview != nullptr);
    preview->triggerClick(); pump (40);
    auto* selector = dynamic_cast<juce::ColourSelector*> (findID (panel, "colour-palette-selector")); REQUIRE (selector != nullptr);
    selector->setCurrentColour (juce::Colour (0xff13579bu)); pump();
    REQUIRE (field->getText() == "#13579B"); REQUIRE (preview->getPreview().getARGB() == 0xff13579bu);
    field->setText ("#ABCDEF", true); pump(); REQUIRE (selector->getCurrentColour().getARGB() == 0xffabcdefu);
    auto* picker = dynamic_cast<scrr::gui::ColourPaletteEditor*> (selector->getParentComponent()); REQUIRE (picker != nullptr);
    const auto staleChange = picker->changed;
    // Dismissal is deferred. Reopening the same slot must reject a callback
    // originating from the previous picker even while that picker still exists.
    preview->onClick(); staleChange (juce::Colours::red);
    REQUIRE (field->getText() == "#ABCDEF"); pump (60);
    selector = dynamic_cast<juce::ColourSelector*> (findID (panel, "colour-palette-selector")); REQUIRE (selector != nullptr);
    REQUIRE (selector->getCurrentColour().getARGB() == 0xffabcdefu);
    juce::Component::SafePointer<juce::ColourSelector> pending (selector);
    panel.showPage (0); pump (100);
    if (pending) { pending->setCurrentColour (juce::Colours::red); pump(); }
    REQUIRE (field->getText() == "#ABCDEF");
    panel.removeFromDesktop();
}

TEST_CASE ("Settings Formula window choice persists to its injected local preference file")
{
    using Preference = scrr::gui::FormulaWindowPreferences;
    formulaPopoutTests::PreferenceScope restore (Preference::Mode::native);
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("spectral-settings-formula-window", "", false);
    struct Cleanup { juce::File directory; ~Cleanup() { directory.deleteRecursively(); } } cleanup { folder };
    const auto languageFile = folder.getChildFile ("prompt-language.txt"), modeFile = folder.getChildFile ("formula-window.txt");
    scrr::gui::SettingsPanel panel (languageFile); panel.setSize (600, 620);
    auto* choice = dynamic_cast<juce::ComboBox*> (findID (panel, "formula-window-mode")); REQUIRE (choice != nullptr);
    REQUIRE (choice->getSelectedId() == 1);
    choice->setSelectedId (2, juce::sendNotificationSync);
    REQUIRE (Preference::get() == Preference::Mode::contained); REQUIRE (Preference::read (modeFile) == Preference::Mode::contained);
    REQUIRE (modeFile.loadFileAsString() == "contained"); REQUIRE (! languageFile.existsAsFile());
    scrr::gui::SettingsPanel reopened (languageFile);
    auto* reopenedChoice = dynamic_cast<juce::ComboBox*> (findID (reopened, "formula-window-mode")); REQUIRE (reopenedChoice != nullptr);
    REQUIRE (reopenedChoice->getSelectedId() == 2);
    choice->setSelectedId (1, juce::sendNotificationSync);
    REQUIRE (Preference::get() == Preference::Mode::native); REQUIRE (Preference::read (modeFile) == Preference::Mode::native);
    reopened.tick(); REQUIRE (reopenedChoice->getSelectedId() == 1);
}

inline bool renderV026Ui (const juce::File& directory)
{
    if (directory.createDirectory().failed()) return false;
    const auto save = [&] (juce::Component& component, const char* name)
    {
        pump (60); juce::MemoryOutputStream bytes;
        const auto image = component.createComponentSnapshot (component.getLocalBounds());
        return juce::PNGImageFormat().writeImageToStream (image, bytes)
            && directory.getChildFile (name).replaceWithData (bytes.getData(), bytes.getDataSize());
    };
    scrr::gui::SettingsPanel settings; settings.setSize (600, 620); settings.showPage (0);
    formulaUiTests::foreground (settings);
    bool ok = save (settings, "settings-general.png"); settings.showPage (1);
    ok = save (settings, "settings-colours.png") && ok;
    auto* swatch = dynamic_cast<juce::Button*> (findID (settings, "colour-preview-7"));
    if (swatch) { swatch->triggerClick(); pump (60); ok = save (settings, "settings-palette.png") && ok; }
    settings.setVisible (false); settings.removeFromDesktop(); pump (80);
    auto processor = testProcessor(); processor->addModule (1, "FrequencyShift");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString(); processor->assignMacro (0, 1, uid, "shift");
    scrr::gui::MacroMappingPanel mappings (*processor); mappings.setSize (800, 560); mappings.showMacro (0);
    formulaUiTests::foreground (mappings);
    ok = save (mappings, "mapping-collapsed.png") && ok;
    if (auto* arrow = dynamic_cast<juce::Button*> (findID (mappings, "map-curve-disclosure"))) { arrow->triggerClick(); pump(); }
    ok = save (mappings, "mapping-expanded.png") && ok;
    if (auto* grid = dynamic_cast<juce::Button*> (findID (mappings, "curve-grid-settings")))
    {
        grid->triggerClick(); pump (80);
        if (auto* menu = juce::Component::getCurrentlyModalComponent()) ok = save (*menu, "curve-grid-menu.png") && ok;
        else ok = false;
        juce::PopupMenu::dismissAllActiveMenus(); pump (40);
    }
    else ok = false;
    mappings.setVisible (false); mappings.removeFromDesktop();
    for (const auto mode : { formulaPopoutTests::Preference::Mode::native, formulaPopoutTests::Preference::Mode::contained })
    {
        formulaPopoutTests::PreferenceScope preference (mode); formulaPopoutTests::Fixture formula;
        formula.editor.openPopout(); auto* window = formula.editor.getPopoutWindow();
        if (window == nullptr) { ok = false; continue; }
        const bool native = mode == formulaPopoutTests::Preference::Mode::native;
        ok = save (*window, native ? "formula-native-large.png" : "formula-contained-large.png") && ok;
        if (auto* details = dynamic_cast<juce::Button*> (findID (*window, "formula-details"))) { details->triggerClick(); pump(); }
        ok = save (*window, native ? "formula-native-settings.png" : "formula-contained-settings.png") && ok;
        if (! native) ok = save (formula.host, "formula-contained-host.png") && ok;
    }
    return ok;
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Update worker ownership survives editor closure and ends with the last processor")
{
    std::weak_ptr<scrr::updates::UpdateService> weak;
    auto first = testProcessor();
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (first->createEditor());
        weak = first->getUpdateService();
    }
    REQUIRE (! weak.expired());
    auto second = testProcessor();
    REQUIRE (second->getUpdateService() == weak.lock());
    first.reset();
    REQUIRE (! weak.expired());
    second.reset();
    REQUIRE (weak.expired());
}

TEST_CASE ("Update versions compare numerically and reject unstable or malformed tags")
{
    using Service = scrr::updates::UpdateService;
    REQUIRE (Service::compareVersions ("v0.2.0", "0.1.12") > 0);
    REQUIRE (Service::compareVersions ("v1.10.0", "1.9.99") > 0);
    REQUIRE (Service::compareVersions ("v0.2.0", "0.2.0") == 0);
    REQUIRE (Service::compareVersions ("0.1.2", "0.2.0") < 0);
    REQUIRE (Service::compareVersions ("v999.0.0-rc1", "0.2.0") == 0);
    REQUIRE (Service::compareVersions ("garbage", "0.2.0") == 0);
    REQUIRE (Service::trustedAsset ("https://github.com/Dirty-Octopus/spectral-corruptor-plugin/releases/download/v0.2.0/SpectralCorruptor-macOS.pkg"));
    REQUIRE (! Service::trustedAsset ("https://github.com.example.org/Dirty-Octopus/spectral-corruptor-plugin/releases/download/v1/x.pkg"));
    REQUIRE (! Service::trustedAsset ("https://github.com/Dirty-Octopus/spectral-corruptor-plugin/releases/download/../../evil"));
    REQUIRE (! Service::trustedAsset ("http://github.com/Dirty-Octopus/spectral-corruptor-plugin/releases/download/v1/x.pkg"));
    Service updater;
    REQUIRE (! updater.schedule (false)); REQUIRE (! updater.schedule (true));
    REQUIRE (updater.snapshot().state != Service::State::scheduled);
}

TEST_CASE ("Named colour profiles validate HEX, persist and restore the factory palette")
{
    using Profiles = scrr::gui::ThemeProfiles;
    auto& profiles = Profiles::get(); const auto original = profiles.selected();
    juce::uint32 colour {};
    REQUIRE (Profiles::parse ("#aBc123", colour)); REQUIRE (colour == 0xffabc123u);
    REQUIRE (! Profiles::parse ("#FFGG00", colour)); REQUIRE (! Profiles::parse ("123", colour)); REQUIRE (! Profiles::parse ("FF000000", colour));
    REQUIRE (! Profiles::parse ("##AABBCC", colour));
    auto values = Profiles::defaults(); values[7] = 0xff112233u; values[8] = 0xffddeeffu;
    REQUIRE (profiles.save ("Regression profile", values)); REQUIRE (profiles.selected() == "Regression profile");
    REQUIRE (profiles.values()[7] == 0xff112233u); REQUIRE (Profiles::file().existsAsFile());
    REQUIRE (profiles.select ("Default")); REQUIRE (profiles.values() == Profiles::defaults());
    REQUIRE (profiles.select ("Regression profile")); REQUIRE (profiles.values() == values);
    REQUIRE (! profiles.save ("Default", values)); REQUIRE (! profiles.save ("", values));
    REQUIRE (profiles.select (original));
}

TEST_CASE ("Colour saves preserve profiles written by another host and settings track shared changes")
{
    using Profiles = scrr::gui::ThemeProfiles;
    auto& profiles = Profiles::get(); REQUIRE (profiles.select ("Default"));
    auto disk = juce::JSON::parse (Profiles::file());
    auto entries = disk["profiles"]; juce::Array<juce::var> colours;
    for (auto value : Profiles::defaults()) colours.add (juce::String::toHexString ((int) (value & 0xffffffu)).paddedLeft ('0', 6));
    entries.getDynamicObject()->setProperty ("Other host", colours);
    REQUIRE (Profiles::file().replaceWithText (juce::JSON::toString (disk)));
    auto values = Profiles::defaults(); values[0] = 0xff123456u;
    REQUIRE (profiles.save ("Local host", values)); REQUIRE (profiles.names().contains ("Other host"));
    scrr::gui::SettingsPanel panel; panel.setVisible (true); panel.showPage (1);
    auto* field = dynamic_cast<juce::TextEditor*> (findID (panel, "colour-0")); REQUIRE (field);
    REQUIRE (field->getText() == "#123456");
    values[0] = 0xffaabbccu; REQUIRE (profiles.save ("Other selection", values)); panel.tick();
    REQUIRE (field->getText() == "#AABBCC");
    field->setText ("#112233", true); pump();
    REQUIRE (profiles.select ("Default")); panel.tick();
    REQUIRE (field->getText() == "#112233");
    panel.setVisible (false); values[0] = 0xff765432u; REQUIRE (profiles.save ("External edit", values)); panel.tick();
    REQUIRE (field->getText() == "#765432"); REQUIRE (profiles.select ("Default"));
}

TEST_CASE ("Update helper waits for its initiating host after registration removal and obeys cancellation")
{
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile (juce::String::fromUTF8 (u8"spectral update ' 中文 cancellation"), "", false);
    REQUIRE (directory.createDirectory().wasOk());
    const auto registry = directory.getChildFile ("hosts"); REQUIRE (registry.createDirectory().wasOk());
    const auto marker = registry.getChildFile ("self.pid"); REQUIRE (marker.replaceWithText (juce::String (scrr::updates::processId())));
    const auto cancel = directory.getChildFile ("cancel"); const auto installer = directory.getChildFile ("not an installer");
    REQUIRE (installer.replaceWithText ("Test payload must never execute."));
    const auto content = scrr::updates::UpdateService::installScript (scrr::updates::processId(), registry, cancel, installer, "invalid-checksum");
   #if JUCE_WINDOWS
    auto script = directory.getChildFile ("wait.ps1");
    juce::StringArray command { "powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script.getFullPathName() };
   #else
    auto script = directory.getChildFile ("wait.sh");
    juce::StringArray command { "/bin/sh", script.getFullPathName() };
   #endif
    REQUIRE (scrr::updates::UpdateService::writeInstallScript (script, content));
    juce::ChildProcess helper; REQUIRE (helper.start (command)); pump (350);
    REQUIRE (helper.isRunning()); REQUIRE (marker.deleteFile()); pump (350);
    REQUIRE (helper.isRunning()); REQUIRE (! directory.getChildFile ("result.txt").existsAsFile());
    REQUIRE (cancel.replaceWithText ("cancel")); REQUIRE (helper.waitForProcessToFinish (5000));
    REQUIRE (helper.getExitCode() == 0);
    REQUIRE (directory.deleteRecursively());
}

TEST_CASE ("Update helper blocks a modified installer before elevation or execution")
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile (juce::String::fromUTF8 (u8"spectral update 中文 integrity"), "", false);
    REQUIRE (directory.createDirectory().wasOk()); auto registry=directory.getChildFile("hosts");REQUIRE(registry.createDirectory().wasOk());
    auto installer=directory.getChildFile("fake installer");REQUIRE(installer.replaceWithText("initial"));
    const auto expected=juce::SHA256(installer).toHexString().toLowerCase();REQUIRE(installer.replaceWithText("tampered"));
    const auto content=scrr::updates::UpdateService::installScript(2147483647,registry,directory.getChildFile("cancel"),installer,expected);
   #if JUCE_WINDOWS
    auto script=directory.getChildFile("integrity.ps1");juce::StringArray command{"powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script.getFullPathName()};
   #else
    auto script=directory.getChildFile("integrity.sh");juce::StringArray command{"/bin/sh",script.getFullPathName()};
   #endif
    REQUIRE(scrr::updates::UpdateService::writeInstallScript(script, content));juce::ChildProcess helper;REQUIRE(helper.start(command));REQUIRE(helper.waitForProcessToFinish(5000));
    REQUIRE(helper.getExitCode()!=0);REQUIRE(directory.getChildFile("result.txt").loadFileAsString().containsIgnoreCase("checksum"));
    REQUIRE(directory.deleteRecursively());
}

TEST_CASE ("Existing editor controls retain semantic colour roles when equal palette slots diverge")
{
    using Profiles = scrr::gui::ThemeProfiles;
    auto& profiles = Profiles::get();
    struct RestoreProfile
    {
        juce::String name;
        ~RestoreProfile() { scrr::gui::ThemeProfiles::get().select (name); }
    } restore { profiles.selected() };
    Profiles::Colours equal; equal.fill (0xff4a4a4au);
    REQUIRE (profiles.save ("Equal colour roles", equal));
    auto processor = testProcessor(); processor->addModule (1, "SpectralFilter"); processor->addModulator (false);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    auto* master = dynamic_cast<juce::Slider*> (findID (*editor, "input-gain"));
    auto* macro = dynamic_cast<juce::Slider*> (findID (*editor, "macro-1"));
    auto* macroName = dynamic_cast<juce::Label*> (findID (*editor, "macro-name-1"));
    auto* license = findID (*editor, "license-panel");
    auto* code = dynamic_cast<juce::TextEditor*> (findID (*editor, "activation-code"));
    auto* save = findButton (*editor, "SAVE");
    auto* licenseStatus = findID (*editor, "license-status");
    REQUIRE (master && macro && macroName && license && code && save && licenseStatus);
    auto* activate = findButton (*license, "ACTIVATE"); REQUIRE (activate);
    code->setFont (scrr::gui::Theme::font (26, true)); code->setCaretVisible (false);
    code->setText ("MMMMMMMM", false);
    auto* modulation = findButton (*editor, "MODULATION"); REQUIRE (modulation);
    modulation->triggerClick(); pump (50);
    auto* rate = dynamic_cast<juce::Slider*> (findID (*editor, "modulation-rate")); REQUIRE (rate);
    const Profiles::Colours distinct { 0xff112233u, 0xff243546u, 0xff374859u, 0xff4a5b6cu,
        0xffd2f4e6u, 0xff8293a4u, 0xff95a6b7u, 0xffc34a51u, 0xffe5bd62u, 0xff487bd9u };
    REQUIRE (profiles.save ("Separated colour roles", distinct));
    const auto colour = [&] (int slot) { return juce::Colour (distinct[(size_t) slot]); };
    REQUIRE (waitForUI ([&] { return master->findColour (juce::Slider::textBoxTextColourId) == colour (4); }));
    REQUIRE (master->findColour (juce::Slider::thumbColourId) == colour (0));
    REQUIRE (master->findColour (juce::Slider::trackColourId) == colour (0));
    REQUIRE (master->findColour (juce::Slider::rotarySliderFillColourId) == colour (0));
    REQUIRE (master->findColour (juce::Slider::textBoxBackgroundColourId) == colour (2));
    REQUIRE (master->findColour (juce::Slider::textBoxOutlineColourId) == colour (3));
    REQUIRE (macro->findColour (juce::Slider::rotarySliderFillColourId) == colour (7));
    REQUIRE (macro->findColour (juce::Slider::textBoxTextColourId) == colour (4));
    REQUIRE (macro->findColour (juce::Slider::textBoxBackgroundColourId) == colour (2));
    REQUIRE (macroName->findColour (juce::Label::backgroundColourId) == colour (1));
    REQUIRE (macroName->findColour (juce::Label::textColourId) == colour (4));
    REQUIRE (save->findColour (juce::TextButton::buttonColourId) == colour (8));
    REQUIRE (save->findColour (juce::TextButton::textColourOffId) == colour (0));
    REQUIRE (activate->findColour (juce::TextButton::buttonColourId) == colour (8));
    REQUIRE (activate->findColour (juce::TextButton::textColourOffId) == colour (0));
    REQUIRE (licenseStatus->findColour (juce::TextButton::buttonColourId) == colour (1));
    REQUIRE (rate->findColour (juce::Slider::thumbColourId) == colour (4));
    REQUIRE (rate->findColour (juce::Slider::textBoxTextColourId) == colour (4));
    REQUIRE (rate->findColour (juce::Slider::textBoxBackgroundColourId) == colour (2));
    REQUIRE (rate->findColour (juce::Slider::textBoxOutlineColourId) == colour (3));
    auto* frequency = dynamic_cast<scrr::gui::ParameterControl*> (findID (*editor, "freq")); REQUIRE (frequency);
    REQUIRE (frequency->getAssignableSlider()->findColour (juce::Slider::textBoxTextColourId) == colour (4));
    REQUIRE (code->getText() == "MMMMMMMM");
    // Existing TextEditor glyphs cache their own colour, independent of findColour().
    const auto rendered = code->createComponentSnapshot (code->getLocalBounds());
    int textPixels = 0;
    for (int y = 0; y < rendered.getHeight(); ++y) for (int x = 0; x < rendered.getWidth(); ++x)
        if (rendered.getPixelAt (x, y) == colour (4)) ++textPixels;
    REQUIRE (textPixels > 20);
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "PromptLanguage.h"
#include "../licensing/LicenseManager.h"

namespace scrr::gui {
class LicensePanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit LicensePanel (licensing::LicenseManager& manager) : license (manager)
    {
        setComponentID ("license-panel");
        machine.setText (license.getMachineCode().isEmpty() ? "MACHINE ID UNAVAILABLE" : license.getMachineCode());
        machine.setReadOnly (true); addAndMakeVisible (machine);
        code.setMultiLine (true); code.setReturnKeyStartsNewLine (true); code.setScrollbarsShown (true);
        code.setTextToShowWhenEmpty ("Paste a code or drop a .sclicense file here", Theme::dim());
        code.setInputRestrictions (4096); code.setComponentID ("activation-code"); addAndMakeVisible (code);
        copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (license.getMachineCode()); status = "Machine code copied."; repaint(); };
        activate.onClick = [this]
        {
            const auto result = license.activate (code.getText());
            status = result.wasOk() ? "ACTIVATED / Normal processing restored." : result.getErrorMessage();
            repaint(); if (changed) changed();
        };
        close.onClick = [this] { setVisible (false); };
        for (auto* button : { &copy, &activate, &close }) addAndMakeVisible (button);
        activate.setColour (juce::TextButton::buttonColourId, Theme::yellow());
        activate.setColour (juce::TextButton::textColourOffId, Theme::black());
        refreshLanguage();
    }
    void refreshLanguage()
    {
        machine.setText (license.getMachineCode().isEmpty() ? promptText ("MACHINE ID UNAVAILABLE") : license.getMachineCode());
        code.setTextToShowWhenEmpty (promptText ("Paste a code or drop a .sclicense file here"), Theme::dim());
        copy.setButtonText (promptText ("COPY")); activate.setButtonText (promptText ("ACTIVATE")); close.setButtonText (promptText ("CLOSE  x")); repaint();
    }
    std::function<void()> changed;
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    { return files.size() == 1 && juce::File::isAbsolutePath (files[0]) && juce::File (files[0]).hasFileExtension ("sclicense"); }
    void fileDragEnter (const juce::StringArray&, int, int) override { fileHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { fileHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        fileHover = false;
        setVisible (true); toFront (true);
        if (! isInterestedInFileDrag (files)) return;
        const juce::File file (files[0]);
        // Bound the read as well as the input field. Never replace a valid licence on failure.
        juce::FileInputStream stream (file);
        if (! stream.openedOk() || stream.getTotalLength() <= 0 || stream.getTotalLength() > 4096)
            status = "Unable to read licence file (maximum 4 KB).";
        else
        {
            char data[4097] {};
            const int bytes = stream.read (data, 4097);
            const auto result = bytes > 4096 ? juce::Result::fail ("Licence file is too large.")
                                            : license.activate (juce::String::fromUTF8 (data, bytes));
            status = result.wasOk() ? "ACTIVATED / Licence file accepted." : result.getErrorMessage();
            if (result.wasOk()) code.clear();
            if (changed) changed();
        }
        repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (24); close.setBounds (area.removeFromTop (32).removeFromRight (92));
        area.removeFromTop (100);
        auto machineRow = area.removeFromTop (32); copy.setBounds (machineRow.removeFromRight (88));
        machineRow.removeFromRight (8); machine.setBounds (machineRow);
        area.removeFromTop (30); code.setBounds (area.removeFromTop (104));
        area.removeFromTop (12); activate.setBounds (area.removeFromTop (34).removeFromLeft (160));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), fileHover ? 7 : 3);
        Theme::text (g, promptText ("MACHINE ACTIVATION"), { 24, 20, getWidth() - 145, 40 }, 23, Theme::yellow(), true);
        Theme::text (g, promptText (license.isActivated() ? "ACTIVATED / THIS MACHINE" : "NOT ACTIVATED / PINK NOISE ONLY"),
                     { 24, 68, getWidth() - 48, 25 }, 15, license.isActivated() ? Theme::yellow() : Theme::red(), true);
        Theme::text (g, promptText ("Send this machine code to receive your activation code."), { 24, 103, getWidth() - 48, 24 }, 12, Theme::white());
        Theme::text (g, promptText (fileHover ? "DROP TO ACTIVATE" : "ACTIVATION CODE / DROP .SCLICENSE"), { 24, 192, getWidth() - 48, 22 }, 11, Theme::dim(), true);
        g.setColour (Theme::white()); g.setFont (Theme::font (12));
        g.drawFittedText (promptText (status), { 24, 378, getWidth() - 48, 48 }, juce::Justification::topLeft, 3);
    }
private:
    licensing::LicenseManager& license;
    juce::TextEditor machine, code;
    juce::TextButton copy { "COPY" }, activate { "ACTIVATE" }, close { "CLOSE  x" };
    juce::String status;
    bool fileHover {};
};
}

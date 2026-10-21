// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "PromptLanguage.h"
#include "RandomPresetSettings.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

namespace scrr::gui {
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (SpectralCrrptProcessor&, juce::File libraryDirectory = {});
    ~PresetBar() override;
    void resized() override;
    void refreshColours();
private:
    class PresetButton : public juce::TextButton
    {
    public:
        explicit PresetButton (const juce::String& label) : juce::TextButton (label) {}
        bool shiftClick {};
        void clicked (const juce::ModifierKeys& modifiers) override
        { shiftClick = modifiers.isShiftDown(); juce::TextButton::clicked (modifiers); }
    };
    void timerCallback() override;
    void setStatus (const juce::String& text) { statusKey = text; status.setText (promptText (text), juce::dontSendNotification); }
    void showMenu();
    void select (int, bool keepRecovery = false);
    void requestSelect (int, bool keepRecovery = false);
    void step (int, bool keepRecovery = false);
    void saveLibrary();
    void exportFile();
    void importFile();
    void saved (const juce::Result&, const juce::String&);
    SpectralCrrptProcessor& processor;
    scrr::PresetManager manager;
    PresetButton preset { "Init" }, previous { "<" }, next { ">" };
    juce::TextButton randomPreset { "RANDOM PRESET" }, save { "SAVE" }, exportButton { "EXPORT" }, load { "IMPORT" };
    juce::Label status;
    juce::String statusKey;
    int index { -1 };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> dialog;
};
} // namespace scrr::gui

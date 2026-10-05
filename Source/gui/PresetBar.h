// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "PromptLanguage.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

namespace scrr::gui {
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (SpectralCrrptProcessor&, juce::File libraryDirectory = {});
    ~PresetBar() override;
    void resized() override;
private:
    void timerCallback() override;
    void setStatus (const juce::String& text) { statusKey = text; status.setText (promptText (text), juce::dontSendNotification); }
    void showMenu();
    void select (int);
    void requestSelect (int);
    void step (int);
    void saveLibrary();
    void exportFile();
    void importFile();
    void saved (const juce::Result&, const juce::String&);
    SpectralCrrptProcessor& processor;
    scrr::PresetManager manager;
    juce::TextButton preset { "Init" }, previous { "<" }, next { ">" }, save { "SAVE" }, exportButton { "EXPORT" }, load { "IMPORT" };
    juce::Label status;
    juce::String statusKey;
    int index { -1 };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> dialog;
};
} // namespace scrr::gui

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "PromptLanguage.h"
#include "../parameters/ParamIDs.h"
namespace scrr::gui {
class HeaderSettingsButton : public juce::TextButton
{
public:
    HeaderSettingsButton() : juce::TextButton ("SETTINGS") {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // The header's red rectangle is the hit area; no separate tile or frame.
        const auto ink = down ? Theme::yellow() : over || hasKeyboardFocus (false) ? Theme::white() : Theme::black();
        Theme::text (g, getButtonText(), getLocalBounds(), 13, ink, true, juce::Justification::centred);
        if (over || hasKeyboardFocus (false))
        { g.setColour (ink); g.fillRect (12, getHeight() - 17, getWidth() - 24, 2); }
    }
};
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (juce::File file = PromptLanguage::preferenceFile(), juce::AudioProcessorValueTreeState* state = nullptr) : preference (std::move (file))
    {
        setComponentID ("settings-panel"); language.setComponentID ("prompt-language");
        language.addItem ("English", 1); language.addItem (juce::String::fromUTF8 (u8"简体中文"), 2);
        language.onChange = [this]
        {
            const auto selected = language.getSelectedId() == 2 ? PromptLanguage::Language::chinese : PromptLanguage::Language::english;
            PromptLanguage::set (selected, false); saved = PromptLanguage::write (selected, preference);
            refresh(); if (changed) changed();
        };
        frequencyLimit.setComponentID ("spectral-frequency-limit");
        frequencyLimit.addItemList (scrr::params::choice::frequencyLimit, 1);
        frequencyLimit.setSelectedItemIndex (2, juce::dontSendNotification);
        frequencyLimit.setEnabled (state != nullptr);
        if (state != nullptr) frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (*state, scrr::params::id::frequencyLimit, frequencyLimit);
        addAndMakeVisible (frequencyLimit);
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (language); addAndMakeVisible (close); refresh();
    }
    std::function<void()> changed;
    void refresh()
    {
        language.setSelectedId (PromptLanguage::get() == PromptLanguage::Language::chinese ? 2 : 1, juce::dontSendNotification);
        close.setButtonText (promptText ("CLOSE  x")); repaint();
    }
    void resized() override
    {
        close.setBounds (getWidth() - 116, 24, 92, 32);
        language.setBounds (24, 108, getWidth() - 48, 38);
        frequencyLimit.setBounds (24, 250, getWidth() - 48, 38);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, promptText ("SETTINGS"), { 24, 20, getWidth() - 152, 40 }, 23, Theme::yellow(), true);
        Theme::text (g, promptText ("PROMPT LANGUAGE"), { 24, 77, getWidth() - 48, 24 }, 13, Theme::white(), true);
        g.setFont (Theme::font (13)); g.setColour (Theme::white());
        g.drawFittedText (promptText ("Applies to activation and dialogs. Effect names and controls stay in English."),
                         { 24, 164, getWidth() - 48, 52 }, juce::Justification::topLeft, 3);
        Theme::text (g, promptText ("SPECTRAL FREQUENCY LIMIT"), { 24, 219, getWidth() - 48, 24 }, 13, Theme::white(), true);
        g.setFont (Theme::font (13)); g.setColour (Theme::white());
        g.drawFittedText (promptText ("Saved in presets and projects. Higher frequencies bypass spectral effects. GENERIC effects still process the channel."),
                         { 24, 302, getWidth() - 48, 56 }, juce::Justification::topLeft, 3);
        g.setColour (saved ? Theme::dim() : Theme::red()); g.setFont (Theme::font (12));
        g.drawFittedText (promptText (saved ? "Language saved on this computer. Applies to all plugin instances." : "Could not save language preference. Check folder permissions."),
                         { 24, 369, getWidth() - 48, 30 }, juce::Justification::topLeft, 2);
    }
private:
    juce::File preference;
    juce::ComboBox language, frequencyLimit;
    juce::TextButton close;
    bool saved { true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> frequencyAttachment;
};
}

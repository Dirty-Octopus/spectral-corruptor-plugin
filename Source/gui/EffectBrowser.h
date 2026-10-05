// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "EffectCatalog.h"
#include "components/ParameterPanel.h"

namespace scrr::gui {
class EffectBrowser : public juce::Component
{
public:
    EffectBrowser()
    {
        setComponentID ("effect-browser"); setWantsKeyboardFocus (true);
        search.setTextToShowWhenEmpty ("Search all effects...", Theme::dim());
        search.setFont (Theme::font (14)); search.onTextChange = [this] { refresh(); };
        addAndMakeVisible (search);
        close.setButtonText ("CLOSE  x"); close.onClick = [this] { setVisible (false); }; addAndMakeVisible (close);
        viewport.setViewedComponent (&list, false); viewport.setScrollBarsShown (true, false); addAndMakeVisible (viewport);
        const juce::StringArray names { "HARMONICS", "FREQUENCY", "DESTRUCTION", "PHASE", "TEXTURE / TIME", "FILTER / UTILITY", "GENERIC" };
        for (int i = 0; i < names.size(); ++i)
        {
            auto button = std::make_unique<juce::TextButton> (names[i]);
            const auto folder = names[i];
            button->onClick = [this, folder] { currentFolder = folder; search.clear(); refresh(); };
            addAndMakeVisible (*button); folders.push_back (std::move (button));
        }
        refresh();
    }
    ~EffectBrowser() override { viewport.setViewedComponent (nullptr, false); }
    std::function<void(const juce::String&)> onChoose;
    void open()
    { search.clear(); refresh(); setVisible (true); toFront (true); search.grabKeyboardFocus(); }
    bool keyPressed (const juce::KeyPress& key) override
    { if (key == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 2);
        Theme::text (g, "EFFECT LIBRARY", { 18, 10, getWidth() - 135, 35 }, 24, Theme::yellow(), true);
        Theme::text (g, "CHOOSE A FOLDER / ADD A STAGE", { 18, 44, getWidth() - 35, 20 }, 10, Theme::dim(), true);
        if (items.empty()) Theme::text (g, "No matching effects", viewport.getBounds().reduced (10), 14, Theme::dim());
    }
    void resized() override
    {
        close.setBounds (getWidth() - 106, 14, 90, 28);
        search.setBounds (18, 73, getWidth() - 36, 32);
        int y = 119;
        const int folderHeight = juce::jmax (22, juce::jmin (34, (getHeight() - 131) / (int) folders.size() - 4));
        for (auto& folder : folders) { folder->setBounds (18, y, 139, folderHeight); y += folderHeight + 4; }
        viewport.setBounds (169, 117, getWidth() - 185, getHeight() - 133);
        int itemY = 0;
        for (auto& item : items) { item->setBounds (0, itemY, viewport.getWidth() - 12, 43); itemY += 48; }
        list.setSize (juce::jmax (10, viewport.getWidth() - 12), juce::jmax (viewport.getHeight(), itemY));
    }
private:
    void refresh()
    {
        items.clear(); const auto query = search.getText().trim();
        for (auto& folder : folders) folder->setToggleState (query.isEmpty() && folder->getButtonText() == currentFolder, juce::dontSendNotification);
        for (const auto& spec : scrr::dsp::getModuleSpecs())
        {
            if (query.isEmpty() ? effectFolder (spec.typeId) != currentFolder : ! spec.displayName.containsIgnoreCase (query)) continue;
            auto button = std::make_unique<juce::TextButton> ("+   " + spec.displayName);
            button->setComponentID ("add-" + spec.typeId); button->setTooltip (moduleDescription (spec.typeId));
            const auto safe = juce::Component::SafePointer<EffectBrowser> (this); const auto type = spec.typeId;
            button->onClick = [safe, type]
            {
                juce::MessageManager::callAsync ([safe, type]
                { if (safe) { safe->setVisible (false); if (safe->onChoose) safe->onChoose (type); } });
            };
            list.addAndMakeVisible (*button); items.push_back (std::move (button));
        }
        viewport.setViewPosition (0, 0); resized(); repaint();
    }
    juce::String currentFolder { "HARMONICS" };
    juce::TextEditor search;
    juce::TextButton close;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<juce::TextButton>> folders, items;
};
} // namespace scrr::gui

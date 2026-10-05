// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PresetBar.h"
namespace scrr::gui {
PresetBar::PresetBar (SpectralCrrptProcessor& p, juce::File libraryDirectory) : processor (p), manager (libraryDirectory)
{
    for (auto* b : { &preset, &previous, &next, &save, &exportButton, &load }) addAndMakeVisible (b);
    save.setColour (juce::TextButton::buttonColourId, Theme::yellow());
    save.setColour (juce::TextButton::textColourOffId, Theme::black());
    preset.onClick = [this] { showMenu(); }; previous.onClick = [this] { step (-1); }; next.onClick = [this] { step (1); };
    save.onClick = [this] { saveLibrary(); }; exportButton.onClick = [this] { exportFile(); }; load.onClick = [this] { importFile(); };
    preset.setTooltip ("Factory and user presets. * means modified.");
    save.setTooltip ("Save a named preset to your user library.");
    status.setFont (Theme::font (11)); status.setColour (juce::Label::textColourId, Theme::dim());
    addAndMakeVisible (status); timerCallback(); startTimerHz (4);
}
PresetBar::~PresetBar()
{
    stopTimer();
    juce::PopupMenu::dismissAllActiveMenus();
    chooser.reset(); dialog.reset();
}
void PresetBar::timerCallback()
{
    status.setText (promptText (statusKey), juce::dontSendNotification);
    if (status.getTooltip().isNotEmpty()) status.setTooltip (promptText (statusKey));
    auto text = processor.getCurrentPresetName() + (processor.isPresetDirty() ? " *" : "");
    if (preset.getButtonText() != text) preset.setButtonText (text);
}
void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous.setBounds (area.removeFromLeft (32).reduced (1)); next.setBounds (area.removeFromLeft (32).reduced (1));
    preset.setBounds (area.removeFromLeft (juce::jmin (340, getWidth() / 3)).reduced (3, 1));
    save.setBounds (area.removeFromLeft (66).reduced (3, 1));
    exportButton.setBounds (area.removeFromLeft (80).reduced (3, 1)); load.setBounds (area.removeFromLeft (80).reduced (3, 1));
    status.setBounds (area.reduced (6, 1));
}
void PresetBar::showMenu()
{
    manager.rescan();
    juce::PopupMenu menu, factory, user;
    auto entries = manager.getAllEntries();
    juce::StringArray folders;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (! e.isFactory) user.addItem (i + 1, e.name);
        else if (e.folder.isEmpty()) factory.addItem (i + 1, e.name);
        else folders.addIfNotAlreadyThere (e.folder);
    }
    for (const auto& folder : folders)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].isFactory && entries[i].folder == folder) sub.addItem (i + 1, entries[i].name);
        factory.addSubMenu (folder, sub);
    }
    menu.addSubMenu ("Factory", factory); menu.addSubMenu ("User library", user);
    const auto safe = juce::Component::SafePointer<PresetBar> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&preset),
                       [safe] (int result) { if (safe && result > 0) safe->requestSelect (result - 1); });
}
void PresetBar::requestSelect (int selected)
{
    if (! processor.isPresetDirty()) { select (selected); return; }
    dialog = std::make_unique<juce::AlertWindow> (promptText ("Unsaved preset"), promptText ("Save your changes before switching?"), juce::AlertWindow::NoIcon);
    dialog->setLookAndFeel (&getLookAndFeel());
    dialog->addButton (promptText ("Keep editing"), 0); dialog->addButton (promptText ("Discard and switch"), 1); dialog->addButton (promptText ("Save as..."), 2);
    const auto safe = juce::Component::SafePointer<PresetBar> (this);
    const auto window = juce::Component::SafePointer<juce::AlertWindow> (dialog.get());
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, selected] (int result)
    {
        if (! safe || ! window || safe->dialog.get() != window.getComponent()) return;
        // exitModalState ends modality, but does not hide an owned AlertWindow.
        // Retire this exact window before another dialog can be created.
        auto closed = std::move (safe->dialog);
        closed->setVisible (false);
        if (result == 1) safe->select (selected);
        if (result == 2) juce::MessageManager::callAsync ([safe] { if (safe) safe->saveLibrary(); });
    }), false); // Owned by this component, never also auto-deleted by JUCE.
}
void PresetBar::select (int selected)
{
    auto state = manager.loadPreset (selected);
    if (! processor.applyPreset (state)) { setStatus ("Invalid preset"); return; }
    index = selected; processor.setPresetName (manager.getEntry (index).name); processor.setPresetDirty (false);
    setStatus ("Preset loaded"); timerCallback();
}
void PresetBar::step (int direction)
{
    manager.rescan();
    auto current = processor.getCurrentPresetName();
    auto entries = manager.getAllEntries();
    index = -1;
    for (int i = 0; i < entries.size(); ++i) if (entries[i].name == current) { index = i; break; }
    if (entries.isEmpty()) return;
    requestSelect ((index + direction + entries.size()) % entries.size());
}
void PresetBar::saved (const juce::Result& result, const juce::String& name)
{
    if (result.failed()) { setStatus (result.getErrorMessage()); status.setTooltip (promptText (result.getErrorMessage())); return; }
    processor.setPresetName (name); processor.setPresetDirty (false);
    setStatus ("Saved"); status.setTooltip ({}); timerCallback();
}
void PresetBar::saveLibrary()
{
    dialog = std::make_unique<juce::AlertWindow> (promptText ("Save preset"), promptText ("Name this sound"), juce::AlertWindow::NoIcon);
    dialog->setLookAndFeel (&getLookAndFeel());
    dialog->addTextEditor ("name", processor.getCurrentPresetName());
    dialog->addButton (promptText ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (promptText ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const auto safe = juce::Component::SafePointer<PresetBar> (this);
    const auto window = juce::Component::SafePointer<juce::AlertWindow> (dialog.get());
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
    {
        if (! safe || ! window || safe->dialog.get() != window.getComponent()) return;
        auto closed = std::move (safe->dialog);
        const auto name = closed->getTextEditorContents ("name").trim();
        closed->setVisible (false);
        if (result != 1) return;
        auto savedResult = safe->manager.saveToUser (name, safe->processor.copyPresetState());
        safe->saved (savedResult, name);
    }), false);
}
void PresetBar::exportFile()
{
    chooser = std::make_unique<juce::FileChooser> (promptText ("Export preset"), manager.getUserDir().getChildFile (processor.getCurrentPresetName() + ".scpreset"), "*.scpreset");
    const auto safe = juce::Component::SafePointer<PresetBar> (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& fc)
        {
            if (! safe || fc.getResult() == juce::File()) return;
            auto file = fc.getResult().withFileExtension ("scpreset");
            safe->saved (safe->manager.saveToFile (file, safe->processor.copyPresetState()), file.getFileNameWithoutExtension());
        });
}
void PresetBar::importFile()
{
    chooser = std::make_unique<juce::FileChooser> (promptText ("Import preset"), manager.getUserDir(), "*.scpreset");
    const auto safe = juce::Component::SafePointer<PresetBar> (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& fc)
        {
            if (! safe || fc.getResult() == juce::File()) return;
            if (! safe->processor.applyPreset (safe->manager.loadFromFile (fc.getResult())))
            { safe->setStatus ("Invalid preset"); return; }
            safe->processor.setPresetName (fc.getResult().getFileNameWithoutExtension()); safe->processor.setPresetDirty (false);
            safe->timerCallback();
        });
}
} // namespace scrr::gui

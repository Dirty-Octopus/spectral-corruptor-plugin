// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "PromptLanguage.h"
#include "UiScale.h"
#include "RandomPresetSettings.h"
#include "ColourPaletteEditor.h"
#include "FormulaWindowPreferences.h"
#include "../parameters/ParamIDs.h"
#include "../updates/UpdateService.h"
namespace scrr::gui {
class HeaderSettingsButton : public juce::TextButton
{
public:
    HeaderSettingsButton() : juce::TextButton ("SETTINGS") {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto ink = down ? Theme::yellow() : over || hasKeyboardFocus (false) ? Theme::white() : Theme::black();
        Theme::text (g, getButtonText(), getLocalBounds(), 13, ink, true, juce::Justification::centred);
        if (over || hasKeyboardFocus (false)) { g.setColour (ink); g.fillRect (12, getHeight() - 17, getWidth() - 24, 2); }
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
            auto selected = language.getSelectedId() == 2 ? PromptLanguage::Language::chinese : PromptLanguage::Language::english;
            PromptLanguage::set (selected, false); saved = PromptLanguage::write (selected, preference); refresh(); if (changed) changed();
        };
        frequencyLimit.setComponentID ("spectral-frequency-limit"); frequencyLimit.addItemList (scrr::params::choice::frequencyLimit, 1);
        frequencyLimit.setSelectedItemIndex (2, juce::dontSendNotification); frequencyLimit.setEnabled (state != nullptr);
        if (state) frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (*state, scrr::params::id::frequencyLimit, frequencyLimit);
        uiScale.setComponentID ("ui-scale"); for (int p = 25; p <= 200; p += 25) uiScale.addItem (juce::String (p) + "%", p);
        uiScale.onChange = [this] { UiScale::set (uiScale.getSelectedId()); saved = UiScale::write (UiScale::get(), preference.getSiblingFile ("scale.txt")); repaint(); };
        randomPresetCount.setComponentID ("random-preset-effect-count");
        for (int count = 1; count <= 12; ++count) randomPresetCount.addItem (juce::String (count), count);
        randomPresetCount.onChange = [this]
        {
            RandomPresetSettings::set (randomPresetCount.getSelectedId());
            saved = RandomPresetSettings::write (RandomPresetSettings::get(), preference.getSiblingFile ("random-preset-count.txt")); repaint();
        };
        formulaWindow.setComponentID ("formula-window-mode");
        formulaWindow.addItem ("Native system window", 1); formulaWindow.addItem ("Inside plugin window", 2);
        formulaWindow.onChange = [this]
        {
            const auto mode = formulaWindow.getSelectedId() == 2 ? FormulaWindowPreferences::Mode::contained : FormulaWindowPreferences::Mode::native;
            FormulaWindowPreferences::set (mode);
            saved = FormulaWindowPreferences::write (mode, preference.getSiblingFile ("formula-window.txt")); repaint();
        };
        close.onClick = [this] { setVisible (false); };
        generalTab.onClick = [this] { showPage (0); }; coloursTab.onClick = [this] { showPage (1); }; updatesTab.onClick = [this] { showPage (2); };
        restore.setComponentID ("restore-preset-recovery"); restore.onClick = [this] { if (restoreRecovery) restoreRecovery(); };
        guide.setComponentID ("open-quick-guide"); guide.onClick = [this] { if (openGuide) openGuide(); };
        profiles.onChange = [this]
        {
            if (profiles.getText().isEmpty()) return;
            saved = ThemeProfiles::get().select (profiles.getText()); loadColours(); if (changed) changed();
        };
        profileName.setComponentID ("colour-profile-name"); profiles.setComponentID ("colour-profiles");
        saveProfile.setComponentID ("save-colour-profile");
        profileName.setTextToShowWhenEmpty ("My profile", Theme::dim()); profileName.setInputRestrictions (48);
        profileName.onTextChange = [this] { colourDraftDirty = true; };
        saveProfile.onClick = [this]
        {
            ThemeProfiles::Colours values;
            for (int i = 0; i < ThemeProfiles::count; ++i)
                if (! ThemeProfiles::parse (hex[(size_t) i].getText(), values[(size_t) i])) { colourMessage = txt ("Use six hexadecimal digits: #RRGGBB", u8"请输入六位十六进制颜色：#RRGGBB"); repaint(); return; }
            saved = ThemeProfiles::get().save (profileName.getText(), values);
            colourMessage = saved ? txt ("Profile saved on this computer.", u8"配色方案已保存在本机。") : txt ("Use a name other than Default; check folder permissions.", u8"请使用 Default 以外的名称，并检查写入权限。");
            loadColours(); if (changed) changed();
        };
        resetProfile.onClick = [this] { saved = ThemeProfiles::get().select ("Default"); loadColours(); if (changed) changed(); };
        checkUpdates.onClick = [this] { if (updates) updates->check(); };
        downloadUpdate.onClick = [this] { savedProjects.setToggleState (false, juce::dontSendNotification); if (updates) updates->download(); };
        installUpdate.onClick = [this]
        {
            if (updates && savedProjects.getToggleState())
            { if (! updates->schedule (true)) updateError = txt ("Could not schedule installation.", u8"无法安排安装。"); tick(); }
        };
        cancelUpdate.onClick = [this] { if (updates) updates->cancelScheduled(); tick(); };
        for (auto* c : std::initializer_list<juce::Component*> { &close, &generalTab, &coloursTab, &updatesTab, &language, &frequencyLimit, &uiScale, &randomPresetCount, &formulaWindow, &restore, &guide,
            &profiles, &profileName, &saveProfile, &resetProfile, &checkUpdates, &downloadUpdate, &installUpdate, &cancelUpdate, &savedProjects }) addAndMakeVisible (c);
        for (int i = 0; i < ThemeProfiles::count; ++i)
        {
            hex[(size_t) i].setComponentID ("colour-" + juce::String (i)); hex[(size_t) i].setInputRestrictions (7, "#0123456789abcdefABCDEF");
            hex[(size_t) i].onTextChange = [this, i] { colourDraftDirty = true; refreshColourPreview (i); };
            swatches[(size_t) i].setComponentID ("colour-preview-" + juce::String (i));
            swatches[(size_t) i].setTooltip (txt ("Click to choose a colour. HEX edits update this preview immediately.", u8"点击打开调色盘，输入 HEX 时实时预览。"));
            swatches[(size_t) i].onClick = [this, i] { openPalette (i); };
            addAndMakeVisible (hex[(size_t) i]); addAndMakeVisible (swatches[(size_t) i]);
        }
        refresh(); loadColours(); showPage (0);
    }
    ~SettingsPanel() override { dismissPalette(); }
    std::function<void()> changed, restoreRecovery, openGuide;
    std::function<bool()> hasRecovery;
    void setUpdateService (std::shared_ptr<scrr::updates::UpdateService> service) { updates = std::move (service); }
    void showPage (int value) { if (value != 1) dismissPalette(); page = value; refreshVisibility(); resized(); repaint(); }
    void visibilityChanged() override { if (! isShowing()) dismissPalette(); }
    void tick()
    {
        if (! randomPresetCount.isPopupActive()) randomPresetCount.setSelectedId (RandomPresetSettings::get(), juce::dontSendNotification);
        if (! formulaWindow.isPopupActive()) formulaWindow.setSelectedId ((int) FormulaWindowPreferences::get(), juce::dontSendNotification);
        if (loadedThemeRevision != ThemeProfiles::get().revision())
        {
            if (colourDraftDirty && isVisible() && page == 1)
            {
                loadedThemeRevision = ThemeProfiles::get().revision();
                colourMessage = txt ("Another instance changed the palette. Your unsaved edits remain; Save applies this draft.", u8"其他实例已更改配色。保留你的未保存编辑；点击保存将应用此草稿。");
            }
            else loadColours();
        }
        restore.setEnabled (hasRecovery && hasRecovery());
        if (updates)
        {
            auto state = updates->snapshot().state; using State = scrr::updates::UpdateService::State;
            checkUpdates.setEnabled (state != State::checking && state != State::downloading && state != State::scheduled);
           #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
            downloadUpdate.setEnabled (false);
           #else
            downloadUpdate.setEnabled (state == State::available);
           #endif
            savedProjects.setEnabled (state == State::ready);
            installUpdate.setEnabled (state == State::ready && savedProjects.getToggleState());
            cancelUpdate.setEnabled (state == State::scheduled);
        }
        if (isVisible()) repaint();
    }
    void refresh()
    {
        loadColours();
        language.setSelectedId (PromptLanguage::get() == PromptLanguage::Language::chinese ? 2 : 1, juce::dontSendNotification);
        uiScale.setSelectedId (UiScale::get(), juce::dontSendNotification);
        randomPresetCount.setSelectedId (RandomPresetSettings::get(), juce::dontSendNotification);
        formulaWindow.changeItemText (1, txt ("Native system window", u8"系统窗口"));
        formulaWindow.changeItemText (2, txt ("Inside plugin window", u8"插件内窗口"));
        formulaWindow.setSelectedId ((int) FormulaWindowPreferences::get(), juce::dontSendNotification);
        close.setButtonText (promptText ("CLOSE  x")); generalTab.setButtonText (txt ("GENERAL", u8"常规")); coloursTab.setButtonText (txt ("COLOURS", u8"配色")); updatesTab.setButtonText (txt ("UPDATES", u8"更新"));
        restore.setButtonText (txt ("RESTORE SHIFT RECOVERY", u8"恢复 Shift 切换前状态")); guide.setButtonText (txt ("OPEN QUICK GUIDE", u8"打开快速指南"));
        saveProfile.setButtonText (txt ("SAVE PROFILE", u8"保存配色")); resetProfile.setButtonText (txt ("RESET TO DEFAULT", u8"恢复默认"));
        checkUpdates.setButtonText (txt ("CHECK NOW", u8"检查更新")); downloadUpdate.setButtonText (txt ("DOWNLOAD & VERIFY", u8"下载并校验"));
        installUpdate.setButtonText (txt ("INSTALL AFTER HOSTS EXIT", u8"宿主退出后安装")); cancelUpdate.setButtonText (txt ("CANCEL UPDATE", u8"取消更新"));
        savedProjects.setButtonText (txt ("I confirm ALL affected projects are saved.", u8"我确认所有受影响的工程均已保存。")); tick();
    }
    void resized() override
    {
        close.setBounds (getWidth() - 110, 18, 90, 30);
        const int w = (getWidth() - 48) / 3;
        generalTab.setBounds (24, 66, w - 4, 32); coloursTab.setBounds (24 + w, 66, w - 4, 32); updatesTab.setBounds (24 + 2*w, 66, w - 4, 32);
        language.setBounds (24, 136, getWidth() - 48, 30); frequencyLimit.setBounds (24, 199, getWidth() - 48, 30); uiScale.setBounds (24, 262, getWidth() - 48, 30);
        randomPresetCount.setBounds (24, 325, getWidth() - 48, 30); formulaWindow.setBounds (24, 388, getWidth() - 48, 30);
        restore.setBounds (24, 477, getWidth() - 48, 30); guide.setBounds (24, 519, getWidth() - 48, 30);
        profiles.setBounds (24, 117, getWidth() - 48, 32); profileName.setBounds (24, 160, getWidth() - 48, 32);
        const int cw = (getWidth() - 60) / 2;
        for (int i = 0; i < ThemeProfiles::count; ++i)
        {
            const int x = 24 + (i%2)*(cw+12), y = 228 + (i/2)*53;
            hex[(size_t) i].setBounds (x, y, cw - 40, 28); swatches[(size_t) i].setBounds (x + cw - 34, y, 34, 28);
        }
        saveProfile.setBounds (24, 498, cw, 32); resetProfile.setBounds (36 + cw, 498, cw, 32);
        checkUpdates.setBounds (24, 260, getWidth()-48, 32); downloadUpdate.setBounds (24, 305, getWidth()-48, 32);
        savedProjects.setBounds (24, 360, getWidth()-48, 34); installUpdate.setBounds (24, 408, getWidth()-48, 36); cancelUpdate.setBounds (24, 456, getWidth()-48, 32);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (Theme::yellow()); g.drawRect (getLocalBounds(), 3);
        Theme::text (g, promptText ("SETTINGS"), {24, 15, getWidth()-150, 38}, 23, Theme::yellow(), true);
        auto label = [&] (const juce::String& text, int y) { Theme::text (g, text, {24,y,getWidth()-48,23}, 13, Theme::white(), true); };
        auto prose = [&] (const juce::String& text, int y, int h) { g.setColour (Theme::dim()); g.setFont (Theme::font (12)); g.drawFittedText (text, {24,y,getWidth()-48,h}, juce::Justification::topLeft, 8); };
        if (page == 0)
        {
            label (promptText ("PROMPT LANGUAGE"), 110); label (promptText ("SPECTRAL FREQUENCY LIMIT"), 173); label (promptText ("UI SCALE"), 236);
            label (txt ("RANDOM PRESET / EFFECT COUNT", u8"随机预设 / 效果数量"), 299);
            label (txt ("FORMULA / LARGE EDITOR WINDOW", u8"FORMULA / 大编辑窗口"), 362);
            prose (txt ("One recovery slot per instance, saved with your DAW project. Restore replaces the current edits and can be undone.", u8"每个实例保留一份恢复缓存，随 DAW 工程保存。恢复会替换当前编辑，可撤销。"), 432, 38);
            prose (txt ("Language, scale, colours and editor options are shared local preferences. Frequency limit belongs to the preset.", u8"语言、缩放、配色和编辑器选项为本机共享设置；频谱上限随预设保存。"), 563, 34);
        }
        else if (page == 1)
        {
            for (int i = 0; i < ThemeProfiles::count; ++i)
                Theme::text (g, ThemeProfiles::labels[i], {hex[(size_t)i].getX(),hex[(size_t)i].getY()-20,hex[(size_t)i].getWidth(),20}, 12, Theme::white());
            prose (colourMessage, 541, 45);
        }
        else
        {
            label ("Spectral Corruptor " JucePlugin_VersionString, 114);
            if (updates)
            {
                auto s = updates->snapshot(); using State = scrr::updates::UpdateService::State;
                juce::String description;
                switch (s.state)
                {
                    case State::idle: description=txt("Ready to check GitHub.",u8"准备检查 GitHub。"); break;
                    case State::checking: description=txt("Checking GitHub...",u8"正在检查 GitHub…"); break;
                    case State::current: description=txt("You are using the latest stable release.",u8"当前已是最新稳定版本。"); break;
                    case State::available: description=txt("Newer Version Available: ",u8"新版本可用：")+s.version;
                       #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
                        description += txt("\nSource edition: rebuild the newer source.", u8"\n源码版：请重新编译新版本源码。");
                       #endif
                        break;
                    case State::downloading: description=txt("Downloading and verifying: ",u8"正在下载并校验：")+juce::String((int)(s.progress*100))+"%"; break;
                    case State::ready: description=txt("Installer verified. Save all projects before continuing.",u8"安装器校验通过。请先保存所有工程。"); break;
                    case State::scheduled: description=txt("Waiting for audio hosts to exit. Close them yourself after saving. OS administrator approval may be required.",u8"等待音频宿主退出。请在保存后自行关闭；系统可能要求管理员授权。"); break;
                    case State::failed: description=txt("Update unavailable: ",u8"暂时无法更新：")+s.message; break;
                }
                prose (description, 153, 91);
            }
            prose (updateError.isNotEmpty()?updateError:txt("No installer runs without your saved-project confirmation. Cancel leaves the running version intact. If a host stays open, installation waits (up to 24 hours).",u8"未确认工程已保存时不会运行安装。取消不影响当前版本。宿主仍在运行时将继续等待，最长 24 小时。"),510,77);
        }
        if (!saved) prose(txt("Could not save preferences. Check folder permissions.",u8"无法保存设置，请检查文件夹权限。"),getHeight()-35,30);
    }
private:
    static juce::String txt (const char* en,const char* zh) { return juce::String::fromUTF8(PromptLanguage::get()==PromptLanguage::Language::chinese?zh:en); }
    void refreshColourPreview (int index)
    {
        juce::uint32 value {};
        const bool valid = ThemeProfiles::parse (hex[(size_t) index].getText(), value);
        const auto colour = valid ? juce::Colour (value) : swatches[(size_t) index].getPreview();
        swatches[(size_t) index].setPreview (colour, valid);
        if (valid && activePaletteSlot == index && paletteEditor) paletteEditor->setColour (colour);
    }
    void dismissPalette()
    {
        if (paletteCallout) paletteCallout->dismiss();
        paletteCallout = nullptr; paletteEditor = nullptr; activePaletteSlot = -1;
    }
    void openPalette (int index)
    {
        dismissPalette(); activePaletteSlot = index;
        auto picker = std::make_unique<ColourPaletteEditor> (swatches[(size_t) index].getPreview());
        paletteEditor = picker.get(); const auto safe = juce::Component::SafePointer<SettingsPanel> (this);
        const auto source = juce::Component::SafePointer<ColourPaletteEditor> (picker.get());
        picker->changed = [safe, source, index] (juce::Colour colour)
        {
            if (! safe || ! source || ! safe->isShowing() || safe->page != 1 || safe->activePaletteSlot != index
                || safe->paletteEditor.getComponent() != source.getComponent()) return;
            safe->hex[(size_t) index].setText ("#" + colour.toDisplayString (false).toUpperCase(), false);
            safe->colourDraftDirty = true; safe->refreshColourPreview (index);
        };
        auto& callout = juce::CallOutBox::launchAsynchronously (std::move (picker), swatches[(size_t) index].getBounds(), this);
        paletteCallout = &callout;
    }
    void loadColours()
    {
        auto& theme=ThemeProfiles::get(); profiles.clear(juce::dontSendNotification); profiles.addItemList(theme.names(),1); profiles.setText(theme.selected(),juce::dontSendNotification);
        profileName.setText(theme.selected()=="Default"?"My profile":theme.selected(),false);
        for(int i=0;i<ThemeProfiles::count;++i)
        { hex[(size_t)i].setText("#"+juce::String::toHexString((int)(theme.values()[(size_t)i]&0xffffffu)).paddedLeft('0',6).toUpperCase(),false); refreshColourPreview (i); }
        loadedThemeRevision = theme.revision(); colourDraftDirty = false;
    }
    void refreshVisibility()
    {
        for(auto* c:std::initializer_list<juce::Component*> {&language,&frequencyLimit,&uiScale,&randomPresetCount,&formulaWindow,&restore,&guide}) c->setVisible(page==0);
        for(auto* c:std::initializer_list<juce::Component*> {&profiles,&profileName,&saveProfile,&resetProfile}) c->setVisible(page==1);
        for(auto& c:hex)c.setVisible(page==1);
        for(auto& c:swatches)c.setVisible(page==1);
        for(auto* c:std::initializer_list<juce::Component*> {&checkUpdates,&downloadUpdate,&installUpdate,&cancelUpdate,&savedProjects})c->setVisible(page==2);
        generalTab.setToggleState(page==0,juce::dontSendNotification);coloursTab.setToggleState(page==1,juce::dontSendNotification);updatesTab.setToggleState(page==2,juce::dontSendNotification);
    }
    juce::File preference; juce::ComboBox language,frequencyLimit,uiScale,randomPresetCount,formulaWindow,profiles;
    juce::TextButton close,generalTab,coloursTab,updatesTab,restore,guide,saveProfile,resetProfile,checkUpdates,downloadUpdate,installUpdate,cancelUpdate;
    juce::ToggleButton savedProjects; juce::TextEditor profileName; std::array<juce::TextEditor,ThemeProfiles::count> hex;
    std::array<ColourPreviewButton, ThemeProfiles::count> swatches;
    juce::Component::SafePointer<juce::CallOutBox> paletteCallout;
    juce::Component::SafePointer<ColourPaletteEditor> paletteEditor;
    int activePaletteSlot { -1 };
    bool saved{true};int page{};juce::String colourMessage,updateError;
    uint64_t loadedThemeRevision {}; bool colourDraftDirty {};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> frequencyAttachment;
    std::shared_ptr<scrr::updates::UpdateService> updates;
};
}

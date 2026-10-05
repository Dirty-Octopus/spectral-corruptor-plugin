// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "EditorContent.h"
#include "../parameters/ParamIDs.h"
#include <cmath>
#include <map>

namespace scrr::gui {
EditorContent::EditorContent (SpectralCrrptProcessor& p)
    : processor (p), presets (p), licensePanel (p.getLicense()), settingsPanel (PromptLanguage::preferenceFile(), &p.getAPVTS()), macroBar (p), macroPanel (p), modulationPage (p), inputSpectrum (p, true), outputSpectrum (p, false)
{
    setLookAndFeel (&look); setOpaque (true);
    addAndMakeVisible (presets); addAndMakeVisible (inputSpectrum); addAndMakeVisible (outputSpectrum);
    licenseButton.setComponentID ("license-status");
    licenseButton.onClick = [this] { settingsPanel.setVisible (false); cancelDrag(); browser.setVisible (false); macroPanel.setVisible (false); licensePanel.setVisible (true); licensePanel.toFront (true); };
    licensePanel.changed = [this] { updateLicenseStatus(); };
    addAndMakeVisible (licenseButton); addChildComponent (licensePanel); updateLicenseStatus();
    settingsButton.setComponentID ("settings-button");
    settingsButton.onClick = [this]
    {
        cancelDrag(); browser.setVisible (false); macroPanel.setVisible (false); licensePanel.setVisible (false);
        settingsPanel.refresh(); settingsPanel.setVisible (! settingsPanel.isVisible()); settingsPanel.toFront (true);
    };
    settingsPanel.changed = [this] { refreshLanguage(); };
    addAndMakeVisible (settingsButton); addChildComponent (settingsPanel);
    addAndMakeVisible (macroBar); addChildComponent (macroPanel);
    macroBar.openMappings = [this] (int index) { settingsPanel.setVisible (false); cancelDrag(); browser.setVisible (false); licensePanel.setVisible (false); macroPanel.showMacro (index); };
    setupSlider (dryWet, 0, 100, .1, 100); dryWet.setTextValueSuffix (" %");
    setupSlider (inputGain, -48, 12, .1, 0); inputGain.setTextValueSuffix (" dB");
    setupSlider (outputGain, -48, 12, .1, 0); outputGain.setTextValueSuffix (" dB");
    for (auto* gain : { &inputGain, &outputGain })
    {
        gain->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        gain->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 22);
        gain->setLookAndFeel (&gainLook);
        gain->setColour (juce::Slider::rotarySliderFillColourId, Theme::black());
    }
    inputGain.setComponentID ("input-gain"); outputGain.setComponentID ("output-gain");
    setupSlider (splitCount, 1, 64, 1, 1); setupSlider (crossfade, 0, 100, .1, 0); crossfade.setTextValueSuffix (" %");
    for (auto* s : { &dryWet, &inputGain, &outputGain, &splitCount, &crossfade })
    {
        s->setColour (juce::Slider::trackColourId, Theme::black());
        s->setColour (juce::Slider::thumbColourId, Theme::black());
        s->setColour (juce::Slider::backgroundColourId, Theme::black().withAlpha (.25f));
        masterContent.addAndMakeVisible (s);
    }
    fftSize.addItemList (scrr::params::choice::fftSizeExtended, 1); oversample.addItemList (scrr::params::choice::oversample, 1);
    window.addItemList (scrr::params::choice::windowType, 1);
    for (auto* c : { &fftSize, &oversample, &window }) masterContent.addAndMakeVisible (c);
    masterViewport.setViewedComponent (&masterContent, false); masterViewport.setScrollBarsShown (true, false); addAndMakeVisible (masterViewport);
    for (auto* b : { &bypass, &solo, &post }) addAndMakeVisible (b);
    auto& state = processor.getAPVTS();
    auto attachSlider = [&] (const juce::String& id, juce::Slider& control) { sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, id, control)); setupParameterGestures (control); };
    auto attachButton = [&] (const juce::String& id, juce::ToggleButton& control) { buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, id, control)); };
    auto attachCombo = [&] (const juce::String& id, juce::ComboBox& control) { comboAttachments.push_back (std::make_unique<ComboAttachment> (state, id, control)); };
    using namespace scrr::params;
    attachSlider (id::drywet, dryWet); attachSlider (id::inputGain, inputGain); attachSlider (id::outputGain, outputGain);
    attachSlider (id::freqSplitNumSplits, splitCount); attachSlider (id::freqSplitCrossfade, crossfade);
    const std::pair<AssignableSlider*, juce::String> globalTargets[] {
        { &dryWet, id::drywet }, { &inputGain, id::inputGain }, { &outputGain, id::outputGain },
        { &splitCount, id::freqSplitNumSplits }, { &crossfade, id::freqSplitCrossfade } };
    for (auto target : globalTargets)
    {
        target.first->mappingMenu = [this, target] { macroMenu (*target.first, 0, {}, juce::Identifier (target.second)); };
        target.first->changeMapping = [this] (auto mapping) { processor.updateMacroMapping (mapping); };
        target.first->unmapRoute = [this, target] (auto mapping) { processor.removeMacroMapping (0, {}, juce::Identifier (target.second), mapping.macro, mapping.sourceUid); };
    }
    attachButton (id::bypass, bypass); attachButton (id::freqSplitSolo, solo); attachButton (id::freqSplitPostPreview, post);
    attachCombo (id::fftSizeExtended, fftSize); attachCombo (id::oversample, oversample); attachCombo (id::windowType, window);
    fftSize.setComponentID ("fft-size");
    fftSize.setTooltip ("128-32768 samples. Legacy automation uses the original FFT Size host parameter (1024-8192). Larger sizes add latency.");
    oversample.setTooltip ("Actual oversampling multiplier. FFT size scales with it to preserve resolution.");
    splitCount.setTooltip ("Logarithmic frequency slices, distributed across Ch 1-4. With 1 split only Ch 1 receives the full spectrum.");
    crossfade.setTooltip ("Blend adjacent frequency slices.");
    solo.setTooltip ("Audition the selected chain. Post Split auditions only its assigned bands.");
    dryWet.setTooltip ("Master effect blend; the dry signal is latency compensated.");
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText ("CH " + juce::String (i + 1)); tabs[i].onClick = [this, i] { selectChannel (i + 1); };
        tabs[i].toggleEnabled = [this, i]
        {
            auto* parameter = processor.getAPVTS().getParameter (scrr::params::id::channelEnabled[(size_t) i]);
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost (parameter->getValue() > .5f ? 0.0f : 1.0f); parameter->endChangeGesture();
        };
        tabs[i].setTooltip ("Click the dot to mute this band. Drop a stage here to move it; Cmd/Ctrl-drag duplicates.");
        tabs[i].setComponentID ("channel-" + juce::String (i + 1));
        addAndMakeVisible (tabs[i]);
    }
    rack.addMouseListener (this, false);
    rackViewport.setViewedComponent (&rack, false); rackViewport.setScrollBarsShown (true, false); addAndMakeVisible (rackViewport);
    inspectorViewport.setViewedComponent (&inspector, false); inspectorViewport.setScrollBarsShown (true, false); addAndMakeVisible (inspectorViewport);
    add.onClick = [this] { addMenu(); }; addAndMakeVisible (add);
    add.setColour (juce::TextButton::buttonColourId, Theme::blue());
    add.setTooltip ("Browse effect folders. Drag a chain stage to reorder; drag into the centre target to remove.");
    addAndMakeVisible (card); addChildComponent (browser);
    browser.onChoose = [this] (const juce::String& type)
    {
        processor.addModule (channel, type);
        auto chain = processor.getModuleChain (channel);
        for (auto child : chain) if (child["type"].toString() == type) selectedUid = child["uid"].toString();
        rebuildRack();
    };
    enabled.onClick = [this]
    {
        processor.setModuleParameter (channel, selectedUid, "enabled", enabled.getToggleState());
        juce::Component::SafePointer<EditorContent> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe) safe->rebuildRack(); });
    };
    addAndMakeVisible (enabled);
    notesPanel.changed = [this] (const juce::String& text) { processor.setModuleParameter (channel, selectedUid, "notes", text); };
    addChildComponent (notesPanel);
    orderNotice.setComponentID ("chain-order-notice");
    orderNotice.setText (promptText ("GENERIC EFFECTS MUST STAY AT THE END OF THE CHAIN"), juce::dontSendNotification);
    orderNotice.setFont (Theme::font (14, true)); orderNotice.setJustificationType (juce::Justification::centred);
    orderNotice.setColour (juce::Label::backgroundColourId, Theme::black()); orderNotice.setColour (juce::Label::textColourId, Theme::yellow());
    orderNotice.setColour (juce::Label::outlineColourId, Theme::yellow()); orderNotice.setInterceptsMouseClicks (false, false); addChildComponent (orderNotice);
    masterContent.draw = [this] (juce::Graphics& g)
    {
    auto label = [&] (juce::String text, const juce::Component& control, const juce::String& parameter = {})
    {
        for (const auto& mapping : displayedMappings)
            if (mapping.channel == 0 && mapping.parameter.toString() == parameter)
                text += " / " + mapping.sourceLabel();
        Theme::text (g, text, { control.getX(), control.getY() - 19, control.getWidth(), 17 }, 11, Theme::black(), true);
    };
    label ("DRY / WET", dryWet, scrr::params::id::drywet); label ("INPUT", inputGain, scrr::params::id::inputGain); label ("OUTPUT", outputGain, scrr::params::id::outputGain);
    label ("FFT SIZE", fftSize); label ("OVERSAMPLE", oversample); label ("FREQUENCY SLICES", splitCount, scrr::params::id::freqSplitNumSplits); label ("BAND CROSSFADE", crossfade, scrr::params::id::freqSplitCrossfade);
    };
    modulationButton.setComponentID ("modulation-tab");
    modulationButton.setColour (juce::TextButton::buttonColourId, Theme::blue());
    modulationButton.onClick = [this]
    {
        cancelDrag(); browser.setVisible (false);
        if (modulationPage.isVisible()) modulationPage.setVisible (false); else modulationPage.open();
        modulationButton.setToggleState (modulationPage.isVisible(), juce::dontSendNotification);
    };
    modulationPage.closed = [this] { modulationButton.setToggleState (false, juce::dontSendNotification); };
    modulationPage.mappingMenu = [this] (auto& target, const auto& uid, const auto& parameter)
    { macroMenu (target, scrr::params::MacroMapping::modulationChannel, uid, parameter); };
    modulationPage.openMappings = [this] (const juce::String& uid) { macroPanel.showSource (uid); };
    addAndMakeVisible (modulationButton); addChildComponent (modulationPage);
    setWantsKeyboardFocus (true);
    setSize (1180, 820); updateChannelColours(); selectChannel (1); startTimerHz (30);
}
EditorContent::~EditorContent()
{
    stopTimer(); juce::PopupMenu::dismissAllActiveMenus();
    rackViewport.setViewedComponent (nullptr, false); inspectorViewport.setViewedComponent (nullptr, false); masterViewport.setViewedComponent (nullptr, false);
    inputGain.setLookAndFeel (nullptr); outputGain.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}
void EditorContent::timerCallback()
{
    if (revision != processor.getStructureRevision()) { cancelDrag(); rebuildRack(); }
    if (shownLanguage != PromptLanguage::get()) refreshLanguage();
    if (std::abs (shownFrequencyLimit - processor.getFrequencyLimit()) > .5f)
    {
        displayedChain = processor.getModuleChain (channel);
        displayedMappings = processor.getMacroMappings();
        rebuildInspector(); resized();
        if (macroPanel.isVisible()) macroPanel.refresh();
    }
    if (! isShowing()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (orderNotice.isVisible() && now >= orderNoticeUntil) orderNotice.setVisible (false);
    const double elapsed = previousTick > 0 ? juce::jmax (0.0, (now - previousTick) / 1000) : 1.0 / 30.0;
    const float dt = (float) juce::jmin (.1, elapsed);
    previousTick = now; ambient += dt;
    const auto ease = [] (float t) { return 1 - std::pow (1 - juce::jlimit (0.0f, 1.0f, t), 3.0f); };
    pageProgress = juce::jlimit (0.0f, 1.0f, (float) ((now - pageStart) / 280));
    const float arrival = ease ((pageProgress - .30f) / .70f);
    card.setAlpha (arrival); inspectorViewport.setAlpha (arrival); enabled.setAlpha (arrival);
    updateCardParameters();
    card.animate ((float) std::floor (ambient * 8) / 8, arrival);
    if (pageProgress >= 1) departingPage = {};
    if (rackProgress < 1)
    {
        rackProgress = juce::jlimit (0.0f, 1.0f, (float) ((now - rackStart) / 220));
        const float t = ease (rackProgress);
        for (size_t i = 0; i < rows.size() && i < rowMotion.size(); ++i)
        {
            const auto& m = rowMotion[i];
            rows[i]->setBounds (m.to.withPosition ((int) std::round ((float) m.from.getX() + t * (float) (m.to.getX() - m.from.getX())),
                                                  (int) std::round ((float) m.from.getY() + t * (float) (m.to.getY() - m.from.getY()))));
            rows[i]->setAlpha (rows[i]->uid == draggingUid ? .25f : .45f + .55f * t);
        }
        if (rackProgress >= 1) retiringRow = {};
    }
    for (auto& row : rows) row->tick (dt);
    processor.readHostBeat (displayedBeat);
    const float beatPhase = indicatorClock.tick (elapsed, displayedBeat);
    const int activeSlices = (int) processor.getEffectiveValue (0, {}, scrr::params::id::freqSplitNumSplits, splitCount.getValue());
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setActivity (processor.getAPVTS().getRawParameterValue (scrr::params::id::channelEnabled[(size_t) i])->load() > .5f,
                             i < activeSlices);
        tabs[i].setBeatPhase (beatPhase); tabs[i].tick (dt);
    }
    if (draggingUid.isNotEmpty() && rackViewport.getBounds().contains (dragPoint))
    {
        const int edge = dragPoint.y - rackViewport.getY();
        const int delta = edge < 24 ? -9 : edge > rackViewport.getHeight() - 24 ? 9 : 0;
        if (delta != 0)
        {
            rackViewport.setViewPosition (0, rackViewport.getViewPositionY() + delta);
            dragStage (draggingUid, localPointToGlobal (dragPoint), false, copyDrag);
        }
    }
    macroBar.refreshNames(); macroBar.repaint(); macroPanel.repaint(); browser.repaint(); modulationPage.tick(); modulationButton.repaint(); settingsButton.repaint();
    refreshMacroControls();
    updateChannelColours();
    updateLicenseStatus();
    const auto legacySize = juce::String (1024 << (int) processor.getAPVTS().getRawParameterValue (scrr::params::id::fftSize)->load()) + " / L";
    const bool legacySelected = fftSize.getSelectedId() == 1;
    fftSize.changeItemText (1, legacySize);
    if (legacySelected && fftSize.getText() != legacySize)
        fftSize.setSelectedId (1, juce::dontSendNotification);
    repaint (detailArea.getUnion (rackArea));
    repaint (masterArea);
}
void EditorContent::updateLicenseStatus()
{
    const bool licensed = processor.getLicense().isActivated();
    const juce::String label = licensed ? "ACTIVATED" : "ACTIVATE";
    licenseButton.setTooltip (promptText (licensed ? "Activated on this machine" : "Not activated: only pink noise is output; input is blocked."));
    if (licenseButton.getButtonText() == label) return;
    licenseButton.setButtonText (label);
    licenseButton.setColour (juce::TextButton::buttonColourId, licensed ? Theme::panel() : Theme::red());

    licensePanel.repaint(); repaint();
}
void EditorContent::refreshLanguage()
{
    shownLanguage = PromptLanguage::get(); settingsPanel.refresh(); licensePanel.refreshLanguage();
    orderNotice.setText (promptText ("GENERIC EFFECTS MUST STAY AT THE END OF THE CHAIN"), juce::dontSendNotification);
    updateLicenseStatus(); repaint();
}
void EditorContent::updateCardParameters()
{
    if (selected < 0) return;
    const auto type = displayedChain.getChild (selected)["type"].toString();
    if (type != "Reverb" && type != "Delay" && type != "Compressor" && type != "Distortion") return;
    const auto chain = processor.getModuleChain (channel);
    for (auto child : chain)
        if (child["uid"].toString() == selectedUid)
        {
            if (const auto* spec = scrr::dsp::findModuleSpec (type))
                for (const auto& param : spec->params)
                {
                    const juce::Identifier key (param.id);
                    const auto value = processor.getEffectiveValue (channel, selectedUid, key, param.fromTree (child));
                    child.setProperty (key, std::isfinite (value) ? juce::jlimit ((double) param.minVal, (double) param.maxVal, value) : param.defaultVal, nullptr);
                }
            card.setParameterPreview (child); break;
        }
}
void EditorContent::updateChannelColours()
{
    const int slices = (int) splitCount.getValue();
    if (shownSplits == slices) return;
    shownSplits = slices; inputSpectrum.repaint(); outputSpectrum.repaint();
    for (int i = 0; i < 4; ++i)
    {
        const bool assigned = slices > 1 && i < slices;
        tabs[i].setChannel (i, assigned);
    }
}
void EditorContent::refreshMacroControls()
{
    const auto values = processor.getModulationValues();
    displayedMappings = processor.getMacroMappings();
    auto refresh = [&] (AssignableSlider& slider, int targetChannel, const juce::String& uid, const juce::Identifier& parameter)
    {
        std::vector<scrr::params::MacroMapping> mappings;
        for (const auto& m : displayedMappings) if (m.channel == targetChannel && m.uid == uid && m.parameter == parameter) mappings.push_back (m);
        slider.setMappings (mappings);
        if (! mappings.empty() && ! slider.isMouseButtonDown()) slider.setValue (mappings[0].centre, juce::dontSendNotification);
        slider.refreshModulation (values);
    };
    for (auto* group : { &common, &controls }) for (auto& control : *group)
        if (auto* slider = control->getAssignableSlider()) refresh (*slider, channel, selectedUid, juce::Identifier (control->getComponentID()));
    using namespace scrr::params;
    const std::pair<AssignableSlider*, juce::String> globals[] {
        { &dryWet, id::drywet }, { &inputGain, id::inputGain }, { &outputGain, id::outputGain },
        { &splitCount, id::freqSplitNumSplits }, { &crossfade, id::freqSplitCrossfade } };
    bool layout = false;
    for (const auto& target : globals)
    {
        const int previous = target.first->modulationHeight();
        refresh (*target.first, 0, {}, juce::Identifier (target.second));
        layout |= previous != target.first->modulationHeight();
    }
    if (layout) resized();
    updateRange();
}
void EditorContent::macroMenu (juce::Component& target, int targetChannel, const juce::String& uid, const juce::Identifier& parameter)
{
    displayedMappings = processor.getMacroMappings();
    int assigned = -1; std::array<bool, 8> assignedMacros {};
    for (const auto& m : displayedMappings) if (m.sourceUid.isEmpty() && m.channel == targetChannel && m.uid == uid && m.parameter == parameter) { assigned = m.macro; assignedMacros[(size_t) m.macro] = true; }
    juce::PopupMenu menu; menu.setLookAndFeel (&look); menu.addSectionHeader ("SEND TO MACRO");
    for (int i = 0; i < 8; ++i) menu.addItem (i + 1, processor.getMacroLabel (i), true, assignedMacros[(size_t) i]);
    const auto sources = targetChannel == scrr::params::MacroMapping::modulationChannel ? std::vector<scrr::params::ModulationSource> {} : processor.getModulators();
    std::vector<bool> sourceAssigned (sources.size(), false);
    juce::PopupMenu lfos, envelopes, randoms;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        for (const auto& m : displayedMappings) if (m.sourceUid == sources[i].uid && m.channel == targetChannel && m.uid == uid && m.parameter == parameter) sourceAssigned[i] = true;
        (sources[i].envelope ? envelopes : sources[i].random ? randoms : lfos).addItem (100 + (int) i, sources[i].name, true, sourceAssigned[i]);
    }
    if (lfos.getNumItems() > 0) menu.addSubMenu ("SEND TO LFO", lfos);
    if (envelopes.getNumItems() > 0) menu.addSubMenu ("SEND TO ENVELOPE", envelopes);
    if (randoms.getNumItems() > 0) menu.addSubMenu ("SEND TO RANDOM", randoms);
    menu.addSeparator(); menu.addItem (10, "REMOVE ALL MODULATION", assigned >= 0 || std::any_of (sourceAssigned.begin(), sourceAssigned.end(), [] (bool value) { return value; }));
    menu.addItem (11, "EDIT MAPPING", assigned >= 0);
    const auto safe = juce::Component::SafePointer<EditorContent> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, targetChannel, uid, parameter, assigned, assignedMacros, sources, sourceAssigned] (int result)
    {
        if (! safe) return;
        if (result >= 1 && result <= 8)
        {
            if (assignedMacros[(size_t) result - 1]) safe->processor.removeMacroMapping (targetChannel, uid, parameter, result - 1);
            else safe->processor.assignMacro (result - 1, targetChannel, uid, parameter);
        }
        if (result >= 100 && result < 100 + (int) sources.size())
        {
            const auto index = (size_t) (result - 100);
            if (sourceAssigned[index]) safe->processor.removeMacroMapping (targetChannel, uid, parameter, -1, sources[index].uid);
            else safe->processor.assignModulator (sources[index].uid, targetChannel, uid, parameter);
        }
        if (result == 10) safe->processor.removeMacroMapping (targetChannel, uid, parameter);
        if (result == 11 && assigned >= 0) safe->macroPanel.showMacro (assigned);
    });
}
void EditorContent::selectChannel (int nextChannel)
{
    cancelDrag(); browser.setVisible (false); modulationPage.setVisible (false); modulationButton.setToggleState (false, juce::dontSendNotification);
    channel = nextChannel; selectedUid.clear(); selected = -1;
    processor.getProcessor().setSoloChannel (channel - 1);
    for (int i = 0; i < 4; ++i) tabs[i].setToggleState (i == channel - 1, juce::dontSendNotification);
    rebuildRack();
}
void EditorContent::rebuildRack()
{
    displayedMappings = processor.getMacroMappings(); macroBar.refresh();
    if (macroPanel.isVisible()) macroPanel.refresh();
    // Save only GUI pixels for an interrupted transition. No processor/tree is
    // retained by the departing page, so host state can change at any point.
    const auto nextChain = processor.getModuleChain (channel);
    int nextSelected = -1;
    for (int i = 0; i < nextChain.getNumChildren(); ++i)
        if (nextChain.getChild (i)["uid"].toString() == selectedUid) nextSelected = i;
    if (nextSelected < 0 && nextChain.getNumChildren() > 0) nextSelected = 0;
    const auto nextUid = nextSelected >= 0 ? nextChain.getChild (nextSelected)["uid"].toString() : juce::String();
    const bool changePage = inspectorUid != nextUid;
    if (changePage && isShowing() && ! detailArea.isEmpty()) departingPage = createComponentSnapshot (detailArea);
    std::map<juce::String, juce::Rectangle<int>> previous;
    for (const auto& row : rows)
    {
        previous[row->uid] = row->getBounds();
        bool survives = false;
        for (const auto& child : nextChain) if (child["uid"].toString() == row->uid) survives = true;
        if (! survives && isShowing())
        {
            retiringRow = row->createComponentSnapshot (row->getLocalBounds());
            retiredBounds = getLocalArea (row.get(), row->getLocalBounds());
        }
    }
    revision = processor.getStructureRevision(); displayedChain = nextChain;
    selected = nextSelected; selectedUid = nextUid;
    rows.clear(); rowMotion.clear();
    const auto safe = juce::Component::SafePointer<EditorContent> (this);
    for (int i = 0; i < displayedChain.getNumChildren(); ++i)
    {
        auto child = displayedChain.getChild (i); auto* spec = scrr::dsp::findModuleSpec (child["type"].toString());
        if (! spec) continue;
        const auto uid = child["uid"].toString();
        auto row = std::make_unique<ModuleRow> (spec->displayName, uid, i, (bool) child["enabled"], selected == i);
        row->generic = scrr::dsp::isGenericEffect (spec->typeId); row->notes = spec->typeId == "Notes";
        row->contextMenu = [safe, uid] (const juce::MouseEvent& event)
        { if (safe) safe->chainContextMenu (uid, event.getEventRelativeTo (safe.getComponent()).getPosition()); };
        row->choose = [safe, uid]
        {
            juce::MessageManager::callAsync ([safe, uid] { if (safe) { safe->selectedUid = uid; safe->rebuildRack(); } });
        };
        row->drag = [safe, uid] (juce::Point<int> point, bool finish, bool duplicate) { if (safe) safe->dragStage (uid, point, finish, duplicate); };
        rack.addAndMakeVisible (*row); rows.push_back (std::move (row));
    }
    rebuildInspector(); inspectorUid = selectedUid;
    layingOutMotion = true; resized(); layingOutMotion = false;
    for (auto& row : rows)
    {
        const auto to = row->getBounds(); const auto found = previous.find (row->uid);
        const auto from = found != previous.end() ? found->second : to.translated (-24, 12);
        rowMotion.push_back ({ from, to }); row->setBounds (from);
    }
    rackStart = juce::Time::getMillisecondCounterHiRes(); rackProgress = 0;
    if (changePage)
    {
        pageStart = rackStart; pageProgress = 0;
        card.setAlpha (0); inspectorViewport.setAlpha (0); enabled.setAlpha (0);
        inspectorViewport.setViewPosition (0, 0);
        if (selected >= 0)
        {
            const int top = selected * 56, bottom = top + 56;
            const int viewTop = rackViewport.getViewPositionY();
            if (top < viewTop) rackViewport.setViewPosition (0, top);
            else if (bottom > viewTop + rackViewport.getHeight()) rackViewport.setViewPosition (0, bottom - rackViewport.getHeight());
        }
    }
    repaint();
}
void EditorContent::rebuildInspector()
{
    shownFrequencyLimit = processor.getFrequencyLimit();
    controls.clear(); common.clear(); selectedName.clear(); selectedDescription.clear();
    const bool valid = selected >= 0;
    notesPanel.setVisible (false); inspectorViewport.setVisible (true);
    card.setVisible (valid);
    enabled.setVisible (valid);
    if (! valid) { updateRange(); return; }
    auto child = displayedChain.getChild (selected); auto type = child["type"].toString();
    auto* spec = scrr::dsp::findModuleSpec (type); if (! spec) return;
    selectedName = spec->displayName; selectedDescription = moduleDescription (type);
    if (type == "Notes")
    {
        card.setVisible (false); enabled.setVisible (false); inspectorViewport.setVisible (false);
        notesPanel.setDocument (selectedUid, child["notes"].toString()); notesPanel.setVisible (true); updateRange(); return;
    }
    enabled.setToggleState ((bool) child["enabled"], juce::dontSendNotification);
    const bool effectActive = (bool) child["enabled"];
    card.setEffect (type, selectedName, selected + 1, effectActive);
    updateCardParameters();
    card.setTooltip (selectedDescription);
    const auto safe = juce::Component::SafePointer<EditorContent> (this);
    const auto uid = selectedUid; const int targetChannel = channel;
    auto makeControl = [&] (const scrr::dsp::ParamSpec& param, bool shared)
    {
        const auto id = param.id;
        auto control = std::make_unique<ParameterControl> (param, child.getProperty (id, param.defaultVal),
            [safe, uid, targetChannel, id] (const juce::var& value)
            {
                if (! safe) return;
                safe->processor.setModuleParameter (targetChannel, uid, juce::Identifier (id), value);
                if (id == "track")
                    for (auto& c : safe->controls)
                        if (c->getComponentID() == "fundamental") { c->setEnabled (! (bool) value); c->setAlpha ((bool) value ? .4f : 1.0f); }
                if (id == "rangeLow" || id == "rangeHigh")
                {
                    safe->displayedChain = safe->processor.getModuleChain (targetChannel); safe->updateRange();
                }
            });
        control->setAccent (effectActive ? Theme::yellow() : Theme::inactive());
        if (auto* slider = control->getAssignableSlider())
        {
            const auto sliderSafe = juce::Component::SafePointer<AssignableSlider> (slider);
            slider->mappingMenu = [safe, sliderSafe, targetChannel, uid, id]
            { if (safe && sliderSafe) safe->macroMenu (*sliderSafe, targetChannel, uid, juce::Identifier (id)); };
            slider->changeMapping = [safe] (auto mapping) { if (safe) safe->processor.updateMacroMapping (mapping); };
            slider->unmapRoute = [safe, targetChannel, uid, id] (auto mapping) { if (safe) safe->processor.removeMacroMapping (targetChannel, uid, juce::Identifier (id), mapping.macro, mapping.sourceUid); };
            std::vector<scrr::params::MacroMapping> mappings;
            for (const auto& mapping : displayedMappings)
                if (mapping.channel == targetChannel && mapping.uid == uid && mapping.parameter == juce::Identifier (id)) mappings.push_back (mapping);
            control->setMacroMappings (mappings);
        }
        inspector.addAndMakeVisible (*control);
        (shared ? common : controls).push_back (std::move (control));
    };
    if (! scrr::dsp::isGenericEffect (type))
    {
        makeControl (scrr::dsp::makeFloat ("rangeLow", "LOW / Hz", 0, shownFrequencyLimit, 0, 1, " Hz"), true);
        makeControl (scrr::dsp::makeFloat ("rangeHigh", "HIGH / Hz", 0, shownFrequencyLimit, shownFrequencyLimit, 1, " Hz"), true);
        makeControl (scrr::dsp::makeFloat ("moduleMix", "STAGE MIX", 0, 100, 100, .1f, " %"), true);
    }
    for (const auto& param : spec->params) makeControl (param, false);
    if ((bool) child.getProperty ("track", false))
        for (auto& c : controls)
            if (c->getComponentID() == "fundamental") { c->setEnabled (false); c->setAlpha (.4f); }
    updateRange();
}
void EditorContent::updateRange()
{
    auto child = displayedChain.getChild (selected);
    if (child["type"].toString() == "Notes")
    { inputSpectrum.setRange (0, shownFrequencyLimit, false); outputSpectrum.setRange (0, shownFrequencyLimit, false); return; }
    const float low = (float) processor.getEffectiveValue (channel, selectedUid, "rangeLow", (double) child.getProperty ("rangeLow", 0.0));
    const float high = (float) processor.getEffectiveValue (channel, selectedUid, "rangeHigh", (double) child.getProperty ("rangeHigh", 96000.0));
    const bool active = (bool) child.getProperty ("enabled", true);
    inputSpectrum.setRange (low, high, active); outputSpectrum.setRange (low, high, active);
}
void EditorContent::addMenu() { cancelDrag(); browser.open(); }
void EditorContent::cancelDrag()
{
    for (auto& row : rows) row->setAlpha (1);
    draggingUid.clear(); dragImage = {}; insertion = -1; removeTarget = false; dropChannel = 0; copyDrag = false; invalidOrder = false;
    repaint();
}
bool EditorContent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && (licensePanel.isVisible() || macroPanel.isVisible() || settingsPanel.isVisible()))
    { licensePanel.setVisible (false); macroPanel.setVisible (false); settingsPanel.setVisible (false); return true; }
    if (key == juce::KeyPress::escapeKey && modulationPage.isVisible()) { modulationPage.setVisible (false); modulationButton.setToggleState (false, juce::dontSendNotification); return true; }
    if (key == juce::KeyPress::escapeKey && draggingUid.isNotEmpty()) { cancelledDragUid = draggingUid; cancelDrag(); return true; }
    return false;
}
void EditorContent::dragStage (const juce::String& uid, juce::Point<int> screen, bool finish, bool duplicateHeld)
{
    if (uid == cancelledDragUid)
    { if (finish) cancelledDragUid.clear(); return; }
    if (processor.getStructureRevision() != revision)
    { cancelDrag(); return; }
    if (draggingUid.isEmpty())
    {
        if (finish) return;
        draggingUid = uid; draggingChannel = channel; browser.setVisible (false);
        for (const auto& row : rows) if (row->uid == uid)
        { dragImage = row->createComponentSnapshot (row->getLocalBounds()); row->setAlpha (.25f); }
        grabKeyboardFocus();
    }
    dragPoint = getLocalPoint (nullptr, screen);
    const auto target = detailArea.withSizeKeepingCentre (juce::jmin (280, detailArea.getWidth() - 40), 126);
    copyDrag = duplicateHeld;
    dropChannel = 0;
    for (int i = 0; i < 4; ++i) if (tabs[i].getBounds().contains (dragPoint)) dropChannel = i + 1;
    removeTarget = target.contains (dragPoint) && ! copyDrag;
    insertion = -1; invalidOrder = false;
    if (rackViewport.getBounds().contains (dragPoint))
    {
        int boundary = 0;
        while (boundary < displayedChain.getNumChildren() && ! scrr::dsp::isGenericEffect (displayedChain.getChild (boundary)["type"].toString())) ++boundary;
        const auto moving = displayedChain.getChildWithProperty ("uid", uid);
        const bool generic = scrr::dsp::isGenericEffect (moving["type"].toString());
        const int requested = juce::jlimit (0, displayedChain.getNumChildren(), (dragPoint.y - rackViewport.getY() + rackViewport.getViewPositionY() + 28) / 56);
        invalidOrder = moving["type"].toString() != "Notes" && (generic ? requested < boundary : requested > boundary);
        if (invalidOrder)
        {
            orderNoticeUntil = juce::Time::getMillisecondCounterHiRes() + 2400;
            orderNotice.setVisible (true); orderNotice.toFront (false);
        }
        else { insertion = requested; orderNotice.setVisible (false); }
    }
    if (finish)
    {
        const bool erase = removeTarget, duplicate = copyDrag;
        const int slot = insertion, sourceChannel = draggingChannel, targetChannel = dropChannel > 0 ? dropChannel : channel;
        const bool validTarget = dropChannel > 0 || slot >= 0;
        const auto movingUid = draggingUid; cancelDrag();
        const auto safe = juce::Component::SafePointer<EditorContent> (this);
        juce::MessageManager::callAsync ([safe, movingUid, sourceChannel, targetChannel, slot, erase, duplicate, validTarget]
        {
            if (! safe) return;
            if (erase)
            {
                const auto chain = safe->processor.getModuleChain (sourceChannel);
                const auto module = chain.getChildWithProperty ("uid", movingUid);
                if (module.isValid()) safe->processor.removeModule (sourceChannel, chain.indexOf (module));
            }
            else if (validTarget)
            {
                const auto next = safe->processor.transferModule (sourceChannel, movingUid, targetChannel, slot, duplicate);
                if (next.isEmpty()) return;
                safe->channel = targetChannel; safe->selectedUid = next;
                safe->processor.getProcessor().setSoloChannel (targetChannel - 1);
                for (int i = 0; i < 4; ++i) safe->tabs[i].setToggleState (i == targetChannel - 1, juce::dontSendNotification);
            }
            else return;
            safe->rebuildRack();
        });
    }
    repaint();
}
void EditorContent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && (e.eventComponent == &rack || rackArea.contains (getLocalPoint (e.eventComponent, e.getPosition())))) chainContextMenu ({}, e.getEventRelativeTo (this).getPosition());
}
void EditorContent::chainContextMenu (const juce::String& uid, juce::Point<int> localPosition)
{
    const auto clipboard = juce::SystemClipboard::getTextFromClipboard();
    juce::PopupMenu menu; menu.setLookAndFeel (&look);
    menu.addItem (1, "COPY EFFECT", uid.isNotEmpty());
    menu.addItem (2, "PASTE EFFECT INTO CH " + juce::String (channel), clipboard.length() < 1024 * 1024 && clipboard.contains ("<SPECTRAL_EFFECT"));
    const auto safe = juce::Component::SafePointer<EditorContent> (this); const int targetChannel = channel;
    const auto screenPosition = localPointToGlobal (localPosition);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withParentComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [safe, uid, clipboard, targetChannel] (int result)
        {
            if (! safe) return;
            if (result == 1) juce::SystemClipboard::copyTextToClipboard (safe->processor.copyModule (targetChannel, uid));
            if (result == 2)
            {
                const auto next = safe->processor.pasteModule (targetChannel, clipboard);
                if (next.isNotEmpty() && safe->channel == targetChannel) { safe->selectedUid = next; safe->rebuildRack(); }
            }
        });
}
void EditorContent::resized()
{
    auto area = getLocalBounds().reduced (12);
    headerArea = area.removeFromTop (76); area.removeFromTop (8);
    settingsButton.setBounds (headerArea.withTrimmedLeft (headerArea.getWidth() - 84));
    auto presetRow = area.removeFromTop (34);
    licenseButton.setBounds (presetRow.removeFromRight (106)); presetRow.removeFromRight (8);
    presets.setBounds (presetRow); area.removeFromTop (10);
    auto spectra = area.removeFromTop (juce::jlimit (154, 196, (getHeight() - 100) / 4));
    const int half = (spectra.getWidth() - 8) / 2;
    inputSpectrum.setBounds (spectra.removeFromLeft (half)); spectra.removeFromLeft (8); outputSpectrum.setBounds (spectra);
    area.removeFromTop (8); macroBar.setBounds (area.removeFromTop (72)); area.removeFromTop (8);
    masterArea = area.removeFromLeft (212); area.removeFromLeft (10);
    auto main = area;
    auto routing = main.removeFromTop (36);
    modulationButton.setBounds (routing.removeFromRight (144).reduced (0, 2));
    for (auto& tab : tabs) tab.setBounds (routing.removeFromLeft (78).reduced (0, 2).withTrimmedRight (4));
    solo.setBounds (routing.removeFromLeft (76)); post.setBounds (routing.removeFromLeft (116));
    main.removeFromTop (8);
    rackArea = main.removeFromLeft (230); main.removeFromLeft (10); detailArea = main;
    auto chain = rackArea;
    chain.removeFromTop (33); add.setBounds (chain.removeFromTop (34)); chain.removeFromTop (26);
    rackViewport.setBounds (chain);
    const int rowWidth = juce::jmax (100, rackViewport.getWidth() - 10);
    if (! layingOutMotion) { rackProgress = 1; retiringRow = {}; }
    int y = 0;
    for (auto& row : rows) { row->setBounds (0, y, rowWidth, 56); y += 56; }
    rack.setSize (rowWidth, juce::jmax (y, chain.getHeight()));
    auto detail = detailArea.reduced (14, 0); moduleHeader = detail.removeFromTop (140);
    card.setBounds (moduleHeader); notesPanel.setBounds (detailArea.reduced (14, 10)); detail.removeFromTop (7);
    auto actions = detail.removeFromTop (25); enabled.setBounds (actions.removeFromLeft (115)); detail.removeFromTop (9);
    browser.setBounds (detailArea);
    orderNotice.setBounds (detailArea.reduced (14, 0).withTrimmedTop (10).withHeight (48));
    modulationPage.setBounds (rackArea.getUnion (detailArea));
    settingsPanel.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 48), 414));
    licensePanel.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (620, getWidth() - 48), 452));
    macroPanel.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (900, getWidth() - 48), 470));
    inspectorViewport.setBounds (detail); emptyArea = detailArea;
    const int width = juce::jmax (180, detail.getWidth() - 12);
    const int commonWidth = width / 3;
    int commonHeight = 0;
    for (const auto& c : common) commonHeight = juce::jmax (commonHeight, c->preferredHeight());
    for (size_t i = 0; i < common.size(); ++i) common[i]->setBounds ((int) i * commonWidth, 0, commonWidth - 8, commonHeight);
    int controlY = common.empty() ? 0 : commonHeight + 10;
    for (size_t i = 0; i < controls.size(); i += 2)
    {
        int height = controls[i]->preferredHeight();
        if (i + 1 < controls.size()) height = juce::jmax (height, controls[i + 1]->preferredHeight());
        controls[i]->setBounds (0, controlY, width / 2 - 12, height);
        if (i + 1 < controls.size()) controls[i + 1]->setBounds (width / 2, controlY, width / 2 - 12, height);
        controlY += height + 8;
    }
    inspector.setSize (width, juce::jmax (detail.getHeight(), controlY));
    masterViewport.setBounds (masterArea.reduced (10, 0).withTrimmedTop (32).withTrimmedBottom (42));
    const int mw = masterViewport.getWidth() - 12;
    int my = 18;
    auto place = [&] (AssignableSlider& slider) { const int height = 27 + slider.modulationHeight(); slider.setBounds (2, my, mw - 4, height); my += height + 21; };
    place (dryWet);
    const int gainModulationHeight = juce::jmax (inputGain.modulationHeight(), outputGain.modulationHeight());
    inputGain.setBounds (2, my, mw / 2 - 6, 72 + inputGain.modulationHeight());
    outputGain.setBounds (mw / 2 + 4, my, mw / 2 - 6, 72 + outputGain.modulationHeight());
    my += 93 + gainModulationHeight;
    const int fftWidth = mw * 3 / 5;
    fftSize.setBounds (2, my, fftWidth - 6, 28); oversample.setBounds (fftWidth + 4, my, mw - fftWidth - 6, 28); my += 34;
    window.setBounds (2, my, mw - 4, 26); my += 47;
    place (splitCount); place (crossfade);
    masterContent.setSize (mw, my - 10);
    bypass.setBounds (masterArea.getX() + 12, masterArea.getBottom() - 36, 115, 28);
}
void EditorContent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::black());
    g.setColour (Theme::red()); g.fillRect (headerArea);
    g.setColour (Theme::black()); g.setFont (Theme::displayFont (48));
    g.drawText ("SPECTRAL CORRUPTOR", headerArea.reduced (16, 0).withTrimmedRight (264).withHeight (55), juce::Justification::centredLeft);
    Theme::text (g, processor.getLicense().isActivated() ? "FREQUENCY DOMAIN  /  MODULAR SIGNAL PROCESSOR"
                                                       : "NOT ACTIVATED  /  PINK NOISE ONLY  /  INPUT BLOCKED",
                 headerArea.reduced (17, 0).withTrimmedTop (48), 11, Theme::black(), true);
    auto mark = juce::Rectangle<int> (headerArea.getRight() - 264, headerArea.getY(), 180, headerArea.getHeight());
    g.setColour (Theme::yellow()); g.fillRect (mark);
    Theme::text (g, "dir.oct.", mark.reduced (18, 0).withTrimmedBottom (17), 35, Theme::black(), true);
    Theme::text (g, "DIRTY OCTOPUS", mark.reduced (18, 0).withTrimmedTop (50), 10, Theme::black(), true);
    g.setColour (Theme::yellow()); g.fillRect (masterArea);
    Theme::text (g, "MASTER / ENGINE", masterArea.reduced (12, 0).withHeight (31), 14, Theme::black(), true);
    auto bypassBlock = bypass.getBounds().expanded (4, 0); g.setColour (Theme::black()); g.fillRect (bypassBlock);
    const double sr = processor.getProcessor().getSampleRate();
    const double latency = sr > 0 ? 1000.0 * processor.getDisplayLatency() / sr : 0;
    Theme::text (g, juce::String (latency, 1) + " ms", { masterArea.getRight() - 82, masterArea.getBottom() - 36, 70, 28 }, 12, Theme::black(), true, juce::Justification::centredRight);
    g.setColour (Theme::panel()); g.fillRect (detailArea);
    Theme::text (g, "CH " + juce::String (channel) + " / SIGNAL CHAIN / " + juce::String (rows.size()), rackArea.withHeight (26), 13, Theme::white(), true);
    Theme::text (g, "DRAG TO CH / CMD-CTRL: COPY", { rackArea.getX(), add.getBottom() + 3, rackArea.getWidth(), 19 }, 9, Theme::dim());
    if (selected >= 0 && selected < (int) rows.size())
    {
        const auto row = getLocalArea (rows[(size_t) selected].get(), rows[(size_t) selected]->getLocalBounds());
        const int y = row.getCentreY();
        // The selected tab grows out of the chain into the page's spine.
        g.setColour ((bool) displayedChain.getChild (selected)["enabled"] ? Theme::yellow() : Theme::inactive());
        if (y >= rackViewport.getY() && y < rackViewport.getBottom())
            g.fillRect (row.getRight(), y - 9, detailArea.getX() - row.getRight() + 3, 18);
        g.fillRect (detailArea.getX(), detailArea.getY(), 3, detailArea.getHeight());
        g.fillRect (detailArea.getX(), moduleHeader.getY(), 14, 3);
    }
    else
    {
        auto blank = emptyArea.reduced (26);
        Theme::text (g, "BUILD A SIGNAL CHAIN", blank.withY (blank.getY() + 30).withHeight (35), 23, Theme::yellow(), true);
        g.setColour (Theme::dim()); g.setFont (Theme::font (14));
        g.drawFittedText ("Add an effect, select its frequency range, then shape the sound.\n\nStart with Harmonic Match to explore pitched textures.", blank.withTrimmedTop (84), juce::Justification::topLeft, 6);
    }
}
void EditorContent::paintOverChildren (juce::Graphics& g)
{
    if (licensePanel.isVisible() || macroPanel.isVisible() || settingsPanel.isVisible() || modulationPage.isVisible()) return;
    if (orderNotice.isVisible()) g.excludeClipRegion (orderNotice.getBounds());
    if (! browser.isVisible() && ! departingPage.isNull() && pageProgress < .46f)
    {
        g.setOpacity (juce::jlimit (0.0f, 1.0f, 1 - pageProgress / .46f));
        g.drawImage (departingPage, detailArea.toFloat()); g.setOpacity (1);
    }
    if (! retiringRow.isNull() && rackProgress < 1)
    {
        g.saveState(); g.reduceClipRegion (rackViewport.getBounds());
        g.setOpacity ((1 - rackProgress) * .6f);
        g.drawImage (retiringRow, retiredBounds.translated ((int) (-rackProgress * 35), 0).toFloat()); g.restoreState();
    }
    if (draggingUid.isEmpty()) return;
    const auto target = detailArea.withSizeKeepingCentre (juce::jmin (280, detailArea.getWidth() - 40), 126);
    g.setColour (Theme::black().withAlpha (.78f)); g.fillRect (detailArea);
    g.setColour (removeTarget ? Theme::red() : Theme::panel()); g.fillRect (target);
    g.setColour (Theme::red()); g.drawRect (target, removeTarget ? 4 : 2);
    Theme::text (g, promptText (copyDrag ? "DROP ON CH TO COPY" : removeTarget ? "RELEASE TO REMOVE" : "DROP HERE TO REMOVE"), target.reduced (16).withTrimmedBottom (35), 20,
                 removeTarget ? Theme::black() : Theme::red(), true, juce::Justification::centred);
    Theme::text (g, promptText ("Outside a target: cancel   /   Esc: cancel"), target.reduced (12).withTrimmedTop (64), 11,
                 removeTarget ? Theme::black() : Theme::dim(), false, juce::Justification::centred);
    if (dropChannel > 0)
    {
        g.setColour (Theme::yellow()); g.drawRect (tabs[dropChannel - 1].getBounds().expanded (2), 3);
    }
    if (insertion >= 0)
    {
        const int y = juce::jlimit (rackViewport.getY() + 2, rackViewport.getBottom() - 3,
                                  rackViewport.getY() + insertion * 56 - rackViewport.getViewPositionY());
        g.setColour (Theme::yellow()); g.fillRect (rackViewport.getX(), y - 2, rackViewport.getWidth(), 4);
        juce::Path arrow; arrow.addTriangle ((float) rackViewport.getX(), (float) y - 7, (float) rackViewport.getX() + 10, (float) y, (float) rackViewport.getX(), (float) y + 7);
        g.fillPath (arrow);
    }
    if (! dragImage.isNull())
    {
        const int x = juce::jlimit (0, getWidth() - dragImage.getWidth(), removeTarget ? target.getCentreX() - dragImage.getWidth() / 2 : dragPoint.x + 18);
        const int y = juce::jlimit (0, getHeight() - dragImage.getHeight() - 20, removeTarget ? target.getBottom() + 10 : dragPoint.y - 20);
        g.setOpacity (.88f); g.drawImageAt (dragImage, x, y); g.setOpacity (1);
        g.setColour (removeTarget ? Theme::red() : Theme::yellow()); g.drawRect (x, y, dragImage.getWidth(), dragImage.getHeight(), 2);
        Theme::text (g, invalidOrder ? promptText ("ORDER NOT ALLOWED") : removeTarget ? promptText ("REMOVE") : dropChannel > 0 ? promptText (copyDrag ? "COPY TO CH " : "MOVE TO CH ") + juce::String (dropChannel) : promptText (insertion >= 0 ? (copyDrag ? "COPY HERE" : "INSERT HERE") : "NO DROP TARGET"), { x, y + dragImage.getHeight(), dragImage.getWidth(), 19 }, 10, Theme::yellow(), true);
    }
}
} // namespace scrr::gui

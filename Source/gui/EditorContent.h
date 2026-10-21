// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "ChannelTab.h"
#include "LicensePanel.h"
#include "SettingsPanel.h"
#include "MacroBar.h"
#include "MacroMappingPanel.h"
#include "ModulationPage.h"
#include "AssignableSlider.h"
#include "PresetBar.h"
#include "EffectCard.h"
#include "NotesPanel.h"
#include "QuickGuide.h"
#include "SpectralEqEditor.h"
#include "components/TransferCurveEditor.h"
#include "components/RemapFormulaEditor.h"
#include "EffectBrowser.h"
#include "ModuleRow.h"
#include "components/ParameterPanel.h"
#include "components/SpectrumDisplay.h"
#include "../PluginProcessor.h"

namespace scrr::gui {
class PaintedPanel : public juce::Component
{
public:
    std::function<void(juce::Graphics&)> draw;
    void paint (juce::Graphics& g) override { if (draw) draw (g); }
};
class EditorContent : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit EditorContent (SpectralCrrptProcessor&);
    ~EditorContent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void chainContextMenu (const juce::String& uid, juce::Point<int> localPosition);
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return licensePanel.isInterestedInFileDrag (files); }
    void filesDropped (const juce::StringArray& files, int x, int y) override { licensePanel.filesDropped (files, x, y); }
private:
    void timerCallback() override;
    void rebuildRack();
    void rebuildInspector();
    void addMenu();
    void selectChannel (int);
    void updateRange();
    void dragStage (const juce::String&, juce::Point<int> screen, bool finish, bool duplicate = false);
    void cancelDrag (bool gestureFinished = false);
    void beginPreview();
    void previewChannel (int);
    void endPreview (bool commit);
    void releaseDragRows();
    void dragModulationOutput (const juce::String&, juce::Point<int>, bool);
    void refreshTheme();
    void updateChannelColours();
    void updateLicenseStatus();
    void refreshLanguage();
    void updateCardParameters();
    void refreshMacroControls();
    void updateProcessingStatus();
    void updatePartialControls();
    void updateRemapAxes();
    void wireFormulaSliders();
    void macroMenu (juce::Component&, int channel, const juce::String& uid, const juce::Identifier& parameter);
    SpectralCrrptProcessor& processor;
    InstrumentLookAndFeel look;
    MacroKnobLook gainLook;
    juce::TooltipWindow tooltip { this, 550 };
    PresetBar presets;
    LicensePanel licensePanel;
    juce::TextButton licenseButton;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    HeaderSettingsButton settingsButton;
    SettingsPanel settingsPanel;
    QuickGuide quickGuide;
    SpectralEqEditor eqEditor;
    TransferCurveEditor remapEditor;
    RemapFormulaEditor remapFormula;
    juce::ComboBox remapInvalidPolicy;
    juce::TextButton remapEditMode { "EDIT" }, remapFormulaMode { "FORMULA" };
    juce::TextButton remapReset { "RESET MAP" }, remapReverse { "REVERSE MAP" };
    std::shared_ptr<scrr::updates::UpdateService> updates;
    juce::TextButton updateNotice;
    uint64_t shownThemeRevision {};
    bool guideConsidered {};
    int shownEqBand { -1 };
    std::vector<std::unique_ptr<ModuleRow>> retainedDragRows;
    juce::Component::SafePointer<ModuleRow> dragOrigin;
    bool stageGestureActive {};
    bool previewing {};
    int previewOriginChannel { 1 }, hoverChannel {};
    juce::String previewOriginUid, hoverModuleUid, draggingSource, cancelledSource;
    juce::Point<int> previewOriginRackScroll, previewOriginInspectorScroll;
    double hoverStarted {}, moduleHoverStarted {};
    juce::Component::SafePointer<AssignableSlider> sourceDropTarget;
    PromptLanguage::Language shownLanguage { PromptLanguage::get() };
    MacroBar macroBar;
    MacroMappingPanel macroPanel;
    ModulationPage modulationPage;
    juce::TextButton modulationButton { "MODULATION" };
    SpectrogramDisplay inputSpectrum, outputSpectrum;
    AssignableSlider dryWet, inputGain, outputGain, splitCount, crossfade, overlapFactor;
    juce::Label overlapActual, processingStatus;
    juce::ComboBox fftSize, oversample, window;
    juce::ToggleButton bypass { "BYPASS" }, solo { "SOLO" }, post { "POST SPLIT" };
    ChannelTab tabs[4];
    scrr::params::HostBeat displayedBeat;
    scrr::params::BeatIndicatorClock indicatorClock;
    juce::TextButton add { "+ ADD EFFECT" };
    juce::ToggleButton enabled { "ENABLED" };
    juce::Label orderNotice;
    EffectCard card;
    NotesPanel notesPanel;
    EffectBrowser browser;
    juce::Viewport rackViewport, inspectorViewport, masterViewport;
    PaintedPanel masterContent;
    juce::Rectangle<int> fftBackground;
    juce::Component rack, inspector;
    std::vector<std::unique_ptr<ModuleRow>> rows;
    std::vector<std::unique_ptr<ParameterControl>> controls;
    std::vector<std::unique_ptr<ParameterControl>> common;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    juce::ValueTree displayedChain;
    std::vector<scrr::params::MacroMapping> displayedMappings;
    juce::String selectedUid, selectedName, selectedDescription, inspectorUid;
    int channel { 1 }, selected { -1 };
    uint64_t revision {};
    juce::Rectangle<int> headerArea, masterArea, rackArea, detailArea, moduleHeader, emptyArea;
    struct RowMotion { juce::Rectangle<int> from, to; };
    std::vector<RowMotion> rowMotion;
    juce::Image departingPage, dragImage, retiringRow;
    juce::Rectangle<int> retiredBounds;
    double pageStart {}, rackStart {}, previousTick {};
    float pageProgress { 1 }, rackProgress { 1 }, ambient {};
    bool layingOutMotion {};
    juce::String draggingUid, cancelledDragUid;
    int draggingChannel {}, insertion { -1 };
    juce::Point<int> dragPoint;
    bool removeTarget {}, copyDrag {}, invalidOrder {};
    double orderNoticeUntil {};
    int dropChannel {};
    int shownSplits {};
    int shownEffectiveSplits { 1 }, shownActualFFT {}, shownActualHop {};
    bool shownPartialSoloBypass {};
    bool shownResourceLimited {};
    size_t shownResourceRequired {}, shownResourceAvailable {}, shownResourceBudget {};
    float shownFrequencyLimit {};
    double shownRemapLimit {};
    int shownUiScale { UiScale::get() };
};
} // namespace scrr::gui

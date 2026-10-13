// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PluginEditor.h"
#include "gui/EditorContent.h"

SpectralCrrptEditor::SpectralCrrptEditor (SpectralCrrptProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // The editor is a thin shell: all UI lives in EditorContent.
    content = std::make_unique<scrr::gui::EditorContent> (processorRef);
    addAndMakeVisible (*content);

    setSize (content->getWidth(), content->getHeight());
    setResizable (true, true);
    applyUiScale();
    startTimerHz (10);
}

SpectralCrrptEditor::~SpectralCrrptEditor() { stopTimer(); }

void SpectralCrrptEditor::timerCallback()
{
    if (std::abs (uiScale - scrr::gui::UiScale::get() * .01) > .001) applyUiScale();
}

void SpectralCrrptEditor::applyUiScale()
{
    const int logicalWidth = juce::roundToInt (getWidth() / uiScale);
    const int logicalHeight = juce::roundToInt (getHeight() / uiScale);
    uiScale = scrr::gui::UiScale::get() * .01;
    content->setTransform (juce::AffineTransform::scale ((float) uiScale));
    setResizeLimits (juce::roundToInt (1040 * uiScale), juce::roundToInt (780 * uiScale),
                     juce::roundToInt (1680 * uiScale), juce::roundToInt (1200 * uiScale));
    setSize (juce::roundToInt (logicalWidth * uiScale), juce::roundToInt (logicalHeight * uiScale));
    resized();
}

void SpectralCrrptEditor::paint (juce::Graphics& g)
{
    // Background handled by EditorContent; nothing here.
    juce::ignoreUnused (g);
}

void SpectralCrrptEditor::resized()
{
    // JUCE owns and positions the resize handle separately.
    if (content) content->setBounds (0, 0, juce::roundToInt (getWidth() / uiScale), juce::roundToInt (getHeight() / uiScale));
}

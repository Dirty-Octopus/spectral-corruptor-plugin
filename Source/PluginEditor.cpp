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
    setResizeLimits (1040, 780, 1680, 1200);
}

SpectralCrrptEditor::~SpectralCrrptEditor() = default;

void SpectralCrrptEditor::paint (juce::Graphics& g)
{
    // Background handled by EditorContent; nothing here.
    juce::ignoreUnused (g);
}

void SpectralCrrptEditor::resized()
{
    // JUCE owns and positions the resize handle separately.
    if (content) content->setBounds (getLocalBounds());
}

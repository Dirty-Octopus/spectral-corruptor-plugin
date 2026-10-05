// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"

namespace scrr::gui {
// Markdown is rendered natively. No web view or external resource loading.
juce::AttributedString renderNotesMarkdown (const juce::String& markdown);

class NotesPanel : public juce::Component
{
    class Preview : public juce::Component
    {
    public:
        juce::AttributedString text;
        void layout (int width)
        {
            lines.createLayout (text, (float) juce::jmax (20, width - 32));
            setSize (width, juce::jmax (40, (int) std::ceil (lines.getHeight()) + 32)); repaint();
        }
        void paint (juce::Graphics& g) override
        { g.fillAll (Theme::field()); lines.draw (g, getLocalBounds().toFloat().reduced (16)); }
    private:
        juce::TextLayout lines;
    } preview;
public:
    NotesPanel();
    ~NotesPanel() override { viewport.setViewedComponent (nullptr, false); }
    void setDocument (const juce::String& uid, const juce::String& markdown);
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void(const juce::String&)> changed;
private:
    void showEditor (bool editing);
    juce::String documentUid;
    juce::TextEditor editor;
    juce::TextButton edit { "EDIT" }, read { "PREVIEW" };
    juce::Viewport viewport;
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "NotesPanel.h"
#include "../../third_party/md4c/md4c.h"
#include <vector>

namespace scrr::gui {
namespace {
struct MarkdownRenderer
{
    struct List { bool ordered; unsigned next; };
    juce::AttributedString result;
    std::vector<List> lists;
    int heading {}, bold {}, italic {}, code {}, link {}, quote {};

    void append (const juce::String& text)
    {
        static const float sizes[] { 14, 26, 22, 19, 17, 15, 14 };
        auto font = code > 0 ? juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13, juce::Font::plain))
                             : Theme::font (sizes[heading], bold > 0 || heading > 0);
        font.setItalic (italic > 0); font.setUnderline (link > 0);
        result.append (text, font, link > 0 ? Theme::yellow() : quote > 0 ? Theme::dim() : Theme::white());
    }
    void newline (bool gap = false)
    {
        if (result.getText().isEmpty()) return;
        if (! result.getText().endsWithChar ('\n')) append ("\n");
        if (gap && ! result.getText().endsWith ("\n\n")) append ("\n");
    }
    static int enterBlock (MD_BLOCKTYPE type, void* detail, void* context)
    {
        auto& r = *static_cast<MarkdownRenderer*> (context);
        switch (type)
        {
            case MD_BLOCK_H: r.newline (true); r.heading = (int) static_cast<MD_BLOCK_H_DETAIL*> (detail)->level; break;
            case MD_BLOCK_CODE: r.newline (true); ++r.code; break;
            case MD_BLOCK_QUOTE: r.newline (true); ++r.quote; r.append (juce::String::fromUTF8 ("│ ")); break;
            case MD_BLOCK_UL: r.newline(); r.lists.push_back ({ false, 0 }); break;
            case MD_BLOCK_OL: r.newline(); r.lists.push_back ({ true, static_cast<MD_BLOCK_OL_DETAIL*> (detail)->start }); break;
            case MD_BLOCK_LI:
            {
                r.newline(); r.append (juce::String::repeatedString ("  ", juce::jmax (0, (int) r.lists.size() - 1)));
                const auto* item = static_cast<MD_BLOCK_LI_DETAIL*> (detail);
                if (item->is_task) r.append (juce::String::fromUTF8 (item->task_mark == ' ' ? "☐ " : "☑ "));
                else if (! r.lists.empty() && r.lists.back().ordered) r.append (juce::String (r.lists.back().next++) + ". ");
                else r.append (juce::String::fromUTF8 ("• "));
                break;
            }
            case MD_BLOCK_HR: r.newline (true); r.append (juce::String::fromUTF8 ("────────────────────")); r.newline (true); break;
            case MD_BLOCK_DOC: case MD_BLOCK_P: case MD_BLOCK_HTML: case MD_BLOCK_TABLE:
            case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: case MD_BLOCK_TR: case MD_BLOCK_TH: case MD_BLOCK_TD: break;
        }
        return 0;
    }
    static int leaveBlock (MD_BLOCKTYPE type, void*, void* context)
    {
        auto& r = *static_cast<MarkdownRenderer*> (context);
        switch (type)
        {
            case MD_BLOCK_H: r.heading = 0; r.newline (true); break;
            case MD_BLOCK_CODE: --r.code; r.newline (true); break;
            case MD_BLOCK_QUOTE: --r.quote; r.newline (true); break;
            case MD_BLOCK_UL: case MD_BLOCK_OL: r.lists.pop_back(); r.newline (r.lists.empty()); break;
            case MD_BLOCK_LI: r.newline(); break;
            case MD_BLOCK_P: if (r.lists.empty()) r.newline (true); break;
            case MD_BLOCK_DOC: case MD_BLOCK_HR: case MD_BLOCK_HTML: case MD_BLOCK_TABLE:
            case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: case MD_BLOCK_TR: case MD_BLOCK_TH: case MD_BLOCK_TD: break;
        }
        return 0;
    }
    void span (MD_SPANTYPE type, int change)
    {
        switch (type)
        {
            case MD_SPAN_EM: italic += change; break;
            case MD_SPAN_STRONG: bold += change; break;
            case MD_SPAN_CODE: code += change; break;
            case MD_SPAN_A: link += change; break;
            case MD_SPAN_IMG: case MD_SPAN_DEL: case MD_SPAN_LATEXMATH:
            case MD_SPAN_LATEXMATH_DISPLAY: case MD_SPAN_WIKILINK: case MD_SPAN_U: break;
        }
    }
    static int enterSpan (MD_SPANTYPE type, void*, void* context) { static_cast<MarkdownRenderer*> (context)->span (type, 1); return 0; }
    static int leaveSpan (MD_SPANTYPE type, void*, void* context) { static_cast<MarkdownRenderer*> (context)->span (type, -1); return 0; }
    static juce::String entity (juce::String text)
    {
        if (text == "&amp;") return "&";
        if (text == "&lt;") return "<";
        if (text == "&gt;") return ">";
        if (text == "&quot;") return "\"";
        if (text == "&apos;") return "'";
        if (text == "&nbsp;") return juce::String::charToString (160);
        if (text.startsWith ("&#"))
        {
            const bool hex = text.startsWithIgnoreCase ("&#x");
            const auto digits = text.substring (hex ? 3 : 2).dropLastCharacters (1);
            const auto value = hex ? digits.getHexValue64() : digits.getLargeIntValue();
            if (value > 0 && value <= 0x10ffff && (value < 0xd800 || value > 0xdfff)) return juce::String::charToString ((juce::juce_wchar) value);
            return juce::String::charToString (0xfffd);
        }
        return text;
    }
    static int text (MD_TEXTTYPE type, const MD_CHAR* value, MD_SIZE size, void* context)
    {
        auto& r = *static_cast<MarkdownRenderer*> (context);
        if (type == MD_TEXT_BR) r.append ("\n");
        else if (type == MD_TEXT_SOFTBR) r.append (" ");
        else if (type == MD_TEXT_NULLCHAR) r.append (juce::String::charToString (0xfffd));
        else { const auto text = juce::String::fromUTF8 (value, (int) size); r.append (type == MD_TEXT_ENTITY ? entity (text) : text); }
        return 0;
    }
};
}
juce::AttributedString renderNotesMarkdown (const juce::String& markdown)
{
    MarkdownRenderer renderer;
    renderer.result.setLineSpacing (4); renderer.result.setWordWrap (juce::AttributedString::byWord);
    MD_PARSER parser {}; parser.flags = MD_FLAG_TASKLISTS | MD_FLAG_NOHTML;
    parser.enter_block = MarkdownRenderer::enterBlock; parser.leave_block = MarkdownRenderer::leaveBlock;
    parser.enter_span = MarkdownRenderer::enterSpan; parser.leave_span = MarkdownRenderer::leaveSpan; parser.text = MarkdownRenderer::text;
    if (md_parse (markdown.toRawUTF8(), (MD_SIZE) markdown.getNumBytesAsUTF8(), &parser, &renderer) != 0)
    { renderer.result.clear(); renderer.result.append (markdown, Theme::font (14), Theme::white()); }
    return renderer.result;
}

NotesPanel::NotesPanel()
{
    setComponentID ("notes-panel"); editor.setComponentID ("notes-editor"); preview.setComponentID ("notes-preview");
    editor.setMultiLine (true, true); editor.setReturnKeyStartsNewLine (true); editor.setTabKeyUsedAsCharacter (true);
    editor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14, juce::Font::plain)));
    editor.setInputRestrictions (65536); editor.setTextToShowWhenEmpty ("Write preset instructions in Markdown...", Theme::dim());
    editor.onTextChange = [this] { if (changed) changed (editor.getText()); };
    edit.setComponentID ("notes-edit"); read.setComponentID ("notes-read");
    edit.onClick = [this] { showEditor (true); if (editor.isShowing()) editor.grabKeyboardFocus(); };
    read.onClick = [this] { showEditor (false); };
    viewport.setViewedComponent (&preview, false); viewport.setScrollBarsShown (true, false);
    juce::Component* children[] { &editor, &viewport, &edit, &read };
    for (auto* child : children) addAndMakeVisible (child);
    showEditor (true);
}
void NotesPanel::setDocument (const juce::String& uid, const juce::String& markdown)
{
    const bool different = documentUid != uid; documentUid = uid;
    editor.setText (markdown, false);
    if (different) { viewport.setViewPosition (0, 0); showEditor (markdown.isEmpty()); }
    else if (viewport.isVisible()) showEditor (false);
}
void NotesPanel::showEditor (bool editing)
{
    editor.setVisible (editing); viewport.setVisible (! editing);
    edit.setToggleState (editing, juce::dontSendNotification); read.setToggleState (! editing, juce::dontSendNotification);
    if (! editing) { preview.text = renderNotesMarkdown (editor.getText()); resized(); }
}
void NotesPanel::resized()
{
    auto area = getLocalBounds(); auto header = area.removeFromTop (38);
    read.setBounds (header.removeFromRight (88).reduced (0, 4)); header.removeFromRight (6); edit.setBounds (header.removeFromRight (66).reduced (0, 4));
    area.removeFromTop (8); editor.setBounds (area); viewport.setBounds (area);
    preview.layout (juce::jmax (20, viewport.getWidth() - 14));
}
void NotesPanel::paint (juce::Graphics& g)
{ Theme::text (g, "NOTES", { 0, 0, getWidth() - 172, 38 }, 24, Theme::yellow(), true); }
}

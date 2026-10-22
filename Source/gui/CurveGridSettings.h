// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "PromptLanguage.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace scrr::gui {

// Editor preference only. Grid density never enters sound state or the audio path.
class CurveGridSettings
{
public:
    struct Counts { int x { 8 }, y { 8 }; };
    static constexpr int maxDivisions = 64;
    static int sanitise (int count) noexcept { return juce::jlimit (1, maxDivisions, count); }
    static juce::File preferenceFile() { return PromptLanguage::preferenceFile().getSiblingFile ("curve-grid.txt"); }
    static Counts read (const juce::File& file)
    {
        if (file.getSize() > 32) return {};
        const auto tokens = juce::StringArray::fromTokens (file.loadFileAsString().trim(), " \t\r\n", "");
        if (tokens.size() != 2) return {};
        for (const auto& token : tokens)
            if (token.isEmpty() || ! token.containsOnly ("0123456789") || token.getIntValue() < 1 || token.getIntValue() > maxDivisions) return {};
        return { tokens[0].getIntValue(), tokens[1].getIntValue() };
    }
    static bool write (Counts counts, const juce::File& file)
    {
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary (file);
        return temporary.getFile().replaceWithText (juce::String (sanitise (counts.x)) + " " + juce::String (sanitise (counts.y)))
            && temporary.overwriteTargetFileWithTemporary();
    }
    static Counts get() { return state().counts; }
    static bool set (int x, int y, bool persist = true)
    {
        const Counts next { sanitise (x), sanitise (y) };
        if (persist && ! write (next, preferenceFile())) return false;
        auto& settings = state();
        if (settings.counts.x != next.x || settings.counts.y != next.y)
        { settings.counts = next; settings.sendChangeMessage(); }
        return true;
    }
    static juce::ChangeBroadcaster& changes() { return state(); }
    static bool shouldSnap (juce::ModifierKeys modifiers) noexcept
    {
       #if JUCE_MAC
        return modifiers.isCommandDown();
       #else
        return modifiers.isAltDown();
       #endif
    }
    static float snap (float value, int divisions) noexcept
    {
        const float count = (float) sanitise (divisions);
        return juce::jlimit (0.0f, 1.0f, std::round (value * count) / count);
    }
    // Draw exactly the same normalized locations used by snap().
    static void drawGrid (juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        const auto counts = get();
        for (int i = 0; i <= counts.x; ++i)
            graphics.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / (float) counts.x), area.getY(), area.getBottom());
        for (int i = 0; i <= counts.y; ++i)
            graphics.drawHorizontalLine ((int) (area.getBottom() - area.getHeight() * (float) i / (float) counts.y), area.getX(), area.getRight());
    }
    static void showMenu (juce::Component& target);
private:
    struct State : juce::ChangeBroadcaster { Counts counts { read (preferenceFile()) }; };
    static State& state() { static State settings; return settings; }
};

// Custom menu content keeps both editable integer counts visible at once.
class CurveGridControls : public juce::PopupMenu::CustomComponent, private juce::ChangeListener
{
public:
    CurveGridControls() : juce::PopupMenu::CustomComponent (false)
    {
        const auto configure = [this] (juce::Slider& slider, juce::Label& label, const char* axis)
        {
            addAndMakeVisible (slider); addAndMakeVisible (label);
            slider.setSliderStyle (juce::Slider::IncDecButtons); slider.setRange (1, CurveGridSettings::maxDivisions, 1);
            slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 22);
            slider.setComponentID ("curve-grid-" + juce::String (axis).toLowerCase());
            slider.setTitle (juce::String (axis) + " grid divisions");
            label.setText (juce::String (axis) + " DIVISIONS", juce::dontSendNotification);
        };
        configure (x, xLabel, "X"); configure (y, yLabel, "Y");
        updateValues();
        x.onValueChange = [this] { CurveGridSettings::set ((int) x.getValue(), CurveGridSettings::get().y); };
        y.onValueChange = [this] { CurveGridSettings::set (CurveGridSettings::get().x, (int) y.getValue()); };
        CurveGridSettings::changes().addChangeListener (this);
    }
    ~CurveGridControls() override { CurveGridSettings::changes().removeChangeListener (this); }
    void getIdealSize (int& width, int& height) override { width = 248; height = 68; }
    void resized() override
    {
        xLabel.setBounds (8, 6, 108, 24); x.setBounds (120, 6, getWidth() - 132, 24);
        yLabel.setBounds (8, 36, 108, 24); y.setBounds (120, 36, getWidth() - 132, 24);
    }
private:
    void updateValues()
    { const auto counts = CurveGridSettings::get(); x.setValue (counts.x, juce::dontSendNotification); y.setValue (counts.y, juce::dontSendNotification); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateValues(); }
    juce::Slider x, y;
    juce::Label xLabel, yLabel;
};

inline void CurveGridSettings::showMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("CURVE GRID");
    menu.addCustomItem (1, std::make_unique<CurveGridControls>(), nullptr, "X and Y grid divisions");
   #if JUCE_MAC
    menu.addItem (2, "Hold Cmd while dragging: snap X and Y", false);
   #else
    menu.addItem (2, "Hold Alt while dragging: snap X and Y", false);
   #endif
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
        [safe = juce::Component::SafePointer<juce::Component> (&target)] (int) { if (safe != nullptr) safe->repaint(); });
}

class CurveGridButton : public juce::TextButton, private juce::ChangeListener
{
public:
    CurveGridButton() : juce::TextButton ("GRID")
    {
        setComponentID ("curve-grid-settings"); setTitle ("Curve grid divisions");
        onClick = [this] { CurveGridSettings::showMenu (*this); };
        CurveGridSettings::changes().addChangeListener (this); updateTooltip();
    }
    ~CurveGridButton() override { CurveGridSettings::changes().removeChangeListener (this); }
private:
    void updateTooltip()
    {
        const auto counts = CurveGridSettings::get();
        setTooltip ("Grid divisions: X " + juce::String (counts.x) + ", Y " + juce::String (counts.y)
                    + ". Set each axis independently. Shared by curve editors on this computer.");
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    { updateTooltip(); if (auto* parent = getParentComponent()) parent->repaint(); }
};

// GUI projection gives the unchanged state decoder a float-safe > .001 gap.
// In particular, previous.x + .001f can subtract back to less than .001f.
struct CurvePointEditing
{
    static constexpr float minimumGap = .001002f;
    static float constrainX (float requested, float current, float previous, float next) noexcept
    {
        const float low = previous + minimumGap, high = next - minimumGap;
        if (low > high) return current; // No inverted jlimit bounds in a dense legacy curve.
        return juce::jlimit (low, high, std::isfinite (requested) ? requested : current);
    }
    static float snapX (float requested, float current, float previous, float next, int divisions) noexcept
    {
        const double count = (double) CurveGridSettings::sanitise (divisions);
        const double first = std::ceil (((double) previous + minimumGap) * count);
        const double last = std::floor (((double) next - minimumGap) * count);
        if (first > last) return constrainX (current, current, previous, next);
        return (float) (juce::jlimit (first, last, std::round ((double) requested * count)) / count);
    }
    template <typename Points>
    static void normalise (Points& points, bool pinCentre = false)
    {
        if (points.size() < 2) return;
        points.front().x = 0; points.back().x = 1;
        size_t centre = points.size();
        if (pinCentre)
            for (size_t i = 1; i + 1 < points.size(); ++i)
                if (std::abs (points[i].x - .5f) < .000001f) { centre = i; points[i].x = .5f; break; }
        const auto project = [&] (size_t left, size_t right)
        {
            for (size_t i = left + 1; i < right; ++i)
            {
                const float low = points[i - 1].x + minimumGap;
                const float high = points[right].x - minimumGap * (float) (right - i);
                if (low <= high) points[i].x = juce::jlimit (low, high, points[i].x);
            }
        };
        if (centre < points.size()) { project (0, centre); project (centre, points.size() - 1); }
        else project (0, points.size() - 1);
    }
};
} // namespace scrr::gui

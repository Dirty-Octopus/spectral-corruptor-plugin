// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "AssignableSlider.h"

namespace scrr::gui {
class ModuleRow : public juce::Component
{
public:
    ModuleRow (juce::String name, juce::String id, int number, bool on, bool active)
        : uid (std::move (id)), title (std::move (name)), position (number), enabled (on), selected (active)
    { setComponentID ("stage-" + uid); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    std::function<void()> choose;
    std::function<void(const juce::MouseEvent&)> contextMenu;
    bool generic {}, notes {};
    std::function<void(juce::Point<int>, bool, bool)> drag;
    juce::String uid;
    void tick (float dt)
    {
        const float target = isMouseOver (true) ? 1.0f : 0.0f;
        hover += (target - hover) * juce::jmin (1.0f, dt * 14.0f);
        if (std::abs (hover - target) > .01f) repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (selected ? (enabled ? Theme::yellow() : Theme::inactive())
                            : Theme::panel().interpolatedWith (enabled ? Theme::blue() : Theme::line(), hover * .5f));
        const auto ink = selected ? Theme::black() : enabled ? Theme::white() : Theme::dim();
        Theme::text (g, juce::String (position + 1).paddedLeft ('0', 2), { 10, 6, 26, 28 }, 18, ink, true);
        Theme::text (g, title, { 42 + (int) (hover * 3), 4, getWidth() - 58, 30 }, 13, ink, true);
        Theme::text (g, notes ? juce::String ("NOTES / TEXT") : (generic ? "GENERIC / " : "FFT / ") + juce::String (enabled ? "ON" : "OFF"), { 43, 33, 120, 15 }, 9, ink, true);
        g.setColour (ink.withAlpha (.6f));
        for (int y = 35; y < 45; y += 4) g.fillRect (getWidth() - 24, y, 11, 1);
        if (selected) { g.setColour (enabled ? Theme::red() : Theme::line()); g.fillRect (0, 0, 4, getHeight() - 2); }
        g.setColour (Theme::black()); g.fillRect (0, getHeight() - 2, getWidth(), 2);
    }
    void mouseDown (const juce::MouseEvent& e) override { dragging = false; popup = e.mods.isPopupMenu(); if (popup && contextMenu) contextMenu (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (popup) return;
        if (e.getDistanceFromDragStart() < 6 && ! dragging) return;
        dragging = true; if (drag) drag (e.getScreenPosition(), false, duplicateModifier (e.mods));
    }
    void mouseUp (const juce::MouseEvent& e) override
    { if (popup) return; if (dragging) { if (drag) drag (e.getScreenPosition(), true, duplicateModifier (e.mods)); } else if (choose) choose(); dragging = false; }
private:
    juce::String title; int position; bool enabled, selected, dragging {}, popup {}; float hover {};
};
} // namespace scrr::gui

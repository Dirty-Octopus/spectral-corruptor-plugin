// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "Painter.h"
#include "Skin.h"

namespace scrr::gui {

/**
    Phase-1 painter using juce::Graphics primitives.
    Draws a clean, dark "corrupted spectrum analyzer" vibe without any image
    assets. Will be replaceable by SpriteSheetPainter later without touching
    component code (components only know about Painter).
*/
class DefaultPainter : public Painter
{
public:
    void paintKnob (juce::Graphics& g, juce::Rectangle<float> b,
                    float value01, State state) override
    {
        const auto& s = Skin::get();
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;

        // Dial body
        g.setColour (s.panelLight);
        g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
        g.setColour (s.panel.darker (0.3f));
        g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 1.0f);

        // Arc track
        const float a0 = juce::MathConstants<float>::pi * 1.25f;
        const float a1 = juce::MathConstants<float>::pi * 2.75f;
        juce::Path track;
        track.addCentredArc (cx, cy, r * 0.8f, r * 0.8f, 0.0f, a0, a1, true);
        g.setColour (s.accentDim);
        g.strokePath (track, juce::PathStrokeType (2.0f));

        // Arc value
        const float av = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, value01);
        juce::Path val;
        val.addCentredArc (cx, cy, r * 0.8f, r * 0.8f, 0.0f, a0, av, true);
        g.setColour (state == State::Hover ? s.accent.brighter (0.2f) : s.accent);
        g.strokePath (val, juce::PathStrokeType (2.5f));

        // Pointer
        const float px = cx + std::sin (av) * r * 0.7f;
        const float py = cy - std::cos (av) * r * 0.7f;
        g.setColour (s.text);
        g.drawLine (cx, cy, px, py, 2.0f);
    }

    void paintButton (juce::Graphics& g, juce::Rectangle<float> b,
                      const juce::String& label, bool toggled, State state) override
    {
        const auto& s = Skin::get();
        juce::Colour fill = toggled ? s.accent : s.panelLight;
        if (state == State::Hover)  fill = fill.brighter (0.15f);
        if (state == State::Down)   fill = fill.darker   (0.2f);
        g.setColour (fill);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (toggled ? s.background : s.textDim);
        g.setFont (12.0f);
        g.drawText (label, b, juce::Justification::centred);
    }

    void paintToggle (juce::Graphics& g, juce::Rectangle<float> b,
                      bool toggled, State state) override
    {
        const auto& s = Skin::get();
        const float track = b.getHeight() * 0.6f;
        auto t = b.withSizeKeepingCentre (b.getWidth(), track);
        g.setColour (toggled ? s.accent : s.panelLight);
        g.fillRoundedRectangle (t, track * 0.5f);
        const float knobR = track * 0.5f;
        const float kx = toggled ? t.getRight() - knobR : t.getX() + knobR;
        g.setColour (state == State::Hover ? s.text.brighter (0.1f) : s.text);
        g.fillEllipse (kx - knobR, t.getY(), 2.0f * knobR, 2.0f * knobR);
    }

    void paintBackground (juce::Graphics& g, juce::Rectangle<int> b) override
    {
        const auto& s = Skin::get();
        g.fillAll (s.background);
        // subtle scanline texture
        g.setColour (s.panel.withAlpha (0.15f));
        for (int y = 0; y < b.getHeight(); y += 3)
            g.fillRect (0, y, b.getWidth(), 1);
    }
};

} // namespace scrr::gui

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace scrr::gui {

/**
    Abstract painting interface. Components hold a Painter and delegate all
    pixel rendering to it. This keeps control logic (state, parameter binding,
    interaction) fully decoupled from visual style.

    Concrete implementations:
      - DefaultPainter   : draws with juce::Graphics primitives (phase 1).
      - SpriteSheetPainter: reads Aseprite-exported PNG sprite sheets (later).
*/
class Painter
{
public:
    virtual ~Painter() = default;

    enum class State { Normal, Hover, Down, Disabled };

    virtual void paintKnob    (juce::Graphics&, juce::Rectangle<float> bounds,
                               float value01, State state) = 0;
    virtual void paintButton  (juce::Graphics&, juce::Rectangle<float> bounds,
                               const juce::String& label, bool toggled, State state) = 0;
    virtual void paintToggle  (juce::Graphics&, juce::Rectangle<float> bounds,
                               bool toggled, State state) = 0;
    virtual void paintBackground (juce::Graphics&, juce::Rectangle<int> bounds) = 0;
};

} // namespace scrr::gui

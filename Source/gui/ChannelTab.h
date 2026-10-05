// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"

namespace scrr::gui {
// Selection is persistent; hover only changes the inset outline and tint.
class ChannelTab : public juce::TextButton
{
public:
    void setChannel (int index, bool receivesBand)
    { channel = index; assigned = receivesBand; repaint(); }
    std::function<void()> toggleEnabled;
    void setActivity (bool enabled, bool receivesBand)
    {
        if (on == enabled && available == receivesBand) return;
        on = enabled; available = receivesBand; repaint();
    }
    void setBeatPhase (float next)
    {
        const bool changed = (phase < .25f) != (next < .25f);
        phase = next;
        if (changed && on && available) repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dotPress = e.x < 21;
        if (dotPress) { if (toggleEnabled) toggleEnabled(); repaint(); }
        else juce::TextButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! dotPress) juce::TextButton::mouseUp (e); dotPress = false; }
    void tick (float dt)
    {
        const float target = isOver() || hasKeyboardFocus (true) ? 1.0f : 0.0f;
        if (std::abs (hover - target) < .0001f) return;
        const float next = hover + (target - hover) * juce::jmin (1.0f, dt * 18.0f);
        hover = std::abs (next - target) < .001f ? target : next; repaint();
    }
    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        const bool selected = getToggleState();
        const auto accent = assigned ? Theme::channelColour (channel) : Theme::yellow();
        const auto base = assigned ? Theme::panel().interpolatedWith (accent, .22f) : Theme::panel();
        const bool muted = available && ! on;
        const auto fill = selected ? (muted ? accent.interpolatedWith (Theme::black(), .65f) : accent)
                                   : base.interpolatedWith (accent, hover * .4f + (down ? .15f : 0.0f));
        const auto ink = selected && ! muted && accent != Theme::blue() ? Theme::black() : Theme::white();
        g.fillAll (fill);
        if (assigned) { g.setColour (accent); g.fillRect (0, 0, getWidth(), 3); }
        g.setColour (Theme::line().interpolatedWith (Theme::white(), hover));
        g.drawRect (getLocalBounds().reduced (1), selected ? 2 : 1);
        Theme::text (g, getButtonText(), getLocalBounds().withTrimmedLeft (18).withTrimmedBottom (3), 13, ink, true, juce::Justification::centred);
        // A bezel-free status LED: hard on/off edges on a shared host-beat clock, independent of signal level.
        g.setColour (! available ? Theme::inactive() : ! on ? Theme::red()
                    : phase < .25f ? juce::Colour (0xff41f174) : juce::Colour (0xff143820));
        g.fillEllipse (7.5f, (float) getHeight() * .5f - 3.5f, 7, 7);
        if (selected) { g.setColour (ink); g.fillRect (8, getHeight() - 5, getWidth() - 16, 3); }
    }
private:
    int channel {};
    bool assigned {};
    float hover {}, phase {};
    bool on { true }, available {}, dotPress {};
};
} // namespace scrr::gui

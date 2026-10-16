// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../modules/SpectralEqualizer.h"

namespace scrr::gui {
// The graph owns a detached state copy. All edits go through the host callback.
class SpectralEqEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectralEqEditor()
    {
        setComponentID ("spectral-eq-editor");
        setWantsKeyboardFocus (true);
        setTooltip ("Drag: frequency / gain. Alt/Option-drag: fine. Scroll: Q. Double-click: reset band. Right-click: band shape / bypass.");
    }
    std::function<void(const juce::String&, const juce::var&)> onParameterChange;
    std::function<void(int)> onBandSelected;
    void setState (const juce::ValueTree& next)
    {
        if (getModuleUid() != next["uid"].toString()) dragging = false;
        state = next.createCopy(); bands = scrr::dsp::readSpectralEqBands (state); repaint();
    }
    juce::String getModuleUid() const { return state["uid"].toString(); }
    void setSampleRate (double rate)
    { if (std::isfinite (rate) && rate >= 1000 && std::abs (rate - sampleRate) > 1.0e-9) { sampleRate = rate; repaint(); } }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    int getSelectedBand() const noexcept { return selected; }
    void setSelectedBand (int band)
    { selected = juce::jlimit (0, scrr::dsp::spectralEqBands - 1, band); repaint(); }
    static constexpr int preferredHeight() noexcept { return 230; }
    juce::Point<float> getBandPosition (int band) const
    {
        const auto& b = bands[(size_t) juce::jlimit (0, scrr::dsp::spectralEqBands - 1, band)];
        return { frequencyToX (b.frequency), gainToY (b.type >= scrr::dsp::SpectralEqBand::LowCut ? 0 : b.gain) };
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::field());
        const auto plot = plotBounds();
        if (plot.isEmpty()) return;
        g.setColour (Theme::line().withAlpha (.42f));
        for (int hz : { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 })
        {
            const float x = frequencyToX (hz);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            if (hz == 20 || hz == 100 || hz == 1000 || hz == 10000)
                Theme::text (g, hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz),
                             { (int) x - 12, (int) plot.getBottom() + 3, 36, 15 }, 9, Theme::dim());
        }
        for (int db : { -24, -12, 0, 12, 24 })
        {
            const float y = gainToY (db);
            g.setColour (db == 0 ? Theme::line() : Theme::line().withAlpha (.35f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            Theme::text (g, juce::String (db), { 1, (int) y - 7, 28, 14 }, 9, Theme::dim());
        }
        scrr::dsp::SpectralEqResponse response; response.configure (bands, sampleRate);
        juce::Path curve;
        const int points = juce::jmax (2, (int) plot.getWidth());
        for (int i = 0; i <= points; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) points;
            const float y = gainToY (juce::jlimit (-24.0, 24.0, response.decibelsAt (xToFrequency (x))));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        auto fill = curve;
        fill.lineTo (plot.getRight(), gainToY (0)); fill.lineTo (plot.getX(), gainToY (0)); fill.closeSubPath();
        g.setColour (accent.withAlpha (.10f)); g.fillPath (fill);
        g.setColour (accent); g.strokePath (curve, juce::PathStrokeType (2.0f));
        for (int i = 0; i < scrr::dsp::spectralEqBands; ++i)
        {
            const auto point = getBandPosition (i); const auto& b = bands[(size_t) i];
            const float r = i == selected ? 8.5f : 6.5f;
            if (i == selected) { g.setColour (accent.withAlpha (.18f)); g.fillEllipse (point.x - 13, point.y - 13, 26, 26); }
            g.setColour (b.enabled ? accent : Theme::inactive()); g.fillEllipse (point.x - r, point.y - r, 2 * r, 2 * r);
            Theme::text (g, juce::String (i + 1), { (int) point.x - 7, (int) point.y - 7, 14, 14 }, 10,
                         Theme::black(), true, juce::Justification::centred);
        }
        const auto& band = bands[(size_t) selected];
        const juce::String status = "B" + juce::String (selected + 1) + "  " + juce::String (band.frequency, 0) + " Hz  "
            + (band.type < scrr::dsp::SpectralEqBand::LowCut ? juce::String (band.gain, 1) + " dB  " : "CUT  ")
            + "Q " + juce::String (band.q, 2);
        Theme::text (g, status, { 33, 2, getWidth() - 39, 18 }, 10, accent, true);
        Theme::text (g, "DRAG: FREQ / GAIN    SCROLL: Q    DOUBLE-CLICK: RESET", { 8, getHeight() - 20, getWidth() - 16, 17 }, 9, Theme::dim());
        g.setColour (Theme::line()); g.drawRect (getLocalBounds());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false; const int hit = hitBand (e.position);
        if (hit < 0) return;
        selected = hit; repaint();
        const auto safe = juce::Component::SafePointer<SpectralEqEditor> (this);
        const auto targetUid = getModuleUid();
        if (onBandSelected) onBandSelected (selected);
        if (! safe || safe->getModuleUid() != targetUid) return;
        if (e.mods.isPopupMenu()) { showBandMenu(); return; }
        grabKeyboardFocus(); dragging = true; anchor = e.position;
        startFrequency = bands[(size_t) selected].frequency; startGain = bands[(size_t) selected].gain;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto plot = plotBounds(); if (plot.isEmpty()) return;
        const float scale = e.mods.isAltDown() ? .15f : 1.0f;
        const auto delta = (e.position - anchor) * scale;
        const double hz = xToFrequency (frequencyToX (startFrequency) + delta.x);
        const double db = juce::jlimit (-24.0, 24.0, startGain - delta.y * 48.0 / plot.getHeight());
        const auto safe = juce::Component::SafePointer<SpectralEqEditor> (this);
        const auto targetUid = getModuleUid();
        const bool supportsGain = bands[(size_t) selected].type < scrr::dsp::SpectralEqBand::LowCut;
        change ("Freq", hz);
        if (safe && safe->getModuleUid() == targetUid && supportsGain) safe->change ("Gain", db);
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    { const int hit = hitBand (e.position); if (hit >= 0) { selected = hit; resetBand(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int hit = hitBand (e.position); if (hit < 0) { Component::mouseWheelMove (e, wheel); return; }
        selected = hit;
        const double delta = wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);
        change ("Q", juce::jlimit (.1, 18.0, bands[(size_t) selected].q * std::pow (2.0, delta * 2)));
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
        {
            selected = (selected + (key.getKeyCode() == juce::KeyPress::rightKey ? 1 : 7)) % 8;
            repaint(); if (onBandSelected) onBandSelected (selected); return true;
        }
        return false;
    }
private:
    juce::Rectangle<float> plotBounds() const
    { return { 32.0f, 25.0f, juce::jmax (1.0f, (float) getWidth() - 49), juce::jmax (1.0f, (float) getHeight() - 70) }; }
    float frequencyToX (double hz) const
    { const auto p = plotBounds(); return p.getX() + p.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20) / std::log (1000.0)); }
    double xToFrequency (float x) const
    { const auto p = plotBounds(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
    float gainToY (double gain) const
    { const auto p = plotBounds(); return p.getY() + p.getHeight() * (float) ((24.0 - gain) / 48.0); }
    int hitBand (juce::Point<float> point) const
    {
        int best = -1; float distance = 15.0f;
        // Selected band wins when several nodes overlap.
        if (point.getDistanceFrom (getBandPosition (selected)) <= distance) return selected;
        for (int i = 0; i < scrr::dsp::spectralEqBands; ++i)
        { const float d = point.getDistanceFrom (getBandPosition (i)); if (d < distance) { distance = d; best = i; } }
        return best;
    }
    void change (const char* suffix, const juce::var& value)
    {
        const auto id = scrr::dsp::spectralEqParameter (selected, suffix);
        state.setProperty (id, value, nullptr); bands = scrr::dsp::readSpectralEqBands (state); repaint();
        if (onParameterChange) onParameterChange (id, value);
    }
    void resetBand()
    {
        const auto safe = juce::Component::SafePointer<SpectralEqEditor> (this);
        const auto targetUid = getModuleUid();
        change ("Freq", scrr::dsp::spectralEqFrequencies[(size_t) selected]);
        if (safe && safe->getModuleUid() == targetUid) safe->change ("Gain", 0.0);
        if (safe && safe->getModuleUid() == targetUid) safe->change ("Q", 0.7071067811865476);
    }
    void showBandMenu()
    {
        juce::PopupMenu menu;
        const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" };
        for (int i = 0; i < names.size(); ++i) menu.addItem (i + 1, names[i], true, bands[(size_t) selected].type == i);
        menu.addSeparator(); menu.addItem (10, "Band enabled", true, bands[(size_t) selected].enabled);
        menu.addItem (11, "Reset frequency / gain / Q");
        const auto safe = juce::Component::SafePointer<SpectralEqEditor> (this); const int target = selected;
        const auto targetUid = getModuleUid();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, target, targetUid] (int result)
        {
            if (! safe || result == 0 || safe->getModuleUid() != targetUid) return;
            safe->selected = target;
            if (result >= 1 && result <= 5) safe->change ("Type", result - 1);
            else if (result == 10) safe->change ("Enabled", ! safe->bands[(size_t) target].enabled);
            else if (result == 11) safe->resetBand();
        });
    }
    juce::ValueTree state { "module" };
    std::array<scrr::dsp::SpectralEqBand, scrr::dsp::spectralEqBands> bands { scrr::dsp::readSpectralEqBands (state) };
    juce::Colour accent { Theme::yellow() };
    double sampleRate { 44100 }, startFrequency {}, startGain {};
    int selected {};
    bool dragging {};
    juce::Point<float> anchor;
};
} // namespace scrr::gui

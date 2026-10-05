// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "FrequencyScale.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include <deque>

namespace scrr::gui {
class ModulationCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    scrr::params::ModulationSource source;
    std::function<void(scrr::params::ModulationSource)> changed;
    ModulationCurve() { setTooltip ("Double-click empty space: add. Right/double-click a point: delete (endpoints stay). Drag a hollow midpoint up/down to bend the segment."); }
    void tick (float value, float phase)
    {
        output = value; playhead = phase;
        if (source.envelope || source.random) { history.push_back (value); if (history.size() > 180) history.pop_front(); }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto a = plot(); const auto accent = source.enabled ? (source.envelope ? Theme::yellow() : source.random ? Theme::red() : Theme::blue()) : Theme::inactive();
        g.fillAll (Theme::field()); g.setColour (Theme::line());
        for (int i = 0; i <= 8; ++i) g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / 8), a.getY(), a.getBottom());
        for (int i = 0; i <= 4; ++i) g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / 4), a.getX(), a.getRight());
        juce::Path line;
        if (source.envelope || source.random)
        {
            for (size_t i = 0; i < history.size(); ++i)
            {
                const float x = a.getX() + a.getWidth() * (float) i / 179, y = a.getBottom() - history[i] * a.getHeight();
                if (i == 0) line.startNewSubPath (x, y);
                else { if (source.random && source.shape == 0 && source.smooth <= 0) line.lineTo (x, a.getBottom() - history[i - 1] * a.getHeight()); line.lineTo (x, y); }
            }
        }
        else
        {
            for (int i = 0; i <= 256; ++i)
            {
                const double x = (double) i / 256;
                const float value = source.shape == 5 && i == 256 ? source.points.back().y : source.valueAt (juce::jmin (.999999, x));
                const auto p = juce::Point<float> (a.getX() + (float) x * a.getWidth(), a.getBottom() - value * a.getHeight());
                if (i == 0) line.startNewSubPath (p); else line.lineTo (p);
            }
        }
        g.setColour (accent); g.strokePath (line, juce::PathStrokeType (2.5f));
        if (! source.envelope && ! source.random)
        {
            g.setColour (Theme::white().withAlpha (.7f)); g.drawVerticalLine ((int) (a.getX() + playhead * a.getWidth()), a.getY(), a.getBottom());
            if (source.shape == 5)
            {
                for (size_t i = 0; i + 1 < source.points.size(); ++i)
                {
                    const auto h = curveHandle ((int) i); const auto circle = juce::Rectangle<float> (7, 7).withCentre (h);
                    g.setColour (Theme::field()); g.fillEllipse (circle);
                    g.setColour ((int) i == curveSegment || (int) i == hoverSegment ? Theme::white() : accent); g.drawEllipse (circle, 1.3f);
                }
                for (auto p : source.points) { g.setColour (Theme::yellow()); g.fillRect (a.getX() + p.x * a.getWidth() - 3, a.getBottom() - p.y * a.getHeight() - 3, 6.0f, 6.0f); }
            }
        }
        Theme::text (g, juce::String (output * 100, 1) + "%", { getWidth() - 63, 3, 58, 17 }, 10, Theme::white(), true, juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (source.envelope || source.random) return;
        ensureCustom(); selected = -1; curveSegment = -1;
        const int handle = nearestHandle (e.position);
        if (handle >= 0 && ! e.mods.isPopupMenu()) { beginCurve (handle, e); return; }
        selected = nearest (e.position);
        if (e.mods.isPopupMenu() && selected > 0 && selected + 1 < (int) source.points.size())
        { source.points.erase (source.points.begin() + selected); selected = -1; submit(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (source.envelope || source.random || e.mods.isPopupMenu()) return;
        if (curveSegment >= 0 && curveSegment + 1 < (int) source.points.size())
        {
            auto& a = source.points[(size_t) curveSegment]; const auto& b = source.points[(size_t) curveSegment + 1];
            const float direction = b.y >= a.y ? 1.0f : -1.0f;
            a.curve = juce::jlimit (-1.0f, 1.0f, curveStart + direction * (float) (curveStartY - e.getScreenY()) / (e.mods.isShiftDown() ? 1000.0f : 150.0f));
            submit(); return;
        }
        if (selected < 0 || selected >= (int) source.points.size()) return;
        auto& p = source.points[(size_t) selected]; const auto a = plot();
        p.y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        if (selected > 0 && selected + 1 < (int) source.points.size()) p.x = juce::jlimit (source.points[(size_t) selected - 1].x + .001f, source.points[(size_t) selected + 1].x - .001f, (e.position.x - a.getX()) / a.getWidth());
        submit();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (source.envelope || source.random) return;
        ensureCustom(); const int existing = nearest (e.position); selected = curveSegment = -1;
        if (existing >= 0)
        {
            if (existing > 0 && existing + 1 < (int) source.points.size()) { source.points.erase (source.points.begin() + existing); submit(); }
            return;
        }
        if (source.points.size() >= 64) return;
        const auto a = plot(); const float x = juce::jlimit (.001f, .999f, (e.position.x - a.getX()) / a.getWidth());
        for (auto p : source.points) if (std::abs (p.x - x) < .002f) return;
        source.points.push_back ({ x, juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight()) });
        std::sort (source.points.begin(), source.points.end(), [] (auto p, auto q) { return p.x < q.x; }); submit();
    }
    void mouseUp (const juce::MouseEvent&) override { selected = curveSegment = -1; repaint(); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int next = ! source.envelope && ! source.random && source.shape == 5 ? nearestHandle (e.position) : -1;
        if (next != hoverSegment) { hoverSegment = next; setMouseCursor (next >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverSegment = -1; repaint(); }
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (8); }
    juce::Point<float> curveHandle (int segment) const
    {
        const auto a = plot(); const float x = (source.points[(size_t) segment].x + source.points[(size_t) segment + 1].x) * .5f;
        return { a.getX() + x * a.getWidth(), a.getBottom() - source.valueAt (x) * a.getHeight() };
    }
    int nearestHandle (juce::Point<float> point) const
    {
        for (size_t i = 0; i + 1 < source.points.size(); ++i) if (point.getDistanceFrom (curveHandle ((int) i)) <= 7) return (int) i;
        return -1;
    }
    void beginCurve (int index, const juce::MouseEvent& e)
    { curveSegment = index; curveStart = source.points[(size_t) index].curve; curveStartY = e.getScreenY(); selected = -1; repaint(); }
    void ensureCustom()
    {
        if (source.shape == 5) return;
        source.points.clear();
        for (int i = 0; i <= 8; ++i) source.points.push_back ({ (float) i / 8, source.valueAt (juce::jmin (.999999, (double) i / 8)) });
        source.shape = 5; submit();
    }
    int nearest (juce::Point<float> point) const
    {
        const auto a = plot(); int best = -1; float distance = 15;
        for (size_t i = 0; i < source.points.size(); ++i)
        {
            const auto p = source.points[i]; const float d = point.getDistanceFrom ({ a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() });
            if (d < distance) { distance = d; best = (int) i; }
        }
        return best;
    }
    void submit() { if (changed) changed (source); repaint(); }
    int selected { -1 }, curveSegment { -1 }, hoverSegment { -1 }, curveStartY {}; float curveStart {}, output {}, playhead {}; std::deque<float> history;
};
class ModulatorCard : public juce::Component
{
public:
    ModulatorCard (SpectralCrrptProcessor& p, scrr::params::ModulationSource s) : processor (p), source (std::move (s))
    {
        setComponentID ("modulator-" + source.uid);
        title.setText (source.name, juce::dontSendNotification); title.setEditable (false, true); title.setFont (Theme::font (19, true));
        title.onTextChange = [this] { source.name = title.getText().trim().substring (0, 32); if (source.name.isEmpty()) source.name = source.envelope ? "Envelope" : source.random ? "Random" : "LFO"; submit(); }; addAndMakeVisible (title);
        enabled.setToggleState (source.enabled, juce::dontSendNotification); enabled.onClick = [this] { source.enabled = enabled.getToggleState(); submit(); }; addAndMakeVisible (enabled);
        remove.onClick = [this] { if (removeSource) removeSource (source.uid); }; addAndMakeVisible (remove);
        mappings.onClick = [this] { if (showMappings) showMappings (source.uid); }; addAndMakeVisible (mappings);
        curve.source = source; curve.changed = [this] (auto next) { source.points = std::move (next.points); source.shape = next.shape; shape.setSelectedItemIndex (source.shape, juce::dontSendNotification); submit(); }; addAndMakeVisible (curve);
        if (source.envelope || source.random) curve.setTooltip ("Live modulation output history.");
        if (source.envelope)
        {
            shape.addItemList ({ "Plugin input", "CH 1 / pre", "CH 2 / pre", "CH 3 / pre", "CH 4 / pre", "Host Sidechain" }, 1);
            shape.setSelectedItemIndex (source.input, juce::dontSendNotification);
            shape.onChange = [this] { source.input = shape.getSelectedItemIndex(); submit(); };
            shape.setTooltip ("Host Sidechain is silent until your DAW sends audio to the optional Sidechain bus. CH pre signals follow the FFT analysis timing.");
            setupSlider (first, -24, 48, .1, 0); first.setValue (source.gain, juce::dontSendNotification); first.setTextValueSuffix (" dB");
            setupSlider (second, .1, 2000, .1, 10); second.setSkewFactorFromMidPoint (40); second.setValue (source.attack, juce::dontSendNotification); second.setTextValueSuffix (" ms");
            setupSlider (third, 1, 5000, .1, 150); third.setSkewFactorFromMidPoint (250); third.setValue (source.release, juce::dontSendNotification); third.setTextValueSuffix (" ms");
            first.onValueChange = [this] { source.gain = (float) first.getValue(); submit(); };
            second.onValueChange = [this] { source.attack = (float) second.getValue(); submit(); };
            third.onValueChange = [this] { source.release = (float) third.getValue(); submit(); };
            addAndMakeVisible (third);
        }
        else
        {
            if (source.random) shape.addItemList ({ "Sample & Hold", "Smooth" }, 1);
            else shape.addItemList ({ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Custom" }, 1);
            shape.setSelectedItemIndex (source.shape, juce::dontSendNotification);
            shape.onChange = [this] { source.shape = shape.getSelectedItemIndex(); submit(); };
            division.addItemList (scrr::params::ModulationSource::divisionNames(), 1); division.setSelectedItemIndex (source.division, juce::dontSendNotification);
            division.onChange = [this] { source.division = division.getSelectedItemIndex(); submit(); }; addAndMakeVisible (division);
            sync.setToggleState (source.sync, juce::dontSendNotification); sync.onClick = [this] { source.sync = sync.getToggleState(); submit(); resized(); }; addAndMakeVisible (sync);
            retrigger.setToggleState (source.retrigger, juce::dontSendNotification); retrigger.onClick = [this] { source.retrigger = retrigger.getToggleState(); submit(); }; addAndMakeVisible (retrigger);
            retrigger.setTooltip ("Restart phase/sequence when host playback begins. When off, synced sources follow host PPQ; Hz sources run freely.");
            setupSlider (first, .01, source.random ? 128 : 40, .01, 1); setupFrequencyScale (first); first.setValue (source.rate, juce::dontSendNotification); first.setTextValueSuffix (" Hz");
            setupSlider (second, 0, source.random ? 999999 : 360, 1, source.random ? 1 : 0); second.setValue (source.random ? (float) source.seed : source.phase, juce::dontSendNotification); second.setTextValueSuffix (source.random ? "" : " deg");
            first.onValueChange = [this] { source.rate = (float) first.getValue(); submit(); };
            second.onValueChange = [this] { if (source.random) source.seed = (int) second.getValue(); else source.phase = (float) second.getValue(); submit(); };
            if (source.random)
            {
                setupSlider (third, 0, 2000, .1, 0); third.setSkewFactorFromMidPoint (100);
                third.setComponentID ("random-smooth"); third.setTextValueSuffix (" ms"); third.setValue (source.smooth, juce::dontSendNotification);
                third.setTooltip ("Random output smoothing time. 0 ms: unchanged. Higher values soften and slow changes in either mode.");
                third.onValueChange = [this] { source.smooth = (float) third.getValue(); submit(); }; addAndMakeVisible (third);
            }
        }
        addAndMakeVisible (shape); addAndMakeVisible (first); addAndMakeVisible (second);
    }
    std::function<void(const juce::String&)> removeSource, showMappings;
    const juce::String& uid() const { return source.uid; }
    void tick (float value, float phase) { curve.tick (value, phase); repaint(); }
    void resized() override
    {
        auto a = getLocalBounds().reduced (12); auto header = a.removeFromTop (26);
        remove.setBounds (header.removeFromRight (26)); header.removeFromRight (6); enabled.setBounds (header.removeFromRight (66)); title.setBounds (header);
        a.removeFromTop (8); curve.setBounds (a.removeFromTop (juce::jmax (56, getHeight() - (source.random ? 236 : 192)))); a.removeFromTop (8);
        auto choice = a.removeFromTop (26); mappings.setBounds (choice.removeFromRight (70)); choice.removeFromRight (8); shape.setBounds (choice);
        a.removeFromTop (19); auto row = a.removeFromTop (25); first.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (8)); second.setBounds (row);
        if (source.envelope || source.random) { a.removeFromTop (19); third.setBounds (a.removeFromTop (25)); }
        if (! source.envelope)
        {
            first.setVisible (! source.sync); division.setVisible (source.sync); division.setBounds (first.getBounds());
            a.removeFromTop (8); row = a.removeFromTop (25); sync.setBounds (row.removeFromLeft (98)); retrigger.setBounds (row);
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (source.enabled ? (source.envelope ? Theme::yellow() : source.random ? Theme::red() : Theme::blue()) : Theme::inactive()); g.fillRect (0, 0, 4, getHeight());
        auto label = [&] (const juce::String& text, const juce::Component& control) { Theme::text (g, text, control.getBounds().translated (0, -17).withHeight (15), 9, Theme::dim(), true); };
        label (source.envelope ? "GAIN" : source.sync ? "BEAT DIVISION" : "RATE", first); label (source.envelope ? "RISE" : source.random ? "SEED" : "PHASE", second);
        if (source.envelope || source.random) label (source.random ? "SMOOTH" : "FALL", third);
    }
private:
    void submit() { curve.source = source; processor.updateModulator (source); repaint(); }
    SpectralCrrptProcessor& processor; scrr::params::ModulationSource source;
    juce::Label title; juce::ToggleButton enabled { "ON" }, sync { "SYNC" }, retrigger { "RESTART" };
    juce::TextButton remove { "x" }, mappings { "MAPS" };
    ModulationCurve curve; juce::ComboBox shape, division; juce::Slider first, second, third;
};
class ModulationPage : public juce::Component
{
public:
    explicit ModulationPage (SpectralCrrptProcessor& p) : processor (p)
    {
        setComponentID ("modulation-page");
        view.setViewedComponent (&list, false); view.setScrollBarsShown (true, false); addAndMakeVisible (view);
        addLFO.setComponentID ("add-lfo"); addEnv.setComponentID ("add-envelope");
        addLFO.onClick = [this] { processor.addModulator (false); refresh(); view.setViewPosition (0, list.getHeight()); };
        addEnv.onClick = [this] { processor.addModulator (true); refresh(); view.setViewPosition (0, list.getHeight()); };
        addRandom.setComponentID ("add-random");
        addRandom.onClick = [this] { processor.addModulator (false, true); refresh(); view.setViewPosition (0, list.getHeight()); };
        close.onClick = [this] { setVisible (false); if (closed) closed(); };
        for (auto* b : { &addLFO, &addEnv, &addRandom, &close }) addAndMakeVisible (b);
    }
    ~ModulationPage() override { view.setViewedComponent (nullptr, false); }
    std::function<void()> closed;
    std::function<void(const juce::String&)> openMappings;
    void open() { refresh(); setVisible (true); toFront (true); }
    void tick()
    {
        if (! isVisible()) return;
        if (revision != processor.getModulatorRevision()) refresh();
        const auto values = processor.getModulationValues();
        for (size_t i = 0; i < cards.size(); ++i) cards[i]->tick (values[i + 8], processor.getModulatorPhase ((int) i));
        repaint();
    }
    void refresh()
    {
        cards.clear(); revision = processor.getModulatorRevision();
        const auto safe = juce::Component::SafePointer<ModulationPage> (this);
        for (auto source : processor.getModulators())
        {
            auto card = std::make_unique<ModulatorCard> (processor, std::move (source));
            card->removeSource = [safe] (const juce::String& uid)
            { juce::MessageManager::callAsync ([safe, uid] { if (safe) { safe->processor.removeModulator (uid); safe->refresh(); } }); };
            card->showMappings = [safe] (const juce::String& uid) { if (safe && safe->openMappings) safe->openMappings (uid); };
            list.addAndMakeVisible (*card); cards.push_back (std::move (card));
        }
        addLFO.setEnabled (cards.size() < (size_t) scrr::params::maxModulators); addEnv.setEnabled (addLFO.isEnabled()); addRandom.setEnabled (addLFO.isEnabled()); resized(); repaint();
    }
    void resized() override
    {
        close.setBounds (getWidth() - 110, 12, 96, 27);
        addLFO.setBounds (14, 50, 100, 27); addEnv.setBounds (122, 50, 152, 27);
        addRandom.setBounds (282, 50, 110, 27);
        view.setBounds (14, 88, getWidth() - 28, juce::jmax (10, getHeight() - 102));
        const int width = view.getWidth() - 14, columns = width >= 690 ? 2 : 1, cardWidth = (width - 10 * (columns - 1)) / columns;
        const int cardHeight = juce::jlimit (292, 344, view.getHeight());
        for (size_t i = 0; i < cards.size(); ++i) cards[i]->setBounds ((int) (i % (size_t) columns) * (cardWidth + 10), (int) (i / (size_t) columns) * (cardHeight + 10), cardWidth, cardHeight);
        list.setSize (width, juce::jmax (view.getHeight(), ((int) cards.size() + columns - 1) / columns * (cardHeight + 10) - 10));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black()); g.setColour (Theme::blue()); g.fillRect (0, 0, 4, getHeight());
        Theme::text (g, "MODULATION / " + juce::String (cards.size()) + " SOURCES", { 14, 8, getWidth() - 136, 33 }, 22, Theme::white(), true);
        Theme::text (g, "Assign from a parameter's right-click menu.", { 406, 50, getWidth() - 420, 27 }, 11, Theme::dim());
        if (cards.empty())
        {
            Theme::text (g, "SET THE SIGNAL IN MOTION", { 28, 121, getWidth() - 56, 40 }, 28, Theme::yellow(), true);
            Theme::text (g, "Add LFO, Envelope Follower or Random. Each source can modulate multiple parameters.", { 28, 174, getWidth() - 56, 48 }, 13, Theme::dim());
        }
    }
private:
    SpectralCrrptProcessor& processor; uint64_t revision {};
    juce::TextButton addLFO { "+ LFO" }, addEnv { "+ ENVELOPE" }, addRandom { "+ RANDOM" }, close { "EFFECTS >" };
    juce::Viewport view; juce::Component list; std::vector<std::unique_ptr<ModulatorCard>> cards;
};
}

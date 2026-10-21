// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "FrequencyScale.h"
#include "Theme.h"
#include "AssignableSlider.h"
#include "PromptLanguage.h"
#include "../PluginProcessor.h"
#include "components/CurveCoordinateMenu.h"
#include <deque>

namespace scrr::gui {
inline juce::String modulationHint (const char* english, const char* chinese)
{ return PromptLanguage::get() == PromptLanguage::Language::chinese ? juce::String::fromUTF8 (chinese) : juce::String (english); }
class ModulationCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    scrr::params::ModulationSource source;
    std::function<void(scrr::params::ModulationSource)> changed;
    std::function<void(juce::Component&, uint32_t)> nodeMenu;
    void setPreviewSource (const scrr::params::ModulationSource& next) { previewSource = next; repaint(); }
    ModulationCurve() { setTooltip ("Double-click empty space: add. Double-click a point: delete (endpoints stay). Right-click a point: assign X or Y to a Macro or modulation source. Drag a hollow midpoint to bend the segment."); }
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
        if (! source.envelope && ! source.random && source.shape == 5 && previewSource.uid == source.uid && ! previewSource.points.empty())
        {
            juce::Path effective;
            for (int i = 0; i <= 256; ++i)
            {
                const double x = (double) i / 256;
                const float y = i == 256 ? previewSource.points.back().y : previewSource.valueAt (x);
                const juce::Point<float> p (a.getX() + (float) x * a.getWidth(), a.getBottom() - y * a.getHeight());
                if (i == 0) effective.startNewSubPath (p); else effective.lineTo (p);
            }
            g.setColour (Theme::yellow()); g.strokePath (effective, juce::PathStrokeType (1.3f));
        }
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
        if (e.mods.isPopupMenu())
        {
            selected = curveSegment = -1;
            if (source.shape != 5) return;
            const int point = nearest (e.position);
            if (point >= 0 && nodeMenu) nodeMenu (*this, source.points[(size_t) point].id);
            return;
        }
        ensureCustom(); selected = -1; curveSegment = -1;
        const int handle = nearestHandle (e.position);
        if (handle >= 0 && ! e.mods.isPopupMenu()) { beginCurve (handle, e); return; }
        selected = nearest (e.position);
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
        source.points.push_back ({ x, juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight()), 0, source.allocatePointId() });
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
    scrr::params::ModulationSource previewSource;
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
        for (int i = 0; i <= 8; ++i) source.points.push_back ({ (float) i / 8, source.valueAt (juce::jmin (.999999, (double) i / 8)), 0, source.allocatePointId() });
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
// A separate handle keeps assigning an output distinct from editing a source
// parameter or dragging its row to reorder the management list.
class ModulationOutputHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModulationOutputHandle()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (modulationHint ("Drag this output over a CH tab, then an effect and its parameter. Release to assign.",
                                   u8"拖动此输出，悬停 CH 标签与效果模块以预览，再松开到参数上建立调制映射。"));
    }
    std::function<void(juce::Point<int>, bool)> drag;
    void setOutput (float next, bool on)
    { level = juce::jlimit (0.0f, 1.0f, next); active = on; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto colour = active ? Theme::yellow() : Theme::inactive();
        g.setColour (isMouseOverOrDragging() ? colour.withAlpha (.22f) : Theme::field()); g.fillRect (getLocalBounds());
        g.setColour (colour); g.drawRect (getLocalBounds(), dragging ? 2 : 1);
        auto a = getLocalBounds().reduced (5, 3);
        Theme::text (g, "OUTPUT", a.withTrimmedBottom (6), 9, colour, true, juce::Justification::centred);
        auto meter = a.removeFromBottom (3); g.setColour (colour.withAlpha (.25f)); g.fillRect (meter);
        g.setColour (colour); g.fillRect (meter.withWidth ((int) std::round (level * (float) meter.getWidth())));
    }
    void mouseDown (const juce::MouseEvent& event) override { dragging = false; popup = event.mods.isPopupMenu(); }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (popup || (event.getDistanceFromDragStart() < 6 && ! dragging)) return;
        dragging = true; repaint(); const auto callback = drag; if (callback) callback (event.getScreenPosition(), false);
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        const bool finish = dragging && ! popup; dragging = false; repaint();
        const auto callback = drag; if (finish && callback) callback (event.getScreenPosition(), true);
    }
private:
    bool active { true }, dragging {}, popup {}; float level {};
};
class ModulatorCard : public juce::Component
{
public:
    ModulatorCard (SpectralCrrptProcessor& p, scrr::params::ModulationSource s) : processor (p), source (std::move (s))
    {
        setComponentID ("modulator-" + source.uid);
        title.setText (source.name, juce::dontSendNotification); title.setEditable (false, true); title.setFont (Theme::font (19, true));
        title.onTextChange = [this] { source.name = title.getText().trim().substring (0, 32); if (source.name.isEmpty()) source.name = source.envelope ? "Audio Follower" : source.random ? "Random" : "LFO"; submit(); }; addAndMakeVisible (title);
        enabled.setToggleState (source.enabled, juce::dontSendNotification); enabled.onClick = [this] { source.enabled = enabled.getToggleState(); submit(); }; addAndMakeVisible (enabled);
        remove.onClick = [this] { if (removeSource) removeSource (source.uid); }; addAndMakeVisible (remove);
        mappings.onClick = [this] { if (showMappings) showMappings (source.uid); }; addAndMakeVisible (mappings);
        output.setComponentID ("modulation-output-" + source.uid);
        output.drag = [this] (juce::Point<int> screen, bool finish) { if (dragOutput) dragOutput (source.uid, screen, finish); };
        addAndMakeVisible (output);
        curve.source = source; curve.changed = [this] (auto next) { source = std::move (next); shape.setSelectedItemIndex (source.shape, juce::dontSendNotification); submit(); }; addAndMakeVisible (curve);
        curve.nodeMenu = [this] (juce::Component& target, uint32_t point)
        {
            const auto safe = juce::Component::SafePointer<ModulatorCard> (this);
            const auto targetUid = source.uid;
            showCurveCoordinateMenu (target, source.canMapCoordinate (point, true), source.canMapCoordinate (point, false),
                "Time", "Value", [safe, targetUid, point] (bool xAxis)
                {
                    if (! safe || safe->source.uid != targetUid || ! safe->source.canMapCoordinate (point, xAxis)) return;
                    if (safe->mappingMenu) safe->mappingMenu (safe->curve, targetUid, scrr::params::curveCoordinateParameter (point, xAxis));
                });
        };
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
        configureMapping (first, source.envelope ? "gain" : "rate");
        configureMapping (second, source.envelope ? "attack" : source.random ? "seed" : "phase");
        if (source.envelope || source.random) configureMapping (third, source.random ? "smooth" : "release");
    }
    std::function<void(const juce::String&)> removeSource, showMappings;
    std::function<void(const juce::String&, juce::Point<int>, bool)> dragOutput;
    const juce::String& uid() const { return source.uid; }
    std::function<void(juce::Component&, const juce::String&, const juce::Identifier&)> mappingMenu;
    int extraHeight() const { return mappingHeight(); }
    void tick (float value, float phase, const scrr::params::ModulationSource& current, const std::vector<scrr::params::MacroMapping>& routes,
               const scrr::params::ModulationValues& values)
    {
        // Base edits in the mapping panel must also reach the card's cached source.
        source = current;
        if (! title.isBeingEdited()) title.setText (source.name, juce::dontSendNotification);
        enabled.setToggleState (source.enabled, juce::dontSendNotification);
        output.setOutput (value, source.enabled);
        const int oldHeight = mappingHeight();
        for (auto* slider : { &first, &second, &third })
        {
            if (slider->getComponentID().isEmpty()) continue;
            const juce::Identifier parameter (slider->getProperties()["sourceParameter"].toString());
            std::vector<scrr::params::MacroMapping> assigned;
            for (const auto& route : routes) if (route.channel == scrr::params::MacroMapping::modulationChannel && route.uid == source.uid && route.parameter == parameter) assigned.push_back (route);
            slider->setMappings (assigned); slider->refreshModulation (values);
            if (! slider->isMouseButtonDown()) slider->setValue (source.parameterValue (parameter), juce::dontSendNotification);
        }
        if (mappingHeight() != oldHeight) resized();
        curve.source = source;
        if (! source.envelope && ! source.random && source.shape == 5)
        {
            auto effective = source;
            for (auto& point : effective.points)
            {
                point.x = (float) processor.getEffectiveValue (scrr::params::MacroMapping::modulationChannel, source.uid,
                    scrr::params::curveCoordinateParameter (point.id, true), point.x);
                point.y = (float) processor.getEffectiveValue (scrr::params::MacroMapping::modulationChannel, source.uid,
                    scrr::params::curveCoordinateParameter (point.id, false), point.y);
            }
            scrr::params::TransferCurve::constrainCoordinates (effective.points.data(), effective.points.size(), false, false, source.points.data());
            curve.setPreviewSource (effective);
        }
        curve.tick (value, phase); repaint();
    }
    void resized() override
    {
        auto a = getLocalBounds().reduced (12); auto header = a.removeFromTop (26);
        remove.setBounds (header.removeFromRight (26)); header.removeFromRight (6); enabled.setBounds (header.removeFromRight (66)); title.setBounds (header);
        a.removeFromTop (6); auto outputRow = a.removeFromTop (26); output.setBounds (outputRow.removeFromRight (86));
        a.removeFromTop (8); curve.setBounds (a.removeFromTop (juce::jmax (68, getHeight() - (source.random ? 268 : 224) - mappingHeight()))); a.removeFromTop (8);
        auto choice = a.removeFromTop (26); mappings.setBounds (choice.removeFromRight (70)); choice.removeFromRight (8); shape.setBounds (choice);
        a.removeFromTop (19); auto row = a.removeFromTop (25 + juce::jmax (first.modulationHeight(), second.modulationHeight())); first.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (8)); second.setBounds (row);
        if (source.envelope || source.random) { a.removeFromTop (19); third.setBounds (a.removeFromTop (25 + third.modulationHeight())); }
        if (! source.envelope)
        {
            first.setVisible (! source.sync); division.setVisible (source.sync); division.setBounds (first.getBounds());
            a.removeFromTop (8); row = a.removeFromTop (25); sync.setBounds (row.removeFromLeft (98)); retrigger.setBounds (row);
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel()); g.setColour (source.enabled ? (source.envelope ? Theme::yellow() : source.random ? Theme::red() : Theme::blue()) : Theme::inactive()); g.fillRect (0, 0, 4, getHeight());
        Theme::text (g, source.envelope ? "AUDIO FOLLOWER" : source.random ? "RANDOM" : "LFO",
                     { 12, 44, juce::jmax (0, getWidth() - 118), 26 }, 11, Theme::dim(), true);
        auto label = [&] (const juce::String& text, const juce::Component& control) { Theme::text (g, text, control.getBounds().translated (0, -17).withHeight (15), 9, Theme::dim(), true); };
        label (source.envelope ? "GAIN" : source.sync ? "BEAT DIVISION" : "RATE", first); label (source.envelope ? "RISE" : source.random ? "SEED" : "PHASE", second);
        if (source.envelope || source.random) label (source.random ? "SMOOTH" : "FALL", third);
    }
private:
    int mappingHeight() const { return juce::jmax (first.modulationHeight(), second.modulationHeight()) + third.modulationHeight(); }
    void configureMapping (AssignableSlider& slider, const char* key)
    {
        const juce::Identifier parameter (key);
        slider.presetResetValue = [this, parameter, fallback = slider.getDoubleClickReturnValue()]
        { return processor.getModuleResetValue (scrr::params::MacroMapping::modulationChannel, source.uid, parameter, fallback); };
        slider.getProperties().set ("sourceParameter", key);
        if (slider.getComponentID().isEmpty()) slider.setComponentID ("modulation-" + juce::String (key));
        slider.mappingMenu = [this, &slider, parameter] { if (mappingMenu) mappingMenu (slider, source.uid, parameter); };
        slider.changeMapping = [this] (auto mapping) { processor.updateMacroMapping (mapping); };
        slider.unmapRoute = [this] (auto mapping) { processor.removeMacroMapping (mapping.channel, mapping.uid, mapping.parameter, mapping.macro); };
    }
    void submit() { curve.source = source; processor.updateModulator (source); repaint(); }
    SpectralCrrptProcessor& processor; scrr::params::ModulationSource source;
    juce::Label title; juce::ToggleButton enabled { "ON" }, sync { "SYNC" }, retrigger { "RESTART" };
    juce::TextButton remove { "x" }, mappings { "MAPS" };
    ModulationOutputHandle output;
    ModulationCurve curve; juce::ComboBox shape, division; AssignableSlider first, second, third;
};
class ModulatorRow : public juce::Component
{
public:
    explicit ModulatorRow (scrr::params::ModulationSource initial) : source (std::move (initial))
    {
        setComponentID ("modulator-row-" + source.uid); setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        output.setComponentID ("modulation-row-output-" + source.uid);
        output.drag = [this] (juce::Point<int> screen, bool finish) { if (dragOutput) dragOutput (source.uid, screen, finish); };
        addAndMakeVisible (output);
    }
    std::function<void(const juce::String&)> choose;
    std::function<void(const juce::String&, juce::Point<int>, bool)> reorder, dragOutput;
    const juce::String& uid() const { return source.uid; }
    void update (const scrr::params::ModulationSource& next, int index, bool isSelected, float value)
    { source = next; number = index; selected = isSelected; output.setOutput (value, source.enabled); repaint(); }
    void setDragged (bool value) { setAlpha (value ? .35f : 1.0f); }
    void resized() override { output.setBounds (getWidth() - 63, 10, 57, 33); }
    void paint (juce::Graphics& g) override
    {
        const auto accent = source.enabled ? Theme::yellow() : Theme::inactive();
        g.fillAll (selected ? accent : isMouseOver (true) ? Theme::panel().brighter (.13f) : Theme::panel());
        const auto ink = selected ? Theme::black() : source.enabled ? Theme::white() : Theme::dim();
        Theme::text (g, juce::String (number + 1).paddedLeft ('0', 2), { 9, 5, 24, 26 }, 16, ink, true);
        Theme::text (g, source.name, { 36, 4, juce::jmax (0, getWidth() - 103), 26 }, 12, ink, true);
        Theme::text (g, source.envelope ? "AUDIO FOLLOWER" : source.random ? "RANDOM" : "LFO",
                     { 36, 30, juce::jmax (0, getWidth() - 103), 16 }, 8, ink, true);
        g.setColour (selected ? Theme::red() : Theme::line()); g.fillRect (0, 0, 3, getHeight() - 2);
        g.setColour (Theme::black()); g.fillRect (0, getHeight() - 2, getWidth(), 2);
    }
    void mouseDown (const juce::MouseEvent& event) override { dragging = false; popup = event.mods.isPopupMenu(); }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (popup || (event.getDistanceFromDragStart() < 6 && ! dragging)) return;
        dragging = true; if (reorder) reorder (source.uid, event.getScreenPosition(), false);
    }
    void mouseUp (const juce::MouseEvent& event) override
    {
        if (popup) return;
        const bool moved = dragging; dragging = false;
        if (moved) { if (reorder) reorder (source.uid, event.getScreenPosition(), true); }
        else if (choose) choose (source.uid);
    }
private:
    scrr::params::ModulationSource source; ModulationOutputHandle output;
    int number {}; bool selected {}, dragging {}, popup {};
};
class ModulationPage : public juce::Component
{
public:
    explicit ModulationPage (SpectralCrrptProcessor& p) : processor (p)
    {
        setComponentID ("modulation-page");
        view.setViewedComponent (&list, false); view.setScrollBarsShown (true, false); addAndMakeVisible (view);
        detailView.setViewedComponent (&details, false); detailView.setScrollBarsShown (true, false); addAndMakeVisible (detailView);
        addLFO.setComponentID ("add-lfo"); addEnv.setComponentID ("add-envelope"); addRandom.setComponentID ("add-random");
        addLFO.onClick = [this] { addSource (false, false); };
        addEnv.onClick = [this] { addSource (true, false); };
        addRandom.onClick = [this] { addSource (false, true); };
        close.onClick = [this] { setVisible (false); if (closed) closed(); };
        for (auto* button : { &addLFO, &addEnv, &addRandom, &close }) addAndMakeVisible (button);
    }
    ~ModulationPage() override { view.setViewedComponent (nullptr, false); detailView.setViewedComponent (nullptr, false); }
    std::function<void()> closed;
    std::function<void(const juce::String&)> openMappings;
    std::function<void(juce::Component&, const juce::String&, const juce::Identifier&)> mappingMenu;
    // EditorContent owns the cross-page preview and drop transaction. It must
    // retain this page while a handle is being dragged, including while hidden.
    std::function<void(const juce::String&, juce::Point<int>, bool)> dragOutput;
    const juce::String& selectedSource() const noexcept { return selectedUid; }
    bool isOutputDragActive() const noexcept { return outputDragging; }
    void open() { refresh(); setVisible (true); toFront (true); }
    void selectSource (const juce::String& uid)
    {
        if (std::none_of (cards.begin(), cards.end(), [&] (const auto& card) { return card->uid() == uid; })) return;
        selectedUid = uid; detailView.setViewPosition (0, 0); updateSelection(); resized(); repaint();
    }
    void cancelReorder()
    {
        draggingUid.clear(); insertion = -1; removeTarget = false;
        for (auto& row : rows) row->setDragged (false);
        repaint();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && draggingUid.isNotEmpty())
        { cancelledUid = draggingUid; cancelReorder(); return true; }
        return false;
    }
    void tick()
    {
        if (! isVisible()) return;
        if (revision != processor.getModulatorRevision())
        {
            if (outputDragging) return;
            cancelReorder(); refresh();
        }
        const auto values = processor.getModulationValues();
        const auto routes = processor.getMacroMappings();
        const auto sources = processor.getModulators(); bool layout = false;
        for (size_t i = 0; i < cards.size() && i < sources.size(); ++i)
        {
            rows[i]->update (sources[i], (int) i, sources[i].uid == selectedUid, values[i + 8]);
            if (sources[i].uid != selectedUid) continue;
            const int previous = cards[i]->extraHeight();
            cards[i]->tick (values[i + 8], processor.getModulatorPhase ((int) i), sources[i], routes, values);
            layout |= previous != cards[i]->extraHeight();
        }
        if (layout) resized();
        if (draggingUid.isNotEmpty() && view.getBounds().contains (dragPoint))
        {
            const int edge = dragPoint.y - view.getY();
            const int delta = edge < 24 ? -9 : edge > view.getHeight() - 24 ? 9 : 0;
            if (delta != 0)
            { view.setViewPosition (0, view.getViewPositionY() + delta); dragRow (draggingUid, localPointToGlobal (dragPoint), false); }
        }
        repaint();
    }
    void refresh()
    {
        // The mouse source keeps delivering events to the original output
        // handle even when EditorContent temporarily hides this page.
        if (outputDragging) return;
        cards.clear(); rows.clear(); revision = processor.getModulatorRevision();
        const auto safe = juce::Component::SafePointer<ModulationPage> (this);
        const auto sources = processor.getModulators();
        if (std::none_of (sources.begin(), sources.end(), [this] (const auto& source) { return source.uid == selectedUid; }))
            selectedUid = sources.empty() ? juce::String {} : sources.front().uid;
        for (const auto& source : sources)
        {
            auto card = std::make_unique<ModulatorCard> (processor, source);
            card->removeSource = [safe] (const juce::String& uid)
            { juce::MessageManager::callAsync ([safe, uid] { if (safe) { safe->processor.removeModulator (uid); safe->refresh(); } }); };
            card->showMappings = [safe] (const juce::String& uid) { if (safe && safe->openMappings) safe->openMappings (uid); };
            card->mappingMenu = [safe] (auto& target, const auto& uid, const auto& parameter) { if (safe && safe->mappingMenu) safe->mappingMenu (target, uid, parameter); };
            card->dragOutput = [safe] (const auto& uid, auto screen, bool finish) { if (safe) safe->forwardOutputDrag (uid, screen, finish); };
            details.addChildComponent (*card); cards.push_back (std::move (card));
            auto row = std::make_unique<ModulatorRow> (source);
            row->choose = [safe] (const auto& uid) { if (safe) safe->selectSource (uid); };
            row->reorder = [safe] (const auto& uid, auto screen, bool finish) { if (safe) safe->dragRow (uid, screen, finish); };
            row->dragOutput = [safe] (const auto& uid, auto screen, bool finish) { if (safe) safe->forwardOutputDrag (uid, screen, finish); };
            list.addAndMakeVisible (*row); rows.push_back (std::move (row));
        }
        addLFO.setEnabled (cards.size() < (size_t) scrr::params::maxModulators); addEnv.setEnabled (addLFO.isEnabled()); addRandom.setEnabled (addLFO.isEnabled());
        updateSelection(); resized(); repaint();
    }
    void resized() override
    {
        const int left = juce::jmin (230, getWidth() / 2);
        close.setBounds (getWidth() - 108, 4, 100, 27);
        addLFO.setBounds (0, 37, (left - 6) / 2, 28); addRandom.setBounds ((left + 6) / 2, 37, (left - 6) / 2, 28);
        addEnv.setBounds (0, 71, left, 28);
        view.setBounds (0, 140, left, juce::jmax (10, getHeight() - 140));
        detailView.setBounds (left + 10, 41, juce::jmax (10, getWidth() - left - 10), juce::jmax (10, getHeight() - 41));
        const int rowWidth = juce::jmax (100, view.getWidth() - view.getScrollBarThickness());
        for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * 56, rowWidth, 56);
        list.setSize (rowWidth, juce::jmax (view.getHeight(), (int) rows.size() * 56));
        const int cardWidth = juce::jmax (120, detailView.getWidth() - detailView.getScrollBarThickness());
        int detailHeight = detailView.getHeight();
        const auto sources = processor.getModulators();
        for (size_t i = 0; i < cards.size(); ++i)
        {
            const bool random = i < sources.size() && sources[i].random;
            const int height = juce::jmax (detailView.getHeight(), (random ? 370 : 338) + cards[i]->extraHeight());
            cards[i]->setBounds (0, 0, cardWidth, height);
            if (cards[i]->uid() == selectedUid) detailHeight = height;
        }
        details.setSize (cardWidth, detailHeight);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black());
        Theme::text (g, "MODULATION / " + juce::String (cards.size()), { 0, 0, 230, 31 }, 18, Theme::white(), true);
        Theme::text (g, modulationHint ("Independent sources", u8"独立调制源"),
                     { detailView.getX() + 8, 0, juce::jmax (0, detailView.getWidth() - 122), 31 }, 11, Theme::dim());
        auto hint = juce::Rectangle<int> (0, 104, view.getWidth(), 32);
        g.setFont (Theme::font (9)); g.setColour (Theme::dim());
        g.drawFittedText (modulationHint ("Drag rows to reorder / right to delete.\nDrag OUTPUT onto an effect parameter.",
                                         u8"拖动列表排序／拖到右侧删除。\n拖动 OUTPUT 到效果参数以映射。"), hint, juce::Justification::centredLeft, 2);
        if (cards.empty())
        {
            Theme::text (g, "SET THE SIGNAL IN MOTION", detailView.getBounds().reduced (18).withHeight (48), 20, Theme::yellow(), true);
            g.setFont (Theme::font (12)); g.setColour (Theme::dim());
            g.drawFittedText (modulationHint ("Add LFO, Audio Follower or Random.\nSelect a source to edit it. Each output can modulate multiple effect parameters.",
                                             u8"添加 LFO、Audio Follower 或 Random。\n点击左侧调制源编辑详情；每个输出都可映射到多个效果参数。"),
                              detailView.getBounds().reduced (18).withTrimmedTop (60), juce::Justification::topLeft, 5);
        }
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (draggingUid.isEmpty()) return;
        const auto target = deleteArea();
        g.setColour (Theme::black().withAlpha (.80f)); g.fillRect (detailView.getBounds());
        g.setColour (removeTarget ? Theme::red() : Theme::panel()); g.fillRect (target);
        g.setColour (Theme::red()); g.drawRect (target, removeTarget ? 3 : 1);
        Theme::text (g, modulationHint (removeTarget ? "RELEASE TO REMOVE SOURCE" : "DROP HERE TO REMOVE SOURCE",
                                        removeTarget ? u8"松开以删除调制源" : u8"拖到此处以删除调制源"),
                     target.reduced (12).withTrimmedBottom (32), 14, removeTarget ? Theme::black() : Theme::red(), true, juce::Justification::centred);
        Theme::text (g, modulationHint ("Outside a target / Esc: cancel", u8"移出目标区域／Esc：取消"),
                     target.reduced (12).withTrimmedTop (54), 10, removeTarget ? Theme::black() : Theme::dim(), false, juce::Justification::centred);
        if (insertion >= 0)
        {
            const int y = juce::jlimit (view.getY() + 2, view.getBottom() - 3, view.getY() + insertion * 56 - view.getViewPositionY());
            g.setColour (Theme::yellow()); g.fillRect (view.getX(), y - 2, view.getWidth(), 4);
        }
    }
private:
    void forwardOutputDrag (juce::String uid, juce::Point<int> screen, bool finish)
    {
        outputDragging = ! finish;
        const auto callback = dragOutput; if (callback) callback (uid, screen, finish);
    }
    void addSource (bool follower, bool random)
    {
        const auto uid = processor.addModulator (follower, random); if (uid.isEmpty()) return;
        selectedUid = uid; refresh(); view.setViewPosition (0, list.getHeight()); detailView.setViewPosition (0, 0);
    }
    void updateSelection()
    {
        const auto sources = processor.getModulators(); const auto values = processor.getModulationValues();
        for (size_t i = 0; i < cards.size(); ++i)
        {
            cards[i]->setVisible (cards[i]->uid() == selectedUid);
            if (i < sources.size()) rows[i]->update (sources[i], (int) i, sources[i].uid == selectedUid, values[i + 8]);
        }
    }
    juce::Rectangle<int> deleteArea() const
    { return detailView.getBounds().withSizeKeepingCentre (juce::jmax (80, juce::jmin (280, detailView.getWidth() - 28)), 112); }
    void dragRow (const juce::String& uid, juce::Point<int> screen, bool finish)
    {
        if (uid == cancelledUid) { if (finish) cancelledUid.clear(); return; }
        if (revision != processor.getModulatorRevision()) { cancelledUid = uid; cancelReorder(); return; }
        if (draggingUid.isEmpty())
        {
            if (finish) return;
            draggingUid = uid; setWantsKeyboardFocus (true); grabKeyboardFocus();
            for (auto& row : rows) row->setDragged (row->uid() == uid);
        }
        dragPoint = getLocalPoint (nullptr, screen); removeTarget = deleteArea().contains (dragPoint);
        insertion = view.getBounds().contains (dragPoint)
            ? juce::jlimit (0, (int) rows.size(), (dragPoint.y - view.getY() + view.getViewPositionY() + 28) / 56) : -1;
        if (finish)
        {
            const bool remove = removeTarget; const int slot = insertion; const juce::String moved = uid;
            cancelReorder(); const auto safe = juce::Component::SafePointer<ModulationPage> (this);
            juce::MessageManager::callAsync ([safe, moved, slot, remove]
            {
                if (! safe) return;
                if (remove) safe->processor.removeModulator (moved);
                else if (slot >= 0) safe->processor.moveModulator (moved, slot);
                else return;
                safe->refresh();
            });
        }
        repaint();
    }
    SpectralCrrptProcessor& processor; uint64_t revision {};
    juce::TextButton addLFO { "+ LFO" }, addEnv { "+ AUDIO FOLLOWER" }, addRandom { "+ RANDOM" }, close { "EFFECTS >" };
    juce::Viewport view, detailView; juce::Component list, details;
    std::vector<std::unique_ptr<ModulatorCard>> cards;
    std::vector<std::unique_ptr<ModulatorRow>> rows;
    juce::String selectedUid, draggingUid, cancelledUid;
    juce::Point<int> dragPoint; int insertion { -1 }; bool removeTarget {}, outputDragging {};
};
}

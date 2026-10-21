// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "EffectCatalog.h"
#include "../modules/GenericCurves.h"
#include "../modules/SpectralEqualizer.h"
#include "../parameters/TransferCurve.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace scrr::gui {
// Vector artwork and a quantised halftone layer. The GUI owns all animation;
// neither the artwork nor its timer touches DSP state.
class EffectCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    EffectCard() { setInterceptsMouseClicks (true, false); setComponentID ("effect-card"); }
    void setEffect (const juce::String& id, const juce::String& name, int stage, bool on = true)
    { type = id; title = name; number = stage; active = on; repaint(); }
    void setParameterPreview (const juce::ValueTree& state)
    {
        if (values.isEquivalentTo (state)) return;
        values = state.createCopy(); repaint();
    }
    void animate (float time, float entrance)
    {
        const float nextPhase = active ? time : phase;
        if (juce::approximatelyEqual (phase, nextPhase) && juce::approximatelyEqual (reveal, entrance)) return;
        phase = nextPhase; reveal = entrance; repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds(); const auto accent = active ? effectColour (type) : Theme::inactive();
        const auto ink = accent == Theme::blue() ? Theme::white() : Theme::black();
        g.fillAll (accent);
        const int artWidth = juce::jlimit (126, 240, getWidth() * 36 / 100);
        auto art = bounds.removeFromRight (artWidth).toFloat();
        g.saveState(); g.reduceClipRegion (art.toNearestInt());
        const float offset = (1 - reveal) * 40;
        art = art.translated (offset, 0);
        g.setColour (Theme::black().withAlpha (.18f));
        const float wave = phase * .85f;
        for (int y = 6; y < getHeight(); y += 9)
            for (int x = (int) art.getX(); x < getWidth() + 10; x += 9)
            {
                const float dot = 1.2f + 1.5f * (.5f + .5f * std::sin (wave + (float) x * .034f + (float) y * .061f));
                g.fillEllipse ((float) x + 1.3f * std::sin (wave + (float) y * .04f), (float) y, dot, dot);
            }
        const float cy = art.getCentreY(), w = art.getWidth(), h = art.getHeight();
        g.setColour (ink);
        if (type == "SpectralClipper")
        {
            const float threshold = (float) values.getProperty ("threshold", -12.0f);
            const float ceiling = juce::jlimit (.1f, .88f, (threshold + 96) / 110);
            const float base = art.getBottom() - 17, available = h - 34;
            for (int i=0;i<18;++i)
            {
                const float x = art.getX()+10+(float)i*(w-20)/18;
                const float before = .18f + .72f * std::abs (std::sin ((float)i*.47f+phase*2.1f));
                const float after = juce::jmin (before,ceiling);
                g.setColour (ink.withAlpha (.20f)); g.fillRect (x, base-before*available, 5.0f, before*available);
                g.setColour (ink); g.fillRect (x, base-after*available, 5.0f, after*available);
            }
            g.setColour (ink); g.drawLine (art.getX()+6,base-ceiling*available,art.getRight()-6,base-ceiling*available,1.4f);
        }
        else if (type == "SpectralEqualizer")
        {
            scrr::dsp::SpectralEqResponse response; response.configure (scrr::dsp::readSpectralEqBands (values), 48000);
            juce::Path curve;
            for (int i=0;i<=100;++i)
            {
                const float x=art.getX()+8+(w-16)*(float)i/100;
                const double frequency=20*std::pow (1000.0,(double)i/100);
                const float y=cy-(float)juce::jlimit (-24.0,24.0,response.decibelsAt(frequency))*(h-32)/48;
                if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);
            }
            g.setColour(ink.withAlpha(.25f));g.drawLine(art.getX()+8,cy,art.getRight()-8,cy,1);
            g.setColour(ink);g.strokePath(curve,juce::PathStrokeType(3));
            const float scanner=art.getX()+8+(w-16)*cycle(3.0f);
            g.setColour(ink.withAlpha(.18f));g.fillRect(scanner-1,12.0f,2.0f,h-24);
            motionDot (g, curve, ink, 3.0f);
        }
        else if (type == "Reverb" || type == "Delay" || type == "Compressor" || type == "Distortion")
            drawGeneric (g, art, ink);
        else drawMotionArtwork (g, art, ink);
        g.restoreState();
        // An angled label cuts across the illustration, keeping the title clean.
        auto textArea = bounds.reduced (14, 0);
        Theme::text (g, juce::String (number).paddedLeft ('0', 2) + "  /  " + effectFolder (type) + (active ? "" : "  /  OFF"), textArea.withY (12).withHeight (17), 10, ink, true);
        auto display = title.toUpperCase();
        const bool compact = getHeight() < 115;
        const int split = display.indexOfChar (' ');
        if (! compact && split > 0) display = display.substring (0, split) + "\n" + display.substring (split + 1);
        g.setColour (ink); g.setFont (Theme::displayFont (compact ? 32.0f : 46.0f));
        g.drawFittedText (display, textArea.withY (30).withHeight (compact ? getHeight() - 35 : 80),
                          juce::Justification::centredLeft, compact ? 1 : 2, .68f);
        if (! compact) Theme::text (g, effectMotto (type), textArea.withY (getHeight() - 27).withHeight (20), 9, ink, true);
        g.setColour (Theme::black()); g.fillRect (0, getHeight() - 3, getWidth(), 3);
    }
private:
    float cycle (float seconds, float offset = 0) const
    { const float value = phase / seconds + offset; return value - std::floor (value); }
    static float ease (float t)
    { t = juce::jlimit (0.0f, 1.0f, t); return t * t * t * (t * (t * 6 - 15) + 10); }
    void motionDot (juce::Graphics& g, const juce::Path& path, juce::Colour ink, float seconds) const
    {
        const float length = path.getLength();
        for (int i = 3; i >= 0; --i)
        {
            const auto point = path.getPointAlongPath (cycle (seconds, (float) -i * .028f) * length);
            const float radius = i == 0 ? 5.0f : 3.0f;
            g.setColour (ink.withAlpha (i == 0 ? 1.0f : .32f - (float) i * .07f));
            g.fillEllipse (point.x - radius, point.y - radius, radius * 2, radius * 2);
        }
    }
    void drawMotionArtwork (juce::Graphics& g, juce::Rectangle<float> art, juce::Colour ink) const
    {
        const auto folder = effectFolder (type);
        const float cx = art.getCentreX(), cy = art.getCentreY(), w = art.getWidth(), h = art.getHeight();
        const float radius = juce::jmin (w * .34f, h * .34f);
        const float tau = juce::MathConstants<float>::twoPi;
        g.setColour (ink);
        if (type == "MagnitudePhaseSwap")
        {
            // Two distinct dimensions exchange places along opposing arcs. The
            // 140 ms holds make each hand-off legible before the next half-turn.
            const float rx = w * .31f, ry = h * .23f;
            auto angleAt = [&] (float secondsBack)
            {
                const float half = cycle (2.8f, -secondsBack / 2.8f) * 2;
                return juce::MathConstants<float>::pi * (std::floor (half) + ease ((half - std::floor (half) - .10f) / .80f));
            };
            g.setColour (ink.withAlpha (.22f)); g.drawEllipse (cx - rx, cy - ry, rx * 2, ry * 2, 1.5f);
            for (int item = 0; item < 2; ++item)
                for (int tail = 4; tail >= 0; --tail)
                {
                    const float angle = angleAt ((float) tail * .055f) + (float) item * juce::MathConstants<float>::pi;
                    const auto p = juce::Point<float> (cx + std::cos (angle) * rx, cy + std::sin (angle) * ry);
                    const float size = tail == 0 ? 18.0f : 12.0f;
                    g.setColour (ink.withAlpha (tail == 0 ? 1.0f : .26f - (float) tail * .04f));
                    if (item == 0) g.fillEllipse (p.x - size * .5f, p.y - size * .5f, size, size);
                    else
                    {
                        juce::Path tile; tile.addRectangle (p.x - size * .5f, p.y - size * .5f, size, size);
                        tile.applyTransform (juce::AffineTransform::rotation (angle * .5f, p.x, p.y)); g.fillPath (tile);
                    }
                }
            g.setColour (ink.withAlpha (.55f)); g.drawLine (cx - 7, cy, cx + 7, cy, 2);
            g.drawLine (cx, cy - 7, cx, cy + 7, 2);
        }
        else if (type == "ComplexRotation")
        {
            const float angle = cycle (3.6f) * tau;
            g.setColour (ink.withAlpha (.30f)); g.drawEllipse (cx - radius, cy - radius, radius * 2, radius * 2, 1.5f);
            g.drawLine (cx - radius - 8, cy, cx + radius + 8, cy, 1); g.drawLine (cx, cy - radius - 8, cx, cy + radius + 8, 1);
            juce::Path sweep; sweep.addCentredArc (cx, cy, radius, radius, 0, angle - .95f, angle, true);
            g.setColour (ink.withAlpha (.45f)); g.strokePath (sweep, juce::PathStrokeType (4));
            const auto tip = juce::Point<float> (cx + std::sin (angle) * radius, cy - std::cos (angle) * radius);
            g.setColour (ink); g.drawArrow ({ { cx, cy }, tip }, 3, 9, 10); g.fillEllipse (cx - 4, cy - 4, 8, 8);
            g.fillEllipse (tip.x - 6, tip.y - 6, 12, 12);
            g.setColour (ink.withAlpha (.45f)); g.drawLine (tip.x, tip.y, tip.x, cy, 1.5f);
        }
        else if (folder == "PHASE")
        {
            const float tilt = (float) (std::abs (type.hashCode() % 5) - 2) * .2f;
            for (int i = 0; i < 3; ++i)
            {
                const float rx = w * (.35f - (float) i * .055f), ry = h * (.27f - (float) i * .045f);
                const float angle = cycle (3.4f + (float) i * .7f, (float) i * .27f) * tau;
                const auto transform = juce::AffineTransform::rotation (tilt + (float) i * .45f, cx, cy);
                juce::Path orbit; orbit.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2); orbit.applyTransform (transform);
                g.setColour (ink.withAlpha (.25f)); g.strokePath (orbit, juce::PathStrokeType (1.5f));
                const auto p = juce::Point<float> (cx + std::cos (angle) * rx, cy + std::sin (angle) * ry).transformedBy (transform);
                g.setColour (ink.withAlpha (i == 0 ? 1.0f : .65f)); g.drawLine (cx, cy, p.x, p.y, 1.5f);
                const float r = i == 0 ? 7.0f : 4.0f; g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
            }
        }
        else if (type == "FrequencyRemap")
        {
            const auto curve = scrr::params::TransferCurve::decode (values["remapCurve"].toString());
            const auto plot = art.reduced (13, 15);
            const auto point = [&] (float t) { return juce::Point<float> (plot.getX() + t * plot.getWidth(), plot.getBottom() - (float) curve.evaluate (t) * plot.getHeight()); };
            g.setColour (ink.withAlpha (.22f));
            for (int i = 0; i <= 4; ++i)
            {
                const float t = (float) i / 4;
                g.drawLine (plot.getX() + t * plot.getWidth(), plot.getY(), plot.getX() + t * plot.getWidth(), plot.getBottom(), 1);
                g.drawLine (plot.getX(), plot.getY() + t * plot.getHeight(), plot.getRight(), plot.getY() + t * plot.getHeight(), 1);
            }
            juce::Path path;
            for (int i = 0; i <= 96; ++i) { const auto p = point ((float) i / 96); if (i == 0) path.startNewSubPath (p); else path.lineTo (p); }
            g.setColour (ink); g.strokePath (path, juce::PathStrokeType (2.5f));
            for (int i = 0; i < 3; ++i)
            {
                const auto p = point (cycle (3.2f, (float) i / 3));
                g.setColour (ink.withAlpha (.3f)); g.drawLine (p.x, plot.getBottom(), p.x, p.y, 1.5f); g.drawLine (plot.getX(), p.y, p.x, p.y, 1.5f);
                g.setColour (ink); g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
            }
        }
        else if (scrr::dsp::isPartialEffect (type))
        {
            const auto plot = art.reduced (12, 14);
            const float movement = .5f + .5f * std::sin (phase * 1.7f), target = .25f + .5f * movement;
            if (type == "PartialPGrid" || type == "PartialFGrid")
                for (int i = 0; i <= 6; ++i)
                {
                    const float t = type == "PartialPGrid" ? std::log2 (1.0f + (float) i) / std::log2 (7.0f) : (float) i / 6;
                    g.setColour (ink.withAlpha (.23f)); g.drawLine (plot.getX() + t * plot.getWidth(), plot.getY(), plot.getX() + t * plot.getWidth(), plot.getBottom(), 1);
                }
            if (type == "PartialCentroid")
            { g.setColour (ink.withAlpha (.5f)); g.drawLine (plot.getX() + target * plot.getWidth(), plot.getY(), plot.getX() + target * plot.getWidth(), plot.getBottom(), 2); }
            for (int i = 0; i < 8; ++i)
            {
                const float source = ((float) i + .5f) / 8;
                float destination = source;
                if (type == "PartialStretcher") destination = std::pow (source, .55f + movement);
                else if (type == "PartialStiffString") destination = source * (1 + movement * source * source) / (1 + movement);
                else if (type == "PartialCentroid") destination = source + (target - source) * (.3f + .55f * movement);
                else if (type == "PartialPGrid") { const float pitched = std::log2 (1 + source * 6); destination = std::round (pitched * (2 + movement * 3)) / (2 + movement * 3) / std::log2 (7.0f); }
                else { const float spacing = 3 + movement * 5; destination = std::round (source * spacing) / spacing; }
                const float x1 = plot.getX() + source * plot.getWidth(), x2 = plot.getX() + juce::jlimit (0.0f, 1.0f, destination) * plot.getWidth();
                const float height = plot.getHeight() * (.25f + .13f * std::sin (phase * 2.3f - (float) i * .6f));
                g.setColour (ink.withAlpha (.3f)); g.drawLine (x1, plot.getY() + 4, x2, plot.getBottom() - height, 1.5f);
                g.setColour (ink); g.fillEllipse (x1 - 2.5f, plot.getY(), 5, 5);
                g.fillRect (x2 - 2.5f, plot.getBottom() - height, 5.0f, height);
                g.fillEllipse (x2 - 4, plot.getBottom() - height - 4, 8, 8);
            }
        }
        else if (type == "SpectralMirror")
        {
            for (int i = 0; i < 6; ++i)
            {
                const float t = cycle (2.8f, (float) i / 6), d = 8 + t * w * .35f;
                g.setColour (ink.withAlpha (.20f + .8f * std::sin (juce::MathConstants<float>::pi * t)));
                juce::Path chevron; chevron.startNewSubPath (cx - d, h * .17f); chevron.lineTo (cx - d * .22f, cy); chevron.lineTo (cx - d, h * .83f);
                g.strokePath (chevron, juce::PathStrokeType (3.5f));
                chevron.applyTransform (juce::AffineTransform::scale (-1, 1, cx, cy)); g.strokePath (chevron, juce::PathStrokeType (3.5f));
            }
            g.setColour (ink); g.drawLine (cx, h * .15f, cx, h * .85f, 2);
        }
        else if (type == "SpectralBloom")
        {
            for (int i = 0; i < 12; ++i)
            {
                const float a = (float) i * tau / 12 + cycle (9.6f) * tau;
                const float swell = .5f + .5f * std::sin (phase * 2.4f - (float) i * .45f);
                const float length = radius * (.7f + swell * .35f);
                g.setColour (ink.withAlpha (.50f + .5f * swell));
                g.drawLine (cx + std::cos (a) * 13, cy + std::sin (a) * 13, cx + std::cos (a) * length, cy + std::sin (a) * length, 5);
            }
            g.setColour (ink); g.fillEllipse (cx - 8, cy - 8, 16, 16);
        }
        else if (type == "SpectralComb" || folder == "HARMONICS")
        {
            for (int i = 0; i < 12; ++i)
            {
                const float x = art.getX() + 10 + (float) i * (w - 20) / 12;
                float height = type == "SpectralComb" ? (i % 2 ? .16f : .66f) : .66f / (1 + (float) i * .16f);
                if (type == "HarmonicSculpt" && i % 2) height *= .3f;
                if (type == "SpectralContrast") height = i % 3 == 0 ? .72f : .12f;
                const float pulse = .5f + .5f * std::cos (phase * 2.8f - (float) i * .55f);
                height *= h * (.65f + .35f * pulse);
                g.setColour (ink); g.fillRect (x, cy - height * .5f, 7.0f, height);
                if (pulse > .65f) g.fillRect (x, cy - height * .5f - 8, 7.0f, 3.0f);
            }
        }
        else if (folder == "TEXTURE / TIME")
        {
            for (int layer = 5; layer >= 0; --layer)
            {
                const float progress = cycle (3.2f, (float) layer / 6);
                const float visibility = std::sin (juce::MathConstants<float>::pi * progress);
                const float x = art.getX() + 8 + progress * (w - 80), y = cy - h * .25f + std::sin (progress * tau) * h * .08f;
                g.setColour (ink.withAlpha (visibility * .85f)); g.drawRect (x, y, 60.0f, h * .50f, 3.0f);
                if (type == "TemporalSmear" || type == "TimeDesync")
                    g.drawLine (x + 9, y + h * .28f, x + 51, y + h * (.20f + .08f * std::sin (progress * tau)), 2);
                else g.fillRect (x + 14, y + h * .13f, 8.0f, h * .24f);
            }
        }
        else if (folder == "DESTRUCTION")
        {
            const int seed = std::abs (type.hashCode() % 97);
            for (int row = 0; row < 5; ++row) for (int column = 0; column < 8; ++column)
            {
                if ((column * 11 + row * 7 + seed) % 5 < 2) continue;
                const float progress = cycle (2.4f, (float) row * .13f);
                const float shift = ease ((progress - .16f) / .62f);
                const float at = std::fmod ((float) column + shift * 2, 8.0f);
                const float size = (type == "SpectralJPEG" ? 12.0f : 7.0f + (float) ((column + row) % 3) * 2);
                const float crush = .55f + .45f * std::abs (std::cos (progress * tau));
                g.setColour (ink.withAlpha (.40f + crush * .6f));
                g.fillRect (art.getX() + 8 + at * (w - 20) / 8, h * .12f + (float) row * h * .15f, size, size * crush);
            }
        }
        else
        {
            const float slope = (float) (std::abs (type.hashCode() % 3) + 1);
            juce::Path wave;
            for (int i = 0; i <= 100; ++i)
            {
                const float t = (float) i / 100, x = art.getX() + 8 + t * (w - 16);
                const float warp = type == "FrequencyWarp" ? t * t : t;
                const float bend = type == "FrequencyDrift" ? .4f * std::sin (phase * 1.2f + t * 5) : 0;
                const float y = cy + std::sin (warp * tau * slope - cycle (2.6f) * tau + bend) * h * .28f;
                if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
            g.setColour (ink.withAlpha (.25f)); g.drawLine (art.getX() + 8, cy, art.getRight() - 8, cy, 1);
            g.setColour (ink); g.strokePath (wave, juce::PathStrokeType (3.5f));
            motionDot (g, wave, ink, 3.8f);
        }
    }
    float value (const char* key, float fallback) const { return (float) values.getProperty (key, fallback); }
    void drawGeneric (juce::Graphics& g, juce::Rectangle<float> art, juce::Colour ink)
    {
        const auto plot = art.reduced (12, 18).withTrimmedBottom (12);
        const auto x = [&] (float t) { return plot.getX() + t * plot.getWidth(); };
        const auto y = [&] (float t) { return plot.getBottom() - t * plot.getHeight(); };
        const auto caption = [&] (const juce::String& text)
        { Theme::text (g, text, art.toNearestInt().reduced (12, 0).withTrimmedTop (art.getHeight() > 30 ? (int) art.getHeight() - 22 : 0).withHeight (17), 9, ink, true); };
        g.setColour (ink.withAlpha (.24f));
        g.drawLine (x (0), y (0), x (1), y (0), 1); g.drawLine (x (0), y (0), x (0), y (1), 1);
        if (type == "Compressor")
        {
            const float threshold = value ("threshold", -18), ratio = value ("ratio", 4), knee = value ("knee", 6);
            const float makeup = juce::Decibels::decibelsToGain (value ("makeup", 0)), mix = value ("mix", 100) * .01f;
            g.drawLine (x (0), y (0), x (1), y (1), 1);
            const float thresholdX = (threshold + 60) / 72;
            g.drawLine (x (thresholdX), y (0), x (thresholdX), y (1), 1);
            juce::Path transfer;
            for (int i = 0; i <= 192; ++i)
            {
                const float t = (float) i / 192, db = t * 72 - 60;
                const float gain = (1 - mix) + mix * makeup * juce::Decibels::decibelsToGain (scrr::dsp::compressorReduction (db, threshold, ratio, knee));
                const float out = juce::jlimit (0.0f, 1.0f, (db + juce::Decibels::gainToDecibels (gain) + 60) / 72);
                if (i == 0) transfer.startNewSubPath (x (t), y (out)); else transfer.lineTo (x (t), y (out));
            }
            g.setColour (ink); g.strokePath (transfer, juce::PathStrokeType (3));
            motionDot (g, transfer, ink, 2.8f);
            // Detector timing response, on a 2.2 s axis: attack then release.
            juce::Path timing; float level = 0;
            for (int i = 0; i <= 160; ++i)
            {
                const float t = (float) i / 160;
                const float coefficient = std::exp (-13.75f / value (i < 32 ? "attack" : "release", i < 32 ? 10.0f : 100.0f));
                level = coefficient * level + (1 - coefficient) * (i < 32 ? 1.0f : 0.0f);
                const float yy = y (.06f + .18f * level);
                if (i == 0) timing.startNewSubPath (x (t), yy); else timing.lineTo (x (t), yy);
            }
            g.setColour (ink.withAlpha (.6f)); g.strokePath (timing, juce::PathStrokeType (1.5f));
            caption (juce::String (threshold, 1) + " dB / " + juce::String (ratio, 1) + ":1");
        }
        else if (type == "Distortion")
        {
            const int mode = (int) value ("mode", 0);
            const float drive = juce::Decibels::decibelsToGain (value ("drive", 6)), bias = value ("bias", 0);
            const float output = juce::Decibels::decibelsToGain (value ("output", -6)), mix = value ("mix", 100) * .01f;
            g.drawLine (x (0), y (.5f), x (1), y (.5f), 1); g.drawLine (x (.5f), y (0), x (.5f), y (1), 1);
            juce::Path wave;
            for (int i = 0; i <= 512; ++i)
            {
                const float t = (float) i / 512, input = t * 2 - 1;
                const float result = input * (1 - mix) + output * mix * scrr::dsp::distortionShape (input * drive + bias, mode);
                const float yy = y (juce::jlimit (0.0f, 1.0f, result * .42f + .5f));
                if (i == 0) wave.startNewSubPath (x (t), yy); else wave.lineTo (x (t), yy);
            }
            g.setColour (ink); g.strokePath (wave, juce::PathStrokeType (2.5f));
            motionDot (g, wave, ink, 2.4f);
            // Tone is a subsequent filter, not part of the static waveshaper.
            g.setColour (ink.withAlpha (.45f)); g.fillRect (x (0), y (0) - 2, plot.getWidth() * std::log (value ("tone", 16000) / 200) / std::log (100.0f), 2.0f);
            const char* names[] { "TUBE", "OVERDRIVE", "SIN FOLD", "LIN FOLD" };
            caption (juce::String (names[juce::jlimit (0, 3, mode)]) + " / " + juce::String (value ("drive", 6), 1) + " dB");
        }
        else if (type == "Reverb")
        {
            const float size = value ("size", 60) * .01f, decay = value ("decay", 2.5f), damping = value ("damping", 45) * .01f;
            const int mode = (int) value ("mode", 0);
            const float width = .30f + .68f * size, height = (mode == 0 ? .40f : mode == 1 ? .90f : .65f) * (.55f + .45f * size);
            const float pre = value ("predelay", 20) / 200 * .12f;
            for (int i = 0; i < 10; ++i)
            {
                const float t = (float) i / 9, scale = 1 - .8f * t;
                const float strength = std::exp (-6.90776f * t * 3 / decay);
                g.setColour (ink.withAlpha (juce::jlimit (.05f, .95f, strength * (.95f - damping * .2f))));
                g.drawRect (x (.5f - width * scale * .5f), y (.53f + height * scale * .5f - pre), plot.getWidth() * width * scale, plot.getHeight() * height * scale, 2.5f);
            }
            juce::Path tail;
            for (int i = 0; i <= 120; ++i)
            {
                const float t = (float) i / 120;
                const float level = t < pre ? 0 : std::exp (-6.90776f * (t - pre) * 12 / decay);
                if (i == 0) tail.startNewSubPath (x (t), y (.02f + .22f * level)); else tail.lineTo (x (t), y (.02f + .22f * level));
            }
            g.setColour (ink); g.strokePath (tail, juce::PathStrokeType (1.5f));
            motionDot (g, tail, ink, 3.2f);
            const char* names[] { "PLATE", "HALL", "ROOM" };
            caption (juce::String (names[juce::jlimit (0, 2, mode)]) + " / " + juce::String (decay, 2) + " s");
        }
        else
        {
            const int mode = (int) value ("mode", 0);
            const bool sync = value ("sync", 0) > .5f;
            const int division = juce::jlimit (0, 20, (int) value ("division", (float) scrr::dsp::defaultDelayDivision));
            const float ms = scrr::dsp::delayTimeMs (sync, division, value ("time", 300), value ("tempo", 120));
            const float spacing = ms / 8000, feedback = value ("feedback", 35) * .01f, mix = value ("mix", 30) * .01f;
            g.drawLine (x (0), y (.5f), x (1), y (.5f), 1);
            for (int i = 0; i < 96; ++i)
            {
                const float time = (float) (i + 1) * spacing; if (time > 1) break;
                const float amplitude = std::pow (feedback, (float) i) * (.25f + .75f * mix); if (amplitude < .003f) break;
                const int taps = mode == 1 ? 3 : 1;
                for (int tap = 0; tap < taps; ++tap)
                {
                    const float at = mode == 1 ? ((float) i + (float) (tap + 1) / 3) * spacing : time;
                    const float tapWeight = mode == 1 ? (tap == 0 ? .55f : tap == 1 ? .3f : .15f) : 1;
                    const float amount = amplitude * tapWeight;
                    g.setColour (ink.withAlpha (juce::jlimit (.12f, 1.0f, amplitude)));
                    const float centre = mode == 2 ? .5f : .1f;
                    const float end = mode == 2 ? centre + (i % 2 == 0 ? .45f : -.45f) * amount : centre + .8f * amount;
                    g.drawLine (x (at), y (centre), x (at), y (end), 3);
                }
            }
            const float sweep = cycle (2.6f) * juce::jmin (1.0f, spacing * 8);
            g.setColour (ink.withAlpha (.30f)); g.drawLine (x (sweep), y (0), x (sweep), y (1), 1.5f);
            g.setColour (ink); g.fillEllipse (x (sweep) - 4, y (mode == 2 ? .5f : .1f) - 4, 8, 8);
            const char* names[] { "NORMAL", "TAP", "PING-PONG" };
            caption (juce::String (names[juce::jlimit (0, 2, mode)]) + " / "
                     + (sync ? scrr::dsp::delayDivisions()[division] + " SYNC / " : juce::String()) + juce::String (ms, 0) + " ms");
        }
    }
    juce::ValueTree values;
    juce::String type, title; int number { 1 }; float phase {}, reveal { 1 };
    bool active { true };
};
} // namespace scrr::gui

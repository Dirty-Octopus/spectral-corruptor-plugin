// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "EffectCatalog.h"
#include "../modules/GenericCurves.h"
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
        const int tick = (int) (phase * 3.0f);
        for (int y = 6; y < getHeight(); y += 9)
            for (int x = (int) art.getX(); x < getWidth() + 10; x += 9)
            {
                const float dot = (float) (1 + ((x / 9 + y / 9 + tick) % 3));
                g.fillEllipse ((float) x, (float) y, dot, dot);
            }
        const auto folder = effectFolder (type);
        const float cx = art.getCentreX(), cy = art.getCentreY(), w = art.getWidth(), h = art.getHeight();
        g.setColour (ink);
        if (type == "Reverb" || type == "Delay" || type == "Compressor" || type == "Distortion")
            drawGeneric (g, art, ink);
        else if (type == "SpectralMirror")
        {
            for (int i = 0; i < 6; ++i)
            {
                float d = (float) i * 12 + 7;
                juce::Path p; p.startNewSubPath (cx - d, 16); p.lineTo (cx - d * .22f, cy); p.lineTo (cx - d, h - 16);
                g.strokePath (p, juce::PathStrokeType (3));
                p.applyTransform (juce::AffineTransform::scale (-1, 1, cx, cy)); g.strokePath (p, juce::PathStrokeType (3));
            }
        }
        else if (type == "SpectralBloom")
        {
            for (int i = 0; i < 14; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 14 + .06f * std::sin (phase);
                const float length = 42 + (float) (i % 3) * 9;
                g.drawLine (cx + std::cos (a) * 14, cy + std::sin (a) * 14, cx + std::cos (a) * length, cy + std::sin (a) * length, 5);
            }
            g.fillEllipse (cx - 8, cy - 8, 16, 16);
        }
        else if (type == "SpectralComb" || folder == "HARMONICS")
        {
            for (int i = 0; i < 12; ++i)
            {
                const float x = art.getX() + 10 + (float) i * (w - 20) / 12;
                float height = type == "SpectralComb" ? (i % 2 ? 18.0f : 91.0f)
                    : (90.0f / (1 + (float) i * .2f)) * (type == "HarmonicSculpt" && i % 2 ? .3f : 1.0f);
                if (type == "SpectralContrast") height = i % 3 == 0 ? 97.0f : 12.0f;
                g.fillRect (x, cy - height * .5f, 7.0f, height);
            }
        }
        else if (folder == "DESTRUCTION")
        {
            const int seed = std::abs (type.hashCode() % 97);
            for (int y = 0; y < 6; ++y) for (int x = 0; x < 9; ++x)
            {
                if ((x * 11 + y * 7 + seed) % 5 < 2) continue;
                const float s = type == "SpectralJPEG" ? 15.0f : 8.0f + (float) ((x + y) % 3) * 3;
                g.fillRect (art.getX() + (float) x * 21, 7.0f + (float) y * 21, s, s);
            }
        }
        else if (folder == "PHASE")
        {
            const int variant = std::abs (type.hashCode() % 5);
            for (int i = 0; i < 4; ++i)
            {
                auto oval = juce::Rectangle<float> (cx - 61 + (float) i * 6, cy - 42 + (float) i * 3, 122 - (float) i * 12, 84 - (float) i * 6);
                juce::Path p; p.addEllipse (oval);
                p.applyTransform (juce::AffineTransform::rotation ((float) variant * .3f + (float) i * .4f, cx, cy));
                g.strokePath (p, juce::PathStrokeType (2.5f));
            }
            g.fillRect (cx - 2, cy - 52, 4.0f, 104.0f);
        }
        else if (folder == "TEXTURE / TIME")
        {
            const int shift = std::abs (type.hashCode() % 8);
            for (int i = 0; i < 5; ++i)
            {
                g.setColour (ink.withAlpha (1.0f - (float) i * .14f));
                g.drawRect (art.getX() + 10 + (float) i * 23, 18.0f + (float) ((i + shift) % 3) * 8, 70.0f, 85.0f, 4.0f);
            }
        }
        else
        {
            juce::Path p;
            const float slope = (float) (std::abs (type.hashCode() % 4) + 1);
            for (int i = 0; i < 48; ++i)
            {
                const float x = art.getX() + (float) i / 47 * w;
                const float y = cy + std::sin ((float) i * .16f * slope) * (h * .29f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (4));
            g.drawLine (art.getX(), cy, art.getRight(), cy, 1);
        }
        g.restoreState();
        // An angled label cuts across the illustration, keeping the title clean.
        auto textArea = bounds.reduced (14, 0);
        Theme::text (g, juce::String (number).paddedLeft ('0', 2) + "  /  " + effectFolder (type) + (active ? "" : "  /  OFF"), textArea.withY (12).withHeight (17), 10, ink, true);
        auto display = title.toUpperCase(); const int split = display.indexOfChar (' ');
        if (split > 0) display = display.substring (0, split) + "\n" + display.substring (split + 1);
        g.setColour (ink); g.setFont (Theme::displayFont (46));
        g.drawFittedText (display, textArea.withY (30).withHeight (80), juce::Justification::centredLeft, 2, .68f);
        Theme::text (g, effectMotto (type), textArea.withY (getHeight() - 27).withHeight (20), 9, ink, true);
        g.setColour (Theme::black()); g.fillRect (0, getHeight() - 3, getWidth(), 3);
    }
private:
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
            const char* names[] { "PLATE", "HALL", "ROOM" };
            caption (juce::String (names[juce::jlimit (0, 2, mode)]) + " / " + juce::String (decay, 2) + " s");
        }
        else
        {
            const int mode = (int) value ("mode", 0);
            const float spacing = value ("time", 300) / 6000, feedback = value ("feedback", 35) * .01f, mix = value ("mix", 30) * .01f;
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
            const char* names[] { "NORMAL", "TAP", "PING-PONG" };
            caption (juce::String (names[juce::jlimit (0, 2, mode)]) + " / " + juce::String (value ("time", 300), 0) + " ms");
        }
    }
    juce::ValueTree values;
    juce::String type, title; int number { 1 }; float phase {}, reveal { 1 };
    bool active { true };
};
} // namespace scrr::gui

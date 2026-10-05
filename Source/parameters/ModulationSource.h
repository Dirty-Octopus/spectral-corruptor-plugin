// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>

namespace scrr::params {
inline constexpr int maxModulators = 128;
inline constexpr int modulationValueCount = 8 + maxModulators;
using ModulationValues = std::array<float, modulationValueCount>;
struct CurvePoint { float x {}, y {}, curve {}; };
struct ModulationSource
{
    juce::String uid, name;
    bool envelope {}, random {}, enabled { true }, sync {}, retrigger {};
    int shape {}, division { 4 }, input {}, seed { 1 };
    float rate { 1 }, phase {}, gain {}, attack { 10 }, release { 150 }, smooth {};
    std::vector<CurvePoint> points { { 0, 0 }, { .5f, 1 }, { 1, 0 } };
    inline static constexpr std::array<double, 12> beats { 32, 16, 8, 4, 2, 1, .5, .25, .125, .0625, 1.0 / 3, 1.0 / 6 };
    static juce::StringArray divisionNames() { return { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/8 T", "1/16 T" }; }
    float valueAt (double position) const noexcept
    {
        if (! std::isfinite (position)) return 0;
        const float x = (float) (position - std::floor (position));
        if (random)
        {
            double cycle = std::fmod (std::floor (position), 4294967296.0);
            if (cycle < 0) cycle += 4294967296.0;
            const auto index = (uint32_t) cycle;
            const auto hash = [this] (uint32_t n)
            {
                n ^= (uint32_t) seed * 0x9e3779b9u + 0x85ebca6bu;
                n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
                return (float) (n >> 8) / 16777215.0f;
            };
            const float a = hash (index), b = hash (index + 1u);
            return shape == 0 ? a : a + (b - a) * x * x * (3 - 2 * x);
        }
        switch (shape)
        {
            case 0: return .5f - .5f * std::cos (juce::MathConstants<float>::twoPi * x);
            case 1: return 1 - std::abs (2 * x - 1);
            case 2: return x;
            case 3: return 1 - x;
            case 4: return x < .5f ? 0.0f : 1.0f;
            default:
                for (size_t i = 1; i < points.size(); ++i)
                    if (x <= points[i].x)
                    {
                        const auto a = points[i - 1], b = points[i];
                        const float t = juce::jlimit (0.0f, 1.0f, (x - a.x) / juce::jmax (.00001f, b.x - a.x));
                        const float bend = a.curve < 0 ? std::pow (t, 1 - 7 * a.curve) : 1 - std::pow (1 - t, 1 + 7 * a.curve);
                        return a.y + (b.y - a.y) * bend;
                    }
                return points.empty() ? 0 : points.back().y;
        }
    }
    juce::ValueTree state() const
    {
        juce::ValueTree tree ("SOURCE");
        tree.setProperty ("uid", uid, nullptr); tree.setProperty ("name", name, nullptr);
        tree.setProperty ("type", envelope ? "envelope" : random ? "random" : "lfo", nullptr);
        tree.setProperty ("seed", seed, nullptr); tree.setProperty ("smooth", smooth, nullptr);
        tree.setProperty ("enabled", enabled, nullptr); tree.setProperty ("sync", sync, nullptr); tree.setProperty ("retrigger", retrigger, nullptr);
        tree.setProperty ("shape", shape, nullptr); tree.setProperty ("division", division, nullptr); tree.setProperty ("input", input, nullptr);
        tree.setProperty ("rate", rate, nullptr); tree.setProperty ("phase", phase, nullptr); tree.setProperty ("gain", gain, nullptr);
        tree.setProperty ("attack", attack, nullptr); tree.setProperty ("release", release, nullptr);
        for (auto point : points)
        { juce::ValueTree p ("POINT"); p.setProperty ("x", point.x, nullptr); p.setProperty ("y", point.y, nullptr); p.setProperty ("curve", point.curve, nullptr); tree.appendChild (p, nullptr); }
        return tree;
    }
    static ModulationSource read (const juce::ValueTree& tree)
    {
        ModulationSource m; m.uid = tree["uid"].toString().substring (0, 64); m.envelope = tree["type"].toString() == "envelope";
        m.random = tree["type"].toString() == "random";
        m.name = tree.getProperty ("name", m.envelope ? "Envelope" : m.random ? "Random" : "LFO").toString().substring (0, 32);
        m.enabled = (bool) tree.getProperty ("enabled", true); m.sync = (bool) tree["sync"]; m.retrigger = (bool) tree["retrigger"];
        const auto number = [&] (const char* key, float fallback, float low, float high)
        { const float v = (float) tree.getProperty (key, fallback); return std::isfinite (v) ? juce::jlimit (low, high, v) : fallback; };
        m.shape = (int) number ("shape", 0, 0, m.random ? 1.0f : 5.0f); m.division = (int) number ("division", 4, 0, 11); m.input = (int) number ("input", 0, 0, 5);
        m.rate = number ("rate", 1, .01f, m.random ? 128.0f : 40.0f); m.phase = number ("phase", 0, 0, 360); m.seed = (int) number ("seed", 1, 0, 999999);
        m.smooth = number ("smooth", 0, 0, 2000);
        m.gain = number ("gain", 0, -24, 48); m.attack = number ("attack", 10, .1f, 2000); m.release = number ("release", 150, 1, 5000);
        std::vector<CurvePoint> points;
        for (auto p : tree)
        {
            if (! p.hasType ("POINT") || points.size() >= 64) continue;
            const float x = (float) p["x"], y = (float) p["y"];
            if (! std::isfinite (x) || ! std::isfinite (y)) continue;
            const float curve = (float) p["curve"];
            points.push_back ({ juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y), std::isfinite (curve) ? juce::jlimit (-1.0f, 1.0f, curve) : 0 });
        }
        std::sort (points.begin(), points.end(), [] (auto a, auto b) { return a.x < b.x; });
        std::vector<CurvePoint> clean;
        for (auto p : points) if (clean.empty() || p.x - clean.back().x >= .001f) clean.push_back (p);
        if (clean.size() >= 2) { clean.front().x = 0; clean.back().x = 1; m.points = std::move (clean); }
        return m;
    }
};
}

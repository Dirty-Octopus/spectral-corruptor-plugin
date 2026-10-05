// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <cmath>
#include "ModulationSource.h"

namespace scrr::params {
struct MacroMapping
{
    static constexpr int modulationChannel = 5;
    enum Mode { unipolarRight, unipolarLeft, bipolarLeftLow, bipolarRightLow };
    int macro {}, channel {}, mode { unipolarRight };
    juce::String uid, label, unit, sourceUid, sourceName;
    int sourceIndex { -1 };
    bool sourceEnabled { true };
    int valueIndex() const noexcept { return sourceUid.isEmpty() ? macro : sourceIndex; }
    juce::String sourceLabel() const { return sourceUid.isEmpty() && sourceName.isEmpty() ? "M" + juce::String (macro + 1) : sourceName; }
    bool sameSource (const MacroMapping& other) const { return sourceUid == other.sourceUid && (sourceUid.isNotEmpty() || macro == other.macro); }
    juce::Identifier parameter;
    double low {}, high { 1 }, centre {}, minimum {}, maximum { 1 }, step {};
    bool logarithmic {};
    // Normalised modulation offsets relative to the editable base parameter.
    // Separate endpoints also preserve asymmetric mappings from older presets.
    double startOffset {}, endOffset {};
    bool relative { true };
    double normalise (double v) const noexcept
    {
        v = juce::jlimit (minimum, maximum, v);
        return logarithmic && minimum > 0
            ? std::log (v / minimum) / std::log (maximum / minimum)
            : (v - minimum) / juce::jmax (1.0e-12, maximum - minimum);
    }
    double denormalise (double t) const noexcept
    {
        t = juce::jlimit (0.0, 1.0, t);
        double result = logarithmic && minimum > 0 ? minimum * std::pow (maximum / minimum, t) : minimum + t * (maximum - minimum);
        if (step > 0) result = minimum + std::round ((result - minimum) / step) * step;
        return juce::jlimit (minimum, maximum, result);
    }
    bool bipolar() const noexcept { return mode >= bipolarLeftLow; }
    double offset (float position) const noexcept
    {
        const double t = std::isfinite (position) ? juce::jlimit (0.0, 1.0, (double) position) : 0;
        return bipolar() ? (t <= .5 ? startOffset * (1 - 2 * t) : endOffset * (2 * t - 1))
                         : startOffset + (endOffset - startOffset) * t;
    }
    void setDepth (double depth, bool bi) noexcept
    {
        endOffset = juce::jlimit (-1.0, 1.0, depth); startOffset = bi ? -endOffset : 0;
        mode = bi ? bipolarLeftLow : unipolarRight; relative = true;
        syncRange();
    }
    void syncRange() noexcept
    { low = denormalise (normalise (centre) + startOffset); high = denormalise (normalise (centre) + endOffset); }
    double fromBase (double base, double modulation) const noexcept { return denormalise (normalise (base) + modulation); }
    bool sameTarget (const MacroMapping& other) const
    { return channel == other.channel && uid == other.uid && parameter == other.parameter; }
    double interpolate (double a, double b, double t) const noexcept
    { return logarithmic && a > 0 && b > 0 ? std::exp (std::log (a) + (std::log (b) - std::log (a)) * t) : a + (b - a) * t; }
    double value (float position) const noexcept
    {
        if (relative) return fromBase (centre, offset (position));
        double t = std::isfinite (position) ? juce::jlimit (0.0, 1.0, (double) position) : 0;
        if (mode == unipolarLeft || mode == bipolarRightLow) t = 1 - t;
        double result = mode <= unipolarLeft ? interpolate (low, high, t)
                         : t <= .5 ? interpolate (low, centre, t * 2) : interpolate (centre, high, (t - .5) * 2);
        if (step > 0) result = minimum + std::round ((result - minimum) / step) * step;
        return juce::jlimit (minimum, maximum, result);
    }
    juce::ValueTree state() const
    {
        juce::ValueTree tree ("MAP");
        tree.setProperty ("source", sourceUid, nullptr);
        tree.setProperty ("macro", macro, nullptr); tree.setProperty ("channel", channel, nullptr);
        tree.setProperty ("uid", uid, nullptr); tree.setProperty ("parameter", parameter.toString(), nullptr);
        tree.setProperty ("low", low, nullptr); tree.setProperty ("high", high, nullptr);
        tree.setProperty ("centre", centre, nullptr); tree.setProperty ("mode", mode, nullptr);
        tree.setProperty ("version", 2, nullptr);
        tree.setProperty ("startOffset", startOffset, nullptr); tree.setProperty ("endOffset", endOffset, nullptr);
        return tree;
    }
};
using MacroValues = std::array<float, 8>;
}

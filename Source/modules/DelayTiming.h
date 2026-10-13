// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace scrr::dsp {
inline constexpr float maximumDelayMs = 8000;
inline constexpr int defaultDelayDivision = 13; // quarter note
inline const juce::StringArray& delayDivisions()
{
    static const juce::StringArray choices {
        "1/64 T", "1/64", "1/64 D", "1/32 T", "1/32", "1/32 D", "1/16 T", "1/16", "1/16 D",
        "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2 T", "1/2", "1/2 D", "1/1 T", "1/1", "1/1 D" };
    return choices;
}
inline float delayTimeMs (bool sync, int division, float milliseconds, double bpm) noexcept
{
    if (! sync) return std::isfinite (milliseconds) ? juce::jlimit (1.0f, 2000.0f, milliseconds) : 300.0f;
    division = juce::jlimit (0, 20, division);
    if (! std::isfinite (bpm) || bpm <= 0) bpm = 120;
    constexpr std::array<double, 3> modifiers { 2.0 / 3.0, 1.0, 1.5 };
    const double beats = std::ldexp (1.0 / 16.0, division / 3) * modifiers[(size_t) (division % 3)];
    return (float) juce::jlimit (1.0, (double) maximumDelayMs, 60000.0 * beats / bpm);
}
}

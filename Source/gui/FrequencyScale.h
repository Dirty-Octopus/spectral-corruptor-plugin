// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace scrr::gui {
// Display/gesture scaling only. Saved Hz values and modulation offsets retain
// their existing meaning. Positive ranges have equal spacing per octave;
// ranges including zero use a signed log1p curve with a 20 Hz transition.
inline juce::NormalisableRange<double> frequencySliderRange (double minimum, double maximum, double step)
{
    const bool positive = minimum > 0;
    const auto toLog = [positive] (double hz)
    { return positive ? std::log (hz) : std::copysign (std::log1p (std::abs (hz) / 20.0), hz); };
    const auto fromLog = [positive] (double value)
    { return positive ? std::exp (value) : std::copysign (20.0 * std::expm1 (std::abs (value)), value); };
    juce::NormalisableRange<double> range (minimum, maximum,
        [toLog, fromLog] (double low, double high, double position)
        {
            // Exact endpoints also preserve the HIGH control's FULL sentinel.
            if (position <= 0) return low;
            if (position >= 1) return high;
            const double a = toLog (low), b = toLog (high);
            return juce::jlimit (low, high, fromLog (a + position * (b - a)));
        },
        [toLog] (double low, double high, double hz)
        {
            if (hz <= low) return 0.0;
            if (hz >= high) return 1.0;
            const double a = toLog (low), b = toLog (high);
            return juce::jlimit (0.0, 1.0, (toLog (hz) - a) / (b - a));
        });
    range.interval = step;
    return range;
}
inline void setupFrequencyScale (juce::Slider& slider)
{
    slider.setNormalisableRange (frequencySliderRange (slider.getMinimum(), slider.getMaximum(), slider.getInterval()));
}
}

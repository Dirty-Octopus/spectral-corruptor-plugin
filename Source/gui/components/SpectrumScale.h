// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <algorithm>
#include <cmath>

namespace scrr::gui {
struct SpectrumScale
{
    static float frequency (float position)
    { return 20.0f * std::pow (1000.0f, std::clamp (position, 0.0f, 1.0f)); }
    static float position (float frequency)
    { return std::log (std::clamp (frequency, 20.0f, 20000.0f) / 20.0f) / std::log (1000.0f); }
    static int bin (float frequency, int bins, double sampleRate)
    { return std::clamp ((int) std::round (frequency * 2.0 * (double) (bins - 1) / std::max (1.0, sampleRate)), 0, bins - 1); }
};
} // namespace scrr::gui

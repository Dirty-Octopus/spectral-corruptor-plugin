// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace scrr::dsp {
// Shared by the audio processor and parameter-driven artwork.
inline float compressorReduction (float inputDb, float threshold, float ratio, float knee) noexcept
{
    const float over = inputDb - threshold;
    if (knee > 0 && std::abs (over) < knee * .5f)
        return (1 / ratio - 1) * std::pow (over + knee * .5f, 2.0f) / (2 * knee);
    return juce::jmax (0.0f, over) * (1 / ratio - 1);
}
inline float distortionShape (float x, int mode) noexcept
{
    switch (mode)
    {
        case 0: return std::tanh (x + .35f) - std::tanh (.35f);
        case 1: return std::tanh (x);
        case 2: return std::sin (x * juce::MathConstants<float>::halfPi);
        default: return 1 - std::abs (std::fmod (std::fmod (x + 1, 4.0f) + 4, 4.0f) - 2);
    }
}
}

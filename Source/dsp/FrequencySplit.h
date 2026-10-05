// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace scrr::dsp {
// Shared by the audio routing and its spectrum overlay. The GUI samples the
// same FFT bins as the spectrogram, including DC/Nyquist and crossfades.
class FrequencySplit
{
public:
    FrequencySplit (double hostRate, int count, float crossfade)
        : slices (std::clamp (count, 1, 64)), cfRange (std::clamp (crossfade, 0.0f, 100.0f) / 100.0f * .5f),
          logMin (std::log (20.0f)),
          invLogRange (1.0f / std::max (1.0e-6f, std::log (std::max (40.0f, (float) (hostRate * .5))) - logMin)) {}

    std::array<float, 4> weightsForBin (int bin, int bins, double fftRate) const noexcept
    {
        if (bin <= 0 || bin >= bins - 1) return { 1, 0, 0, 0 };
        const float frequency = (float) bin * ((float) (fftRate * .5) / (float) (bins - 1));
        const float logPos = (std::log (std::max (20.0f, frequency)) - logMin) * invLogRange;
        const float pos = std::clamp (logPos * (float) slices, 0.0f, (float) (slices - 1) + .999f);
        const int slice = std::clamp ((int) pos, 0, slices - 1);
        const float frac = pos - (float) slice;
        float weight = 1.0f;
        if (cfRange > 0)
        {
            const float distance = std::min (frac, 1 - frac);
            if (distance < cfRange) weight = distance / cfRange;
        }
        std::array<float, 4> weights {};
        weights[(size_t) (slice % 4)] = weight;
        if (cfRange > 0 && weight < 1)
        {
            if (frac < .5f && slice > 0) weights[(size_t) ((slice - 1) % 4)] += 1 - weight;
            else if (frac >= .5f && slice < slices - 1) weights[(size_t) ((slice + 1) % 4)] += 1 - weight;
        }
        float sum = 0; for (auto w : weights) sum += w;
        if (sum > 1.0e-6f) for (auto& w : weights) w /= sum;
        else weights[0] = 1;
        return weights;
    }
private:
    int slices;
    float cfRange, logMin, invLogRange;
};
} // namespace scrr::dsp

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
    struct SliceWeights
    {
        int first {}, second { -1 };
        float firstWeight { 1.0f }, secondWeight {};
    };

    FrequencySplit (double hostRate, int count, float crossfade)
        : slices (std::clamp (count, 1, 64)), cfRange (std::clamp (crossfade, 0.0f, 100.0f) / 100.0f * .5f),
          logMin (std::log (20.0f)),
          invLogRange (1.0f / std::max (1.0e-6f, std::log (std::max (40.0f, (float) (hostRate * .5))) - logMin)) {}

    // Keep slice identity until a caller explicitly folds slices into four racks.
    SliceWeights sliceWeightsForBin (int bin, int bins, double fftRate) const noexcept
    {
        if (bin <= 0 || bin >= bins - 1) return {};
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
        SliceWeights result { slice, -1, weight, 0.0f };
        if (cfRange > 0 && weight < 1)
        {
            if (frac < .5f && slice > 0) result.second = slice - 1;
            else if (frac >= .5f && slice < slices - 1) result.second = slice + 1;
            if (result.second >= 0) result.secondWeight = 1 - weight;
        }
        const float sum = result.firstWeight + result.secondWeight;
        if (sum > 1.0e-6f) { result.firstWeight /= sum; result.secondWeight /= sum; }
        else result = {};
        return result;
    }

    std::array<float, 4> weightsForBin (int bin, int bins, double fftRate) const noexcept
    {
        const auto slicesForBin = sliceWeightsForBin (bin, bins, fftRate);
        std::array<float, 4> weights {};
        weights[(size_t) (slicesForBin.first % 4)] = slicesForBin.firstWeight;
        if (slicesForBin.second >= 0)
            weights[(size_t) (slicesForBin.second % 4)] += slicesForBin.secondWeight;
        return weights;
    }
private:
    int slices;
    float cfRange, logMin, invLogRange;
};
} // namespace scrr::dsp

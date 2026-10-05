// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <complex>

namespace scrr::dsp {

/**
    Phase 2 spectral corruption module #1.

    Randomly swaps frequency bins. `amount` in [0,1] controls how many bins are
    shuffled each frame (0 = transparent, 1 = fully shuffled). DC (bin 0) and
    Nyquist (bin numBins-1) are never moved. RNG is seeded from `seed` and only
    re-seeded when the seed parameter changes.
*/
class BinShuffle : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "BinShuffle"; }

    void prepare (double sampleRate, int numBins) override
    {
        juce::ignoreUnused (sampleRate);
        perm.resize ((size_t) numBins);
        temp.resize ((size_t) numBins);
        rng = juce::Random (seed);
    }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = state.getProperty ("enabled", false);
        amount = (float) state.getProperty ("amount", 0.0f);

        const int newSeed = (int) state.getProperty ("seed", 0);
        if (newSeed != seed)
        {
            seed = newSeed;
            rng = juce::Random (seed);
        }
    }

    void reset() override { rng = juce::Random (seed); }

    void processSpectrum (std::complex<float>* spectrum, int numBins, double sampleRate) override
    {
        juce::ignoreUnused (sampleRate);
        if (! enabled || amount <= 0.0f || numBins <= 2)
            return;

        const int lo = 1;            // first mutable bin (skip DC)
        const int hi = numBins - 2;  // last mutable bin (skip Nyquist)
        if (hi < lo)
            return;

        for (int i = 0; i < numBins; ++i)
            perm[(size_t) i] = i;

        // Partial Fisher-Yates: each interior position is swapped (with prob
        // `amount`) with a random position at or above it. amount=0 -> identity,
        // amount=1 -> uniform random permutation. Endpoints never move.
        auto& activeRng = rng;

        for (int i = lo; i <= hi; ++i)
        {
            if (activeRng.nextFloat() < amount)
            {
                const int j = i + activeRng.nextInt (hi - i + 1); // [i, hi]
                std::swap (perm[(size_t) i], perm[(size_t) j]);
            }
        }

        for (int i = 0; i < numBins; ++i)
            temp[(size_t) i] = spectrum[perm[(size_t) i]];

        for (int i = 0; i < numBins; ++i)
            spectrum[(size_t) i] = temp[(size_t) i];
    }

private:
    bool  enabled { true };
    float amount { 0.0f };
    int   seed  { 0 };

    juce::Random rng;

    std::vector<int> perm;
    std::vector<std::complex<float>> temp;
};

} // namespace scrr::dsp

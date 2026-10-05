// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <array>
#include <cstdint>

namespace scrr::licensing {
// Voss-style octave rows plus white noise for the highest octave. Fixed state,
// bounded output, no allocations, OS calls or locks in the audio callback.
class PinkNoise
{
public:
    explicit PinkNoise (uint32_t value = 0x52a73f19u) : seed (value != 0 ? value : 1)
    { for (auto& row : rows) { row = white(); sum += row; } }
    float next() noexcept
    {
        ++counter; uint32_t bits = counter; unsigned row = 0;
        while ((bits & 1u) == 0 && row < rows.size()) { bits >>= 1; ++row; }
        if (row < rows.size()) { sum -= rows[row]; rows[row] = white(); sum += rows[row]; }
        if ((counter & 0xffffu) == 0) { sum = 0; for (auto value : rows) sum += value; }
        return (sum + white()) * .0065f;
    }
private:
    float white() noexcept
    {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    uint32_t seed, counter {};
    std::array<float, 16> rows {};
    float sum {};
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>
#include <memory>

namespace scrr::dsp {
// SPSC mailbox: producer, consumer and exchange slot each own a distinct block.
// Storage never moves, including during FFT reconfiguration. Only the exchange
// index crosses threads; the producer cannot reclaim the consumer's block.
class TripleBuffer
{
public:
    static constexpr size_t capacity = 131072 / 2 + 1;
    void resize (size_t n) noexcept { logicalSize.store (std::min (n, capacity)); }
    size_t size() const noexcept { return logicalSize.load(); }
    void write (const float* source, size_t n, double sampleRate = 44100.0) noexcept
    {
        auto& slot = buffers[(size_t) writeIndex];
        slot.count = std::min (n, capacity);
        slot.sampleRate = sampleRate;
        std::memcpy (slot.values.data(), source, slot.count * sizeof (float));
        writeIndex = exchangeIndex.exchange (writeIndex | dirtyBit, std::memory_order_acq_rel) & indexMask;
    }
    bool read (std::vector<float>& out, double* sampleRate = nullptr) const
    {
        if ((exchangeIndex.load (std::memory_order_acquire) & dirtyBit) == 0) return false;
        readIndex = exchangeIndex.exchange (readIndex, std::memory_order_acq_rel) & indexMask;
        const auto& slot = buffers[(size_t) readIndex];
        out.assign (slot.values.data(), slot.values.data() + slot.count);
        if (sampleRate != nullptr) *sampleRate = slot.sampleRate;
        return true;
    }
private:
    struct Slot { std::array<float, capacity> values {}; size_t count {}; double sampleRate { 44100.0 }; };
    std::unique_ptr<Slot[]> buffers { std::make_unique<Slot[]> (3) };
    static constexpr int dirtyBit = 4, indexMask = 3;
    mutable std::atomic<int> exchangeIndex { 2 };
    int writeIndex { 1 };
    mutable int readIndex { 0 };
    std::atomic<size_t> logicalSize { 0 };
};
} // namespace scrr::dsp

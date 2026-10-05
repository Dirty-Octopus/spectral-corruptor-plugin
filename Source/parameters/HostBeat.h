// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace scrr::params {
struct HostBeat
{
    double bpm { 120 }, ppq {};
    bool playing {}, hasTimeline {};
    uint64_t revision {};
};

// One audio-thread writer. All fields are atomic; bounded reads never hold up
// the message thread or take a lock on the audio thread.
class HostBeatMailbox
{
public:
    void publish (const HostBeat& beat) noexcept
    {
        sequence.fetch_add (1);
        bpm.store (beat.bpm); ppq.store (beat.ppq);
        flags.store ((beat.playing ? 1u : 0u) | (beat.hasTimeline ? 2u : 0u));
        sequence.fetch_add (1);
    }
    bool read (HostBeat& beat) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = sequence.load();
            if ((before & 1u) != 0) continue;
            HostBeat next; next.bpm = bpm.load(); next.ppq = ppq.load();
            const auto state = flags.load(); next.playing = (state & 1u) != 0; next.hasTimeline = (state & 2u) != 0;
            const auto after = sequence.load();
            if (before == after) { next.revision = after; beat = next; return true; }
        }
        return false;
    }
private:
    static_assert (std::atomic<double>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<double> bpm { 120 }, ppq {};
    std::atomic<unsigned> flags {};
    std::atomic<uint64_t> sequence {};
};

// Shared GUI clock: hard blinking at quarter-note beats, including while the
// host is stopped or suspends processing. A new playing PPQ anchors the phase.
class BeatIndicatorClock
{
public:
    float tick (double seconds, const HostBeat& beat) noexcept
    {
        if (std::isfinite (beat.bpm) && beat.bpm > 0) bpm = beat.bpm;
        if (std::isfinite (seconds) && seconds > 0) phase = wrap (phase + seconds * bpm / 60);
        if (beat.revision != revision)
        {
            revision = beat.revision;
            if (beat.playing && beat.hasTimeline && std::isfinite (beat.ppq)) phase = wrap (beat.ppq);
        }
        return (float) phase;
    }
private:
    static double wrap (double value) noexcept { return value - std::floor (value); }
    double bpm { 120 }, phase {};
    uint64_t revision {};
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_core/juce_core.h>
#include "../modules/DelayTiming.h"
#include "RemapFormulaRuntime.h"
#include "../parameters/TransferCurve.h"
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace scrr::dsp {

/** One shared runtime per active Formula rack slot, never per clone. It may
    retain the previous program during the identity fade; the two-times program
    reservation covers both, including bounded string/allocator headroom. */
inline size_t estimateRemapFormulaBytes() noexcept
{
    return 4096 + 2 * (sizeof (RemapFormulaRuntime) + sizeof (scrr::params::FormulaProgram) + 16 * 256);
}

namespace spectralResourceDetail {
inline size_t scratchPayloadBytes (const juce::String& type, int numBins) noexcept
{
    if (numBins < 3) return 0;
    const auto bins = (size_t) numBins;
    constexpr auto complexBytes = sizeof (std::complex<float>);
    if (type == "BinShuffle") return bins * (sizeof (int) + complexBytes);
    if (type == "RandomBinDeath") return bins * sizeof (uint8_t);
    if (type == "MagnitudePhaseSwap") return bins * 2 * sizeof (float);
    if (type == "SpectralBloom") return (bins * 2 + 1) * sizeof (double);
    if (type == "BinTeleport" || type == "SpectralPixelation"
        || type == "FrequencyWarp" || type == "FrequencyDrift"
        || type == "FrequencyRemap" || type == "FrequencyShift"
        || type == "HarmonicMatch" || type == "SpectralMirror")
        return bins * complexBytes;
    return 0;
}
} // namespace spectralResourceDetail

/** Charge this once per rack slot when its clones borrow a shared workspace.
    Zero means the module needs no shared buffers, so no workspace is necessary. */
inline size_t estimateSpectralWorkspaceBytes (const juce::String& type, int numBins) noexcept
{
    const auto payload = spectralResourceDetail::scratchPayloadBytes (type, numBins);
    return payload == 0 ? 0 : 4096 + 2 * payload;
}

/** Conservative reservation for one additional ordinary spectral-module clone.
    The payloads below match the owned buffers allocated by prepare(), including
    both channels where the module retains channel-specific state. Reserve twice
    their payload for vector capacity/replacement headroom, plus 4 KiB for the
    object, allocator metadata and small fixed state. Shared parameter-tree/String
    storage, the original rack instance and engine/partial buffers are separate.

    A retained clone must not be re-prepared at a different FFT size: discard the
    pool on engine reconfiguration, or charge its largest prepared bin count.
    New module types must be audited here before per-slice cloning is admitted.
    Unknown, non-spectral and invalid requests fail closed with size_t::max().
    sharedScratch=true excludes borrowed workspace from this clone; the caller
    must charge estimateSpectralWorkspaceBytes() once for that slot instead.
*/
inline size_t estimateSpectralCloneBytes (const juce::String& type, int numBins,
                                        bool sharedScratch = false) noexcept
{
    constexpr auto refused = std::numeric_limits<size_t>::max();
    constexpr size_t objectAndAllocatorSlack = 4096;
    constexpr size_t complexBytes = sizeof (std::complex<float>);
    constexpr size_t randomFrameBytes = 4096 * sizeof (uint32_t);
    if (numBins < 3) return refused;

    size_t bytesPerBin = 0, fixedPayload = 0;
    if (type == "SpectralDispersion")
        bytesPerBin = 2 * 96 * complexBytes;
    else if (type == "TemporalSmear" || type == "TimeDesync")
        bytesPerBin = 64 * complexBytes;
    else if (type == "FrequencyRemap")
    {
        // Scratch + two channels of carrier/previous phases and history flags.
        bytesPerBin = complexBytes + 4 * sizeof (double) + 2 * sizeof (uint8_t);
        // Two retained 64-point curves and a temporary decoded/sanitised curve.
        fixedPayload = 3 * 64 * sizeof (scrr::params::TransferCurve::Point);
    }
    else if (type == "BinHold")
        bytesPerBin = 2 * (complexBytes + sizeof (int));
    else if (type == "FrameHold")
        bytesPerBin = 2 * complexBytes;
    else if (type == "MagnitudePhaseSwap")
        bytesPerBin = 4 * sizeof (float);
    else if (type == "SpectralBloom")
    {
        bytesPerBin = 2 * sizeof (double);
        fixedPayload = sizeof (double); // Prefix has numBins + 1 entries.
    }
    else if (type == "BinShuffle")
        bytesPerBin = sizeof (int) + complexBytes;
    else if (type == "FrequencyDrift")
    {
        bytesPerBin = complexBytes;
        fixedPayload = 4096 * sizeof (float);
    }
    else if (type == "PhaseNoise" || type == "ComplexRotation")
        fixedPayload = randomFrameBytes;
    else if (type == "BinTeleport" || type == "SpectralPixelation"
             || type == "FrequencyWarp" || type == "FrequencyShift"
             || type == "HarmonicMatch" || type == "SpectralMirror")
        bytesPerBin = complexBytes;
    else if (type == "SpectralEqualizer")
        bytesPerBin = sizeof (float);
    else if (type == "RandomBinDeath" || type == "SpectralDropout")
        bytesPerBin = sizeof (uint8_t);
    else if (type != "SpectralJPEG" && type != "PhaseCorruption"
             && type != "SpectralFilter" && type != "SpectralContrast"
             && type != "HarmonicSculpt" && type != "SpectralComb"
             && type != "SpectralClipper")
        return refused;

    const auto bins = (size_t) numBins;
    const auto maximumPayload = (refused - objectAndAllocatorSlack) / 2;
    if (fixedPayload > maximumPayload
        || (bytesPerBin != 0 && bins > (maximumPayload - fixedPayload) / bytesPerBin))
        return refused;
    auto payload = bytesPerBin * bins + fixedPayload;
    if (sharedScratch) payload -= spectralResourceDetail::scratchPayloadBytes (type, numBins);
    return objectAndAllocatorSlack + 2 * payload;
}

/** Original rack-instance reservation, including time-domain histories. Pass
    the same (possibly oversampled) rate supplied to the module's prepare().
    Partial effects are budgeted separately with their processor/workspace. */
inline size_t estimateOriginalModuleBytes (const juce::String& type, int numBins,
                                          double actualRate) noexcept
{
    constexpr auto refused = std::numeric_limits<size_t>::max();
    if (type == "Notes") return 0;
    if (type == "Utility" || type == "Compressor" || type == "Distortion") return 4096;
    if (type != "Delay" && type != "Reverb") return estimateSpectralCloneBytes (type, numBins);
    if (! std::isfinite (actualRate) || actualRate <= 0) return refused;

    // GenericEffects::prepare: stereo delay, or eight FDN lines plus stereo
    // predelay. Match ceil() and the four guard samples on every buffer.
    const double samples = type == "Delay"
        ? 2 * (std::ceil (actualRate * (maximumDelayMs * .001 + .01)) + 4)
        : 8 * (std::ceil (actualRate * .18) + 4) + 2 * (std::ceil (actualRate * .201) + 4);
    const double reserved = 4096 + 2 * samples * sizeof (float);
    if (! std::isfinite (reserved) || reserved >= (double) refused) return refused;
    return (size_t) reserved;
}

} // namespace scrr::dsp

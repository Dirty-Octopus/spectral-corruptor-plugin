// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include "../parameters/TransferCurve.h"
#include "../dsp/RemapFormulaRuntime.h"
#include <array>

namespace scrr::dsp {
/** A forward frequency map: x is source Hz, y is destination Hz, both logarithmic.
    Source-range selection happens here, before scattering, so moved material is
    not erased by the ordinary rack's destination-range restoration. */
class FrequencyRemapModule final : public SpectralModule
{
public:
    const char* getName() const noexcept override { return "Frequency Remap"; }
    void prepare (double, int bins) override
    {
        curve.points.reserve (scrr::params::TransferCurve::maxPoints);
        requestedCurve.points.reserve (scrr::params::TransferCurve::maxPoints);
        auto& scratch = scratchBuffer (ownScratch);
        scratch.assign ((size_t) bins, {});
        for (auto& p : phases) p.assign ((size_t) bins, 0.0);
        for (auto& p : previousInputPhase) p.assign ((size_t) bins, 0.0);
        for (auto& h : phaseHistory) h.assign ((size_t) bins, 0);
        hop = juce::jmax (1, (bins - 1) / 2);
        reset();
    }
    void setHopSize (int samples) override { hop = juce::jmax (1, samples); }
    void beginChannel (int index) override { channel = juce::jlimit (0, 1, index); }
    void reset() override
    {
        for (auto& p : phases) std::fill (p.begin(), p.end(), 0.0);
        for (auto& p : previousInputPhase) std::fill (p.begin(), p.end(), 0.0);
        for (auto& h : phaseHistory) std::fill (h.begin(), h.end(), 0);
        activeMix.fill (0); hasProcessed.fill (false);
    }
    void setFrequencyLimit (double hz) noexcept { limit = hz; }
    void setFormulaRuntime (RemapFormulaRuntime* runtime) noexcept { formulaRuntime = runtime; }
    void setEffectiveCurve (const scrr::params::TransferCurve& effective)
    {
        requestedCurve.points.assign (effective.points.begin(), effective.points.end());
        updateRequestedIdentity();
    }
    void updateParameters (const juce::ValueTree& state) override
    {
        requestedAmount = juce::jlimit (0.0, 1.0, (double) state.getProperty ("amount", 100.0) / 100.0);
        formulaMode = (int) state.getProperty ("remapMode", 0) == 1;
        preserveInvalid = (int) state.getProperty ("remapInvalidPolicy", 0) == 1;
        low = (double) state.getProperty ("rangeLow", 0.0);
        high = (double) state.getProperty ("rangeHigh", 96000.0);
        const auto encoded = state["remapCurve"].toString();
        if (encoded != curveText) { curveText = encoded; requestedCurve = scrr::params::TransferCurve::decode (encoded); }
        updateRequestedIdentity();
    }
    void updateRequestedIdentity()
    {
        requestedIdentity = requestedAmount <= 0 || (formulaMode ? formulaRuntime && formulaRuntime->isIdentity() : isIdentity (requestedCurve));
        // Keep the last processed mapping during a short return-to-identity
        // crossfade. Resetting its carrier immediately creates a phase jump.
        if (! requestedIdentity)
        {
            if (! curve.sameAs (requestedCurve)) curve = requestedCurve;
            amount = requestedAmount;
            activeFormulaMode = formulaMode;
        }
    }
    void processSpectrum (std::complex<float>* spectrum, int bins, double sampleRate) override
    {
        auto& scratch = scratchBuffer (ownScratch);
        if (bins < 3 || scratch.size() != (size_t) bins || sampleRate <= 0 || ! std::isfinite (sampleRate)) return;
        const double top = juce::jmin (limit, sampleRate * .5);
        if (top <= 20.0) return;
        if (formulaMode && formulaRuntime) formulaRuntime->prepareTable (top);
        updateRequestedIdentity();
        auto& mix = activeMix[(size_t) channel];
        const double targetMix = requestedIdentity ? 0.0 : 1.0;
        if (! hasProcessed[(size_t) channel]) { mix = targetMix; hasProcessed[(size_t) channel] = true; }
        const double frameMix = mix;
        const double step = hop / (sampleRate * .02);
        mix += juce::jlimit (-step, step, targetMix - mix);
        if (frameMix <= 0)
        {
            std::fill (phases[(size_t) channel].begin(), phases[(size_t) channel].end(), 0.0);
            std::fill (phaseHistory[(size_t) channel].begin(), phaseHistory[(size_t) channel].end(), 0);
            return;
        }
        const double binHz = sampleRate / (2.0 * (bins - 1));
        const double logRange = std::log (top / 20.0);
        std::fill (scratch.begin(), scratch.end(), std::complex<float>{});
        scratch.front() = spectrum[0]; scratch.back() = spectrum[bins - 1];
        auto& phase = phases[(size_t) channel];
        auto& previous = previousInputPhase[(size_t) channel];
        auto& history = phaseHistory[(size_t) channel];
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double frameDuration = hop / sampleRate;
        const double centrePhase = juce::MathConstants<double>::pi * (2.0 * (bins - 1) - 1) / (2.0 * (bins - 1));
        for (int b = 1; b < bins - 1; ++b)
        {
            const double hz = b * binHz;
            const double inputPhase = std::arg (spectrum[b]);
            const auto age = history[(size_t) b];
            double sourceHz = hz;
            if (age > 0)
            {
                // A sinusoid leaks into neighbouring FFT bins. Estimate its
                // actual frequency so a 200 Hz tone maps to 800 Hz at every FFT
                // size, rather than mapping each leakage lobe's bin centre.
                const double deviation = std::remainder (inputPhase - previous[(size_t) b] - twoPi * hz * frameDuration, twoPi);
                sourceHz = juce::jlimit (0.0, sampleRate * .5, hz + deviation / (twoPi * frameDuration));
            }
            previous[(size_t) b] = inputPhase;
            if (std::norm (spectrum[b]) < 1.0e-20f)
            {
                history[(size_t) b] = 0; phase[(size_t) b] = 0;
                scratch[(size_t) b] += spectrum[b];
                continue;
            }
            history[(size_t) b] = (uint8_t) juce::jmin (2, (int) age + 1);
            const bool selected = hz >= 20.0 && hz <= top
                && (low <= high ? hz >= low && hz <= high : hz <= high || hz >= low);
            if (! selected || amount <= 0) { scratch[(size_t) b] += spectrum[b]; phase[(size_t) b] = 0; continue; }
            const double clampedSource = juce::jlimit (20.0, top, sourceHz);
            double destination = clampedSource;
            bool validDestination = true;
            if (activeFormulaMode)
            {
                const auto mapped = formulaRuntime ? formulaRuntime->evaluateHz (clampedSource)
                                                   : scrr::params::FormulaResult { clampedSource, false, -1 };
                validDestination = mapped.valid;
                destination = mapped.value;
            }
            else
            {
                const double x = std::log (clampedSource / 20.0) / logRange;
                const double mapped = evaluateCurve (x);
                // Preserve exact endpoints instead of rounding a legal upper
                // endpoint a few ulps beyond the host's processing limit.
                destination = scrr::params::formulaExactEqual (mapped, 1.0) ? top
                            : scrr::params::formulaExactEqual (mapped, 0.0) ? 20.0
                            : 20.0 * std::exp (mapped * logRange);
            }
            if (! validDestination || ! std::isfinite (destination) || destination < 20.0 || destination > top)
            {
                // The policy concerns only this selected source bin, before
                // Amount interpolation. Valid neighbours still run normally.
                if (preserveInvalid) scratch[(size_t) b] += spectrum[b];
                phase[(size_t) b] = 0;
                continue;
            }
            const double delta = amount * (destination - clampedSource);
            if (std::abs (delta) < 1.0e-7)
            {
                // A modulated formula may cross zero shift at just one source
                // frequency. Keep that carrier continuous; a global identity
                // request uses the separate wet-to-dry transition above.
                if (activeFormulaMode) scratch[(size_t) b] += spectrum[b] * std::polar (1.0f, (float) phase[(size_t) b]);
                else { scratch[(size_t) b] += spectrum[b]; phase[(size_t) b] = 0; }
                continue;
            }
            // Preserve each sinusoid's local window shape while translating its
            // physical frequency; the curve still selects the source frequency.
            const double target = hz + delta;
            const double position = target / binHz;
            const int index = (int) std::floor (position);
            const float fraction = (float) (position - index);
            if (age == 1) phase[(size_t) b] = std::remainder (twoPi * delta * frameDuration, twoPi);
            const auto rotated = spectrum[b] * std::polar (1.0f, (float) phase[(size_t) b]);
            // Interpolate around the window centre. Adjacent uncentred FFT
            // lobes often have opposite signs; plain complex interpolation
            // would cancel fractional-bin shifts and cause large level losses.
            if (index > 0 && index < bins - 1 && index * binHz <= top)
                scratch[(size_t) index] += rotated * std::polar (1.0f - fraction, (float) (fraction * centrePhase));
            if (index + 1 > 0 && index + 1 < bins - 1 && (index + 1) * binHz <= top)
                scratch[(size_t) index + 1] += rotated * std::polar (fraction, (float) ((fraction - 1) * centrePhase));
            phase[(size_t) b] = std::remainder (phase[(size_t) b]
                + twoPi * delta * frameDuration, twoPi);
        }
        if (frameMix >= 1) std::copy (scratch.begin(), scratch.end(), spectrum);
        else for (int b = 0; b < bins; ++b)
            spectrum[b] = spectrum[b] * (float) (1 - frameMix) + scratch[(size_t) b] * (float) frameMix;
    }
private:
    static bool isIdentity (const scrr::params::TransferCurve& candidate) noexcept
    {
        for (const auto& p : candidate.points)
            if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.curve)
                || std::abs (p.x - p.y) > 1.0e-7f || std::abs (p.curve) > 1.0e-7f) return false;
        return true;
    }
    double evaluateCurve (double x) const noexcept
    {
        // Frequency estimates vary each frame, so a static bin cache would be
        // stale. Binary search avoids scanning up to 64 curve nodes per bin.
        const auto it = std::upper_bound (curve.points.begin(), curve.points.end(), (float) x,
            [] (float value, const auto& p) { return value < p.x; });
        if (it == curve.points.begin()) return it->y;
        if (it == curve.points.end()) return curve.points.back().y;
        return scrr::params::TransferCurve::between (*(it - 1), *it, x);
    }
    scrr::params::TransferCurve curve, requestedCurve;
    RemapFormulaRuntime* formulaRuntime {};
    juce::String curveText;
    std::vector<std::complex<float>> ownScratch;
    std::array<std::vector<double>, 2> phases;
    std::array<std::vector<double>, 2> previousInputPhase;
    std::array<std::vector<uint8_t>, 2> phaseHistory;
    std::array<double, 2> activeMix {};
    std::array<bool, 2> hasProcessed {};
    bool requestedIdentity { true };
    bool formulaMode {}, activeFormulaMode {}, preserveInvalid {};
    double requestedAmount { 1 }, amount { 1 }, low {}, high { 96000 }, limit { 22050 };
    int channel {}, hop { 512 };
};
}

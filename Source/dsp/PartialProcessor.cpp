// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PartialProcessor.h"
#include "FrequencySplit.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace scrr::dsp {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double maxComponent = 1.0e12;
constexpr int waveTableSize = 4096, waveTableLevels = 11;
constexpr size_t waveTableStride = waveTableSize + 1;

double finite (double value, double fallback = 0.0) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

std::complex<float> finiteSample (std::complex<float> sample) noexcept
{
    return { (float) std::clamp (finite (sample.real()), -maxComponent, maxComponent),
             (float) std::clamp (finite (sample.imag()), -maxComponent, maxComponent) };
}

void add (std::complex<float>& destination, std::complex<float> value) noexcept
{
    destination = { (float) std::clamp (finite ((double) destination.real() + value.real()), -maxComponent, maxComponent),
                    (float) std::clamp (finite ((double) destination.imag() + value.imag()), -maxComponent, maxComponent) };
}
}

PartialProcessor::PartialProcessor() = default;
PartialProcessor::~PartialProcessor() = default;

PartialProcessor::Workspace::Workspace() = default;
PartialProcessor::Workspace::~Workspace() = default;

size_t PartialProcessor::Workspace::estimatedBytes (int size) noexcept
{
    // Window + one real FFT work frame + spectrum copy + platform FFT plan.
    // The generous FFT-plan/headroom allowance also covers allocator capacity.
    return 4096 + (size_t) std::max (0, size) * 96;
}

size_t PartialProcessor::Workspace::estimatedWaveformBytes() noexcept
{
    // Three shapes, harmonic ceilings 1..1024, and one wrap sample. Double
    // payload reservation also covers preparation's temporary FFT/frame.
    return 4096 + 2 * 3 * waveTableLevels * waveTableStride * sizeof (float);
}

void PartialProcessor::Workspace::prepareWaveforms()
{
    if (hasWaveforms()) return;
    waveforms.assign (3 * waveTableLevels * waveTableStride, 0.0f);
    juce::dsp::FFT fft (12);
    std::vector<float> frame (2 * waveTableSize);
    for (int shape = 1; shape <= 3; ++shape)
    {
        double previousPower = 0;
        for (int level = 0; level < waveTableLevels; ++level)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            const int maximum = 1 << level;
            double coefficientPower = 0;
            for (int harmonic = 1; harmonic <= maximum; ++harmonic)
            {
                if (shape != (int) Shape::Saw && harmonic % 2 == 0) continue;
                const double coefficient = shape == (int) Shape::Triangle ? 1.0 / ((double) harmonic * harmonic) : 1.0 / harmonic;
                coefficientPower += coefficient * coefficient;
                // All fundamentals use cosine phase, matching the existing
                // sine oscillator. The other phases produce the named shapes.
                const double phase = shape == (int) Shape::Saw ? (harmonic - 1) * pi * .5
                    : shape == (int) Shape::Square ? ((harmonic - 1) / 2) * pi : 0;
                frame[(size_t) (2 * harmonic)] = (float) (waveTableSize * .5 * coefficient * std::cos (phase));
                frame[(size_t) (2 * harmonic + 1)] = (float) (waveTableSize * .5 * coefficient * std::sin (phase));
            }
            fft.performRealOnlyInverseTransform (frame.data());
            const double normalization = 1.0 / std::sqrt (std::max (coefficientPower, 1.0e-20));
            waveformCorrelations[(size_t) ((shape - 1) * waveTableLevels + level)] = level == 0 ? 1 : std::sqrt (previousPower / coefficientPower);
            previousPower = coefficientPower;
            auto* table = waveforms.data() + (size_t) ((shape - 1) * waveTableLevels + level) * waveTableStride;
            for (int sample = 0; sample < waveTableSize; ++sample) table[sample] = (float) (frame[(size_t) sample] * normalization);
            table[waveTableSize] = table[0];
        }
    }
}

const float* PartialProcessor::Workspace::waveform (Shape shape, int maximum) const noexcept
{
    if (! hasWaveforms() || shape == Shape::Sine) return nullptr;
    int level = 0;
    while (level + 1 < waveTableLevels && (1 << (level + 1)) <= maximum) ++level;
    const int shapeIndex = std::clamp ((int) shape - 1, 0, 2);
    return waveforms.data() + (size_t) (shapeIndex * waveTableLevels + level) * waveTableStride;
}

void PartialProcessor::Workspace::renderWaveform (std::vector<float>& frame, Shape shape, double amplitude,
                                                 double phase, double frequency, double limit,
                                                 double actualRate, bool accumulate) const noexcept
{
    const double available = limit / std::max (frequency, 1.0e-12);
    const int maximum = (int) std::clamp (std::floor (available), 1.0, (double) waveTableSize / 4);
    const auto* table = waveform (shape, maximum); if (! table) return;
    int level = 0; while (level + 1 < waveTableLevels && (1 << (level + 1)) <= maximum) ++level;
    const auto* lower = level > 0 ? waveform (shape, 1 << (level - 1)) : table;
    // Fade between two tables whose highest harmonics are BOTH under the cap.
    // At every power-of-two boundary the incoming table starts at zero weight,
    // so continuous pitch changes do not switch harmonic groups abruptly.
    const double blend = level == 0 ? 1 : std::clamp (std::log2 (available / (1 << level)), 0.0, 1.0);
    const double correlation = waveformCorrelations[(size_t) (std::clamp ((int) shape - 1, 0, 2) * waveTableLevels + level)];
    const double energy = (1 - blend) * (1 - blend) + blend * blend + 2 * blend * (1 - blend) * correlation;
    const double gain = amplitude / std::sqrt (std::max (energy, 1.0e-20));
    double cycle = phase / twoPi; cycle -= std::floor (cycle);
    const double increment = frequency / actualRate;
    for (size_t sample = 0; sample < window.size(); ++sample)
    {
        const double position = cycle * waveTableSize; const int first = (int) position;
        const double fraction = position - first;
        const double upperValue = (1.0 - fraction) * table[first] + fraction * table[first + 1];
        const double lowerValue = (1.0 - fraction) * lower[first] + fraction * lower[first + 1];
        const auto value = (float) (gain * ((1 - blend) * lowerValue + blend * upperValue) * window[sample]);
        if (accumulate) frame[sample] += value; else frame[sample] = value;
        cycle += increment; if (cycle >= 1.0) cycle -= 1.0;
    }
}

size_t PartialProcessor::estimatedPrivateBytes (int frames) noexcept
{
    return 4096 + 2 * (sizeof (PartialProcessor)
        + (size_t) std::max (1, frames) * sizeof (std::array<Movement, maxSlices>)
        + sizeof (Stage) + 256);
}

void PartialProcessor::Workspace::prepare (int size, const std::vector<float>& analysisWindow, bool allRoutes)
{
    synthesisFFT.reset();
    if (size < 4 || (size & (size - 1)) != 0) return;
    window.assign ((size_t) size, 1.0f);
    if ((int) analysisWindow.size() == size)
        for (int n = 0; n < size; ++n)
            window[(size_t) n] = (float) finite (analysisWindow[(size_t) n]);
    windowPower = 0;
    double centre = 0;
    for (int n = 0; n < size; ++n)
    {
        const double power = (double) window[(size_t) n] * window[(size_t) n];
        windowPower += power;
        centre += (double) n * power;
    }
    windowPower = std::max (windowPower, 1.0e-20);
    windowBinPhase = twoPi * centre / (windowPower * (double) size);
    sliceInput.resize ((size_t) (size / 2 + 1));
    for (size_t r = 0; r < oscillatorFrames.size(); ++r)
        oscillatorFrames[r].assign (r == 0 || allRoutes ? (size_t) (2 * size) : 0, 0.0f);
    int order = 0;
    for (int n = size; n > 1; n >>= 1) ++order;
    synthesisFFT = std::make_unique<juce::dsp::FFT> (order);
}

void PartialProcessor::prepare (double actualSampleRate, int size, int hop,
                                int numChannels, const std::vector<float>& analysisWindow,
                                int maxFramesPerBlock, std::shared_ptr<Workspace> sharedWorkspace)
{
    sampleRate = std::max (1.0, finite (actualSampleRate, 44100.0));
    fftSize = size;
    hopSize = std::clamp (hop, 1, std::max (1, size));
    channels = std::clamp (numChannels, 1, 2);
    workspace = std::move (sharedWorkspace);
    const bool sliceOnly = workspace != nullptr;
    ownsWorkspace = ! sliceOnly;
    if (! workspace)
    {
        workspace = std::make_shared<Workspace>();
        workspace->prepare (size, analysisWindow, true);
    }
    if (! workspace->synthesisFFT)
    {
        fftSize = 0;
        return;
    }
    if (ownsWorkspace)
        for (const auto& stage : stages)
            if (stage.enabled && stage.mode == Mode::Resynth && stage.shape != Shape::Sine) { workspace->prepareWaveforms(); break; }
    windowPower = workspace->windowPower;
    windowBinPhase = workspace->windowBinPhase;
    assignments.resize (sliceOnly ? 0 : (size_t) (fftSize / 2 + 1));
    movementHistory.resize ((size_t) std::max (1, maxFramesPerBlock));
    cachedSlices = 0;
    reset();
}

void PartialProcessor::reset() noexcept
{
    state = {};
    mappedAnchors = {};
    beginBlock();
    if (workspace) for (auto& frame : workspace->oscillatorFrames) std::fill (frame.begin(), frame.end(), 0.0f);
}

void PartialProcessor::beginBlock() noexcept
{
    frameCursors = {};
    sliceFrameCursors = {};
    recordedFrames = 0;
}

void PartialProcessor::setStages (const std::vector<Stage>& next)
{
    stages.assign (next.begin(), next.begin() + (std::ptrdiff_t) std::min (next.size(), (size_t) maxStages));
    if (ownsWorkspace && workspace)
    {
        bool needsWaveforms = false;
        for (const auto& stage : stages)
            needsWaveforms = needsWaveforms || (stage.enabled && stage.mode == Mode::Resynth && stage.shape != Shape::Sine);
        if (needsWaveforms) workspace->prepareWaveforms(); else workspace->releaseWaveforms();
    }
}

void PartialProcessor::updateStageParameters (size_t index, double amount, double pitch) noexcept
{
    if (index >= stages.size()) return;
    auto& stage = stages[index];
    stage.amount = amount;
    stage.pitch = pitch;
}

void PartialProcessor::setFundamentalHz (double hz) noexcept
{
    referenceHz = std::clamp (finite (hz, 110.0), 1.0, 96000.0);
}

bool PartialProcessor::hasActiveStages() const noexcept
{
    for (const auto& stage : stages) if (stage.enabled) return true;
    return false;
}

double PartialProcessor::mapFrequency (double frequency, double reference, const Stage& stage) noexcept
{
    frequency = std::clamp (finite (frequency), 0.0, maxComponent);
    if (! stage.enabled) return frequency;
    reference = std::clamp (finite (reference, 110.0), 1.0, 96000.0);
    const double amount = finite (stage.amount);
    const double blend = std::clamp (amount / 100.0, 0.0, 1.0);
    double result = frequency;
    switch (stage.type)
    {
        case Type::Stretcher:
            // The reference partial stays fixed. +12 doubles the intervals:
            // 1,2,3 times the reference become 1,3,5 times the reference.
            result = reference + (frequency - reference) * std::exp2 (std::clamp (amount, -48.0, 48.0) / 12.0);
            break;
        case Type::StiffString:
        {
            // Normalized stiff-string dispersion. Keep the reference fixed and
            // increase upper-partial separation; B is a bounded stiffness.
            const double stiffness = 0.01 * blend * blend;
            const double ratio = frequency / reference;
            result = frequency * std::sqrt ((1.0 + stiffness * ratio * ratio) / (1.0 + stiffness));
            break;
        }
        case Type::Centroid:
        {
            const double target = reference * std::exp2 (std::clamp (finite (stage.pitch), -96.0, 96.0) / 12.0);
            result += (target - frequency) * blend;
            break;
        }
        case Type::PGrid:
        {
            if (frequency <= 0) break;
            const double spacing = std::clamp (finite (stage.pitch, 12.0), 0.01, 96.0);
            const double note = 12.0 * std::log2 (frequency / reference);
            const double target = reference * std::exp2 (std::round (note / spacing) * spacing / 12.0);
            result += (target - frequency) * blend;
            break;
        }
        case Type::FGrid:
        {
            const double spacing = std::clamp (finite (stage.pitch, 100.0), 0.01, 96000.0);
            const double target = std::round (frequency / spacing) * spacing;
            result += (target - frequency) * blend;
            break;
        }
    }
    return std::clamp (finite (result, maxComponent), 0.0, maxComponent);
}

void PartialProcessor::updateRouting (int slices, float crossfade, double hostRate) noexcept
{
    if (slices == cachedSlices && std::abs (crossfade - cachedCrossfade) < 1.0e-6f
        && std::abs (hostRate - cachedHostRate) < 1.0e-6) return;
    cachedSlices = slices;
    cachedCrossfade = crossfade;
    cachedHostRate = hostRate;
    const int bins = (int) assignments.size();
    const double top = std::max (40.0, hostRate * 0.5);
    for (int s = 0; s < maxSlices; ++s)
    {
        const double position = ((double) s + 0.5) / (double) slices;
        spans[(size_t) s] = { bins, -1, 20.0 * std::pow (top / 20.0, position) };
    }
    const FrequencySplit splitter (hostRate, slices, crossfade);
    for (int b = 0; b < bins; ++b)
    {
        const auto weights = splitter.sliceWeightsForBin (b, bins, sampleRate);
        assignments[(size_t) b] = { weights.first, weights.second, weights.firstWeight, weights.secondWeight };
        if (b == 0 || b == bins - 1) continue;
        auto include = [&] (int s, float weight)
        {
            if (s < 0 || weight <= 0) return;
            auto& span = spans[(size_t) s];
            span.first = std::min (span.first, b);
            span.last = std::max (span.last, b);
        };
        include (weights.first, weights.firstWeight);
        include (weights.second, weights.secondWeight);
    }
}

void PartialProcessor::process (const std::complex<float>* source, Routes destinations, int bins,
                                int sliceCount, float crossfade, double hostRate,
                                float frequencyLimit, int channel) noexcept
{
    if (source == nullptr || bins < 2) return;
    for (const auto* destination : destinations) if (destination == nullptr) return;
    for (auto* destination : destinations) std::fill_n (destination, bins, std::complex<float> {});
    const int slices = std::clamp (sliceCount, 1, maxSlices);
    crossfade = (float) std::clamp (finite (crossfade), 0.0, 100.0);
    hostRate = std::max (80.0, finite (hostRate, sampleRate));
    const bool prepared = workspace && workspace->synthesisFFT && bins == fftSize / 2 + 1
        && assignments.size() == (size_t) bins && ! workspace->oscillatorFrames[3].empty();
    if (! prepared || slices == 1 || ! hasActiveStages())
    {
        const FrequencySplit splitter (hostRate, slices, crossfade);
        for (int b = 0; b < bins; ++b)
        {
            const auto weights = splitter.weightsForBin (b, bins, sampleRate);
            const auto value = finiteSample (source[b]);
            for (size_t r = 0; r < 4; ++r) destinations[r][b] = value * weights[r];
        }
        return;
    }

    updateRouting (slices, crossfade, hostRate);
    channel = std::clamp (channel, 0, channels - 1);
    const auto frameIndex = frameCursors[(size_t) channel]++;
    const bool recordMovement = channel == 0 && frameIndex < movementHistory.size();
    const bool useSharedMovement = channel > 0 && frameIndex < recordedFrames;
    if (recordMovement)
    {
        movementHistory[frameIndex] = {};
        recordedFrames = frameIndex + 1;
    }
    const double binHz = sampleRate / (double) fftSize;
    const double limit = std::clamp (finite (frequencyLimit, hostRate * 0.5), 0.0, sampleRate * 0.5);
    const int lastInputBin = std::min (bins - 2, (int) std::floor (limit / binHz));
    const double phasePerHz = twoPi * (double) hopSize / sampleRate;
    const double smoothing = 1.0 - std::exp (-(double) hopSize / (sampleRate * 0.01));
    std::array<bool, 4> containsOscillators {};
    for (auto& frame : workspace->oscillatorFrames) std::fill (frame.begin(), frame.end(), 0.0f);
    destinations[0][0] = finiteSample (source[0]);
    destinations[0][bins - 1] = finiteSample (source[bins - 1]);

    // Frequencies the host has excluded from processing retain the legacy split
    // routing, independently of any band translations below the limit.
    for (int b = std::max (1, lastInputBin + 1); b < bins - 1; ++b)
    {
        const auto& a = assignments[(size_t) b];
        add (destinations[(size_t) (a.first % 4)][b], finiteSample (source[b]) * a.a);
        if (a.second >= 0) add (destinations[(size_t) (a.second % 4)][b], finiteSample (source[b]) * a.b);
    }

    for (int s = 0; s < slices; ++s)
    {
        const auto& span = spans[(size_t) s];
        auto& running = state[(size_t) channel][(size_t) s];
        const int end = std::min (span.last, lastInputBin);
        if (span.first > end) continue;
        double power = 0, frequencyPower = 0, peakPower = -1.0;
        std::complex<float> peak {};
        auto weightFor = [&] (int bin)
        {
            const auto& a = assignments[(size_t) bin];
            return a.first == s ? a.a : (a.second == s ? a.b : 0.0f);
        };
        for (int b = span.first; b <= end; ++b)
        {
            const auto value = finiteSample (source[b]) * weightFor (b);
            const double p = (double) value.real() * value.real() + (double) value.imag() * value.imag();
            power += p;
            frequencyPower += p * (double) b * binHz;
            if (p > peakPower) { peak = value; peakPower = p; }
        }
        double anchor = span.centre;
        if (anchorMode == AnchorMode::EnergyCentroid)
            anchor = power > 1.0e-20 ? frequencyPower / power : (running.lastAnchor > 0 ? running.lastAnchor : anchor);
        if (useSharedMovement && movementHistory[frameIndex][(size_t) s].anchor > 0)
            anchor = movementHistory[frameIndex][(size_t) s].anchor;
        running.lastAnchor = anchor;
        double destinationHz = anchor;
        bool resynth = false;
        Shape resynthShape = Shape::Sine;
        for (const auto& stage : stages)
        {
            if (! stage.enabled) continue;
            destinationHz = mapFrequency (destinationHz, referenceHz, stage);
            resynth = resynth || stage.mode == Mode::Resynth;
            if (stage.mode == Mode::Resynth) resynthShape = stage.shape;
        }
        const double requestedShift = destinationHz - anchor;
        if (! running.shiftReady)
        {
            running.smoothedShift = requestedShift;
            running.smoothedDestination = destinationHz;
            running.shiftReady = true;
        }
        else if (resynth)
        {
            // A full Centroid stage must stay at its requested pitch even when
            // the incoming band's energy centroid moves between frames.
            running.smoothedDestination += (destinationHz - running.smoothedDestination) * smoothing;
            running.smoothedShift = running.smoothedDestination - anchor;
        }
        else running.smoothedShift += (requestedShift - running.smoothedShift) * smoothing;
        if (std::abs (running.smoothedShift) < 1.0e-9) running.smoothedShift = 0;
        destinationHz = anchor + running.smoothedShift;
        running.smoothedDestination = destinationHz;
        if (useSharedMovement && movementHistory[frameIndex][(size_t) s].anchor > 0)
        {
            const auto& movement = movementHistory[frameIndex][(size_t) s];
            running.smoothedShift = movement.shift;
            destinationHz = movement.destination;
            running.smoothedDestination = destinationHz;
        }
        if (recordMovement) movementHistory[frameIndex][(size_t) s] = { anchor, running.smoothedShift, destinationHz };
        if (channel == 0) mappedAnchors[(size_t) s] = destinationHz;
        const size_t route = (size_t) (s % 4);

        if (resynth)
        {
            if (! running.sineReady)
            {
                running.sinePhase = peakPower > 1.0e-20 ? std::arg (peak) : 0;
                running.sineReady = true;
            }
            if (destinationHz >= binHz && destinationHz <= std::min (limit, (double) (bins - 2) * binHz) && power > 1.0e-20)
            {
                // Parseval calibration: positive-bin energy of a real sinusoid
                // is approximately A^2 * N * sum(window^2) / 4.
                const double amplitude = std::min (8.0, 2.0 * std::sqrt (power / ((double) fftSize * windowPower)));
                const double step = twoPi * destinationHz / sampleRate;
                const double stepRe = std::cos (step), stepIm = std::sin (step);
                double re = std::cos (running.sinePhase), im = std::sin (running.sinePhase);
                auto& frame = workspace->oscillatorFrames[route];
                if (resynthShape != Shape::Sine)
                    workspace->renderWaveform (frame, resynthShape, amplitude, running.sinePhase, destinationHz,
                                              std::min (limit, (double) (bins - 2) * binHz), sampleRate, true);
                else for (int n = 0; n < fftSize; ++n)
                {
                    frame[(size_t) n] += (float) (amplitude * re * workspace->window[(size_t) n]);
                    const double nextRe = re * stepRe - im * stepIm;
                    im = im * stepRe + re * stepIm;
                    re = nextRe;
                }
                containsOscillators[route] = true;
            }
        }
        else
        {
            if (std::abs (running.smoothedShift) < 1.0e-12) running.shiftPhase = 0;
            double shiftBins = running.smoothedShift / binHz;
            if (std::abs (shiftBins - std::round (shiftBins)) < 1.0e-9) shiftBins = std::round (shiftBins);
            // A fresh neutral Scale chain is exactly transparent. The phase
            // carrier keeps a translated band continuous across STFT hops.
            const auto rotation = std::polar (1.0f, (float) running.shiftPhase);
            // Adjacent bins of a windowed tone have a phase slope determined
            // by the window centre. Remove that slope during interpolation so
            // opposite main-lobe phases do not cancel at fractional offsets.
            const double fractionOfShift = shiftBins - std::floor (shiftBins);
            const auto leftRotation = rotation * std::polar (1.0f, (float) (fractionOfShift * windowBinPhase));
            const auto rightRotation = rotation * std::polar (1.0f, (float) ((fractionOfShift - 1.0) * windowBinPhase));
            for (int b = span.first; b <= end; ++b)
            {
                const double target = (double) b + shiftBins;
                if (target < 1.0 || target > (double) lastInputBin) continue;
                const int left = (int) std::floor (target);
                const float fraction = (float) (target - (double) left);
                const auto value = finiteSample (source[b]) * weightFor (b);
                add (destinations[route][left], value * leftRotation * (1.0f - fraction));
                if (fraction > 0 && left < lastInputBin) add (destinations[route][left + 1], value * rightRotation * fraction);
            }
        }
        // Keep both representations' time coordinates alive while switching
        // modes; oscillator phase is initialized from the source on first use.
        running.shiftPhase = std::remainder (running.shiftPhase + phasePerHz * running.smoothedShift, twoPi);
        if (running.sineReady) running.sinePhase = std::remainder (running.sinePhase + phasePerHz * destinationHz, twoPi);
    }

    for (size_t r = 0; r < 4; ++r)
    {
        if (! containsOscillators[r]) continue;
        auto& frame = workspace->oscillatorFrames[r];
        workspace->synthesisFFT->performRealOnlyForwardTransform (frame.data(), true);
        for (int b = 1; b <= lastInputBin; ++b)
            add (destinations[r][b], finiteSample ({ frame[(size_t) (2 * b)], frame[(size_t) (2 * b + 1)] }));
    }
}

void PartialProcessor::processSlice (std::complex<float>* spectrum, int bins, SliceContext& context,
                                     float frequencyLimit, int channel) noexcept
{
    if (! workspace || ! workspace->synthesisFFT || bins != fftSize / 2 + 1 || spectrum == nullptr || ! hasActiveStages()) return;
    if (! workspace->hasWaveforms())
    {
        if (context.singleSine && context.resynthShape != Shape::Sine) return;
        for (const auto& stage : stages) if (stage.enabled && stage.mode == Mode::Resynth && stage.shape != Shape::Sine) return;
    }
    const auto index = (size_t) std::clamp (context.index, 0, maxSlices - 1);
    channel = std::clamp (channel, 0, channels - 1);
    const auto frameIndex = sliceFrameCursors[(size_t) channel][index]++;
    const bool record = channel == 0 && frameIndex < movementHistory.size();
    const bool shared = anchorMode == AnchorMode::BandCentre && channel > 0
        && frameIndex < recordedFrames && movementHistory[frameIndex][index].anchor > 0;
    if (record && frameIndex >= recordedFrames)
    {
        movementHistory[frameIndex] = {};
        recordedFrames = frameIndex + 1;
    }
    for (int b = 0; b < bins; ++b)
        spectrum[b] = workspace->sliceInput[(size_t) b] = finiteSample (spectrum[b]);
    const double binHz = sampleRate / (double) fftSize;
    const double limit = std::clamp (finite (frequencyLimit, sampleRate * 0.5), 0.0, sampleRate * 0.5);
    const int last = std::min (bins - 2, (int) std::floor (limit / binHz));
    double power = 0, weightedFrequency = 0, peakPower = -1;
    std::complex<float> peak {};
    for (int b = 1; b <= last; ++b)
    {
        const auto value = finiteSample (workspace->sliceInput[(size_t) b]);
        const double p = (double) value.real() * value.real() + (double) value.imag() * value.imag();
        power += p;
        weightedFrequency += p * (double) b * binHz;
        if (p > peakPower) { peakPower = p; peak = value; }
        spectrum[b] = {};
    }
    auto& running = state[(size_t) channel][index];
    double anchor = context.currentAnchorHz > 0 ? context.currentAnchorHz : context.originalCentreHz;
    if (anchorMode == AnchorMode::EnergyCentroid && power > 1.0e-20) anchor = weightedFrequency / power;
    if (shared) anchor = movementHistory[frameIndex][index].anchor;
    anchor = std::max (1.0, finite (anchor, binHz));
    double destination = anchor;
    bool resynth = context.singleSine;
    Shape resynthShape = context.resynthShape;
    bool explicitResynth = false;
    for (const auto& stage : stages)
    {
        if (! stage.enabled) continue;
        destination = mapFrequency (destination, referenceHz, stage);
        resynth = resynth || stage.mode == Mode::Resynth;
        if (stage.mode == Mode::Resynth) { resynthShape = stage.shape; explicitResynth = true; }
    }
    // An inherited waveform's anchor remains its fundamental. Selecting its
    // energy centroid changes the anchor of the operation, not a neutral
    // Scale stage's oscillator pitch.
    if (context.singleSine && ! explicitResynth && context.currentAnchorHz > 0)
        destination += context.currentAnchorHz - anchor;
    const double smoothing = 1.0 - std::exp (-(double) hopSize / (sampleRate * 0.01));
    const double requestedShift = destination - anchor;
    if (! running.shiftReady)
    {
        running.smoothedShift = requestedShift;
        running.smoothedDestination = destination;
        running.shiftReady = true;
    }
    else if (resynth)
    {
        running.smoothedDestination += (destination - running.smoothedDestination) * smoothing;
        running.smoothedShift = running.smoothedDestination - anchor;
    }
    else running.smoothedShift += (requestedShift - running.smoothedShift) * smoothing;
    if (std::abs (running.smoothedShift) < 1.0e-9) running.smoothedShift = 0;
    destination = anchor + running.smoothedShift;
    if (shared)
    {
        running.smoothedShift = movementHistory[frameIndex][index].shift;
        destination = movementHistory[frameIndex][index].destination;
    }
    running.smoothedDestination = destination;
    if (record) movementHistory[frameIndex][index] = { anchor, running.smoothedShift, destination };
    if (channel == 0) mappedAnchors[index] = destination;
    context.currentAnchorHz = destination;
    context.singleSine = resynth;
    if (resynth) context.resynthShape = resynthShape;

    if (resynth)
    {
        if (! running.sineReady)
        {
            running.sinePhase = peakPower > 1.0e-20 ? std::arg (peak) : 0;
            running.sineReady = true;
        }
        if (power > 1.0e-20 && destination >= binHz && destination <= (double) last * binHz)
        {
            auto& frame = workspace->oscillatorFrames[0];
            std::fill (frame.begin(), frame.end(), 0.0f);
            const double amplitude = std::min (8.0, 2.0 * std::sqrt (power / ((double) fftSize * windowPower)));
            const double step = twoPi * destination / sampleRate;
            const double stepRe = std::cos (step), stepIm = std::sin (step);
            double re = std::cos (running.sinePhase), im = std::sin (running.sinePhase);
            if (resynthShape != Shape::Sine)
                workspace->renderWaveform (frame, resynthShape, amplitude, running.sinePhase, destination,
                                          (double) last * binHz, sampleRate, false);
            else for (int n = 0; n < fftSize; ++n)
            {
                frame[(size_t) n] = (float) (amplitude * re * workspace->window[(size_t) n]);
                const double next = re * stepRe - im * stepIm;
                im = im * stepRe + re * stepIm;
                re = next;
            }
            workspace->synthesisFFT->performRealOnlyForwardTransform (frame.data(), true);
            for (int b = 1; b <= last; ++b)
                spectrum[b] = finiteSample ({ frame[(size_t) (2 * b)], frame[(size_t) (2 * b + 1)] });
        }
    }
    else
    {
        if (std::abs (running.smoothedShift) < 1.0e-12) running.shiftPhase = 0;
        double shift = running.smoothedShift / binHz;
        if (std::abs (shift - std::round (shift)) < 1.0e-9) shift = std::round (shift);
        const double fraction = shift - std::floor (shift);
        const auto carrier = std::polar (1.0f, (float) running.shiftPhase);
        const auto leftPhase = carrier * std::polar (1.0f, (float) (fraction * windowBinPhase));
        const auto rightPhase = carrier * std::polar (1.0f, (float) ((fraction - 1.0) * windowBinPhase));
        for (int b = 1; b <= last; ++b)
        {
            const double target = (double) b + shift;
            if (target < 1 || target > last) continue;
            const int left = (int) std::floor (target);
            const float t = (float) (target - left);
            const auto value = finiteSample (workspace->sliceInput[(size_t) b]);
            add (spectrum[left], value * leftPhase * (1 - t));
            if (t > 0 && left < last) add (spectrum[left + 1], value * rightPhase * t);
        }
    }
    const double phasePerHz = twoPi * hopSize / sampleRate;
    running.shiftPhase = std::remainder (running.shiftPhase + phasePerHz * running.smoothedShift, twoPi);
    if (running.sineReady) running.sinePhase = std::remainder (running.sinePhase + phasePerHz * destination, twoPi);
}

} // namespace scrr::dsp

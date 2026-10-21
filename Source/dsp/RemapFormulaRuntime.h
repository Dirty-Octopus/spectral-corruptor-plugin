// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../parameters/RemapFormula.h"
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>

namespace scrr::dsp {

/** One serially shared runtime per rack slot, including all its slice clones.
    The control thread compiles the program. Numeric automation only changes a
    fixed parameter array. Affine maps cache two exact endpoint evaluations;
    other maps execute at each phase-derived source coordinate (at most64 scalar
    instructions). An FFT-bin lookup would alter nonlinear functions because
    physical source frequencies differ from bin centres and between clones.
    Evaluation/parameter updates never parse strings or allocate storage. */
class RemapFormulaRuntime
{
public:
    void setProgram (std::shared_ptr<const scrr::params::FormulaProgram> next) noexcept
    {
        if (program == next) return;
        program = std::move (next); dirty = true;
        values.fill (0);
        if (program) for (size_t i = 0; i < program->parameterCount; ++i) values[i] = program->parameters[i].defaultValue;
    }
    void updateParameters (const juce::ValueTree& state) noexcept
    {
        if (! program) return;
        for (size_t i = 0; i < program->parameterCount; ++i)
        {
            const auto& parameter = program->parameters[i];
            const double requested = (double) state.getProperty (program->properties[i], parameter.defaultValue);
            const double value = std::isfinite (requested) ? juce::jlimit (parameter.minimum, parameter.maximum, requested) : parameter.defaultValue;
            if (! scrr::params::formulaExactEqual (values[i], value)) { values[i] = value; dirty = true; }
        }
    }
    void prepareTable (double frequencyLimit) noexcept
    {
        const double nextTop = std::isfinite (frequencyLimit) ? std::max (20.000001, frequencyLimit) : 22050.0;
        if (! scrr::params::formulaExactEqual (top, nextTop)) { top = nextTop; logRange = std::log (top / 20.0); dirty = true; }
        if (! dirty) return;
        dirty = false; ++buildCount;
        errorNode.store (-1, std::memory_order_relaxed); invalidCount.store (0, std::memory_order_relaxed);
        identity = ! program || ! program->valid;
        if (identity) { recordError (-1); return; }
        const double nextInputLow = program->unit == scrr::params::FormulaUnit::Normalized ? 0.0 : 20.0;
        const double nextInputHigh = program->unit == scrr::params::FormulaUnit::Normalized ? 1.0 : top;
        bool nextAffineReady = false;
        double nextOutputLow = 0, nextOutputHigh = 0;
        if (program->affineInInput)
        {
            const auto a = program->evaluate (nextInputLow, values), b = program->evaluate (nextInputHigh, values);
            if (! a.valid || ! b.valid) recordError (! a.valid ? a.errorNode : b.errorNode);
            else
            {
                nextOutputLow = a.value; nextOutputHigh = b.value;
                nextAffineReady = std::isfinite (b.value - a.value);
                identity = scrr::params::formulaExactEqual (a.value, nextInputLow) && scrr::params::formulaExactEqual (b.value, nextInputHigh);
            }
        }
        // FrequencyRemap fades the last wet map to dry over20ms. Retain its
        // function/carrier relationship until that fade finishes; replacing
        // coefficients with identity here would reset phase abruptly. Storage
        // is fixed and shared by all clones, including a previous nonlinear map.
        if (identity && activeProgram) return;
        activeProgram = program; activeValues = values;
        affineReady = nextAffineReady;
        inputLow = nextInputLow; inputHigh = nextInputHigh;
        outputLow = nextOutputLow; outputHigh = nextOutputHigh;
    }

    /** Destination Hz. Finite out-of-range output means discard; invalid math
        means preserve this source and expose a diagnostic. Display settings
        and plot sampling precision do not change this evaluation. */
    scrr::params::FormulaResult evaluateHz (double hz) const noexcept
    {
        if (! activeProgram || ! activeProgram->valid || top <= 20 || ! std::isfinite (hz)) return { hz, false, -1 };
        const double clampedHz = juce::jlimit (20.0, top, hz);
        const double input = activeProgram->unit == scrr::params::FormulaUnit::Normalized ? std::log (clampedHz / 20.0) / logRange : clampedHz;
        const auto result = affineReady ? scrr::params::FormulaResult {
            outputLow + (outputHigh - outputLow) * ((input - inputLow) / (inputHigh - inputLow)), true, -1 }
            : activeProgram->evaluate (input, activeValues);
        if (! result.valid || ! std::isfinite (result.value)) { recordError (result.errorNode); return { hz, false, result.errorNode }; }
        if (activeProgram->unit == scrr::params::FormulaUnit::Hertz) return result;
        if (result.value < 0) return { 0, true, -1 };
        if (result.value > 1) return { top + 1, true, -1 };
        return { 20.0 * std::exp (result.value * logRange), true, -1 };
    }
    bool isIdentity() const noexcept { return identity; }
    bool usesAffineCache() const noexcept { return affineReady; }
    uint64_t getBuildCount() const noexcept { return buildCount; }
    int getInvalidSampleCount() const noexcept { return invalidCount.load (std::memory_order_relaxed); }
    int getErrorNode() const noexcept { return errorNode.load (std::memory_order_relaxed); }
    const std::shared_ptr<const scrr::params::FormulaProgram>& getProgram() const noexcept { return program; }

private:
    void recordError (int node) const noexcept
    {
        errorNode.store (node, std::memory_order_relaxed);
        const auto count = invalidCount.load (std::memory_order_relaxed);
        invalidCount.store (std::min (65535, count + 1), std::memory_order_relaxed);
    }
    std::shared_ptr<const scrr::params::FormulaProgram> program;
    std::shared_ptr<const scrr::params::FormulaProgram> activeProgram;
    std::array<double, scrr::params::RemapFormula::maxParameters> values {};
    std::array<double, scrr::params::RemapFormula::maxParameters> activeValues {};
    mutable std::atomic<int> errorNode { -1 }, invalidCount {};
    double top {}, logRange { 1 }, inputLow {}, inputHigh { 1 }, outputLow {}, outputHigh { 1 };
    uint64_t buildCount {};
    bool dirty { true }, identity { true }, affineReady {};
};
} // namespace scrr::dsp

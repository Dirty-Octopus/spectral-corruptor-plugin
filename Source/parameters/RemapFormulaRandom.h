// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "RemapFormula.h"

namespace scrr::params {
inline constexpr int randomRemapDefaultComplexity = 8;
inline constexpr int randomRemapMinimumComplexity = 3;
inline constexpr int randomRemapMaximumComplexity = (int) RemapFormula::maxNodes;

/** Authoring-only, reproducible connected graph. Complexity counts every node,
    including constants and the input. Existing parameter definitions/values are
    deliberately left to the caller: this generator creates no parameter IDs.
    Outputs stay in the processing domain for inputs in that domain; the graph
    uses ordinary Formula operations, without special runtime evaluation. */
inline RemapFormula makeRandomRemapFormula (FormulaUnit unit, double frequencyLimit,
                                           int complexity, juce::int64 seed)
{
    RemapFormula formula; formula.unit = unit; formula.nodes.clear();
    const int count = juce::jlimit (randomRemapMinimumComplexity, randomRemapMaximumComplexity, complexity);
    formula.nodes.reserve ((size_t) count);
    juce::Random random (seed);
    const double top = std::isfinite (frequencyLimit) ? std::max (20.000001, frequencyLimit) : 24000.0;
    const double span = top - 20.0;
    auto between = [&] (double low, double high) { return low + (high - low) * random.nextDouble(); };
    auto add = [&] (FormulaOp op, int a = -1, int b = -1, int c = -1, double value = 0.0)
    {
        FormulaNode node; node.id = (int) formula.nodes.size(); node.op = op;
        node.inputs = {{ a, b, c }}; node.value = value;
        // A compact serpentine reading order keeps adjacent operations close,
        // while leaving long graphs available through the infinite canvas.
        const int row = node.id / 4, column = node.id % 4;
        node.canvasX = 40.0 + 270.0 * (row % 2 == 0 ? column : 3 - column);
        node.canvasY = 60.0 + 210.0 * row;
        formula.nodes.push_back (node); return node.id;
    };
    auto constant = [&] (double value) { return add (FormulaOp::Constant, -1, -1, -1, value); };
    const int input = add (FormulaOp::Input);

    if (unit == FormulaUnit::Hertz && count <= 8)
    {
        int current = input;
        if (count < 5)
        {
            const bool lower = random.nextBool();
            const int bound = constant (20.0 + span * (lower ? between (.08, .35) : between (.65, .92)));
            current = add (lower ? FormulaOp::Max : FormulaOp::Min, current, bound);
        }
        else
        {
            const double gain = between (.35, .8);
            const int scale = constant (gain);
            current = add (FormulaOp::Multiply, current, scale);
            const int offset = constant (20.0 * (1.0 - gain) + span * between (.04, (1.0 - gain) * .8));
            current = add (FormulaOp::Add, current, offset);
            if (count >= 7)
            {
                const double low = 20.0 * gain + formula.nodes[(size_t) offset].value;
                const int cap = constant (low + span * gain * between (.55, .85));
                current = add (FormulaOp::Min, current, cap);
            }
        }
        // Native-Hz quantisation is a useful single-node variation; it also
        // keeps small even-sized graphs connected without padding with no-ops.
        if (count == 4 || count == 6 || count == 8) current = add (FormulaOp::Floor, current);
        formula.outputNode = current;
        return formula;
    }

    int low = -1, width = -1, current = input;
    if (unit == FormulaUnit::Hertz)
    {
        low = constant (20.0);
        current = add (FormulaOp::Subtract, current, low);
        width = constant (span);
        current = add (FormulaOp::Divide, current, width);
    }
    const int normalizedInput = current;
    const int outputNodes = unit == FormulaUnit::Hertz ? 2 : 0;
    const int remaining = count - (int) formula.nodes.size() - outputNodes;
    const bool blendFinal = remaining >= 5;
    int work = remaining - (blendFinal ? 5 : 2), sinceBlend = 0;
    auto blendWithInput = [&]
    {
        const int sum = add (FormulaOp::Add, current, normalizedInput);
        const int half = constant (.5);
        current = add (FormulaOp::Multiply, sum, half);
    };
    while (work > 0)
    {
        if (work >= 3 && sinceBlend >= 6)
        {
            blendWithInput(); work -= 3; sinceBlend = 0; continue;
        }
        const int choice = random.nextInt (work >= 3 ? 12 : (work >= 2 ? 11 : 4));
        int used = 1;
        switch (choice)
        {
            case 0: current = add (FormulaOp::Sqrt, current); break;
            case 1: current = add (FormulaOp::Multiply, current, current); break;
            case 2: current = add (FormulaOp::Sin, current); break;
            case 3: current = add (FormulaOp::Cos, current); break;
            case 4: { const int power = constant (between (.35, 2.8)); current = add (FormulaOp::Power, current, power); used = 2; break; }
            case 5: { const int one = constant (1); current = add (FormulaOp::Subtract, one, current); used = 2; break; }
            case 6: { const int cap = constant (between (.65, .95)); current = add (FormulaOp::Min, current, cap); used = 2; break; }
            case 7: { const int floor = constant (between (.05, .35)); current = add (FormulaOp::Max, current, floor); used = 2; break; }
            case 8: { const int scale = constant (between (.45, .9)); current = add (FormulaOp::Multiply, current, scale); used = 2; break; }
            case 9: { const int divisor = constant (between (1.1, 2)); current = add (FormulaOp::Divide, current, divisor); used = 2; break; }
            case 10: { const int period = constant (between (.45, .95)); current = add (FormulaOp::Modulo, current, period); used = 2; break; }
            default:
            {
                const int lower = constant (between (.05, .25)), upper = constant (between (.65, .95));
                current = add (FormulaOp::Clamp, current, lower, upper); used = 3; break;
            }
        }
        work -= used; sinceBlend += used;
    }
    if (blendFinal)
    {
        // This final contraction/blend guarantees a nonidentity result and at
        // least 7.5% endpoint separation, even after a folded or saturated core.
        const int scale = constant (between (.55, .85));
        current = add (FormulaOp::Multiply, current, scale);
        blendWithInput();
    }
    else
    {
        const int scale = constant (between (.55, .85));
        current = add (FormulaOp::Multiply, current, scale);
    }
    if (unit == FormulaUnit::Hertz)
    {
        current = add (FormulaOp::Multiply, current, width);
        current = add (FormulaOp::Add, current, low);
    }
    formula.outputNode = current;
    return formula;
}
} // namespace scrr::params

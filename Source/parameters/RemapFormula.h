// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

namespace scrr::params {

// Exact comparison is intentional for cache invalidation, algebraic identity
// and mathematical zero domains. A tolerance would change the user's formula.
inline bool formulaExactEqual (double a, double b) noexcept { return std::equal_to<double>{} (a, b); }

enum class FormulaUnit { Hertz = 0, Normalized = 1 };
enum class FormulaOp { Input, Constant, Parameter, Add, Subtract, Multiply, Divide, Power,
    Negate, Abs, Min, Max, Clamp, Sqrt, Exp, Log, Log2, Log10, Sin, Cos, Tan,
    Floor, Ceil, Round, Modulo, Sign };

inline const char* formulaOpName (FormulaOp op) noexcept
{
    static constexpr const char* names[] { "x", "Constant", "Parameter", "+", "-", "Multiply", "Divide", "Power",
        "Negate", "Abs", "Min", "Max", "Clamp", "Sqrt", "Exp", "Log", "Log2", "Log10", "Sin", "Cos", "Tan",
        "Floor", "Ceil", "Round", "Modulo", "Sign" };
    const auto i = (int) op;
    return i >= 0 && i <= (int) FormulaOp::Sign ? names[i] : "Unknown";
}
inline int formulaOpArity (FormulaOp op) noexcept
{
    switch (op)
    {
        case FormulaOp::Input: case FormulaOp::Constant: case FormulaOp::Parameter: return 0;
        case FormulaOp::Add: case FormulaOp::Subtract: case FormulaOp::Multiply: case FormulaOp::Divide:
        case FormulaOp::Power: case FormulaOp::Min: case FormulaOp::Max: case FormulaOp::Modulo: return 2;
        case FormulaOp::Clamp: return 3;
        case FormulaOp::Negate: case FormulaOp::Abs: case FormulaOp::Sqrt: case FormulaOp::Exp:
        case FormulaOp::Log: case FormulaOp::Log2: case FormulaOp::Log10: case FormulaOp::Sin:
        case FormulaOp::Cos: case FormulaOp::Tan: case FormulaOp::Floor: case FormulaOp::Ceil:
        case FormulaOp::Round: case FormulaOp::Sign: return 1;
    }
    return 0;
}

struct FormulaNode
{
    int id {};
    FormulaOp op { FormulaOp::Input };
    std::array<int, 3> inputs {{ -1, -1, -1 }};
    double value {};
    juce::String parameterId;
    double canvasX { 40 }, canvasY { 80 };
};
struct FormulaParameter
{
    juce::String id, name;
    double minimum {}, maximum { 1 }, defaultValue {}, step { .001 };
    bool logarithmic {};
    juce::Identifier property() const { return juce::Identifier ("formulaParam_" + id); }
};

inline bool validFormulaParameter (const FormulaParameter& parameter) noexcept
{
    return parameter.id.isNotEmpty() && parameter.id.length() <= 48
        && parameter.id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
        && parameter.name.trim().isNotEmpty() && parameter.name.length() <= 48
        && ! parameter.name.trim().equalsIgnoreCase ("x") && ! parameter.name.trim().equalsIgnoreCase ("y")
        && std::isfinite (parameter.minimum) && std::isfinite (parameter.maximum) && parameter.minimum < parameter.maximum
        && std::isfinite (parameter.maximum - parameter.minimum)
        && std::isfinite (parameter.defaultValue) && parameter.defaultValue >= parameter.minimum && parameter.defaultValue <= parameter.maximum
        && std::isfinite (parameter.step) && parameter.step >= 0 && parameter.step <= parameter.maximum - parameter.minimum
        && (! parameter.logarithmic || parameter.minimum > 0);
}

/** Authoring model only: JSON parsing and compilation belong on the control
    thread. Stable parameter IDs survive display-name and range changes. */
struct RemapFormula
{
    static constexpr size_t maxNodes = 64, maxParameters = 16;
    FormulaUnit unit { FormulaUnit::Hertz };
    int outputNode {};
    std::vector<FormulaNode> nodes { FormulaNode {} };
    std::vector<FormulaParameter> parameters;

    juce::String encode() const
    {
        auto* root = new juce::DynamicObject;
        root->setProperty ("version", 1); root->setProperty ("unit", (int) unit); root->setProperty ("output", outputNode);
        juce::Array<juce::var> nodeArray, parameterArray;
        for (const auto& node : nodes)
        {
            auto* item = new juce::DynamicObject;
            item->setProperty ("id", node.id); item->setProperty ("op", (int) node.op);
            juce::Array<juce::var> inputs; for (const int input : node.inputs) inputs.add (input);
            item->setProperty ("inputs", inputs); item->setProperty ("value", node.value);
            item->setProperty ("parameter", node.parameterId); item->setProperty ("x", node.canvasX); item->setProperty ("y", node.canvasY);
            nodeArray.add (juce::var (item));
        }
        for (const auto& parameter : parameters)
        {
            auto* item = new juce::DynamicObject;
            item->setProperty ("id", parameter.id); item->setProperty ("name", parameter.name);
            item->setProperty ("min", parameter.minimum); item->setProperty ("max", parameter.maximum);
            item->setProperty ("default", parameter.defaultValue); item->setProperty ("step", parameter.step);
            item->setProperty ("log", parameter.logarithmic); parameterArray.add (juce::var (item));
        }
        root->setProperty ("nodes", nodeArray); root->setProperty ("parameters", parameterArray);
        return juce::JSON::toString (juce::var (root), true);
    }

    static RemapFormula decode (const juce::String& encoded, juce::String* error = nullptr)
    {
        if (error) error->clear();
        auto fail = [&] (const char* message) { if (error) *error = message; return RemapFormula {}; };
        if (encoded.isEmpty()) return {};
        if (encoded.length() > 65536) return fail ("Formula data exceeds 64 KiB.");
        // Bound nesting before handing externally supplied preset data to JSON.
        int depth = 0; bool quoted = false, escaped = false;
        for (const auto ch : encoded)
        {
            if (quoted) { if (escaped) escaped = false; else if (ch == '\\') escaped = true; else if (ch == '"') quoted = false; }
            else if (ch == '"') quoted = true;
            else if (ch == '{' || ch == '[') { if (++depth > 16) return fail ("Formula data is nested too deeply."); }
            else if (ch == '}' || ch == ']') { if (--depth < 0) return fail ("Malformed formula data."); }
        }
        juce::var parsed;
        if (juce::JSON::parse (encoded, parsed).failed() || ! parsed.isObject()) return fail ("Malformed formula data.");
        const auto* root = parsed.getDynamicObject();
        if ((int) root->getProperty ("version") != 1) return fail ("Unsupported formula version.");
        const int unitValue = (int) root->getProperty ("unit");
        if (unitValue < 0 || unitValue > 1) return fail ("Unknown formula units.");
        const auto* nodeArray = root->getProperty ("nodes").getArray();
        const auto* parameterArray = root->getProperty ("parameters").getArray();
        if (! nodeArray || nodeArray->size() > (int) maxNodes
            || ! parameterArray || parameterArray->size() > (int) maxParameters) return fail ("Formula supports 64 nodes and 16 parameters.");
        RemapFormula result; result.unit = (FormulaUnit) unitValue; result.outputNode = (int) root->getProperty ("output"); result.nodes.clear();
        for (const auto& item : *nodeArray)
        {
            const auto* object = item.getDynamicObject(); if (! object) return fail ("Malformed formula node.");
            FormulaNode node; node.id = (int) object->getProperty ("id"); const int op = (int) object->getProperty ("op");
            if (node.id < 0 || node.id > 1000000 || op < 0 || op > (int) FormulaOp::Sign) return fail ("Unknown formula node.");
            node.op = (FormulaOp) op;
            const auto* inputs = object->getProperty ("inputs").getArray();
            if (! inputs || inputs->size() != 3) return fail ("Malformed node connections.");
            for (int i = 0; i < 3; ++i) node.inputs[(size_t) i] = (int) inputs->getReference (i);
            node.value = (double) object->getProperty ("value"); node.parameterId = object->getProperty ("parameter").toString();
            node.canvasX = (double) object->getProperty ("x"); node.canvasY = (double) object->getProperty ("y");
            if (! std::isfinite (node.value) || ! std::isfinite (node.canvasX) || ! std::isfinite (node.canvasY)
                || node.parameterId.length() > 48) return fail ("Invalid node value.");
            result.nodes.push_back (std::move (node));
        }
        for (const auto& item : *parameterArray)
        {
            const auto* object = item.getDynamicObject(); if (! object) return fail ("Malformed formula parameter.");
            FormulaParameter parameter; parameter.id = object->getProperty ("id").toString(); parameter.name = object->getProperty ("name").toString();
            parameter.minimum = (double) object->getProperty ("min"); parameter.maximum = (double) object->getProperty ("max");
            parameter.defaultValue = (double) object->getProperty ("default"); parameter.step = (double) object->getProperty ("step");
            parameter.logarithmic = (bool) object->getProperty ("log"); parameter.name = parameter.name.trim();
            if (! validFormulaParameter (parameter)) return fail ("Invalid parameter name or range.");
            for (const auto& existing : result.parameters)
                if (existing.id == parameter.id || existing.name.equalsIgnoreCase (parameter.name)) return fail ("Parameter IDs and names must be unique.");
            result.parameters.push_back (std::move (parameter));
        }
        return result;
    }
};

struct FormulaResult { double value {}; bool valid { true }; int errorNode { -1 }; };

enum class FormulaTraceStatus { NotEvaluated, Value, Error };
struct FormulaTraceNode
{
    int nodeId { -1 };
    double value {};
    FormulaTraceStatus status { FormulaTraceStatus::NotEvaluated };
};
struct FormulaTrace
{
    FormulaResult result;
    std::array<FormulaTraceNode, RemapFormula::maxNodes> nodes {};
    size_t nodeCount {};

    const FormulaTraceNode* find (int nodeId) const noexcept
    {
        for (size_t i = 0; i < nodeCount; ++i)
            if (nodes[i].nodeId == nodeId) return &nodes[i];
        return nullptr;
    }
};

/** Fixed-size, topologically ordered scalar program. Evaluation neither allocates
    nor parses strings, and invalid domains never propagate NaN into audio. */
struct FormulaProgram
{
    struct Instruction { FormulaOp op {}; std::array<int, 3> inputs {{ -1, -1, -1 }}; double value {}; int parameter {}, nodeId {}; };
    bool valid {};
    juce::String error;
    FormulaUnit unit { FormulaUnit::Hertz };
    std::array<Instruction, RemapFormula::maxNodes> instructions {};
    std::array<FormulaParameter, RemapFormula::maxParameters> parameters {};
    std::array<juce::Identifier, RemapFormula::maxParameters> properties {};
    size_t instructionCount {}, parameterCount {};
    int outputIndex {};
    bool affineInInput {};

    bool sameEvaluationAs (const FormulaProgram& other) const noexcept
    {
        if (valid != other.valid || unit != other.unit || instructionCount != other.instructionCount
            || parameterCount != other.parameterCount || outputIndex != other.outputIndex) return false;
        for (size_t i = 0; i < instructionCount; ++i)
        {
            const auto& a = instructions[i]; const auto& b = other.instructions[i];
            if (a.op != b.op || a.inputs != b.inputs || ! formulaExactEqual (a.value, b.value) || a.parameter != b.parameter || a.nodeId != b.nodeId) return false;
        }
        for (size_t i = 0; i < parameterCount; ++i)
        {
            const auto& a = parameters[i]; const auto& b = other.parameters[i];
            if (a.id != b.id || ! formulaExactEqual (a.minimum, b.minimum) || ! formulaExactEqual (a.maximum, b.maximum)
                || ! formulaExactEqual (a.defaultValue, b.defaultValue)) return false;
        }
        return true;
    }

    static FormulaProgram compile (const RemapFormula& formula)
    {
        FormulaProgram program; program.unit = formula.unit;
        auto fail = [&] (juce::String message) { program.error = std::move (message); return program; };
        if (formula.nodes.empty()) return fail ("Connect a node to output y.");
        if (formula.nodes.size() > RemapFormula::maxNodes || formula.parameters.size() > RemapFormula::maxParameters)
            return fail ("Formula supports 64 nodes and 16 parameters.");
        for (const auto& parameter : formula.parameters)
        {
            if (! validFormulaParameter (parameter)) return fail ("Invalid parameter name or range.");
            for (size_t i = 0; i < program.parameterCount; ++i)
                if (program.parameters[i].id == parameter.id || program.parameters[i].name.equalsIgnoreCase (parameter.name)) return fail ("Parameter IDs and names must be unique.");
            program.parameters[program.parameterCount] = parameter; program.properties[program.parameterCount] = parameter.property(); ++program.parameterCount;
        }
        for (size_t i = 0; i < formula.nodes.size(); ++i)
        {
            const auto& node = formula.nodes[i];
            if (node.id < 0 || (int) node.op < 0 || (int) node.op > (int) FormulaOp::Sign || ! std::isfinite (node.value)) return fail ("Invalid formula node.");
            for (size_t j = 0; j < i; ++j) if (formula.nodes[j].id == node.id) return fail ("Node IDs must be unique.");
        }
        auto indexOf = [&] (int id) { for (size_t i = 0; i < formula.nodes.size(); ++i) if (formula.nodes[i].id == id) return (int) i; return -1; };
        const int root = indexOf (formula.outputNode); if (root < 0) return fail ("Connect a node to output y.");
        std::array<uint8_t, RemapFormula::maxNodes> visit {};
        std::array<int, RemapFormula::maxNodes> emitted {}; emitted.fill (-1);
        auto emit = [&] (auto&& self, int index) -> bool
        {
            if (visit[(size_t) index] == 2) return true;
            if (visit[(size_t) index] == 1) { program.error = "Connections cannot form a cycle."; return false; }
            visit[(size_t) index] = 1;
            const auto& node = formula.nodes[(size_t) index]; Instruction instruction; instruction.op = node.op; instruction.value = node.value; instruction.nodeId = node.id;
            for (int pin = 0; pin < formulaOpArity (node.op); ++pin)
            {
                const int input = indexOf (node.inputs[(size_t) pin]);
                if (input < 0) { program.error = "Node " + juce::String (node.id) + ": connect every input."; return false; }
                if (! self (self, input)) return false;
                instruction.inputs[(size_t) pin] = emitted[(size_t) input];
            }
            if (node.op == FormulaOp::Parameter)
            {
                int parameter = -1;
                for (size_t i = 0; i < program.parameterCount; ++i) if (program.parameters[i].id == node.parameterId) parameter = (int) i;
                if (parameter < 0) { program.error = "Node " + juce::String (node.id) + ": choose a parameter."; return false; }
                instruction.parameter = parameter;
            }
            emitted[(size_t) index] = (int) program.instructionCount; program.instructions[program.instructionCount++] = instruction;
            visit[(size_t) index] = 2; return true;
        };
        if (! emit (emit, root)) return program;
        program.outputIndex = emitted[(size_t) root];
        // Conservatively identify affine maps; nonlinear and piecewise maps
        // must execute exactly, because sampling can alias sin/floor/modulo.
        std::array<int, RemapFormula::maxNodes> degree {};
        for (size_t i = 0; i < program.instructionCount; ++i)
        {
            const auto& instruction = program.instructions[i];
            auto inputDegree = [&] (size_t pin) { return instruction.inputs[pin] >= 0 ? degree[(size_t) instruction.inputs[pin]] : 0; };
            const int a = inputDegree (0), b = inputDegree (1), c = inputDegree (2);
            if (instruction.op == FormulaOp::Input) degree[i] = 1;
            else if (a == 0 && b == 0 && c == 0) degree[i] = 0;
            else if (instruction.op == FormulaOp::Add || instruction.op == FormulaOp::Subtract) degree[i] = std::max (a, b);
            else if (instruction.op == FormulaOp::Negate) degree[i] = a;
            else if (instruction.op == FormulaOp::Multiply) degree[i] = std::min (2, a + b);
            else if (instruction.op == FormulaOp::Divide && b == 0) degree[i] = a;
            else degree[i] = 2;
        }
        program.affineInInput = degree[(size_t) program.outputIndex] <= 1;
        program.valid = true; return program;
    }

    FormulaResult evaluate (double x, const std::array<double, RemapFormula::maxParameters>& values) const noexcept
    {
        return evaluateImpl<false> (x, values, nullptr);
    }

    /** UI probe in the program's native units, with the caller's effective
        parameter values. Only output-reachable compiled nodes are included.
        Failed nodes report Error; execution stops exactly where evaluate does,
        leaving later nodes NotEvaluated. An invalid program or nonfinite x
        leaves every node NotEvaluated and returns the usual invalid result. */
    FormulaTrace evaluateTrace (double x, const std::array<double, RemapFormula::maxParameters>& values) const noexcept
    {
        FormulaTrace trace; trace.nodeCount = instructionCount;
        for (size_t i = 0; i < instructionCount; ++i) trace.nodes[i].nodeId = instructions[i].nodeId;
        trace.result = evaluateImpl<true> (x, values, &trace);
        return trace;
    }

private:
    // The audio specialization has no trace storage, writes or runtime branch.
    // Keeping the switch shared makes UI readouts use identical arithmetic.
    template <bool Trace>
    FormulaResult evaluateImpl (double x, const std::array<double, RemapFormula::maxParameters>& values,
                                FormulaTrace* trace) const noexcept
    {
        if (! valid || ! std::isfinite (x)) return { x, false, -1 };
        std::array<double, RemapFormula::maxNodes> results {};
        for (size_t i = 0; i < instructionCount; ++i)
        {
            const auto& instruction = instructions[i];
            auto input = [&] (size_t pin) { return instruction.inputs[pin] >= 0 ? results[(size_t) instruction.inputs[pin]] : 0.0; };
            const double a = input (0), b = input (1), c = input (2); double result = 0; bool domain = true;
            switch (instruction.op)
            {
                case FormulaOp::Input: result = x; break;
                case FormulaOp::Constant: result = instruction.value; break;
                case FormulaOp::Parameter: result = values[(size_t) instruction.parameter]; break;
                case FormulaOp::Add: result = a + b; break;
                case FormulaOp::Subtract: result = a - b; break;
                case FormulaOp::Multiply: result = a * b; break;
                case FormulaOp::Divide: domain = ! formulaExactEqual (b, 0); if (domain) result = a / b; break;
                case FormulaOp::Power: result = std::pow (a, b); break;
                case FormulaOp::Negate: result = -a; break;
                case FormulaOp::Abs: result = std::abs (a); break;
                case FormulaOp::Min: result = std::min (a, b); break;
                case FormulaOp::Max: result = std::max (a, b); break;
                case FormulaOp::Clamp: domain = b <= c; if (domain) result = juce::jlimit (b, c, a); break;
                case FormulaOp::Sqrt: domain = a >= 0; if (domain) result = std::sqrt (a); break;
                case FormulaOp::Exp: result = std::exp (a); break;
                case FormulaOp::Log: domain = a > 0; if (domain) result = std::log (a); break;
                case FormulaOp::Log2: domain = a > 0; if (domain) result = std::log2 (a); break;
                case FormulaOp::Log10: domain = a > 0; if (domain) result = std::log10 (a); break;
                case FormulaOp::Sin: result = std::sin (a); break;
                case FormulaOp::Cos: result = std::cos (a); break;
                case FormulaOp::Tan: domain = std::abs (std::cos (a)) > 1.0e-12; if (domain) result = std::tan (a); break;
                case FormulaOp::Floor: result = std::floor (a); break;
                case FormulaOp::Ceil: result = std::ceil (a); break;
                case FormulaOp::Round: result = std::round (a); break;
                case FormulaOp::Modulo: domain = ! formulaExactEqual (b, 0); if (domain) result = std::fmod (a, b); break;
                case FormulaOp::Sign: result = a > 0 ? 1 : (a < 0 ? -1 : 0); break;
            }
            if (! domain || ! std::isfinite (result))
            {
                if constexpr (Trace) trace->nodes[i].status = FormulaTraceStatus::Error;
                return { x, false, instruction.nodeId };
            }
            if constexpr (Trace)
            {
                trace->nodes[i].value = result;
                trace->nodes[i].status = FormulaTraceStatus::Value;
            }
            results[i] = result;
        }
        return { results[(size_t) outputIndex], true, -1 };
    }
};
} // namespace scrr::params

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"
#include "modules/FrequencyRemap.h"
#include "dsp/FFTEngine.h"

using namespace scrr::dsp;

namespace {
scrr::params::RemapFormula gainFormula (scrr::params::FormulaUnit unit = scrr::params::FormulaUnit::Hertz)
{
    using namespace scrr::params;
    RemapFormula formula; formula.unit = unit;
    FormulaParameter parameter; parameter.id = "gain_17"; parameter.name = unit == FormulaUnit::Hertz ? "Ratio" : "Offset";
    parameter.minimum = unit == FormulaUnit::Hertz ? .25 : -.5;
    parameter.maximum = unit == FormulaUnit::Hertz ? 8 : .5;
    parameter.defaultValue = unit == FormulaUnit::Hertz ? 4 : std::log (4.0) / std::log (1200.0);
    formula.parameters.push_back (parameter);
    FormulaNode parameterNode; parameterNode.id = 1; parameterNode.op = FormulaOp::Parameter; parameterNode.parameterId = parameter.id;
    FormulaNode output; output.id = 2; output.op = unit == FormulaUnit::Hertz ? FormulaOp::Multiply : FormulaOp::Add; output.inputs = {{ 0, 1, -1 }};
    formula.nodes.push_back (parameterNode); formula.nodes.push_back (output); formula.outputNode = 2;
    return formula;
}
std::shared_ptr<const scrr::params::FormulaProgram> compileFormula (const scrr::params::RemapFormula& formula)
{
    return std::make_shared<const scrr::params::FormulaProgram> (scrr::params::FormulaProgram::compile (formula));
}
}

TEST_CASE ("Remap Formula roundtrips stable graph and parameter identities")
{
    using namespace scrr::params;
    auto original = gainFormula(); original.nodes[1].canvasX = 190; original.nodes[1].canvasY = -20;
    juce::String error;
    const auto restored = RemapFormula::decode (original.encode(), &error);
    REQUIRE (error.isEmpty()); CHECK (restored.encode() == original.encode());
    auto program = FormulaProgram::compile (restored); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {}; values[0] = 4;
    const auto result = program.evaluate (200, values);
    CHECK (result.valid); CHECK (std::abs (result.value - 800) < 1.0e-10);
    original.parameters[0].name = "Stretch";
    CHECK (original.parameters[0].property() == restored.parameters[0].property());
    original.parameters[0].name = "x";
    CHECK (! FormulaProgram::compile (original).valid);
}

TEST_CASE ("Remap Formula rejects cycles oversized graphs and malformed external data")
{
    using namespace scrr::params;
    auto formula = gainFormula(); formula.nodes[2].inputs[0] = 2;
    CHECK (! FormulaProgram::compile (formula).valid);
    formula = gainFormula(); formula.nodes[2].inputs[0] = 999;
    CHECK (! FormulaProgram::compile (formula).valid);
    formula = gainFormula(); formula.nodes[1].id = 0;
    CHECK (! FormulaProgram::compile (formula).valid);
    formula = gainFormula(); formula.nodes.resize (RemapFormula::maxNodes + 1);
    CHECK (! FormulaProgram::compile (formula).valid);
    juce::String error;
    RemapFormula::decode ("{not valid}", &error); CHECK (error.isNotEmpty());
    RemapFormula::decode (juce::String::repeatedString ("[", 1000), &error); CHECK (error.isNotEmpty());
    RemapFormula::decode (juce::String::repeatedString ("x", 65537), &error); CHECK (error.isNotEmpty());
    formula = gainFormula(); formula.parameters[0].minimum = 8;
    RemapFormula::decode (formula.encode(), &error); CHECK (error.isNotEmpty());
}

TEST_CASE ("Remap Formula bounded math reports domains and overflow without emitting nonfinite audio values")
{
    using namespace scrr::params;
    const std::array<double, RemapFormula::maxParameters> values {};
    for (const auto op : { FormulaOp::Divide, FormulaOp::Sqrt, FormulaOp::Log, FormulaOp::Log2, FormulaOp::Log10,
                           FormulaOp::Power, FormulaOp::Exp, FormulaOp::Modulo, FormulaOp::Clamp })
    {
        RemapFormula formula;
        formula.nodes[0].op = FormulaOp::Constant; formula.nodes[0].value = op == FormulaOp::Exp ? 1000 : -1;
        FormulaNode other; other.id = 1; other.op = FormulaOp::Constant; other.value = op == FormulaOp::Power ? .5 : 0;
        FormulaNode output; output.id = 2; output.op = op; output.inputs = {{ 0, 1, 0 }};
        formula.nodes.push_back (other); formula.nodes.push_back (output); formula.outputNode = 2;
        const auto program = FormulaProgram::compile (formula); REQUIRE (program.valid);
        const auto result = program.evaluate (200, values);
        CHECK (! result.valid); CHECK (result.errorNode == 2); CHECK (std::isfinite (result.value));
    }
    auto formula = gainFormula(); formula.nodes[2].op = FormulaOp::Min;
    auto program = FormulaProgram::compile (formula); auto parameters = values; parameters[0] = 5;
    CHECK (formulaExactEqual (program.evaluate (200, parameters).value, 5));
    formula.nodes[2].op = FormulaOp::Max; program = FormulaProgram::compile (formula);
    CHECK (formulaExactEqual (program.evaluate (200, parameters).value, 200));
}

TEST_CASE ("Remap Formula shared affine cache updates once for automation and preserves Hz and log-normalized semantics")
{
    using namespace scrr::params;
    for (const auto unit : { FormulaUnit::Hertz, FormulaUnit::Normalized })
    {
        const auto formula = gainFormula (unit);
        RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (formula));
        juce::ValueTree state ("module"); runtime.updateParameters (state); runtime.prepareTable (24000);
        REQUIRE (runtime.getBuildCount() == 1);
        for (const double hz : { 20.0, 200.0, 375.3, 1000.0, 3000.0 })
        {
            const auto result = runtime.evaluateHz (hz); REQUIRE (result.valid);
            CHECK (std::abs (result.value - hz * 4) < 1.0e-7);
        }
        for (int clone = 0; clone < 16; ++clone) { runtime.updateParameters (state); runtime.prepareTable (24000); }
        CHECK (runtime.getBuildCount() == 1);
        state.setProperty (formula.parameters[0].property(), unit == FormulaUnit::Hertz ? 2 : std::log (2.0) / std::log (1200.0), nullptr);
        runtime.updateParameters (state);
        CHECK (runtime.getBuildCount() == 1); // A control update never executes the graph.
        for (int clone = 0; clone < 16; ++clone) runtime.prepareTable (24000);
        CHECK (runtime.getBuildCount() == 2);
        CHECK (std::abs (runtime.evaluateHz (200).value - 400) < 1.0e-7);
    }
}

TEST_CASE ("Remap Formula discards finite out-of-range output and preserves invalid-domain sources")
{
    using namespace scrr::params;
    for (const double constant : { -1.0, 25000.0 })
    {
        RemapFormula formula; formula.nodes[0].op = FormulaOp::Constant; formula.nodes[0].value = constant;
        RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (formula));
        FrequencyRemapModule module; module.prepare (48000, 1025); module.setFrequencyLimit (24000); module.setFormulaRuntime (&runtime);
        juce::ValueTree state ("module"); state.setProperty ("remapMode", 1, nullptr); module.updateParameters (state);
        std::vector<std::complex<float>> spectrum (1025); spectrum[16] = { 1, 0 };
        module.processSpectrum (spectrum.data(), 1025, 48000);
        double energy = 0; for (const auto value : spectrum) energy += std::norm (value);
        CHECK (formulaExactEqual (energy, 0));
    }
    RemapFormula formula; formula.nodes[0].op = FormulaOp::Constant; formula.nodes[0].value = -1;
    FormulaNode output; output.id = 1; output.op = FormulaOp::Sqrt; output.inputs[0] = 0;
    formula.nodes.push_back (output); formula.outputNode = 1;
    RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (formula));
    FrequencyRemapModule module; module.prepare (48000, 1025); module.setFormulaRuntime (&runtime);
    juce::ValueTree state ("module"); state.setProperty ("remapMode", 1, nullptr); module.updateParameters (state);
    std::vector<std::complex<float>> spectrum (1025); spectrum[16] = { .7f, .2f }; const auto original = spectrum;
    module.processSpectrum (spectrum.data(), 1025, 48000);
    CHECK (spectrum == original); CHECK (runtime.getInvalidSampleCount() > 0); CHECK (runtime.getErrorNode() == 1);
}

TEST_CASE ("Remap Formula evaluates fast nonlinear functions and discontinuities exactly without lookup aliasing")
{
    using namespace scrr::params;
    RemapFormula formula; formula.unit = FormulaUnit::Normalized;
    auto add = [&] (int id, FormulaOp op, std::array<int, 3> inputs, double value = 0.0)
    { FormulaNode node; node.id = id; node.op = op; node.inputs = inputs; node.value = value; formula.nodes.push_back (node); };
    add (1, FormulaOp::Constant, {{ -1, -1, -1 }}, juce::MathConstants<double>::twoPi * 1024);
    add (2, FormulaOp::Multiply, {{ 0, 1, -1 }});
    add (3, FormulaOp::Sin, {{ 2, -1, -1 }});
    add (4, FormulaOp::Constant, {{ -1, -1, -1 }}, .01);
    add (5, FormulaOp::Multiply, {{ 3, 4, -1 }});
    add (6, FormulaOp::Add, {{ 0, 5, -1 }}); formula.outputNode = 6;
    RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (formula)); runtime.prepareTable (24000);
    CHECK (! runtime.isIdentity()); CHECK (! runtime.usesAffineCache());
    constexpr double x = .25 + 1.0 / 4096;
    const double source = 20 * std::pow (1200.0, x), expected = 20 * std::pow (1200.0, x + .01);
    const auto result = runtime.evaluateHz (source);
    CHECK (result.valid); CHECK (std::abs (result.value - expected) < 1.0e-8);
    formula = RemapFormula {}; FormulaNode floor; floor.id = 1; floor.op = FormulaOp::Floor; floor.inputs[0] = 0;
    formula.nodes.push_back (floor); formula.outputNode = 1;
    runtime.setProgram (compileFormula (formula)); runtime.prepareTable (24000);
    CHECK (! runtime.usesAffineCache());
    CHECK (formulaExactEqual (runtime.evaluateHz (512.999999).value, 512));
    CHECK (formulaExactEqual (runtime.evaluateHz (513.000001).value, 513));
    formula.nodes[1].op = FormulaOp::Modulo;
    FormulaNode divisor; divisor.id = 2; divisor.op = FormulaOp::Constant; divisor.value = 7.3;
    formula.nodes.push_back (divisor); formula.nodes[1].inputs[1] = 2;
    runtime.setProgram (compileFormula (formula)); runtime.prepareTable (24000);
    CHECK (! runtime.usesAffineCache());
    CHECK (formulaExactEqual (runtime.evaluateHz (513.000001).value, std::fmod (513.000001, 7.3)));
}

TEST_CASE ("Remap Formula renders 200 to 800 Hz in both units across arbitrary hops")
{
    constexpr double rate = 48000;
    for (const auto unit : { scrr::params::FormulaUnit::Hertz, scrr::params::FormulaUnit::Normalized })
        for (const int size : { 1024, 4096 }) for (const int factor : { 3, 7 })
        {
            const int hop = (int) std::round ((double) size / factor);
            FFTEngine fft; fft.configure (size, hop, WindowType::Hann); fft.prepare (rate, 257, 1);
            RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (gainFormula (unit)));
            FrequencyRemapModule module; module.prepare (rate, fft.getNumBins()); module.setHopSize (hop); module.setFrequencyLimit (24000); module.setFormulaRuntime (&runtime);
            juce::ValueTree state ("module"); state.setProperty ("remapMode", 1, nullptr); module.updateParameters (state);
            std::vector<float> input (32768), output (input.size());
            for (size_t i = 0; i < input.size(); ++i) input[i] = .3f * (float) std::sin (juce::MathConstants<double>::twoPi * 200 * (double) i / rate);
            for (size_t offset = 0; offset < input.size();)
            {
                const int count = std::min (257, (int) (input.size() - offset));
                fft.process (input.data() + offset, output.data() + offset, count, 0,
                    [&] (std::complex<float>* data, int bins, float) { module.processSpectrum (data, bins, rate); });
                offset += (size_t) count;
            }
            std::complex<double> target {}, original {}; double power = 0;
            const int start = 5 * size, count = 9600;
            for (int i = start; i < start + count; ++i)
            {
                const double sample = output[(size_t) i]; REQUIRE (std::isfinite (sample)); power += sample * sample;
                target += sample * std::polar (1.0, -juce::MathConstants<double>::twoPi * 800 * i / rate);
                original += sample * std::polar (1.0, -juce::MathConstants<double>::twoPi * 200 * i / rate);
            }
            target *= 2.0 / count; original *= 2.0 / count; power /= count;
            CHECK (std::abs (target) > .1); CHECK (std::abs (original) < .01);
            CHECK (std::max (0.0, power - .5 * std::norm (target)) / std::max (power, 1.0e-20) < .03);
            CHECK (runtime.getBuildCount() == 1);
        }
}

TEST_CASE ("Remap Formula keeps its previous mapping during the return to exact identity")
{
    constexpr int bins = 1025, hop = 293;
    constexpr double rate = 48000;
    const auto formula = gainFormula();
    RemapFormulaRuntime runtime; runtime.setProgram (compileFormula (formula));
    FrequencyRemapModule module; module.prepare (rate, bins); module.setHopSize (hop); module.setFrequencyLimit (24000); module.setFormulaRuntime (&runtime);
    juce::ValueTree state ("module"); state.setProperty ("remapMode", 1, nullptr); module.updateParameters (state);
    bool foundTransition = false;
    for (int frame = 0; frame < 12; ++frame)
    {
        if (frame == 3)
        {
            state.setProperty (formula.parameters[0].property(), 1.0, nullptr);
            runtime.updateParameters (state); module.updateParameters (state);
        }
        std::vector<std::complex<float>> spectrum (bins);
        spectrum[16] = std::polar (1.0f, (float) (juce::MathConstants<double>::twoPi * 375 * frame * hop / rate));
        const auto input = spectrum; module.processSpectrum (spectrum.data(), bins, rate);
        if (frame > 3 && std::abs (spectrum[16]) > .1f && std::abs (spectrum[16]) < .9f) foundTransition = true;
        if (frame >= 8) CHECK (spectrum == input);
    }
    CHECK (foundTransition);
}

TEST_CASE ("Frequency Remap identity and zero amount preserve complex input")
{
    FrequencyRemapModule module; module.prepare (48000, 1025); module.setHopSize (293);
    std::vector<std::complex<float>> spectrum (1025);
    for (size_t i = 0; i < spectrum.size(); ++i) spectrum[i] = { (float) i * .001f, -.2f };
    const auto original = spectrum;
    juce::ValueTree state ("module"); module.updateParameters (state);
    module.processSpectrum (spectrum.data(), 1025, 48000);
    for (size_t i = 0; i < spectrum.size(); ++i) CHECK (std::abs (spectrum[i] - original[i]) < 1.0e-5f);
    scrr::params::TransferCurve curve; curve.points = { {0, 1, 0}, {1, 0, 0} };
    state.setProperty ("remapCurve", curve.encode(), nullptr); state.setProperty ("amount", 0, nullptr);
    module.updateParameters (state); spectrum = original; module.processSpectrum (spectrum.data(), 1025, 48000);
    CHECK (spectrum == original);
}

TEST_CASE ("Frequency Remap moves source-selected bins and advances by actual hop")
{
    constexpr int bins = 1025, hop = 293;
    constexpr double rate = 48000, limit = 24000;
    FrequencyRemapModule module; module.prepare (rate, bins); module.setHopSize (hop); module.setFrequencyLimit (limit);
    scrr::params::TransferCurve curve;
    const float offset = (float) (std::log (4.0) / std::log (limit / 20.0));
    curve.points = { {0, offset, 0}, {1 - offset, 1, 0}, {1, 1, 0} };
    juce::ValueTree state ("module"); state.setProperty ("remapCurve", curve.encode(), nullptr);
    state.setProperty ("rangeLow", 300, nullptr); state.setProperty ("rangeHigh", 400, nullptr);
    module.updateParameters (state);
    std::vector<std::complex<float>> spectrum (bins); spectrum[16] = {1,0}; spectrum[100] = {.3f,.1f};
    module.processSpectrum (spectrum.data(), bins, rate);
    CHECK (std::abs (spectrum[16]) < 1.0e-5f);
    CHECK (std::abs (spectrum[64] - std::complex<float>(1,0)) < .001f);
    CHECK (spectrum[100] == std::complex<float>(.3f,.1f));
    std::fill (spectrum.begin(), spectrum.end(), std::complex<float>{});
    spectrum[16] = std::polar (1.0f, (float) (juce::MathConstants<double>::twoPi * 375.0 * hop / rate));
    module.processSpectrum (spectrum.data(), bins, rate);
    const auto expected = std::polar (1.0f, (float) (juce::MathConstants<double>::twoPi * 1500.0 * hop / rate));
    CHECK (std::abs (spectrum[64] - expected) < .002f);
}

TEST_CASE ("Frequency Remap many to one stays finite and leaves protected highs dry")
{
    FrequencyRemapModule module; module.prepare (48000, 1025); module.setFrequencyLimit (16000);
    scrr::params::TransferCurve curve; curve.points = { {0,.5f,0}, {1,.5f,0} };
    juce::ValueTree state ("module"); state.setProperty ("remapCurve", curve.encode(), nullptr); module.updateParameters (state);
    std::vector<std::complex<float>> spectrum (1025, {.01f,.02f}); const auto high = spectrum[900];
    for (int frame = 0; frame < 30; ++frame)
    {
        module.processSpectrum (spectrum.data(), 1025, 48000);
        for (const auto value : spectrum) CHECK (std::isfinite (value.real()) && std::isfinite (value.imag()));
        CHECK (spectrum[900] == high);
    }
}

TEST_CASE ("Frequency Remap renders a 200 Hz tone at 800 Hz across FFT sizes and arbitrary hops")
{
    constexpr double rate = 48000, limit = 24000;
    double minimumAmplitude = 1, maximumAmplitude = 0, maximumResidual = 0, maximumOriginal = 0;
    for (const int size : { 1024, 2048, 4096 })
        for (const int factor : { 3, 4, 7 })
        {
            const int hop = (int) std::round ((double) size / factor);
            FFTEngine fft; fft.configure (size, hop, WindowType::Hann); fft.prepare (rate, 257, 1);
            FrequencyRemapModule module; module.prepare (rate, fft.getNumBins()); module.setHopSize (hop); module.setFrequencyLimit (limit);
            scrr::params::TransferCurve curve;
            const float offset = (float) (std::log (4.0) / std::log (limit / 20.0));
            curve.points = { {0, offset, 0}, {1 - offset, 1, 0}, {1, 1, 0} };
            juce::ValueTree state ("module"); state.setProperty ("remapCurve", curve.encode(), nullptr); module.updateParameters (state);
            std::vector<float> input (48000), output (input.size());
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = .3f * (float) std::sin (juce::MathConstants<double>::twoPi * 200 * (double) i / rate);
            for (size_t offsetSample = 0; offsetSample < input.size();)
            {
                const int count = std::min (257, (int) (input.size() - offsetSample));
                fft.process (input.data() + offsetSample, output.data() + offsetSample, count, 0,
                    [&] (std::complex<float>* x, int bins, float) { module.processSpectrum (x, bins, rate); });
                offsetSample += (size_t) count;
            }
            constexpr int count = 9600;
            const int start = 5 * size;
            std::complex<double> target {}, original {};
            double power = 0;
            for (int i = 0; i < count; ++i)
            {
                const double sample = output[(size_t) (start + i)];
                REQUIRE (std::isfinite (sample));
                const double time = (start + i) / rate;
                target += sample * std::polar (1.0, -juce::MathConstants<double>::twoPi * 800 * time);
                original += sample * std::polar (1.0, -juce::MathConstants<double>::twoPi * 200 * time);
                power += sample * sample;
            }
            target *= 2.0 / count; original *= 2.0 / count; power /= count;
            const double residual = power > 0 ? std::max (0.0, power - .5 * std::norm (target)) / power : 1;
            minimumAmplitude = std::min (minimumAmplitude, std::abs (target));
            maximumAmplitude = std::max (maximumAmplitude, std::abs (target));
            maximumResidual = std::max (maximumResidual, residual);
            maximumOriginal = std::max (maximumOriginal, std::abs (original));
            if (std::abs (target) <= .1 || residual >= .03)
                std::cerr << "Remap FFT=" << size << " hop=" << hop << " target amplitude=" << std::abs (target) << " residual=" << residual << '\n';
            CHECK (std::abs (target) > .1);
            CHECK (std::abs (original) < .01);
            CHECK (residual < .03);
        }
    std::cout << "Remap 200->800 Hz: output amplitude " << minimumAmplitude << ".." << maximumAmplitude
              << " for input0.3; maximum residual energy ratio " << maximumResidual
              << "; maximum remaining200Hz amplitude " << maximumOriginal << '\n';
}

TEST_CASE ("Frequency Remap fades back to exact identity when its curve or amount is reset")
{
    constexpr int bins = 1025, hop = 293;
    constexpr double rate = 48000, limit = 24000;
    for (const bool resetAmount : { false, true })
    {
        FrequencyRemapModule module; module.prepare (rate, bins); module.setHopSize (hop); module.setFrequencyLimit (limit);
        scrr::params::TransferCurve curve;
        const float offset = (float) (std::log (4.0) / std::log (limit / 20.0));
        curve.points = { {0, offset, 0}, {1 - offset, 1, 0}, {1, 1, 0} };
        juce::ValueTree state ("module"); state.setProperty ("remapCurve", curve.encode(), nullptr); module.updateParameters (state);
        bool foundTransition = false;
        for (int frame = 0; frame < 12; ++frame)
        {
            if (frame == 3)
            {
                if (resetAmount) state.setProperty ("amount", 0, nullptr);
                else { curve.reset(); state.setProperty ("remapCurve", curve.encode(), nullptr); }
                module.updateParameters (state);
            }
            std::vector<std::complex<float>> spectrum (bins);
            spectrum[16] = std::polar (1.0f, (float) (juce::MathConstants<double>::twoPi * 375 * frame * hop / rate));
            const auto input = spectrum;
            module.processSpectrum (spectrum.data(), bins, rate);
            if (frame > 3 && std::abs (spectrum[16]) > .1f && std::abs (spectrum[16]) < .9f) foundTransition = true;
            if (frame >= 8) CHECK (spectrum == input);
        }
        CHECK (foundTransition);
    }
}

TEST_CASE ("Frequency Remap renders a descending curve with independently moved source tones")
{
    constexpr double rate = 48000, limit = 24000;
    constexpr int size = 2048, hop = 293, samples = 32768;
    FFTEngine fft; fft.configure (size, hop, WindowType::Hann); fft.prepare (rate, 257, 1);
    FrequencyRemapModule module; module.prepare (rate, fft.getNumBins()); module.setHopSize (hop); module.setFrequencyLimit (limit);
    auto coordinate = [] (double hz) { return (float) (std::log (hz / 20.0) / std::log (limit / 20.0)); };
    scrr::params::TransferCurve curve;
    curve.points = { {0, 0, 0}, {coordinate (200), coordinate (800), 0},
                     {coordinate (1000), coordinate (500), 0}, {1, 1, 0} };
    juce::ValueTree state ("module"); state.setProperty ("remapCurve", curve.encode(), nullptr); module.updateParameters (state);
    std::vector<float> input (samples), output (samples);
    for (int i = 0; i < samples; ++i)
        input[(size_t) i] = (float) (.2 * std::sin (juce::MathConstants<double>::twoPi * 200 * i / rate)
                                  + .15 * std::sin (juce::MathConstants<double>::twoPi * 1000 * i / rate));
    for (int offset = 0; offset < samples;)
    {
        const int count = std::min (257, samples - offset);
        fft.process (input.data() + offset, output.data() + offset, count, 0,
            [&] (std::complex<float>* x, int bins, float) { module.processSpectrum (x, bins, rate); });
        offset += count;
    }
    std::array<std::complex<double>, 4> components {};
    const std::array<double, 4> frequencies { 800, 500, 200, 1000 };
    double power = 0;
    for (int i = 10000; i < 19600; ++i)
    {
        const double sample = output[(size_t) i]; power += sample * sample;
        for (size_t f = 0; f < frequencies.size(); ++f)
            components[f] += sample * std::polar (1.0, -juce::MathConstants<double>::twoPi * frequencies[f] * i / rate);
    }
    for (auto& component : components) component *= 2.0 / 9600;
    power /= 9600;
    CHECK (std::abs (components[0]) > .12);
    CHECK (std::abs (components[1]) > .09);
    CHECK (std::abs (components[2]) < .01);
    CHECK (std::abs (components[3]) < .01);
    const double residual = std::max (0.0, power - .5 * (std::norm (components[0]) + std::norm (components[1]))) / std::max (power, 1.0e-20);
    CHECK (residual < .03);
}

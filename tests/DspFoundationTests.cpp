// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"

#include "dsp/GainStage.h"
#include "dsp/DryWetMixer.h"
#include "dsp/FFTEngine.h"
#include "dsp/Oversampler.h"
#include "modules/ModuleInstance.h"
#include "modules/ModuleSpecs.h"

#include <algorithm>
#include <cmath>
#include <vector>

static bool isFiniteBuffer (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }
    return true;
}

TEST_CASE ("GainStage does not produce NaN or Inf")
{
    scrr::dsp::GainStage stage;
    stage.prepare (44100.0, 128, 1);
    stage.setGainDb (-12.0f);

    juce::AudioBuffer<float> buffer (1, 128);
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, (i % 7 == 0) ? 2.0f : 0.5f);

    stage.process (buffer);
    CHECK (isFiniteBuffer (buffer));
}

TEST_CASE ("DryWetMixer mix zero outputs delayed dry")
{
    scrr::dsp::DryWetMixer mixer;
    mixer.prepare (44100.0, 1024, 1);
    mixer.setWetDelay (3);
    mixer.setMix (0.0f);

    juce::AudioBuffer<float> primeDry (1, 1024);
    juce::AudioBuffer<float> primeWet (1, 1024);
    juce::AudioBuffer<float> primeOut (1, 1024);
    primeDry.clear();
    primeWet.clear();
    primeOut.clear();
    mixer.process (primeDry, primeWet, primeOut);

    juce::AudioBuffer<float> dry (1, 8);
    juce::AudioBuffer<float> wet (1, 8);
    juce::AudioBuffer<float> out (1, 8);
    for (int i = 0; i < 8; ++i)
    {
        dry.setSample (0, i, 10.0f + (float) i);
        wet.setSample (0, i, 20.0f + (float) i);
    }
    out.clear();

    mixer.process (dry, wet, out);

    REQUIRE (std::fabs (out.getSample (0, 0) - 0.0f) < 1e-5f);
    REQUIRE (std::fabs (out.getSample (0, 3) - 10.0f) < 1e-5f);
    REQUIRE (std::fabs (out.getSample (0, 7) - 14.0f) < 1e-5f);
}

TEST_CASE ("DryWetMixer mix one outputs wet")
{
    scrr::dsp::DryWetMixer mixer;
    mixer.prepare (44100.0, 1024, 1);
    mixer.setWetDelay (3);
    mixer.setMix (1.0f);

    juce::AudioBuffer<float> dry (1, 8);
    juce::AudioBuffer<float> wet (1, 8);
    juce::AudioBuffer<float> out (1, 8);
    for (int i = 0; i < 8; ++i)
    {
        dry.setSample (0, i, 10.0f + (float) i);
        wet.setSample (0, i, 20.0f + (float) i);
    }
    out.clear();

    mixer.process (dry, wet, out);

    for (int i = 0; i < 8; ++i)
        REQUIRE (std::fabs (out.getSample (0, i) - (20.0f + (float) i)) < 1e-5f);
}

TEST_CASE ("FFTEngine passthrough does not produce NaN or Inf")
{
    scrr::dsp::FFTEngine fft;
    fft.configure (2048, 512, scrr::dsp::WindowType::Hann);
    fft.prepare (44100.0, 512, 1);

    constexpr int totalSamples = 4096;
    constexpr int blockSize = 512;

    juce::AudioBuffer<float> input (1, totalSamples);
    juce::AudioBuffer<float> output (1, totalSamples);
    input.clear();
    output.clear();

    for (int i = 0; i < totalSamples; ++i)
        input.setSample (0, i, std::sin (0.01f * (float) i));

    auto passthrough = [] (std::complex<float>*, int, float) {};

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int n = std::min (blockSize, totalSamples - offset);
        fft.process (input.getReadPointer (0) + offset,
                     output.getWritePointer (0) + offset,
                     n, 0, passthrough);
    }

    CHECK (isFiniteBuffer (output));
}

TEST_CASE ("FFTEngine impulse latency matches PROJECT.md")
{
    scrr::dsp::FFTEngine fft;
    fft.configure (2048, 512, scrr::dsp::WindowType::Hann);
    fft.prepare (44100.0, 512, 1);

    constexpr int totalSamples = 8192;
    constexpr int blockSize = 512;
    constexpr int impulseIndex = 3072;
    constexpr int expectedLatency = 2047;
    constexpr int expectedOutputIndex = impulseIndex + expectedLatency;

    juce::AudioBuffer<float> input (1, totalSamples);
    juce::AudioBuffer<float> output (1, totalSamples);
    input.clear();
    output.clear();
    input.setSample (0, impulseIndex, 1.0f);

    auto passthrough = [] (std::complex<float>*, int, float) {};

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int n = std::min (blockSize, totalSamples - offset);
        fft.process (input.getReadPointer (0) + offset,
                     output.getWritePointer (0) + offset,
                     n, 0, passthrough);
    }

    int firstNonZero = -1;
    for (int i = 0; i < totalSamples; ++i)
    {
        if (std::fabs (output.getSample (0, i)) > 1.0e-4f)
        {
            firstNonZero = i;
            break;
        }
    }

    REQUIRE (firstNonZero == expectedOutputIndex);
    REQUIRE (std::fabs (output.getSample (0, firstNonZero) - 1.0f) < 5.0e-3f);
}

TEST_CASE ("All modules are transparent when disabled or amount is zero")
{
    const std::vector<std::complex<float>> original {
        { 10.0f, 0.0f },
        { 1.0f, -1.0f },
        { 2.0f, -2.0f },
        { 3.0f, -3.0f },
        { 4.0f, -4.0f },
        { 5.0f, -5.0f },
        { 6.0f, -6.0f },
        { 7.0f, -7.0f },
        { 8.0f, -8.0f },
        { 20.0f, 0.0f }
    };

    for (const auto& spec : scrr::dsp::getModuleSpecs())
    {
        auto state = scrr::dsp::createDefaultModuleState (spec.typeId);
        auto module = scrr::dsp::createModule (spec.typeId);
        if (spec.typeId == "Notes")
        { REQUIRE (module == nullptr); REQUIRE (spec.params.empty()); continue; }
        REQUIRE (module != nullptr);

        module->prepare (44100.0, (int) original.size());
        module->updateParameters (state);

        auto spectrum = original;
        module->processSpectrum (spectrum.data(), (int) spectrum.size(), 44100.0);

        for (size_t i = 0; i < original.size(); ++i)
        {
            REQUIRE (std::fabs (spectrum[i].real() - original[i].real()) < 1.0e-5f);
            REQUIRE (std::fabs (spectrum[i].imag() - original[i].imag()) < 1.0e-5f);
        }
    }
}

TEST_CASE ("Oversampling settings represent multipliers, not JUCE stage counts")
{
    for (int factor : { 1, 2, 4 })
    {
        scrr::dsp::Oversampler sampler;
        sampler.prepare (48000, 128, 2, factor);
        juce::AudioBuffer<float> buffer (2, 128); buffer.clear();
        auto block = juce::dsp::AudioBlock<float> (buffer);
        auto up = sampler.processUp (block);
        REQUIRE (up.getNumSamples() == (size_t) (128 * factor));
        sampler.processDown (block);
        CHECK (isFiniteBuffer (buffer));
        if (factor == 1) REQUIRE (sampler.getLatencySamples() == 0);
    }
}

TEST_CASE ("Contrast changes relative spectral dynamics without creating energy from silence")
{
    scrr::dsp::SpectralContrast fx;
    auto state = scrr::dsp::createDefaultModuleState ("SpectralContrast");
    state.setProperty ("enabled", true, nullptr); state.setProperty ("amount", 70.0, nullptr); fx.updateParameters (state);
    std::vector<std::complex<float>> bins (33); bins[3] = { 1, 0 }; bins[8] = { .1f, 0 };
    fx.processSpectrum (bins.data(), 33, 48000);
    REQUIRE (std::abs (bins[3]) / std::abs (bins[8]) > 10.0f);
    std::fill (bins.begin(), bins.end(), std::complex<float>()); fx.processSpectrum (bins.data(), 33, 48000);
    for (auto bin : bins) REQUIRE (std::abs (bin) < 1e-9f);
}

TEST_CASE ("Frequency Shift translates an isolated bin by the requested Hz")
{
    scrr::dsp::FrequencyShift fx; fx.prepare (48000, 1025);
    auto state = scrr::dsp::createDefaultModuleState ("FrequencyShift");
    state.setProperty ("enabled", true, nullptr); state.setProperty ("shift", 468.75, nullptr); fx.updateParameters (state);
    std::vector<std::complex<float>> bins (1025); bins[100] = { 1, 0 };
    fx.processSpectrum (bins.data(), 1025, 48000);
    REQUIRE (std::abs (bins[120]) > .99f); REQUIRE (std::abs (bins[100]) < .001f);
}

TEST_CASE ("Harmonic Sculpt independently controls odd and even partials")
{
    scrr::dsp::HarmonicSculpt fx;
    auto s = scrr::dsp::createDefaultModuleState ("HarmonicSculpt");
    s.setProperty ("enabled", true, nullptr); s.setProperty ("track", false, nullptr);
    s.setProperty ("fundamental", 468.75, nullptr); s.setProperty ("amount", 100, nullptr);
    s.setProperty ("odd", 0, nullptr); s.setProperty ("even", -24, nullptr); fx.updateParameters (s);
    std::vector<std::complex<float>> bins (1025); bins[20] = { 1, 0 }; bins[40] = { 1, 0 }; bins[60] = { 1, 0 };
    fx.processSpectrum (bins.data(), 1025, 48000);
    REQUIRE (std::abs (bins[20]) > .99f); REQUIRE (std::abs (bins[40]) < .07f); REQUIRE (std::abs (bins[60]) > .99f);
}

TEST_CASE ("All Harmonic Match shapes and modes are finite and silence preserving")
{
    for (int shape = 0; shape < 5; ++shape) for (int mode = 0; mode < 2; ++mode)
    {
        scrr::dsp::HarmonicMatch fx; fx.prepare (48000, 1025);
        auto s = scrr::dsp::createDefaultModuleState ("HarmonicMatch");
        s.setProperty ("enabled", true, nullptr); s.setProperty ("shape", shape, nullptr);
        s.setProperty ("mode", mode, nullptr); s.setProperty ("amount", 100, nullptr);
        s.setProperty ("track", false, nullptr); s.setProperty ("fundamental", 468.75, nullptr);
        for (int colour : { 0, 50, 100 })
        {
            s.setProperty ("colour", colour, nullptr); fx.updateParameters (s);
            std::vector<std::complex<float>> bins (1025);
            fx.processSpectrum (bins.data(), 1025, 48000);
            for (auto bin : bins) REQUIRE (std::abs (bin) < 1.0e-9f);
            for (int h = 1; h <= 30; ++h) bins[(size_t) h * 20] = { 1.0f / (float) h, 0 };
            fx.processSpectrum (bins.data(), 1025, 48000);
            for (auto bin : bins) { REQUIRE (std::isfinite (bin.real())); REQUIRE (std::abs (bin) < 16.01f); }
            if (shape < 2) REQUIRE (std::abs (bins[40]) < .001f);
        }
    }
    REQUIRE (scrr::dsp::HarmonicMatch::envelope (0, 3, .5f) < scrr::dsp::HarmonicMatch::envelope (1, 3, .5f));
    REQUIRE (scrr::dsp::HarmonicMatch::envelope (2, 2, .5f) > 0);
    REQUIRE (scrr::dsp::HarmonicMatch::envelope (3, 3, .5f) > scrr::dsp::HarmonicMatch::envelope (2, 3, .5f));
}

TEST_CASE ("Mirror reflects bins about its centre and preserves disabled or zero amount input")
{
    scrr::dsp::SpectralMirror fx; fx.prepare (48000, 1025);
    auto state = scrr::dsp::createDefaultModuleState ("SpectralMirror");
    state.setProperty ("enabled", true, nullptr); state.setProperty ("pivot", 1875.0, nullptr);
    state.setProperty ("amount", 100.0, nullptr); fx.updateParameters (state);
    std::vector<std::complex<float>> bins (1025); bins[60] = { 1, .5f };
    fx.processSpectrum (bins.data(), 1025, 48000);
    REQUIRE (std::abs (bins[100] - std::complex<float> (1, -.5f)) < 1e-5f);
    REQUIRE (std::abs (bins[60]) < 1e-5f);
    state.setProperty ("amount", 0.0, nullptr); fx.updateParameters (state); auto before = bins;
    fx.processSpectrum (bins.data(), 1025, 48000); REQUIRE (bins == before);
}

TEST_CASE ("Bloom spreads occupied bins while preserving total spectral power and silence")
{
    scrr::dsp::SpectralBloom fx; fx.prepare (48000, 1025);
    auto state = scrr::dsp::createDefaultModuleState ("SpectralBloom");
    state.setProperty ("enabled", true, nullptr); state.setProperty ("amount", 100.0, nullptr);
    state.setProperty ("spread", 140.625, nullptr); fx.updateParameters (state);
    std::vector<std::complex<float>> bins (1025); bins[100] = { 1, 0 };
    fx.processSpectrum (bins.data(), 1025, 48000);
    double power = 0; for (const auto bin : bins) power += std::norm (bin);
    REQUIRE (std::abs (power - 1.0) < 1e-5);
    REQUIRE (std::abs (bins[95]) > .2f); REQUIRE (std::abs (bins[100]) < .3f);
    REQUIRE (std::abs (bins[107]) < 1e-8f);
    std::fill (bins.begin(), bins.end(), std::complex<float>()); fx.processSpectrum (bins.data(), 1025, 48000);
    for (const auto bin : bins) REQUIRE (std::abs (bin) < 1e-8f);
}

TEST_CASE ("Comb selects periodic teeth and offset shifts the pass pattern without gain")
{
    scrr::dsp::SpectralComb fx;
    auto state = scrr::dsp::createDefaultModuleState ("SpectralComb");
    state.setProperty ("enabled", true, nullptr); state.setProperty ("amount", 100.0, nullptr);
    state.setProperty ("spacing", 468.75, nullptr); fx.updateParameters (state);
    std::vector<std::complex<float>> bins (1025, { 1, 0 }); fx.processSpectrum (bins.data(), 1025, 48000);
    REQUIRE (std::abs (bins[20]) > .99f); REQUIRE (std::abs (bins[30]) < 1e-5f);
    state.setProperty ("offset", 50.0, nullptr); fx.updateParameters (state);
    std::fill (bins.begin(), bins.end(), std::complex<float> (1, 0)); fx.processSpectrum (bins.data(), 1025, 48000);
    REQUIRE (std::abs (bins[20]) < 1e-5f); REQUIRE (std::abs (bins[30]) > .99f);
    for (const auto bin : bins) REQUIRE (std::abs (bin) <= 1);
}

TEST_CASE ("New spectral shapes tolerate engine reconfiguration and parameter extremes")
{
    for (const auto* type : { "SpectralMirror", "SpectralBloom", "SpectralComb" })
    {
        auto fx = scrr::dsp::createModule (type); auto s = scrr::dsp::createDefaultModuleState (type);
        const auto* spec = scrr::dsp::findModuleSpec (type);
        s.setProperty ("enabled", true, nullptr);
        for (int n : { 513, 16385, 1025 }) for (double sr : { 22050.0, 48000.0, 192000.0 })
        {
            fx->prepare (sr, n);
            for (int edge = 0; edge < 2; ++edge)
            {
                for (const auto& param : spec->params) s.setProperty (param.id, edge == 0 ? param.minVal : param.maxVal, nullptr);
                s.setProperty ("amount", 100.0, nullptr); fx->updateParameters (s);
                for (int ch = 0; ch < 2; ++ch)
                {
                    fx->beginChannel (ch);
                    std::vector<std::complex<float>> bins ((size_t) n);
                    for (int i = 1; i < n - 1; ++i) bins[(size_t) i] = std::polar (.1f, (float) i * .3f);
                    for (int frame = 0; frame < 4; ++frame) fx->processSpectrum (bins.data(), n, sr);
                    for (auto bin : bins) { REQUIRE (std::isfinite (bin.real())); REQUIRE (std::isfinite (bin.imag())); }
                }
            }
        }
    }
}

int main()
{
    return doctest::runTests();
}

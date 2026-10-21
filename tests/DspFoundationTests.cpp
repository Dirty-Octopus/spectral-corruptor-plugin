// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"

#include "dsp/GainStage.h"
#include "dsp/DryWetMixer.h"
#include "dsp/FFTEngine.h"
#include "dsp/Oversampler.h"
#include "dsp/SpectralResourceBudget.h"
#include "modules/ModuleInstance.h"
#include "modules/ModuleSpecs.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
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

TEST_CASE ("Shared spectral scratch preserves independent clone histories and stereo phases")
{
    using namespace scrr::dsp;
    constexpr int bins = 513, hop = 137;
    constexpr double rate = 48000;
    const char* types[] { "BinShuffle", "RandomBinDeath", "BinTeleport", "SpectralPixelation",
        "FrequencyWarp", "FrequencyDrift", "FrequencyRemap", "FrequencyShift", "HarmonicMatch",
        "SpectralMirror", "SpectralBloom", "MagnitudePhaseSwap" };
    for (const auto* type : types)
    {
        SpectralScratchWorkspace workspace;
        std::array<std::unique_ptr<SpectralModule>, 2> privateModules, sharedModules;
        for (size_t i = 0; i < 2; ++i)
        {
            privateModules[i] = createModule (type); sharedModules[i] = createModule (type);
            REQUIRE (privateModules[i] != nullptr); REQUIRE (sharedModules[i] != nullptr);
            sharedModules[i]->setSharedScratchWorkspace (&workspace);
            juce::ValueTree state ("module");
            state.setProperty ("enabled", true, nullptr);
            state.setProperty ("amount", juce::String (type) == "BinShuffle" ? .74 : 74.0, nullptr);
            state.setProperty ("seed", 17 + (int) i, nullptr);
            state.setProperty ("shift", 171.0 - 200.0 * (double) i, nullptr);
            state.setProperty ("rate", 2.7 + (double) i, nullptr);
            state.setProperty ("depth", 5, nullptr);
            state.setProperty ("track", false, nullptr);
            state.setProperty ("fundamental", 700, nullptr);
            state.setProperty ("shape", 1, nullptr);
            state.setProperty ("blockSize", 8, nullptr);
            state.setProperty ("pivot", 3000, nullptr);
            state.setProperty ("spread", 500, nullptr);
            scrr::params::TransferCurve curve;
            curve.points = { { 0, .15f + .1f * (float) i, 0 }, { 1, 1, 0 } };
            state.setProperty ("remapCurve", curve.encode(), nullptr);
            for (auto* module : { privateModules[i].get(), sharedModules[i].get() })
            {
                module->prepare (rate, bins); module->setHopSize (hop); module->updateParameters (state);
            }
        }

        std::vector<std::complex<float>> reference (bins), shared (bins);
        for (int channel = 0; channel < 2; ++channel)
        {
            for (size_t i = 0; i < 2; ++i)
            {
                privateModules[i]->beginChannel (channel); sharedModules[i]->beginChannel (channel);
            }
            for (int frame = 0; frame < 24; ++frame)
                for (size_t i = 0; i < 2; ++i)
                {
                    for (int b = 0; b < bins; ++b)
                        reference[(size_t) b] = std::polar (.1f + .8f * std::abs (std::sin ((float) b * .27f + (float) i)),
                            (float) b * .31f + (float) frame * (.19f + .03f * (float) i) + (float) channel * .73f);
                    shared = reference;
                    privateModules[i]->processSpectrum (reference.data(), bins, rate);
                    sharedModules[i]->processSpectrum (shared.data(), bins, rate);
                    float maximumError = 0;
                    for (int b = 0; b < bins; ++b)
                        maximumError = std::max (maximumError, std::abs (reference[(size_t) b] - shared[(size_t) b]));
                    CHECK (std::isfinite (maximumError));
                    CHECK (maximumError < 1.0e-6f);
                }
        }
    }
}

TEST_CASE ("Clone budget admits every audited spectral factory type and refuses unknown types")
{
    using namespace scrr::dsp;
    constexpr auto refused = std::numeric_limits<size_t>::max();
    for (const auto& spec : getModuleSpecs())
    {
        auto module = createModule (spec.typeId);
        if (module == nullptr || isPartialEffect (spec.typeId)) continue;
        CHECK (estimateOriginalModuleBytes (spec.typeId, 65537, 384000) < refused);
        if (module->isTimeDomain()) continue;
        const auto full = estimateSpectralCloneBytes (spec.typeId, 65537);
        const auto shared = estimateSpectralCloneBytes (spec.typeId, 65537, true);
        const auto workspace = estimateSpectralWorkspaceBytes (spec.typeId, 65537);
        CHECK (full < refused);
        CHECK (shared <= full);
        CHECK (shared + workspace >= full);
    }
    CHECK (estimateSpectralCloneBytes ("UnreviewedFutureEffect", 1025) == refused);
    CHECK (estimateSpectralCloneBytes ("FrequencyRemap", -1) == refused);
    CHECK (estimateOriginalModuleBytes ("Delay", 1025, std::numeric_limits<double>::infinity()) == refused);
    // Histories cannot be shared; Remap saves only its complex output scratch.
    CHECK (estimateSpectralCloneBytes ("SpectralDispersion", 65537, true)
           == estimateSpectralCloneBytes ("SpectralDispersion", 65537));
    CHECK (estimateSpectralCloneBytes ("FrequencyRemap", 65537, true) > (size_t) 34 * 65537);
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

TEST_CASE ("Every FFT window reconstructs unity gain for integer and nondivisor hops")
{
    using namespace scrr::dsp;
    for (const int size : { 128, 1024, 4096 })
        for (int window = 0; window <= (int) WindowType::Triangle; ++window)
            for (int factor = 2; factor <= 16; ++factor)
            {
                const int hop = juce::jmax (1, (int) std::round ((double) size / factor));
                FFTEngine fft;
                fft.configure (size, hop, (WindowType) window);
                fft.prepare (48000, 257, 1);
                REQUIRE (fft.getHopSize() == hop);
                REQUIRE (fft.getAnalysisWindow().size() == (size_t) size);
                REQUIRE (fft.getWindowAmplitudeSum() > 0);
                std::vector<float> input ((size_t) size * 6 + 257), output (input.size());
                juce::Random random (173);
                for (auto& sample : input) sample = (random.nextFloat() - .5f) * .6f + .1f;
                for (size_t offset = 0; offset < input.size();)
                {
                    const int count = std::min (257, (int) (input.size() - offset));
                    fft.process (input.data() + offset, output.data() + offset, count, 0, {});
                    offset += (size_t) count;
                }
                // The causal engine fades in until its first full overlap history.
                float error = 0;
                for (size_t i = (size_t) size * 2; i < output.size(); ++i)
                {
                    REQUIRE (std::isfinite (output[i]));
                    error = std::max (error, std::abs (output[i] - input[i - (size_t) fft.getLatencySamples()]));
                }
                if (error >= 3.0e-5f) std::cerr << "FFT=" << size << " window=" << window << " factor=" << factor << " error=" << error << '\n';
                CHECK (error < 3.0e-5f);
            }
}

TEST_CASE ("FFT configuration rejects invalid or uncovered window hops")
{
    using namespace scrr::dsp;
    FFTEngine fft;
    auto rejects = [&] (int size, int hop)
    {
        try { fft.configure (size, hop, WindowType::Hann); }
        catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK (rejects (200, 50));
    CHECK (rejects (128, 0));
    CHECK (rejects (128, 129));
    CHECK (rejects (128, 128));
    fft.configure (128, 128, WindowType::Rectangular);
    CHECK (fft.getHopSize() == 128);
}

TEST_CASE ("Legacy Blackman normalization retains its historical gain envelope")
{
    using namespace scrr::dsp;
    constexpr int size = 1024, hop = 256;
    FFTEngine fft; fft.configure (size, hop, WindowType::Blackman, true); fft.prepare (48000, 257, 1);
    std::vector<float> input (size * 5, 1.0f), output (input.size());
    for (size_t offset = 0; offset < input.size();)
    {
        const int count = std::min (257, (int) (input.size() - offset));
        fft.process (input.data() + offset, output.data() + offset, count, 0, {});
        offset += (size_t) count;
    }
    const auto& window = fft.getAnalysisWindow();
    float sum = 0; for (float w : window) sum += w * w;
    for (int i = 2 * size; i < 3 * size; ++i)
    {
        float expected = 0;
        const int position = (i - fft.getLatencySamples()) % hop;
        for (int n = position; n < size; n += hop) expected += window[(size_t) n] * window[(size_t) n] * hop / sum;
        CHECK (std::abs (output[(size_t) i] - expected) < 2.0e-6f);
    }
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

TEST_CASE ("Spectral Clipper calibrates its bin ceiling across FFT sizes and windows")
{
    using namespace scrr::dsp;
    for (int size : { 128, 512, 2048, 8192 })
        for (auto window : { WindowType::Hann, WindowType::Hamming, WindowType::Blackman, WindowType::Rectangular,
                             WindowType::BlackmanHarris, WindowType::Gaussian, WindowType::FlatTop, WindowType::Welch, WindowType::Triangle })
        {
            FFTEngine fft; fft.configure (size, size / 4, window); fft.prepare (48000, size, 1);
            SpectralClipper clipper; clipper.prepare (48000, fft.getNumBins());
            clipper.setSpectrumCalibration (fft.getWindowAmplitudeSum());
            auto state = createDefaultModuleState ("SpectralClipper");
            state.setProperty ("enabled", true, nullptr); state.setProperty ("threshold", -12.0, nullptr);
            clipper.updateParameters (state);
            // Keep both real-sinusoid lobes well separated for broad/sidelobed
            // windows; coherent gain does not eliminate negative-frequency leakage.
            const int toneBin = size / 8;
            std::vector<float> input ((size_t) size * 3), output (input.size());
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = .8f * (float) std::sin (juce::MathConstants<double>::twoPi * toneBin * (double) i / size);
            int frames = 0;
            auto process = [&] (std::complex<float>* spectrum, int bins, float)
            {
                const auto original = spectrum[toneBin];
                const double inputAmplitude = 2 * std::abs (original) / fft.getWindowAmplitudeSum();
                CHECK (std::abs (inputAmplitude - .8) < .002);
                clipper.processSpectrum (spectrum, bins, 48000);
                const double outputAmplitude = 2 * std::abs (spectrum[toneBin]) / fft.getWindowAmplitudeSum();
                CHECK (std::abs (outputAmplitude - std::pow (10.0, -12.0 / 20)) < .00001);
                CHECK (std::abs (std::arg (spectrum[toneBin] / original)) < .000001f);
                ++frames;
            };
            for (int block = 0; block < 3; ++block)
                fft.process (input.data() + block * size, output.data() + block * size, size, 0, process);
            REQUIRE (frames > 1);
        }
}

TEST_CASE ("Spectral Clipper handles endpoint bins drive soft knee and output independently")
{
    using namespace scrr::dsp;
    SpectralClipper clipper; clipper.prepare (48000, 513); clipper.setSpectrumCalibration (512);
    auto state = createDefaultModuleState ("SpectralClipper"); state.setProperty ("enabled", true, nullptr);
    state.setProperty ("threshold", -12.0, nullptr); state.setProperty ("drive", 6.0, nullptr);
    state.setProperty ("output", -3.0, nullptr); clipper.updateParameters (state);
    std::vector<std::complex<float>> spectrum (513);
    spectrum[0] = { 512, 0 }; spectrum[512] = { -512, 0 }; spectrum[17] = std::polar (256.0f, .7f);
    spectrum[31] = std::polar (2.56f, -.6f);
    clipper.processSpectrum (spectrum.data(), 513, 48000);
    const double ceiling = std::pow (10.0, -15.0 / 20);
    CHECK (std::abs (std::abs (spectrum[0]) / 512.0 - ceiling) < .000001);
    CHECK (std::abs (std::abs (spectrum[512]) / 512.0 - ceiling) < .000001);
    CHECK (spectrum[512].real() < 0);
    CHECK (std::abs (std::abs (spectrum[17]) / 256.0 - ceiling) < .000001);
    CHECK (std::abs (std::arg (spectrum[17]) - .7f) < .000001f);
    CHECK (std::abs (std::abs (spectrum[31]) / 256.0 - .01 * std::pow (10.0, 3.0 / 20)) < .000001);
    CHECK (spectrum[99] == std::complex<float>());
    state.setProperty ("drive", 0.0, nullptr); state.setProperty ("output", 0.0, nullptr);
    state.setProperty ("knee", 12.0, nullptr); clipper.updateParameters (state);
    double previous = 0;
    for (int level = -30; level <= 12; ++level)
    {
        const float amplitude = (float) std::pow (10.0, (double) level / 20);
        spectrum[17] = { amplitude * 256, 0 }; clipper.processSpectrum (spectrum.data(), 513, 48000);
        const double result = std::abs (spectrum[17]) / 256.0;
        CHECK (result + .000001 >= previous); CHECK (result <= std::pow (10.0, -12.0 / 20) + .000001);
        if (level <= -18) CHECK (std::abs (result - amplitude) < .000001);
        previous = result;
    }
}

TEST_CASE ("Spectral Equalizer centre gain phase and inverse bands have the expected response")
{
    using namespace scrr::dsp;
    auto state = createDefaultModuleState ("SpectralEqualizer"); state.setProperty ("enabled", true, nullptr);
    SpectralEqualizer eq; eq.prepare (48000, 1025); eq.updateParameters (state);
    const auto value = std::polar (1.0f, .83f);
    std::vector<std::complex<float>> spectrum (1025, value);
    eq.processSpectrum (spectrum.data(), 1025, 48000);
    for (auto bin : spectrum) CHECK (bin == value);
    state.setProperty ("band1Freq", 937.5, nullptr); state.setProperty ("band1Gain", 12.0, nullptr);
    state.setProperty ("band1Q", 2.0, nullptr); eq.updateParameters (state);
    eq.processSpectrum (spectrum.data(), 1025, 48000);
    CHECK (std::abs (std::abs (spectrum[40]) - std::pow (10.0, 12.0 / 20)) < .00001);
    CHECK (std::abs (std::arg (spectrum[40]) - .83f) < .000001f);
    CHECK (std::abs (std::abs (spectrum[0]) - 1) < .00001);
    CHECK (std::abs (std::abs (spectrum[1024]) - 1) < .00001);
    state.setProperty ("band2Freq", 937.5, nullptr); state.setProperty ("band2Gain", -12.0, nullptr);
    state.setProperty ("band2Q", 2.0, nullptr); eq.updateParameters (state);
    std::fill (spectrum.begin(), spectrum.end(), value); eq.processSpectrum (spectrum.data(), 1025, 48000);
    for (auto bin : spectrum) CHECK (std::abs (bin - value) < .00001f);
}

TEST_CASE ("Spectral Equalizer shelves cuts and extreme settings stay finite at every supported rate")
{
    using namespace scrr::dsp;
    auto state = createDefaultModuleState ("SpectralEqualizer");
    state.setProperty ("band1Freq", 1000.0, nullptr); state.setProperty ("band1Gain", 12.0, nullptr);
    SpectralEqResponse response;
    state.setProperty ("band1Type", (int) SpectralEqBand::LowShelf, nullptr);
    response.configure (readSpectralEqBands (state), 48000);
    CHECK (std::abs (response.decibelsAt (0) - 12) < .00001);
    CHECK (std::abs (response.decibelsAt (24000)) < .00001);
    state.setProperty ("band1Type", (int) SpectralEqBand::HighShelf, nullptr);
    response.configure (readSpectralEqBands (state), 48000);
    CHECK (std::abs (response.decibelsAt (0)) < .00001);
    CHECK (std::abs (response.decibelsAt (24000) - 12) < .00001);
    for (int type : { (int) SpectralEqBand::LowCut, (int) SpectralEqBand::HighCut })
    {
        state.setProperty ("band1Type", type, nullptr); response.configure (readSpectralEqBands (state), 48000);
        CHECK (std::abs (response.decibelsAt (1000) + 3.0103) < .0001);
        CHECK (response.gainAt (type == SpectralEqBand::LowCut ? 0 : 24000) < .000001);
    }
    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int type = 0; type <= 4; ++type)
            for (double q : { .1, 18.0 })
                for (double frequency : { 20.0, 20000.0 })
                {
                    for (int i = 0; i < spectralEqBands; ++i)
                    {
                        state.setProperty (spectralEqParameter (i, "Type"), type, nullptr);
                        state.setProperty (spectralEqParameter (i, "Freq"), frequency, nullptr);
                        state.setProperty (spectralEqParameter (i, "Gain"), 24.0, nullptr);
                        state.setProperty (spectralEqParameter (i, "Q"), q, nullptr);
                    }
                    response.configure (readSpectralEqBands (state), rate);
                    for (int bin = 0; bin <= 256; ++bin)
                    { const double gain = response.gainAt (rate * .5 * bin / 256); CHECK (std::isfinite (gain)); CHECK (gain >= 0); CHECK (gain <= 63096); }
                }
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

TEST_CASE ("Frequency Shift and Mirror retain requested audio frequencies across hop sizes")
{
    using namespace scrr::dsp;
    constexpr int size = 1024;
    constexpr double sampleRate = 48000;
    for (const int hop : { 512, 256, 205, 137, 64 })
        for (const bool mirror : { false, true })
        {
            FFTEngine fft; fft.configure (size, hop, WindowType::Hann); fft.prepare (sampleRate, 257, 1);
            auto effect = createModule (mirror ? "SpectralMirror" : "FrequencyShift");
            effect->prepare (sampleRate, size / 2 + 1); effect->setHopSize (hop);
            auto state = createDefaultModuleState (mirror ? "SpectralMirror" : "FrequencyShift");
            state.setProperty ("enabled", true, nullptr);
            state.setProperty ("amount", 100.0, nullptr);
            state.setProperty ("shift", 4 * sampleRate / size, nullptr);
            state.setProperty ("pivot", 18 * sampleRate / size, nullptr);
            effect->updateParameters (state);
            std::vector<float> input (size * 8), output (input.size());
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = .35f * (float) std::sin (juce::MathConstants<double>::twoPi * 16 * (double) i / size);
            for (size_t offset = 0; offset < input.size();)
            {
                const int count = std::min (257, (int) (input.size() - offset));
                effect->beginChannel (0);
                fft.process (input.data() + offset, output.data() + offset, count, 0,
                    [&] (std::complex<float>* x, int bins, float) { effect->processSpectrum (x, bins, sampleRate); });
                offset += (size_t) count;
            }
            float error = 0;
            for (int i = size * 2; i < (int) output.size(); ++i)
            {
                const double angle = juce::MathConstants<double>::twoPi * 20 * (i - fft.getLatencySamples()) / size;
                const float expected = (mirror ? -.35f : .35f) * (float) std::sin (angle);
                error = std::max (error, std::abs (output[(size_t) i] - expected));
            }
            if (error >= 5.0e-4f) std::cerr << "hop=" << hop << " mirror=" << mirror << " error=" << error << '\n';
            CHECK (error < 5.0e-4f);
        }
}

TEST_CASE ("Bloom oscillator phase and random Hz rates use the actual hop")
{
    using namespace scrr::dsp;
    constexpr int size = 1024, bins = size / 2 + 1;
    for (const int hop : { 512, 256, 205, 137, 64 })
    {
        SpectralBloom bloom; bloom.prepare (48000, bins); bloom.setHopSize (hop);
        auto state = createDefaultModuleState ("SpectralBloom");
        state.setProperty ("enabled", true, nullptr); state.setProperty ("amount", 100, nullptr);
        state.setProperty ("spread", 200, nullptr); bloom.updateParameters (state);
        std::vector<std::complex<float>> spectrum ((size_t) bins); spectrum[32] = { 1, 0 };
        bloom.processSpectrum (spectrum.data(), bins, 48000);
        const auto first = spectrum[30]; REQUIRE (std::abs (first) > .1f);
        std::fill (spectrum.begin(), spectrum.end(), std::complex<float>()); spectrum[32] = { 1, 0 };
        bloom.processSpectrum (spectrum.data(), bins, 48000);
        const auto measured = spectrum[30] / first;
        const auto expected = std::polar (1.0f, (float) (juce::MathConstants<double>::twoPi * 30 * hop / size));
        CHECK (std::abs (measured - expected) < 2.0e-5f);

        FrameRandomSource random; random.prepare(); random.beginChannel (0);
        const auto initial = random.next (17, true, 8, bins, 48000, 391, hop);
        int elapsed = 0;
        for (int frame = 1; frame < 200; ++frame)
            if (random.next (17, true, 8, bins, 48000, 391, hop) != initial)
            { elapsed = frame * hop; break; }
        REQUIRE (elapsed > 0);
        CHECK (std::abs (elapsed - 6000) <= (hop + 1) / 2);
    }
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

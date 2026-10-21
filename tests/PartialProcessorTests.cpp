// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"
#include "dsp/PartialProcessor.h"
#include "dsp/FrequencySplit.h"
#include "dsp/FFTEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace {
using Partial = scrr::dsp::PartialProcessor;
constexpr double partialPi = 3.14159265358979323846;

std::vector<float> partialWindow (int n)
{
    std::vector<float> result ((size_t) n);
    for (int i = 0; i < n; ++i)
        result[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * partialPi * i / (n - 1)));
    return result;
}

struct PartialRoutes
{
    explicit PartialRoutes (int n) { for (auto& spectrum : data) spectrum.resize ((size_t) n); }
    Partial::Routes pointers() { return { data[0].data(), data[1].data(), data[2].data(), data[3].data() }; }
    std::array<std::vector<std::complex<float>>, 4> data;
};

Partial::Stage partialStage (Partial::Type type, double amount, double pitch = 0,
                             Partial::Mode mode = Partial::Mode::Scale)
{
    Partial::Stage stage;
    stage.uid = "test-stage";
    stage.type = type;
    stage.amount = amount;
    stage.pitch = pitch;
    stage.mode = mode;
    return stage;
}
}

TEST_CASE ("Partial transform equations distinguish interval stretch from frequency grids")
{
    const auto stretch = partialStage (Partial::Type::Stretcher, 12);
    CHECK (std::abs ((Partial::mapFrequency (100, 100, stretch)) - (100)) < 1.0e-6);
    CHECK (std::abs ((Partial::mapFrequency (200, 100, stretch)) - (300)) < 1.0e-6);
    CHECK (std::abs ((Partial::mapFrequency (300, 100, stretch)) - (500)) < 1.0e-6);
    const auto compress = partialStage (Partial::Type::Stretcher, -12);
    CHECK (std::abs ((Partial::mapFrequency (300, 100, compress)) - (200)) < 1.0e-6);

    const auto centroid = partialStage (Partial::Type::Centroid, 100, 12);
    CHECK (std::abs ((Partial::mapFrequency (70, 110, centroid)) - (220)) < 1.0e-6);
    CHECK (std::abs ((Partial::mapFrequency (9000, 110, centroid)) - (220)) < 1.0e-6);
    const auto halfway = partialStage (Partial::Type::Centroid, 50, 0);
    CHECK (std::abs ((Partial::mapFrequency (500, 100, halfway)) - (300)) < 1.0e-6);

    const auto octaves = partialStage (Partial::Type::PGrid, 100, 12);
    CHECK (std::abs ((Partial::mapFrequency (180, 100, octaves)) - (200)) < 1.0e-6);
    CHECK (std::abs ((Partial::mapFrequency (410, 100, octaves)) - (400)) < 1.0e-6);
    const auto linear = partialStage (Partial::Type::FGrid, 100, 100);
    CHECK (std::abs ((Partial::mapFrequency (180, 110, linear)) - (200)) < 1.0e-6);
    CHECK (std::abs ((Partial::mapFrequency (310, 110, linear)) - (300)) < 1.0e-6);

    const auto stiff = partialStage (Partial::Type::StiffString, 80);
    CHECK (std::abs ((Partial::mapFrequency (100, 100, stiff)) - (100)) < 1.0e-6);
    CHECK (Partial::mapFrequency (800, 100, stiff) > 8 * Partial::mapFrequency (100, 100, stiff));

    for (const auto type : { Partial::Type::Stretcher, Partial::Type::StiffString,
                             Partial::Type::Centroid, Partial::Type::PGrid, Partial::Type::FGrid })
        CHECK (std::abs ((Partial::mapFrequency (1234.5, 110, partialStage (type, 0, 12))) - (1234.5)) < 1.0e-6);
}

TEST_CASE ("Individual slice weights fold to legacy routing and conserve split amplitude")
{
    for (const int slices : { 1, 2, 4, 17, 64 })
        for (const float crossfade : { 0.0f, 30.0f, 100.0f })
        {
            const scrr::dsp::FrequencySplit splitter (48000, slices, crossfade);
            for (int b = 0; b < 2049; ++b)
            {
                const auto slice = splitter.sliceWeightsForBin (b, 2049, 96000);
                const auto channels = splitter.weightsForBin (b, 2049, 96000);
                CHECK (slice.first >= 0);
                CHECK (slice.first < slices);
                CHECK (slice.second < slices);
                CHECK (std::abs ((slice.firstWeight + slice.secondWeight) - (1.0)) < 1.0e-6);
                float sum = 0;
                for (const auto weight : channels) sum += weight;
                CHECK (std::abs ((sum) - (1.0)) < 1.0e-6);
            }
        }
}

TEST_CASE ("Partial bypass and neutral Scale preserve split audio including endpoints")
{
    constexpr int n = 1024, bins = n / 2 + 1;
    std::vector<std::complex<float>> input ((size_t) bins);
    for (int b = 0; b < bins; ++b) input[(size_t) b] = { std::sin ((float) b), std::cos ((float) b) };
    Partial partial;
    partial.prepare (48000, n, n / 4, 2, partialWindow (n), 2);
    PartialRoutes output (bins);
    for (const int slices : { 1, 2, 4, 17, 64 })
        for (const float crossfade : { 0.0f, 60.0f, 100.0f })
            for (const bool neutralStage : { false, true })
            {
                partial.reset();
                partial.setStages (neutralStage ? std::vector<Partial::Stage> { partialStage (Partial::Type::Stretcher, 0) }
                                               : std::vector<Partial::Stage> {});
                partial.process (input.data(), output.pointers(), bins, slices, crossfade, 48000, 18000, 0);
                const scrr::dsp::FrequencySplit splitter (48000, slices, crossfade);
                for (int b = 0; b < bins; ++b)
                {
                    const auto weights = splitter.weightsForBin (b, bins, 48000);
                    for (size_t r = 0; r < 4; ++r)
                        REQUIRE (std::abs (output.data[r][(size_t) b] - input[(size_t) b] * weights[r]) < 1.0e-6f);
                }
            }
}

TEST_CASE ("Partial Scale shifts the complete band texture and keeps individual slices separate")
{
    constexpr int n = 4096, bins = n / 2 + 1;
    constexpr double binHz = 48000.0 / n;
    Partial partial;
    partial.prepare (48000, n, n / 4, 2, partialWindow (n), 2);
    partial.setAnchorMode (Partial::AnchorMode::EnergyCentroid);
    partial.setStages ({ partialStage (Partial::Type::Centroid, 100) });
    const double anchorBin = (20.0 * 4.0 + 25.0 * 9.0) / 13.0;
    partial.setFundamentalHz ((anchorBin + 8) * binHz);
    std::vector<std::complex<float>> input ((size_t) bins);
    input[20] = { 2, 0 };
    input[25] = { 0, 3 };
    PartialRoutes output (bins);
    partial.process (input.data(), output.pointers(), bins, 4, 0, 48000, 24000, 0);
    CHECK (std::abs (output.data[1][28] - std::complex<float> (2, 0)) < 1.0e-5f);
    CHECK (std::abs (output.data[1][33] - std::complex<float> (0, 3)) < 1.0e-5f);
    CHECK (std::abs (output.data[1][20]) < 1.0e-5f);
    CHECK (std::abs (output.data[1][25]) < 1.0e-5f);

    // Slices 0 and 4 share CH1 but remain independently movable. Both land in
    // one destination bin; complex addition retains both contributions.
    partial.reset();
    std::fill (input.begin(), input.end(), std::complex<float> {});
    input[2] = { 1, 0 };
    input[12] = { 2, 0 };
    partial.setFundamentalHz (100 * binHz);
    partial.process (input.data(), output.pointers(), bins, 16, 0, 48000, 24000, 0);
    CHECK (std::abs (output.data[0][100] - std::complex<float> (3, 0)) < 1.0e-5f);
}

TEST_CASE ("Partial Scale phase advance uses actual hop and stereo movement is linked")
{
    constexpr int n = 4096, bins = n / 2 + 1;
    constexpr double binHz = 48000.0 / n;
    for (const int hop : { 128, 512, 1024 })
    {
        Partial partial;
        partial.prepare (48000, n, hop, 2, partialWindow (n), 2);
        partial.setAnchorMode (Partial::AnchorMode::EnergyCentroid);
        partial.setFundamentalHz (28 * binHz);
        auto stage = partialStage (Partial::Type::Centroid, 100);
        partial.setStages ({ stage });
        std::vector<std::complex<float>> left ((size_t) bins), right ((size_t) bins);
        PartialRoutes outputL (bins), outputR (bins);
        for (int frame = 0; frame < 12; ++frame)
        {
            partial.beginBlock();
            left[20] = std::polar (1.0f, (float) (2 * partialPi * 20 * hop * frame / n));
            right[22] = std::polar (0.5f, (float) (2 * partialPi * 22 * hop * frame / n));
            // Same UID edits must not restart a running phase carrier.
            if (frame == 5) partial.setStages ({ stage });
            partial.process (left.data(), outputL.pointers(), bins, 4, 0, 48000, 24000, 0);
            partial.process (right.data(), outputR.pointers(), bins, 4, 0, 48000, 24000, 1);
            const auto expectedL = std::polar (1.0f, (float) (2 * partialPi * 28 * hop * frame / n));
            const auto expectedR = std::polar (0.5f, (float) (2 * partialPi * 30 * hop * frame / n));
            CHECK (std::abs (outputL.data[1][28] - expectedL) < 1.0e-4f);
            CHECK (std::abs (outputR.data[1][30] - expectedR) < 1.0e-4f);
            CHECK (std::abs (outputR.data[1][28]) < 1.0e-6f);
        }
    }
}

TEST_CASE ("Partial Resynth creates a continuous off-bin sine through actual WOLA")
{
    constexpr int n = 1024, blockSize = 128, samples = 32768;
    constexpr double rate = 48000, targetHz = 757.3;
    for (const int hop : { 128, 256 })
    {
        scrr::dsp::FFTEngine fft;
        fft.configure (n, hop, scrr::dsp::WindowType::Hann);
        fft.prepare (rate, blockSize, 1);
        Partial partial;
        partial.prepare (rate, n, hop, 1, fft.getAnalysisWindow(), 4);
        partial.setAnchorMode (Partial::AnchorMode::EnergyCentroid);
        partial.setFundamentalHz (targetHz);
        partial.setStages ({ partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth) });
        std::array<float, blockSize> input {};
        std::array<std::array<float, blockSize>, 4> route {};
        std::vector<double> audio;
        audio.reserve (samples);
        for (int base = 0; base < samples; base += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
                input[(size_t) i] = (float) (0.2 * std::sin (2 * partialPi * 350.0 * (base + i) / rate));
            partial.beginBlock();
            fft.processRouted (input.data(), { route[0].data(), route[1].data(), route[2].data(), route[3].data() },
                blockSize, 0, [&] (const std::complex<float>* spectrum, Partial::Routes destinations, int bins)
                { partial.process (spectrum, destinations, bins, 4, 0, rate, (float) (rate / 2), 0); });
            for (int i = 0; i < blockSize; ++i)
                audio.push_back ((double) route[0][(size_t) i] + route[1][(size_t) i] + route[2][(size_t) i] + route[3][(size_t) i]);
        }
        // Fit the known continuous target oscillator after the FFT startup.
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, energy = 0;
        for (int i = 8192; i < samples; ++i)
        {
            const double a = 2 * partialPi * targetHz * i / rate;
            const double s = std::sin (a), c = std::cos (a), y = audio[(size_t) i];
            ss += s * s; cc += c * c; sc += s * c; ys += y * s; yc += y * c; energy += y * y;
        }
        const double determinant = ss * cc - sc * sc;
        const double sineGain = (ys * cc - yc * sc) / determinant;
        const double cosineGain = (yc * ss - ys * sc) / determinant;
        double error = 0;
        for (int i = 8192; i < samples; ++i)
        {
            const double a = 2 * partialPi * targetHz * i / rate;
            const double difference = audio[(size_t) i] - sineGain * std::sin (a) - cosineGain * std::cos (a);
            error += difference * difference;
        }
        CHECK (energy > 1.0);
        CHECK (std::sqrt (error / energy) < 0.02);
    }
}

TEST_CASE ("Partial Resynth explicit zero amount converts while silence and disabled split stay dry")
{
    constexpr int n = 2048, bins = n / 2 + 1;
    Partial partial;
    partial.prepare (48000, n, 256, 2, partialWindow (n), 2);
    partial.setStages ({ partialStage (Partial::Type::Stretcher, 0, 0, Partial::Mode::Resynth) });
    std::vector<std::complex<float>> input ((size_t) bins);
    PartialRoutes output (bins);
    partial.process (input.data(), output.pointers(), bins, 16, 100, 48000, 24000, 0);
    for (const auto& spectrum : output.data)
        for (const auto value : spectrum) REQUIRE (value == std::complex<float> {});
    input[20] = { 5, 3 };
    partial.beginBlock();
    partial.process (input.data(), output.pointers(), bins, 1, 0, 48000, 24000, 0);
    CHECK (output.data[0] == input);
    partial.reset();
    partial.process (input.data(), output.pointers(), bins, 4, 0, 48000, 24000, 0);
    CHECK (std::abs (output.data[1][20] - input[20]) > 0.1f);
    const auto peak = std::max_element (output.data[1].begin(), output.data[1].end(),
        [] (auto a, auto b) { return std::norm (a) < std::norm (b); });
    CHECK (std::abs (*peak) > 0.1f);
    CHECK (std::distance (output.data[1].begin(), peak) == 12);
    for (const auto& spectrum : output.data)
        for (const auto value : spectrum)
        {
            REQUIRE (std::isfinite (value.real()));
            REQUIRE (std::isfinite (value.imag()));
        }
}

TEST_CASE ("Rack partial Energy Centroid follows a right-only source")
{
    constexpr int n = 2048, bins = n / 2 + 1;
    Partial partial;
    partial.prepare (48000, n, 256, 2, partialWindow (n), 2);
    partial.setAnchorMode (Partial::AnchorMode::EnergyCentroid);
    partial.setFundamentalHz (110);
    partial.setStages ({ partialStage (Partial::Type::Stretcher, 12, 0, Partial::Mode::Resynth) });
    std::vector<std::complex<float>> left ((size_t) bins), right ((size_t) bins);
    right[40] = { 10, 0 };
    Partial::SliceContext leftSlice { 2, 1000, 1000, false }, rightSlice = leftSlice;
    partial.beginBlock();
    partial.processSlice (left.data(), bins, leftSlice, 24000, 0);
    partial.processSlice (right.data(), bins, rightSlice, 24000, 1);
    CHECK (std::abs (rightSlice.currentAnchorHz - 1765.0) < 1.0e-6);
    CHECK (std::abs (rightSlice.currentAnchorHz - leftSlice.currentAnchorHz) > 100.0);
    double power = 0;
    for (int b = 0; b < bins; ++b)
    {
        REQUIRE (left[(size_t) b] == std::complex<float> {});
        power += std::norm (right[(size_t) b]);
    }
    CHECK (power > 1.0);
    CHECK (rightSlice.singleSine);
}

TEST_CASE ("Shared partial work buffers preserve private oscillator phases across stages and stereo")
{
    constexpr int n = 1024, bins = n / 2 + 1, hop = 147;
    const auto window = partialWindow (n);
    auto workspace = std::make_shared<Partial::Workspace>();
    workspace->prepare (n, window);
    std::array<Partial, 2> shared, independent;
    for (size_t stage = 0; stage < shared.size(); ++stage)
    {
        shared[stage].prepare (48000, n, hop, 2, window, 3, workspace);
        independent[stage].prepare (48000, n, hop, 2, window, 3);
        for (auto* engine : { &shared[stage], &independent[stage] })
        {
            engine->setAnchorMode (Partial::AnchorMode::EnergyCentroid);
            engine->setFundamentalHz (stage == 0 ? 700.3 : 1234.5);
            engine->setStages ({ partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth) });
        }
    }
    for (int frame = 0; frame < 9; ++frame)
    {
        for (size_t stage = 0; stage < shared.size(); ++stage)
        { shared[stage].beginBlock(); independent[stage].beginBlock(); }
        for (int channel = 0; channel < 2; ++channel)
            for (int slice : { 0, 4, 8 })
                for (size_t stage = 0; stage < shared.size(); ++stage)
                {
                    std::vector<std::complex<float>> actual ((size_t) bins);
                    actual[(size_t) (11 + slice)] = std::polar (3.0f + (float) channel,
                        (float) (0.3 * frame + 0.6 * channel + (double) stage));
                    auto expected = actual;
                    Partial::SliceContext a { slice, 500, 500, false }, b = a;
                    shared[stage].processSlice (actual.data(), bins, a, 24000, channel);
                    independent[stage].processSlice (expected.data(), bins, b, 24000, channel);
                    REQUIRE (std::abs (a.currentAnchorHz - b.currentAnchorHz) < 1.0e-8);
                    for (int bin = 0; bin < bins; ++bin)
                        REQUIRE (std::abs (actual[(size_t) bin] - expected[(size_t) bin]) < 1.0e-6f);
                }
    }
}

TEST_CASE ("Partial Resynth shapes have the expected harmonics and preserve band energy")
{
    constexpr int n = 4096, bins = n / 2 + 1, fundamentalBin = 32;
    constexpr double binHz = 48000.0 / n, frequency = fundamentalBin * binHz;
    auto workspace = std::make_shared<Partial::Workspace>(); workspace->prepare (n, partialWindow (n));
    REQUIRE (! workspace->hasWaveforms()); workspace->prepareWaveforms(); REQUIRE (workspace->hasWaveforms());
    for (const auto shape : { Partial::Shape::Sine, Partial::Shape::Triangle, Partial::Shape::Saw, Partial::Shape::Square })
    {
        Partial engine; engine.prepare (48000, n, 585, 2, partialWindow (n), 2, workspace);
        auto stage = partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth); stage.shape = shape;
        engine.setStages ({ stage }); engine.setFundamentalHz (frequency);
        std::vector<std::complex<float>> spectrum ((size_t) bins); spectrum[fundamentalBin] = { 100, 0 };
        Partial::SliceContext context { 1, frequency, frequency, false };
        engine.processSlice (spectrum.data(), bins, context, 5000, 0);
        REQUIRE (context.resynthShape == shape); REQUIRE (std::abs (context.currentAnchorHz - frequency) < 1.0e-8);
        const auto first = std::abs (spectrum[fundamentalBin]); REQUIRE (first > 1);
        const auto second = std::abs (spectrum[fundamentalBin * 2]) / first;
        const auto third = std::abs (spectrum[fundamentalBin * 3]) / first;
        if (shape == Partial::Shape::Sine) { REQUIRE (second < 1.0e-4f); REQUIRE (third < 1.0e-4f); }
        if (shape == Partial::Shape::Triangle) { REQUIRE (second < 1.0e-4f); REQUIRE (std::abs (third - 1.0f / 9) < .001f); }
        if (shape == Partial::Shape::Saw) { REQUIRE (std::abs (second - .5f) < .001f); REQUIRE (std::abs (third - 1.0f / 3) < .001f); }
        if (shape == Partial::Shape::Square) { REQUIRE (second < 1.0e-4f); REQUIRE (std::abs (third - 1.0f / 3) < .001f); }
        double power = 0; for (const auto value : spectrum) { REQUIRE (std::isfinite (value.real())); REQUIRE (std::isfinite (value.imag())); power += std::norm (value); }
        REQUIRE (std::abs (power / 10000 - 1) < .005);
        for (int bin = 430; bin < bins; ++bin) REQUIRE (std::abs (spectrum[(size_t) bin]) < 1.0e-6f);
    }
    workspace->releaseWaveforms(); REQUIRE (! workspace->hasWaveforms());
}

TEST_CASE ("Downstream Partial Scale retains oscillator shape and neutral centroid does not retune its fundamental")
{
    constexpr int n = 4096, bins = n / 2 + 1;
    constexpr double binHz = 48000.0 / n, frequency = 32 * binHz;
    auto workspace = std::make_shared<Partial::Workspace>(); workspace->prepare (n, partialWindow (n)); workspace->prepareWaveforms();
    Partial resynth, neutral, stretch, replace;
    for (auto* engine : { &resynth, &neutral, &stretch, &replace }) engine->prepare (48000, n, 512, 2, partialWindow (n), 2, workspace);
    auto wave = partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth); wave.shape = Partial::Shape::Saw;
    resynth.setStages ({ wave }); resynth.setFundamentalHz (frequency);
    neutral.setStages ({ partialStage (Partial::Type::Stretcher, 0) }); neutral.setAnchorMode (Partial::AnchorMode::EnergyCentroid);
    stretch.setStages ({ partialStage (Partial::Type::Centroid, 100) }); stretch.setFundamentalHz (frequency * 2);
    replace.setStages ({ partialStage (Partial::Type::Stretcher, 0, 0, Partial::Mode::Resynth) });
    std::vector<std::complex<float>> spectrum ((size_t) bins); spectrum[32] = { 100, 0 };
    Partial::SliceContext context { 1, frequency, frequency, false };
    resynth.processSlice (spectrum.data(), bins, context, 12000, 0);
    neutral.processSlice (spectrum.data(), bins, context, 12000, 0);
    REQUIRE (context.resynthShape == Partial::Shape::Saw); REQUIRE (std::abs (context.currentAnchorHz - frequency) < 1.0e-8);
    stretch.processSlice (spectrum.data(), bins, context, 12000, 0);
    REQUIRE (context.resynthShape == Partial::Shape::Saw); REQUIRE (std::abs (context.currentAnchorHz - frequency * 2) < 1.0e-8);
    REQUIRE (std::abs (std::abs (spectrum[128]) / std::abs (spectrum[64]) - .5f) < .001f);
    replace.processSlice (spectrum.data(), bins, context, 12000, 0);
    REQUIRE (context.resynthShape == Partial::Shape::Sine); REQUIRE (std::abs (spectrum[128]) / std::abs (spectrum[64]) < 1.0e-4f);
}

TEST_CASE ("Partial waveform fundamentals keep actual-hop phase and the legacy Sine is exact")
{
    constexpr int n = 2048, bins = n / 2 + 1, hop = 293, bin = 24;
    constexpr double frequency = 48000.0 * bin / n;
    auto workspace = std::make_shared<Partial::Workspace>(); workspace->prepare (n, partialWindow (n)); workspace->prepareWaveforms();
    for (const auto shape : { Partial::Shape::Triangle, Partial::Shape::Saw, Partial::Shape::Square })
    {
        Partial engine; engine.prepare (48000, n, hop, 2, partialWindow (n), 2, workspace);
        auto stage = partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth); stage.shape = shape; engine.setStages ({ stage }); engine.setFundamentalHz (frequency);
        std::array<double, 2> previous {};
        for (int frame = 0; frame < 12; ++frame)
        {
            engine.beginBlock(); if (frame == 6) engine.setStages ({ stage });
            for (int channel = 0; channel < 2; ++channel)
            {
                std::vector<std::complex<float>> spectrum ((size_t) bins); spectrum[bin] = std::polar (100.0f, (float) (.3 + .7 * channel));
                Partial::SliceContext context { 1, frequency, frequency, false };
                engine.processSlice (spectrum.data(), bins, context, 12000, channel);
                const double phase = std::arg (spectrum[bin]);
                if (frame > 0) REQUIRE (std::abs (std::remainder (phase - previous[(size_t) channel] - 2 * partialPi * bin * hop / n, 2 * partialPi)) < 2.0e-4);
                previous[(size_t) channel] = phase;
            }
            REQUIRE (std::abs (std::remainder (previous[1] - previous[0] - .7, 2 * partialPi)) < 2.0e-4);
        }
    }
    Partial legacy, explicitSine;
    for (auto* engine : { &legacy, &explicitSine }) { engine->prepare (48000, n, hop, 2, partialWindow (n), 2); engine->setFundamentalHz (frequency); }
    auto stage = partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth); legacy.setStages ({ stage }); stage.shape = Partial::Shape::Sine; explicitSine.setStages ({ stage });
    std::vector<std::complex<float>> a ((size_t) bins); a[bin] = { 100, 15 }; auto b = a;
    Partial::SliceContext first { 1, frequency, frequency, false }, second = first;
    legacy.processSlice (a.data(), bins, first, 12000, 0); explicitSine.processSlice (b.data(), bins, second, 12000, 0); REQUIRE (a == b);
}

TEST_CASE ("Partial waveform harmonic-bank transitions are continuous under moving pitch")
{
    constexpr int n = 4096, bins = n / 2 + 1;
    auto workspace = std::make_shared<Partial::Workspace>(); workspace->prepare (n, partialWindow (n)); workspace->prepareWaveforms();
    for (const auto shape : { Partial::Shape::Triangle, Partial::Shape::Saw, Partial::Shape::Square })
    {
        std::array<std::vector<std::complex<float>>, 2> output;
        for (int side = 0; side < 2; ++side)
        {
            const double frequency = 1500 * (1.0 + (side == 0 ? -1.0e-7 : 1.0e-7));
            Partial engine; engine.prepare (48000, n, 585, 2, partialWindow (n), 2, workspace);
            auto stage = partialStage (Partial::Type::Centroid, 100, 0, Partial::Mode::Resynth); stage.shape = shape;
            engine.setStages ({ stage }); engine.setFundamentalHz (frequency);
            output[(size_t) side].resize (bins); output[(size_t) side][50] = { 100, 0 };
            Partial::SliceContext context { 1, 1500, 1500, false };
            engine.processSlice (output[(size_t) side].data(), bins, context, 12000, 0);
        }
        double difference = 0, energy = 0;
        for (int bin = 0; bin < bins; ++bin) { difference += std::norm (output[0][(size_t) bin] - output[1][(size_t) bin]); energy += std::norm (output[0][(size_t) bin]); }
        REQUIRE (energy > 1); REQUIRE (std::sqrt (difference / energy) < .001);
    }
}

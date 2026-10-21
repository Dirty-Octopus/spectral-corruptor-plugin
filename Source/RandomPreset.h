// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "modules/ModuleSpecs.h"
#include "parameters/TransferCurve.h"
#include <array>
#include <cmath>

namespace scrr {

// Pure, seeded rack generation. The caller owns UIDs, Undo and all global state.
struct RandomPreset
{
    static constexpr int minimumEffects = 1, maximumEffects = 12, defaultEffects = 6;
    struct Result
    {
        std::array<juce::ValueTree, 4> chains { juce::ValueTree ("ch1"), juce::ValueTree ("ch2"),
                                             juce::ValueTree ("ch3"), juce::ValueTree ("ch4") };
        int effectCount {};
    };

    static Result generate (int count, juce::int64 seed, int slices, double frequencyLimit,
                            std::array<int, 4> capacity = { 32, 32, 32, 32 })
    {
        count = juce::jlimit (minimumEffects, maximumEffects, count);
        const int channels = juce::jlimit (1, 4, slices);
        const double limit = std::isfinite (frequencyLimit) ? juce::jlimit (40.0, 96000.0, frequencyLimit) : 22050.0;
        Result result;
        int available = 0;
        for (int ch = 0; ch < channels; ++ch) { capacity[(size_t) ch] = juce::jlimit (0, 32, capacity[(size_t) ch]); available += capacity[(size_t) ch]; }
        if (available < count) return result;

        // Decorrelate adjacent user/test seeds before JUCE's 48-bit generator.
        // Otherwise seeds 1..32 all select index zero on the first shuffle and
        // systematically move the first effect outside a short generated rack.
        auto mixed = (uint64_t) seed + UINT64_C (0x9e3779b97f4a7c15);
        mixed = (mixed ^ (mixed >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
        mixed = (mixed ^ (mixed >> 27)) * UINT64_C (0x94d049bb133111eb);
        mixed ^= mixed >> 31;
        juce::Random random ((juce::int64) (mixed & UINT64_C (0x7fffffffffffffff)));
        std::vector<const dsp::ModuleSpec*> pool;
        for (const auto& spec : dsp::getModuleSpecs())
            if (spec.typeId != "Notes" && spec.typeId != "Utility" && (slices > 1 || ! dsp::isPartialEffect (spec.typeId)))
                pool.push_back (&spec);
        for (int i = (int) pool.size() - 1; i > 0; --i) std::swap (pool[(size_t) i], pool[(size_t) random.nextInt (i + 1)]);
        int nextChannel = random.nextInt (channels);
        for (int index = 0; index < count; ++index)
        {
            while (capacity[(size_t) nextChannel] == 0) nextChannel = (nextChannel + 1) % channels;
            auto& chain = result.chains[(size_t) nextChannel]; --capacity[(size_t) nextChannel];
            auto module = makeModule (*pool[(size_t) index % pool.size()], random, limit);
            int insertion = chain.getNumChildren();
            if (! dsp::isGenericEffect (module["type"].toString()))
                for (int i = 0; i < chain.getNumChildren(); ++i)
                    if (dsp::isGenericEffect (chain.getChild (i)["type"].toString())) { insertion = i; break; }
            chain.addChild (module, insertion, nullptr);
            ++result.effectCount; nextChannel = (nextChannel + 1) % channels;
        }
        return result;
    }

private:
    static juce::ValueTree makeModule (const dsp::ModuleSpec& spec, juce::Random& random, double limit)
    {
        auto module = dsp::createDefaultModuleState (spec.typeId);
        module.setProperty ("enabled", true, nullptr);
        const auto& type = spec.typeId;
        auto between = [&] (double low, double high, bool logarithmic = false)
        {
            if (high < low) std::swap (low, high);
            const double position = random.nextDouble();
            return logarithmic && low > 0 ? std::exp (std::log (low) + position * (std::log (high) - std::log (low)))
                                         : low + position * (high - low);
        };
        auto set = [&] (const char* id, double low, double high, bool logarithmic = false)
        {
            for (const auto& p : spec.params) if (p.id == id)
            {
                low = juce::jlimit ((double) p.minVal, (double) p.maxVal, low);
                high = juce::jlimit (low, (double) p.maxVal, high);
                double value = between (low, high, logarithmic);
                if (p.step > 0) value = (double) p.minVal + std::round ((value - p.minVal) / p.step) * p.step;
                value = juce::jlimit ((double) p.minVal, (double) p.maxVal, value);
                module.setProperty (p.id, p.type == dsp::ParamSpec::Float ? juce::var (value) : juce::var (juce::roundToInt (value)), nullptr);
                return;
            }
        };
        for (const auto& p : spec.params)
        {
            if (p.type == dsp::ParamSpec::Bool) module.setProperty (p.id, random.nextBool(), nullptr);
            else if (p.type == dsp::ParamSpec::Choice || p.id == "seed") set (p.id.toRawUTF8(), p.minVal, p.maxVal);
            else if (p.logScale) set (p.id.toRawUTF8(), p.minVal, p.maxVal, true);
            else set (p.id.toRawUTF8(), p.minVal + (p.maxVal - p.minVal) * .15, p.minVal + (p.maxVal - p.minVal) * .6);
        }
        // Keep destructive mixes useful, feedback bounded, and automatic gains
        // conservative. These are sound-design ranges, not output guarantees.
        set ("amount", 15, 55); set ("mix", 25, 65); set ("rate", .15, 3, true);
        set ("length", 2, 16); set ("blockSize", 2, 16); set ("density", 10, 50);
        set ("depth", .5, 5); set ("magDelay", 1, 12); set ("phaseDelay", 1, 16);
        set ("freq", 80, juce::jmin (10000.0, limit * .8), true); set ("q", .4, 1.4);
        set ("fundamental", 80, juce::jmin (440.0, limit * .5), true);
        set ("odd", -12, 3); set ("even", -12, 3); set ("drive", 0, 8); set ("output", -6, 0);
        set ("threshold", -24, -6); set ("makeup", 0, 0);
        set ("shift", -juce::jmin (300.0, limit * .08), juce::jmin (300.0, limit * .08));
        set ("pivot", 100, juce::jmin (4000.0, limit * .7), true); set ("spread", 30, 600, true);
        set ("spacing", 60, 1000, true); set ("width", 20, 65);
        if (type == "BinShuffle") set ("amount", .05, .35);
        if (type == "RandomBinDeath" || type == "BinTeleport" || type == "SpectralDropout") set ("amount", 10, 40);
        if (type == "SpectralContrast") set ("amount", -30, 30);
        if (type == "FrequencyWarp") set ("shape", -65, 65);
        if (type == "BinHold" || type == "FrameHold") set ("rate", 4, 32);
        if (type == "ComplexRotation") module.setProperty ("randomAmount", false, nullptr);
        if (type == "HarmonicSculpt" || type == "HarmonicMatch") module.setProperty ("track", true, nullptr);
        if (dsp::isPartialEffect (type))
        {
            set ("reference", 55, 440, true);
            if (type == "PartialStretcher") set ("amount", -12, 12);
            if (type == "PartialStiffString") set ("amount", 3, 25);
            if (type == "PartialCentroid") set ("target", 80, juce::jmin (3000.0, limit * .75), true);
            if (type == "PartialPGrid") set ("pitch", 3, 12);
            if (type == "PartialFGrid") set ("pitch", 30, 300, true);
        }
        if (type == "SpectralEqualizer")
            for (int band = 0; band < dsp::spectralEqBands; ++band)
            {
                const auto key = [&] (const char* suffix) { return dsp::spectralEqParameter (band, suffix); };
                module.setProperty (key ("Enabled"), band % 2 == 0, nullptr);
                module.setProperty (key ("Type"), 0, nullptr);
                set (key ("Freq").toRawUTF8(), dsp::spectralEqFrequencies[(size_t) band] / 1.4,
                     juce::jmin (limit * .9, dsp::spectralEqFrequencies[(size_t) band] * 1.4), true);
                set (key ("Gain").toRawUTF8(), -5, 3); set (key ("Q").toRawUTF8(), .5, 1.5);
            }
        if (type == "Reverb")
        { set ("decay", .4, 4.5, true); set ("size", 20, 80); set ("predelay", 0, 60); set ("damping", 20, 80); set ("mix", 10, 35); }
        if (type == "Delay")
        { module.setProperty ("sync", false, nullptr); set ("time", 70, 600, true); set ("feedback", 10, 50); set ("mix", 15, 40); }
        if (type == "Compressor")
        { set ("ratio", 1.5, 5); set ("attack", 5, 40, true); set ("release", 40, 400, true); set ("knee", 3, 12); set ("mix", 50, 100); }
        if (type == "Distortion")
        {
            set ("drive", 2, 12); set ("bias", -.12, .12); set ("tone", 2000, juce::jmin (16000.0, limit), true); set ("mix", 10, 40);
            module.setProperty ("output", -(double) module["drive"] * .65 - 3, nullptr);
        }
        if (type == "FrequencyRemap")
        {
            params::TransferCurve curve;
            curve.points = { { 0, 0, (float) between (-.2, .2), 1 }, { .5f, (float) between (.3, .7), (float) between (-.2, .2), 3 }, { 1, 1, 0, 2 } };
            curve.nextPointId = 4; module.setProperty ("remapCurve", curve.encode(), nullptr); module.setProperty ("remapMode", 0, nullptr);
        }
        module.setProperty ("rangeLow", 0.0, nullptr); module.setProperty ("rangeHigh", 96000.0, nullptr);
        module.setProperty ("moduleMix", dsp::isGenericEffect (type) || dsp::isPartialEffect (type) ? 100.0 : between (60, 95), nullptr);
        return module;
    }
};
} // namespace scrr

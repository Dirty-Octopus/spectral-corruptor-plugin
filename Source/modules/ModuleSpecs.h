// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_core/juce_core.h>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace scrr::dsp {

inline bool isGenericEffect (const juce::String& type)
{ return type == "Reverb" || type == "Delay" || type == "Compressor" || type == "Distortion" || type == "Utility"; }

struct ParamSpec
{
    juce::String id;
    juce::String label;
    enum Type { Float, Int, Bool, Choice } type { Float };
    float minVal { 0.0f };
    float maxVal { 1.0f };
    float defaultVal { 0.0f };
    float step { 0.01f };
    juce::String unit;
    juce::StringArray choices;
    bool logScale { false };

    float fromTree (const juce::ValueTree& vt) const
    {
        if (type == Bool)
            return vt.getProperty (id, defaultVal) ? 1.0f : 0.0f;
        return (float) vt.getProperty (id, (double) defaultVal);
    }

    int intFromTree (const juce::ValueTree& vt) const
    {
        return (int) vt.getProperty (id, (int) defaultVal);
    }

    bool boolFromTree (const juce::ValueTree& vt) const
    {
        return (bool) vt.getProperty (id, (bool) defaultVal);
    }
};

struct ModuleSpec
{
    juce::String typeId;
    juce::String displayName;
    std::vector<ParamSpec> params;
};

inline ParamSpec makeFloat (const char* id, const char* label, float minV, float maxV, float def, float step, const char* unit = "", bool log = false)
{
    ParamSpec p;
    p.id = id; p.label = label; p.type = ParamSpec::Float;
    p.minVal = minV; p.maxVal = maxV; p.defaultVal = def; p.step = step; p.unit = unit;
    p.logScale = log;
    return p;
}

inline ParamSpec makeInt (const char* id, const char* label, int minV, int maxV, int def, const char* unit = "")
{
    ParamSpec p;
    p.id = id; p.label = label; p.type = ParamSpec::Int;
    p.minVal = (float) minV; p.maxVal = (float) maxV; p.defaultVal = (float) def; p.step = 1.0f; p.unit = unit;
    return p;
}

inline ParamSpec makeBool (const char* id, const char* label, bool def)
{
    ParamSpec p;
    p.id = id; p.label = label; p.type = ParamSpec::Bool;
    p.minVal = 0; p.maxVal = 1; p.defaultVal = def ? 1.0f : 0.0f; p.step = 1.0f;
    return p;
}

inline ParamSpec makeChoice (const char* id, const char* label, std::initializer_list<juce::String> choices, int defaultIdx)
{
    ParamSpec p;
    p.id = id; p.label = label; p.type = ParamSpec::Choice;
    p.minVal = 0.0f; p.maxVal = (float) ((int) choices.size() - 1);
    p.defaultVal = (float) defaultIdx; p.step = 1.0f;
    p.choices = juce::StringArray (choices);
    return p;
}

inline const std::vector<ModuleSpec>& getModuleSpecs()
{
    static const auto specs = [] {
    std::vector<ModuleSpec> result;
    auto* s = &result;
    auto add = [s] (const char* id, const char* name, std::vector<ParamSpec> params)
    { ModuleSpec m; m.typeId = id; m.displayName = name; m.params = std::move (params); s->push_back (std::move (m)); };

    add ("BinShuffle", "Bin Shuffle", {
        makeFloat ("amount", "Amount", 0.0f, 1.0f, 0.0f, 0.001f),
        makeInt ("seed", "Seed", 0, 999999, 0),
    });
    add ("RandomBinDeath", "Random Bin Death", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("seed", "Seed", 0, 999999, 0),
    });
    add ("SpectralDropout", "Spectral Dropout", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("density", "Density", 0.0f, 100.0f, 50.0f, 0.1f, " %"),
    });
    add ("BinTeleport", "Bin Teleport", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("seed", "Seed", 0, 999999, 0),
    });
    add ("SpectralPixelation", "Spectral Pixelation", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("blockSize", "Block Size", 1, 64, 64, " bins"),
    });
    add ("SpectralJPEG", "Spectral JPEG", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("blockSize", "Block Size", 1, 64, 8, " bins"),
    });
    add ("FrequencyWarp", "Frequency Warp", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("shape", "Shape", -100.0f, 100.0f, 50.0f, 0.1f),
    });
    add ("FrequencyDrift", "Frequency Drift", {
        makeFloat ("rate", "Rate", 0.01f, 10.0f, 0.25f, 0.01f, " Hz"),
        makeFloat ("depth", "Depth", 0.0f, 64.0f, 0.0f, 0.1f, " bins"),
    });
    add ("PhaseCorruption", "Phase Corruption", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("seed", "Seed", 0, 999999, 0),
    });
    add ("PhaseNoise", "Phase Noise", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("rate", "Rate", 0.01f, 64.0f, 8.0f, 0.01f, " Hz"),
    });
    add ("ComplexRotation", "Complex Rotation", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeBool ("randomAmount", "Random Amount", false),
        makeFloat ("randomRate", "Random Rate", 0.01f, 10.0f, 2.0f, 0.01f, " Hz"),
    });
    add ("MagnitudePhaseSwap", "Magnitude Phase Swap", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
    });
    add ("SpectralDispersion", "Spectral Dispersion", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("curve", "Curve", 0.0f, 200.0f, 50.0f, 0.1f),
    });
    add ("BinHold", "Bin Hold", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("rate", "Rate", 1.0f, 128.0f, 8.0f, 1.0f, " frames"),
        makeInt ("length", "Length", 1, 128, 8, " frames"),
        makeInt ("seed", "Seed", 0, 999999, 0),
    });
    add ("FrameHold", "Frame Hold", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeFloat ("rate", "Rate", 1.0f, 128.0f, 8.0f, 1.0f, " frames"),
        makeInt ("length", "Length", 1, 128, 8, " frames"),
    });
    add ("TemporalSmear", "Temporal Smear", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("length", "Length", 1, 64, 12, " frames"),
    });
    add ("TimeDesync", "Time Desync", {
        makeFloat ("amount", "Amount", 0.0f, 100.0f, 0.0f, 0.1f, " %"),
        makeInt ("magDelay", "Mag Delay", 0, 63, 1, " frames"),
        makeInt ("phaseDelay", "Phase Delay", 0, 63, 8, " frames"),
    });
    add ("SpectralFilter", "Spectral Filter", {
        makeChoice ("filterType", "Type", { "Low Pass", "High Pass", "Band Pass", "Notch" }, 0),
        makeFloat ("freq", "Frequency", 20.0f, 20000.0f, 1000.0f, 0.1f, " Hz", true),
        makeChoice ("slope", "Slope", { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1),
        makeFloat ("q", "Q", 0.1f, 20.0f, 0.707f, 0.001f),
        makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0.1f, " %"),
    });
    add ("Utility", "Utility", {
        makeChoice ("invert", "Phase Invert", { "None", "Left", "Right", "Both" }, 0),
        makeChoice ("mode", "Channel Mode", { "Stereo", "Left Only", "Right Only" }, 0),
        makeBool ("swap", "Swap L/R", false),
        makeFloat ("width", "Width", 0.0f, 200.0f, 100.0f, 0.1f, " %"),
        makeBool ("mono", "Mono", false),
        makeFloat ("gain", "Gain", -48.0f, 12.0f, 0.0f, 0.1f, " dB"),
        makeFloat ("balance", "Balance", -100.0f, 100.0f, 0.0f, 0.1f),
    });

    add ("SpectralContrast", "Spectral Contrast", {
        makeFloat ("amount", "Contrast", -100, 100, 0, 0.1f, " %") });
    add ("FrequencyShift", "Frequency Shift", {
        makeFloat ("shift", "Shift", -5000, 5000, 0, 0.1f, " Hz") });
    add ("HarmonicSculpt", "Harmonic Sculpt", {
        makeFloat ("amount", "Amount", 0, 100, 0, 0.1f, " %"),
        makeBool ("track", "Auto Track", true),
        makeFloat ("fundamental", "Fundamental", 40, 2000, 220, 0.1f, " Hz", true),
        makeFloat ("odd", "Odd Harmonics", -48, 12, 0, 0.1f, " dB"),
        makeFloat ("even", "Even Harmonics", -48, 12, 0, 0.1f, " dB"),
        makeFloat ("width", "Band Width", 5, 95, 30, 0.1f, " %") });
    add ("HarmonicMatch", "Harmonic Match", {
        makeChoice ("shape", "Shape", { "Triangle", "Square", "Saw", "Pointy", "Sweep" }, 0),
        makeChoice ("mode", "Mode", { "Scale", "Resynth" }, 0),
        makeFloat ("amount", "Amount", 0, 100, 0, 0.1f, " %"),
        makeFloat ("colour", "Colour / Sweep", 0, 100, 50, 0.1f, " %"),
        makeBool ("track", "Auto Track", true),
        makeFloat ("fundamental", "Fundamental", 40, 2000, 220, 0.1f, " Hz", true),
        makeFloat ("width", "Band Width", 5, 95, 50, 0.1f, " %") });
    add ("SpectralMirror", "Spectral Mirror", {
        makeFloat ("amount", "Amount", 0, 100, 0, .1f, " %"),
        makeFloat ("pivot", "Mirror Centre", 20, 10000, 1000, .1f, " Hz", true) });
    add ("SpectralBloom", "Spectral Bloom", {
        makeFloat ("amount", "Amount", 0, 100, 0, .1f, " %"),
        makeFloat ("spread", "Spread", 10, 3000, 150, .1f, " Hz", true) });
    add ("SpectralComb", "Spectral Comb", {
        makeFloat ("amount", "Depth", 0, 100, 0, .1f, " %"),
        makeFloat ("spacing", "Tooth Spacing", 20, 4000, 220, .1f, " Hz", true),
        makeFloat ("offset", "Offset", 0, 100, 0, .1f, " %"),
        makeFloat ("width", "Tooth Width", 2, 98, 35, .1f, " %") });
    add ("Reverb", "Reverb", {
        makeChoice ("mode", "Space", { "Plate", "Hall", "Room" }, 0),
        makeFloat ("decay", "Decay", .1f, 12, 2.5f, .01f, " s"),
        makeFloat ("size", "Size", 0, 100, 60, .1f, " %"),
        makeFloat ("predelay", "Pre-delay", 0, 200, 20, .1f, " ms"),
        makeFloat ("damping", "Damping", 0, 100, 45, .1f, " %"),
        makeFloat ("mix", "Mix", 0, 100, 25, .1f, " %") });
    add ("Delay", "Delay", {
        makeChoice ("mode", "Mode", { "Normal", "Tap Delay", "Ping-Pong" }, 0),
        makeFloat ("time", "Time", 1, 2000, 300, .1f, " ms", true),
        makeFloat ("feedback", "Feedback", 0, 95, 35, .1f, " %"),
        makeFloat ("mix", "Mix", 0, 100, 30, .1f, " %") });
    add ("Compressor", "Compressor", {
        makeFloat ("threshold", "Threshold", -60, 0, -18, .1f, " dB"),
        makeFloat ("ratio", "Ratio", 1, 20, 4, .1f, " :1"),
        makeFloat ("attack", "Attack", .1f, 200, 10, .1f, " ms", true),
        makeFloat ("release", "Release", 10, 2000, 100, .1f, " ms", true),
        makeFloat ("knee", "Knee", 0, 24, 6, .1f, " dB"),
        makeFloat ("makeup", "Makeup", 0, 24, 0, .1f, " dB"),
        makeFloat ("mix", "Mix", 0, 100, 100, .1f, " %") });
    add ("Distortion", "Distortion", {
        makeChoice ("mode", "Shape", { "Tube", "Overdrive", "Sin Fold", "Lin Fold" }, 0),
        makeFloat ("drive", "Drive", 0, 36, 6, .1f, " dB"),
        makeFloat ("bias", "Bias", -1, 1, 0, .001f),
        makeFloat ("tone", "Tone", 200, 20000, 16000, 1, " Hz", true),
        makeFloat ("output", "Output", -36, 12, -6, .1f, " dB"),
        makeFloat ("mix", "Mix", 0, 100, 100, .1f, " %") });
    add ("Notes", "Notes", {});
    return result;
    }();
    return specs;
}

inline const ModuleSpec* findModuleSpec (const juce::String& typeId)
{
    for (const auto& s : getModuleSpecs())
        if (s.typeId == typeId)
            return &s;
    return nullptr;
}

inline juce::ValueTree createDefaultModuleState (const juce::String& typeId)
{
    auto vt = juce::ValueTree ("module");
    vt.setProperty ("type", typeId, nullptr);
    vt.setProperty ("enabled", false, nullptr);
    if (typeId == "Notes") vt.setProperty ("notes", juce::String(), nullptr);

    if (auto* spec = findModuleSpec (typeId))
        for (const auto& p : spec->params)
        {
            if (p.type == ParamSpec::Bool)
                vt.setProperty (p.id, p.defaultVal > 0.5f, nullptr);
            else if (p.type == ParamSpec::Int || p.type == ParamSpec::Choice)
                vt.setProperty (p.id, (int) p.defaultVal, nullptr);
            else
                vt.setProperty (p.id, (double) p.defaultVal, nullptr);
        }

    return vt;
}

} // namespace scrr::dsp

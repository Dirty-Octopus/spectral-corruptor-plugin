// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "Theme.h"
#include "../modules/ModuleSpecs.h"

namespace scrr::gui {
inline juce::String effectFolder (const juce::String& type)
{
    if (scrr::dsp::isGenericEffect (type)) return "GENERIC";
    if (type.startsWith ("Harmonic") || type == "SpectralContrast") return "HARMONICS";
    if (type.startsWith ("Frequency") || type == "SpectralMirror" || type == "BinTeleport") return "FREQUENCY";
    if (type.contains ("Phase") || type == "ComplexRotation" || type == "SpectralDispersion") return "PHASE";
    if (type.contains ("Hold") || type == "TemporalSmear" || type == "TimeDesync" || type == "SpectralBloom") return "TEXTURE / TIME";
    if (type == "Utility" || type == "SpectralFilter" || type == "SpectralComb") return "FILTER / UTILITY";
    return "DESTRUCTION";
}
inline juce::String effectMotto (const juce::String& type)
{
    if (type == "Reverb") return "GIVE THE SIGNAL A SPACE";
    if (type == "Delay") return "REPEAT. SCATTER. RETURN.";
    if (type == "Compressor") return "KEEP THE PRESSURE UNDER CONTROL";
    if (type == "Distortion") return "DRAW A DIFFERENT WAVE";
    if (type == "HarmonicMatch") return "REWRITE THE OVERTONES";
    if (type == "HarmonicSculpt") return "ODD. EVEN. YOUR RULES.";
    if (type == "SpectralContrast") return "PULL THE DETAIL FORWARD";
    if (type == "SpectralJPEG") return "BEAUTY IN THE ARTIFACTS";
    if (type == "SpectralPixelation") return "LESS RESOLUTION. MORE CHARACTER.";
    if (type == "SpectralMirror") return "TURN THE SPECTRUM INSIDE OUT";
    if (type == "SpectralBloom") return "LET THE PARTIALS BLEED";
    if (type == "SpectralComb") return "CUT A RHYTHM INTO FREQUENCY";
    if (type == "FrequencyShift") return "MOVE BEYOND THE HARMONIC GRID";
    if (type == "FrequencyWarp") return "BEND THE FREQUENCY AXIS";
    if (type == "FrequencyDrift") return "NOTHING STAYS IN PLACE";
    if (type == "BinShuffle") return "REARRANGE THE EVIDENCE";
    if (type == "BinTeleport") return "NOW HERE. NOW ELSEWHERE.";
    if (type == "RandomBinDeath") return "MAKE SPACE BY SUBTRACTION";
    if (type == "SpectralDropout") return "THE GAPS BECOME THE SIGNAL";
    if (type == "BinHold") return "SMALL FRAGMENTS. LONG SHADOWS.";
    if (type == "FrameHold") return "CAPTURE A MOMENT. REPEAT IT.";
    if (type == "TemporalSmear") return "PRESENT TENSE / PAST SIGNAL";
    if (type == "TimeDesync") return "PULL TIME APART";
    if (type == "PhaseCorruption") return "BREAK THE ALIGNMENT";
    if (type == "PhaseNoise") return "DISORDER IN THE ANGLES";
    if (type == "ComplexRotation") return "A DIFFERENT POINT OF VIEW";
    if (type == "MagnitudePhaseSwap") return "EXCHANGE THE DIMENSIONS";
    if (type == "SpectralDispersion") return "EVERY PARTIAL TAKES ITS TIME";
    if (type == "SpectralFilter") return "SHAPE WHAT GETS THROUGH";
    return "TAKE CONTROL OF THE SIGNAL";
}
inline juce::Colour effectColour (const juce::String& type)
{
    const auto folder = effectFolder (type);
    if (folder == "PHASE" || folder == "TEXTURE / TIME") return Theme::blue();
    if (folder == "DESTRUCTION" || folder == "FREQUENCY") return Theme::red();
    return Theme::yellow();
}
} // namespace scrr::gui

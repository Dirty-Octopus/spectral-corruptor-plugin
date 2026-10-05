// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include "ModuleSpecs.h"

#include "BinShuffle.h"
#include "RandomBinDeath.h"
#include "SpectralDropout.h"
#include "BinTeleport.h"
#include "SpectralPixelation.h"
#include "SpectralJPEG.h"
#include "FrequencyWarp.h"
#include "FrequencyDrift.h"
#include "PhaseCorruption.h"
#include "PhaseNoise.h"
#include "ComplexRotation.h"
#include "MagnitudePhaseSwap.h"
#include "SpectralDispersion.h"
#include "BinHold.h"
#include "FrameHold.h"
#include "TemporalSmear.h"
#include "TimeDesync.h"
#include "SpectralFilter.h"
#include "Utility.h"
#include "HarmonicTools.h"
#include "SpectralShapes.h"
#include "GenericEffects.h"

namespace scrr::dsp {

inline std::unique_ptr<SpectralModule> createModule (const juce::String& typeId)
{
    if (typeId == "BinShuffle")          return std::make_unique<BinShuffle>();
    if (typeId == "RandomBinDeath")      return std::make_unique<RandomBinDeathModule>();
    if (typeId == "SpectralDropout")     return std::make_unique<SpectralDropoutModule>();
    if (typeId == "BinTeleport")         return std::make_unique<BinTeleportModule>();
    if (typeId == "SpectralPixelation")  return std::make_unique<SpectralPixelationModule>();
    if (typeId == "SpectralJPEG")        return std::make_unique<SpectralJPEGModule>();
    if (typeId == "FrequencyWarp")       return std::make_unique<FrequencyWarpModule>();
    if (typeId == "FrequencyDrift")      return std::make_unique<FrequencyDriftModule>();
    if (typeId == "PhaseCorruption")     return std::make_unique<PhaseCorruptionModule>();
    if (typeId == "PhaseNoise")          return std::make_unique<PhaseNoiseModule>();
    if (typeId == "ComplexRotation")     return std::make_unique<ComplexRotationModule>();
    if (typeId == "MagnitudePhaseSwap")  return std::make_unique<MagnitudePhaseSwapModule>();
    if (typeId == "SpectralDispersion")  return std::make_unique<SpectralDispersionModule>();
    if (typeId == "BinHold")             return std::make_unique<BinHoldModule>();
    if (typeId == "FrameHold")           return std::make_unique<FrameHoldModule>();
    if (typeId == "TemporalSmear")       return std::make_unique<TemporalSmearModule>();
    if (typeId == "TimeDesync")          return std::make_unique<TimeDesyncModule>();
    if (typeId == "SpectralFilter")      return std::make_unique<SpectralFilterModule>();
    if (typeId == "Utility")             return std::make_unique<UtilityModule>();
    if (typeId == "SpectralContrast") return std::make_unique<SpectralContrast>();
    if (typeId == "FrequencyShift") return std::make_unique<FrequencyShift>();
    if (typeId == "HarmonicSculpt") return std::make_unique<HarmonicSculpt>();
    if (typeId == "HarmonicMatch") return std::make_unique<HarmonicMatch>();
    if (typeId == "SpectralMirror") return std::make_unique<SpectralMirror>();
    if (typeId == "SpectralBloom") return std::make_unique<SpectralBloom>();
    if (typeId == "SpectralComb") return std::make_unique<SpectralComb>();
    if (typeId == "Reverb") return std::make_unique<GenericReverb>();
    if (typeId == "Delay") return std::make_unique<GenericDelay>();
    if (typeId == "Compressor") return std::make_unique<GenericCompressor>();
    if (typeId == "Distortion") return std::make_unique<GenericDistortion>();
    return nullptr;
}

} // namespace scrr::dsp

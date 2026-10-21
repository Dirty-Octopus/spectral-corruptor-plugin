// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"

namespace scrr::dsp {

/** Rack marker for the slice-aware engine in SpectralProcessor. A partial stage
    requires the original slice identity and carried anchor; processing the
    already-summed channel spectrum would lose that information. */
class PartialEffectModule final : public SpectralModule
{
public:
    explicit PartialEffectModule (const juce::String& type) : name (type.toStdString()) {}
    const char* getName() const noexcept override { return name.c_str(); }
    void processSpectrum (std::complex<float>*, int, double) override {}
private:
    std::string name;
};

} // namespace scrr::dsp

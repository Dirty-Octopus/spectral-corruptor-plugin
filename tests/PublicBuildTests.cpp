// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "doctest.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"

TEST_CASE ("The source placeholder never activates or opens the input signal path")
{
    auto license = scrr::licensing::LicenseManager::shared();
    REQUIRE (! license->isActivated());
    REQUIRE (license->validate ("anything").failed());
    REQUIRE (license->activate ("anything").failed());
    REQUIRE (! license->isActivated());
    REQUIRE (scrr::licensing::LicenseManager::defaultFile() == juce::File());
    REQUIRE (scrr::factoryPresets::getEntries().isEmpty());
    SpectralCrrptProcessor signal, silence;
    signal.prepareToPlay (48000, 128); silence.prepareToPlay (48000, 128);
    juce::AudioBuffer<float> a (2, 257), b (2, 257); juce::MidiBuffer midi;
    signal.getAPVTS().getParameter (scrr::params::id::drywet)->setValueNotifyingHost (0);
    signal.getAPVTS().getParameter (scrr::params::id::bypass)->setValueNotifyingHost (1);
    double power = 0;
    for (int block = 0; block < 100; ++block)
    {
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 257; ++i) a.setSample (ch, i, .75f);
        b.clear();
        if (block % 2 == 0) signal.processBlock (a, midi); else signal.processBlockBypassed (a, midi);
        silence.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 257; ++i)
        {
            const float value = a.getSample (ch, i);
            REQUIRE (std::isfinite (value)); REQUIRE (std::abs (value) < .12f);
            REQUIRE (std::abs (value - b.getSample (ch, i)) < 1.0e-7f);
            power += value * value;
        }
    }
    REQUIRE (power > 0);
    juce::MemoryBlock state; signal.getStateInformation (state);
    silence.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (! silence.getLicense().isActivated());
    for (int i = 0; i < 4; ++i)
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (signal.createEditor());
        REQUIRE (editor != nullptr);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    return doctest::runTests();
}

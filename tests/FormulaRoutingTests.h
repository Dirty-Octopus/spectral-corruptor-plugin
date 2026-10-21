// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

namespace {
scrr::params::RemapFormula routedRatioFormula()
{
    using namespace scrr::params;
    RemapFormula formula;
    FormulaParameter parameter; parameter.id = "stable_ratio_1"; parameter.name = "Ratio";
    parameter.minimum = 1; parameter.maximum = 5; parameter.defaultValue = 2; parameter.step = .001;
    formula.parameters.push_back (parameter);
    FormulaNode control; control.id = 1; control.op = FormulaOp::Parameter; control.parameterId = parameter.id;
    FormulaNode output; output.id = 2; output.op = FormulaOp::Multiply; output.inputs = {{ 0, 1, -1 }};
    formula.nodes.push_back (control); formula.nodes.push_back (output); formula.outputNode = 2;
    return formula;
}
}

TEST_CASE ("Formula custom Macro renders the changed frequency while its saved base stays unchanged")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 5);
    setParam (*p, id::windowTypeExtended, 1); setParam (*p, id::overlapFactor, 7.35f);
    setParam (*p, id::freqSplitNumSplits, 1);
    p->addModule (1, "FrequencyRemap"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto formula = routedRatioFormula(); const auto parameter = formula.parameters[0].property();
    p->setModuleParameter (1, uid, "remapFormula", formula.encode()); p->setModuleParameter (1, uid, "remapMode", 1);
    p->assignMacro (0, 1, uid, parameter);
    REQUIRE (p->getMacroMappings().size() == 1);
    auto mapping = p->getMacroMappings().front(); mapping.setDepth (.5, false); p->updateMacroMapping (mapping);
    p->prepareToPlay (48000, 128);
    juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
    std::vector<float> rendered (32768);
    const auto reserved = p->getProcessor().getEffectCacheBytes();
    for (int base = 0; base < (int) rendered.size(); base += 128)
    {
        if (base == 16384) setParam (*p, id::macros[0], 100);
        for (int sample = 0; sample < 128; ++sample)
        {
            const float input = .3f * (float) std::sin (juce::MathConstants<double>::twoPi * 200 * (base + sample) / 48000.0);
            block.setSample (0, sample, input); block.setSample (1, sample, input);
        }
        p->processBlock (block, midi);
        for (int sample = 0; sample < 128; ++sample) rendered[(size_t) (base + sample)] = block.getSample (0, sample);
    }
    auto amplitude = [&] (int start, double hz)
    {
        std::complex<double> coefficient {};
        for (int sample = start; sample < start + 4800; ++sample)
            coefficient += (double) rendered[(size_t) sample] * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * sample / 48000.0);
        return std::abs (coefficient) * 2.0 / 4800;
    };
    REQUIRE (amplitude (8192, 400) > .1); REQUIRE (amplitude (8192, 800) < .01);
    REQUIRE (amplitude (24576, 800) > .1); REQUIRE (amplitude (24576, 400) < .01);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter] - 2) < 1.0e-12);
    REQUIRE (std::abs (p->getEffectiveValue (1, uid, parameter, 0) - 4) < 1.0e-12);
    REQUIRE (p->getProcessor().getEffectCacheBytes() == reserved);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    REQUIRE (std::abs ((double) restored->getModuleChain (1).getChild (0)[parameter] - 2) < 1.0e-12);
    REQUIRE (restored->getMacroMappings().size() == 1);
}

TEST_CASE ("Formula structural edits preserve stable routes and atomically remove parameters with undo")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap"); const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto formula = routedRatioFormula(); const auto parameter = formula.parameters[0].property();
    p->setModuleParameter (1, uid, "remapFormula", formula.encode()); p->setModuleParameter (1, uid, "remapMode", 1);
    p->setModuleParameter (1, uid, parameter, 4.0); p->assignMacro (0, 1, uid, parameter);
    formula.parameters[0].name = "Stretch"; formula.parameters[0].maximum = 3;
    p->setModuleParameter (1, uid, "remapFormula", formula.encode());
    REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (p->getMacroMappings().front().parameter == parameter);
    REQUIRE (p->getMacroMappings().front().label.contains ("Stretch"));
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter] - 3) < 1.0e-12);
    formula.parameters.clear(); formula.nodes.resize (1); formula.outputNode = 0;
    p->setModuleParameter (1, uid, "remapFormula", formula.encode());
    REQUIRE (! p->getModuleChain (1).getChild (0).hasProperty (parameter)); REQUIRE (p->getMacroMappings().empty());
    REQUIRE (p->undo()); REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)[parameter] - 3) < 1.0e-12);
    REQUIRE (p->redo()); REQUIRE (p->getMacroMappings().empty());
    REQUIRE (! p->getModuleChain (1).getChild (0).hasProperty (parameter));
}

TEST_CASE ("Partial waveform cache is lazy shared and released with its last requester")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 5);
    setParam (*p, id::freqSplitNumSplits, 4);
    p->addModule (1, "PartialStretcher"); p->addModule (1, "PartialStretcher");
    const auto first = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto second = p->getModuleChain (1).getChild (1)["uid"].toString();
    for (const auto& uid : { first, second }) p->setModuleParameter (1, uid, "mode", 1);
    p->prepareToPlay (48000, 128); juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
    auto process = [&] { block.clear(); p->processBlock (block, midi); };
    process(); const auto sineBytes = p->getProcessor().getEffectCacheBytes();
    const auto waveformBytes = scrr::dsp::PartialProcessor::Workspace::estimatedWaveformBytes();
    p->setModuleParameter (1, first, "shape", 1); process();
    REQUIRE (p->getProcessor().getEffectCacheBytes() == sineBytes + waveformBytes);
    p->setModuleParameter (1, second, "shape", 3); process();
    REQUIRE (p->getProcessor().getEffectCacheBytes() == sineBytes + waveformBytes);
    p->setModuleParameter (1, first, "shape", 0); process();
    REQUIRE (p->getProcessor().getEffectCacheBytes() == sineBytes + waveformBytes);
    p->setModuleParameter (1, second, "shape", 0); process();
    REQUIRE (p->getProcessor().getEffectCacheBytes() == sineBytes);
    p->setModuleParameter (1, second, "shape", 2); process();
    const auto beforeDisable = p->getProcessor().getEffectCacheBytes();
    p->setModuleParameter (1, second, "enabled", false); process();
    const int frames = (128 + p->getProcessor().getHopSize() - 1) / p->getProcessor().getHopSize() + 2;
    REQUIRE (p->getProcessor().getEffectCacheBytes() == beforeDisable - waveformBytes - scrr::dsp::PartialProcessor::estimatedPrivateBytes (frames));
}

TEST_CASE ("Insufficient waveform cache bypasses only its requesting partial effect")
{
    using namespace scrr::params;
    auto limited = testProcessor(), reference = testProcessor();
    limited->getProcessor().setEffectCacheBudgetBytes (1024u * 1024u);
    for (auto* processor : { limited.get(), reference.get() })
    {
        setParam (*processor, id::oversample, 0); setParam (*processor, id::fftSizeExtended, 5);
        setParam (*processor, id::windowTypeExtended, 1); setParam (*processor, id::freqSplitNumSplits, 4);
    }
    limited->addModule (1, "PartialStretcher");
    const auto rejected = limited->getModuleChain (1).getChild (0)["uid"].toString();
    limited->setModuleParameter (1, rejected, "mode", 1); limited->setModuleParameter (1, rejected, "shape", 2);
    juce::String survivor;
    for (auto* processor : { limited.get(), reference.get() })
    {
        processor->addModule (1, "PartialStretcher"); const auto chain = processor->getModuleChain (1);
        const auto uid = chain.getChild (chain.getNumChildren() - 1)["uid"].toString();
        processor->setModuleParameter (1, uid, "mode", 1);
        if (processor == limited.get()) survivor = uid;
        processor->prepareToPlay (48000, 128);
    }
    juce::AudioBuffer<float> a (2, 128), b (2, 128); juce::MidiBuffer midi; double energy = 0;
    for (int block = 0; block < 100; ++block)
    {
        for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < 128; ++sample)
            a.setSample (channel, sample, .2f * (float) std::sin (juce::MathConstants<double>::twoPi * 80 * (block * 128 + sample) / 48000.0));
        b.makeCopyOf (a); limited->processBlock (a, midi); reference->processBlock (b, midi);
        for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < 128; ++sample)
        {
            REQUIRE (std::abs (a.getSample (channel, sample) - b.getSample (channel, sample)) < 1.0e-7f);
            energy += (double) a.getSample (channel, sample) * a.getSample (channel, sample);
        }
    }
    REQUIRE (energy > .1);
    REQUIRE (limited->getProcessor().getModuleResourceStatus (1, rejected).blocked);
    REQUIRE (! limited->getProcessor().getModuleResourceStatus (1, survivor).blocked);
    REQUIRE ((bool) limited->getModuleChain (1).getChild (0)["enabled"]);
    REQUIRE (limited->getProcessor().getEffectCacheBytes() <= 1024u * 1024u);
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Frequency Remap curve survives host preset clipboard and undo")
{
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    scrr::params::TransferCurve curve; curve.points = { {0, .2f, -.4f}, {.5f, .8f, .3f}, {1, 1, 0} };
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    auto state = p->copyPresetState(); auto restored = testProcessor(); REQUIRE (restored->applyPreset (state));
    REQUIRE (restored->getModuleChain (1).getChild (0)["remapCurve"].toString() == curve.encode());
    juce::MemoryBlock block; p->getStateInformation (block); restored->setStateInformation (block.getData(), (int) block.getSize());
    REQUIRE (restored->getModuleChain (1).getChild (0)["remapCurve"].toString() == curve.encode());
    const auto copied = p->pasteModule (2, p->copyModule (1, uid)); REQUIRE (copied.isNotEmpty());
    REQUIRE (p->getModuleChain (2).getChild (0)["remapCurve"].toString() == curve.encode());
    scrr::params::TransferCurve reverse; reverse.points = { {0,1,0}, {1,0,0} };
    p->setModuleParameter (1, uid, "remapCurve", reverse.encode());
    REQUIRE (p->undo()); REQUIRE (p->getModuleChain (1).getChild (0)["remapCurve"].toString() == curve.encode());
    REQUIRE (p->redo()); REQUIRE (p->getModuleChain (1).getChild (0)["remapCurve"].toString() == reverse.encode());
    p->setModuleParameter (1, uid, "remapCurve", "malformed");
    REQUIRE (std::abs (scrr::params::TransferCurve::decode (p->getModuleChain (1).getChild (0)["remapCurve"].toString()).evaluate (.25) - .25) < 1.0e-6);
}

TEST_CASE ("Extended window selection preserves the old host parameter and legacy preset fallback")
{
    auto p = testProcessor();
    auto* original = dynamic_cast<juce::AudioParameterChoice*> (p->getAPVTS().getParameter (scrr::params::id::windowType));
    REQUIRE (original != nullptr); REQUIRE (original->choices.size() == 4);
    setParam (*p, scrr::params::id::windowType, 2);
    auto legacy = p->copyPresetState(); legacy.removeChild (legacy.getChildWithProperty ("id", scrr::params::id::windowTypeExtended), nullptr);
    setParam (*p, scrr::params::id::windowTypeExtended, 7);
    REQUIRE (p->applyPreset (legacy));
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::windowTypeExtended)->load()) < .001f);
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::windowType)->load() - 2) < .001f);
}

TEST_CASE ("Overlap accepts fractional factors and legacy states restore four")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0);
    setParam (*p, id::fftSizeExtended, 6); setParam (*p, id::windowTypeExtended, 8);
    setParam (*p, id::overlapFactor, 7.35f); p->prepareToPlay (48000, 128);
    REQUIRE (p->getProcessor().getFFTSize() == 4096);
    REQUIRE (p->getProcessor().getHopSize() == (int) std::round (4096.0 / 7.35));
    auto state = p->copyPresetState(); auto other = testProcessor(); REQUIRE (other->applyPreset (state));
    REQUIRE (std::abs (other->getAPVTS().getRawParameterValue (id::overlapFactor)->load() - 7.35f) < .001f);
    state.removeChild (state.getChildWithProperty ("id", id::overlapFactor), nullptr);
    REQUIRE (other->applyPreset (state));
    REQUIRE (std::abs (other->getAPVTS().getRawParameterValue (id::overlapFactor)->load() - 4.0f) < .001f);
}

TEST_CASE ("Explicit FFT windows keep pre-band follower levels calibrated")
{
    using namespace scrr::params;
    for (int window = 1; window <= 9; ++window) for (float overlap : {2.0f,7.35f})
    {
        auto p = testProcessor(); setParam (*p,id::oversample,0); setParam (*p,id::fftSizeExtended,6);
        setParam (*p,id::windowTypeExtended,(float)window); setParam (*p,id::overlapFactor,overlap);
        p->prepareToPlay (48000,128); juce::AudioBuffer<float> audio (2,128); juce::MidiBuffer midi;
        for (int block = 0; block < 100; ++block)
        {
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n)
                audio.setSample(c,n,.2f * (float)std::sin (juce::MathConstants<double>::twoPi * 3000 * (block*128+n) / 48000.0));
            p->processBlock(audio,midi);
        }
        REQUIRE (std::abs (p->getProcessor().getPreBandLevels()[0] - .2 / std::sqrt(2.0)) < .0002);
    }
}

TEST_CASE ("Partial stages retain mode anchor mappings and order in ordinary racks")
{
    auto p = testProcessor();
    p->addModule (2, "PartialStretcher"); p->addModule (2, "FrequencyShift"); p->addModule (2, "PartialCentroid");
    const auto uid = p->getModuleChain (2).getChild (0)["uid"].toString();
    p->setModuleParameter (2, uid, "mode", 1); p->setModuleParameter (2, uid, "anchor", 1);
    p->setModuleParameter (2, uid, "reference", 220.0); p->setModuleParameter (2, uid, "amount", 12.0);
    p->assignMacro (0, 2, uid, "amount");
    auto route = p->getMacroMappings().front(); route.responseCurve.points = {{0,0,-.5f}, {.5f,.2f,0}, {1,1,0}};
    p->updateMacroMapping (route);
    p->moveModule (2, 0, 2);
    REQUIRE (p->getModuleChain (2).getChild (2)["uid"].toString() == uid);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    auto module = restored->getModuleChain (2).getChild (2);
    REQUIRE (module["type"].toString() == "PartialStretcher"); REQUIRE ((int) module["mode"] == 1); REQUIRE ((int) module["anchor"] == 1);
    REQUIRE (std::abs ((double) module["reference"] - 220) < .001);
    REQUIRE (restored->getMacroMappings().size() == 1);
    REQUIRE (restored->getMacroMappings().front().responseCurve.sameAs (route.responseCurve));
    const auto pasted = p->pasteModule (3, p->copyModule (2, uid)); REQUIRE (pasted.isNotEmpty());
    REQUIRE (p->getModuleChain (3).getChild (0)["type"].toString() == "PartialStretcher");
    REQUIRE (p->getMacroMappings().size() == 2); REQUIRE (p->undo()); REQUIRE (p->getModuleChain (3).getNumChildren() == 0);
    REQUIRE (p->redo()); REQUIRE (p->getModuleChain (3).getNumChildren() == 1);
}

TEST_CASE ("Partial stages are transparent while frequency splitting is off")
{
    auto original = testProcessor(), altered = testProcessor();
    for (const auto& type : {"PartialStretcher", "PartialStiffString", "PartialCentroid", "PartialPGrid", "PartialFGrid"})
    {
        altered->addModule (1, type);
        const auto chain = altered->getModuleChain (1); const auto uid = chain.getChild (chain.getNumChildren() - 1)["uid"].toString();
        altered->setModuleParameter (1, uid, "mode", 1); altered->setModuleParameter (1, uid, "amount", 12.0);
    }
    for (auto* p : {original.get(), altered.get()}) { setParam (*p, scrr::params::id::oversample, 0); p->prepareToPlay (48000, 128); }
    juce::AudioBuffer<float> a (2,128), b (2,128); juce::MidiBuffer midi;
    for (int block = 0; block < 100; ++block)
    {
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n) a.setSample (c,n,.15f * std::sin ((float) (block * 128 + n) * .071f));
        b.makeCopyOf (a); original->processBlock (a,midi); altered->processBlock (b,midi);
        for (int c = 0; c < 2; ++c) REQUIRE (std::memcmp (a.getReadPointer(c), b.getReadPointer(c), 128 * sizeof(float)) == 0);
    }
}

TEST_CASE ("Retired Metal stages and their mappings are dropped without changing surviving rack effects")
{
    REQUIRE (! scrr::dsp::isPartialEffect ("PartialMetal"));
    REQUIRE (scrr::dsp::findModuleSpec ("PartialMetal") == nullptr);
    REQUIRE (scrr::dsp::createModule ("PartialMetal") == nullptr);
    auto original = testProcessor();
    original->addModule (1, "PartialStretcher"); original->addModule (1, "FrequencyShift"); original->addModule (1, "PartialCentroid");
    const auto chain = original->getModuleChain (1);
    const auto stretchUid = chain.getChild (0)["uid"].toString(), shiftUid = chain.getChild (1)["uid"].toString();
    original->setModuleParameter (1, stretchUid, "amount", 9.0);
    original->setModuleParameter (1, stretchUid, "mode", 1);
    original->setModuleParameter (1, shiftUid, "shift", 120.0);
    original->setModuleParameter (1, chain.getChild (2)["uid"].toString(), "target", 725.0);
    original->assignMacro (0, 1, stretchUid, "amount");
    const auto source = original->addModulator (false);
    original->assignModulator (source, 1, shiftUid, "shift");
    REQUIRE (original->getMacroMappings().size() == 2);
    auto legacy = original->copyPresetState();
    juce::ValueTree metal ("module");
    metal.setProperty ("type", "PartialMetal", nullptr); metal.setProperty ("uid", "retired-metal", nullptr);
    metal.setProperty ("enabled", true, nullptr); metal.setProperty ("amount", 12.0, nullptr);
    metal.setProperty ("quant", 80.0, nullptr); metal.setProperty ("mode", 1, nullptr);
    legacy.getChildWithName ("ch1").addChild (metal, 1, nullptr);
    auto mappings = legacy.getChildWithName ("macroMappings");
    for (int index = 0; index < 2; ++index)
    {
        auto retired = mappings.getChild (index).createCopy();
        retired.setProperty ("uid", "retired-metal", nullptr);
        retired.setProperty ("parameter", index == 0 ? "amount" : "quant", nullptr);
        mappings.appendChild (retired, nullptr);
    }
    const auto verify = [&] (SpectralCrrptProcessor& restored)
    {
        const auto rack = restored.getModuleChain (1);
        REQUIRE (rack.getNumChildren() == 3);
        REQUIRE (rack.getChild (0)["type"].toString() == "PartialStretcher");
        REQUIRE (rack.getChild (1)["type"].toString() == "FrequencyShift");
        REQUIRE (rack.getChild (2)["type"].toString() == "PartialCentroid");
        REQUIRE (std::abs ((double) rack.getChild (0)["amount"] - 9.0) < 1.0e-8);
        REQUIRE ((int) rack.getChild (0)["mode"] == 1);
        REQUIRE (std::abs ((double) rack.getChild (1)["shift"] - 120.0) < 1.0e-8);
        REQUIRE (std::abs ((double) rack.getChild (2)["target"] - 725.0) < 1.0e-8);
        const auto routes = restored.getMacroMappings(); REQUIRE (routes.size() == 2);
        REQUIRE (routes[0].uid == rack.getChild (0)["uid"].toString());
        REQUIRE (routes[0].parameter.toString() == "amount"); REQUIRE (routes[0].sourceUid.isEmpty());
        REQUIRE (routes[1].uid == rack.getChild (1)["uid"].toString());
        REQUIRE (routes[1].parameter.toString() == "shift"); REQUIRE (routes[1].sourceUid == source);
        REQUIRE (restored.getModulators().size() == 1);
    };
    auto preset = testProcessor(); REQUIRE (preset->applyPreset (legacy)); verify (*preset);
    juce::MemoryBlock hostState; const auto xml = legacy.createXml(); REQUIRE (xml != nullptr);
    juce::AudioProcessor::copyXmlToBinary (*xml, hostState);
    auto host = testProcessor(); host->setStateInformation (hostState.getData(), (int) hostState.getSize()); verify (*host);
    host->addModule (1, "PartialMetal"); verify (*host);
    juce::ValueTree clipboard ("SPECTRAL_EFFECT"); clipboard.setProperty ("version", 1, nullptr);
    clipboard.appendChild (metal.createCopy(), nullptr);
    REQUIRE (host->pasteModule (1, clipboard.toXmlString()).isEmpty()); verify (*host);
}

namespace spectralUpgradeTests {
inline std::vector<float> renderPartialOrder (bool shiftFirst, int mode, bool rightOnly = false, bool twoTones = false, bool blockedEffect = false)
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 6);
    setParam (*p, id::windowTypeExtended, 1); setParam (*p, id::overlapFactor, 7.35f);
    setParam (*p, id::freqSplitNumSplits, 4);
    for (int c = 0; c < 4; ++c) setParam (*p, id::channelEnabled[(size_t)c], c == 1 ? 1.0f : 0.0f);
    if (blockedEffect)
    {
        p->getProcessor().setEffectCacheBudgetBytes (1024 * 1024);
        p->addModule (2, "SpectralDispersion");
    }
    // Use exactly 20 bins at N=4096: this tests Partial order independently
    // of the pre-existing FrequencyShift fractional-bin interpolation loss.
    if (shiftFirst) p->addModule (2, "FrequencyShift");
    p->addModule (2, "PartialCentroid");
    if (! shiftFirst) p->addModule (2, "FrequencyShift");
    for (auto module : p->getModuleChain (2))
    {
        const auto uid = module["uid"].toString();
        if (module["type"].toString() == "FrequencyShift") p->setModuleParameter (2, uid, "shift", 234.375);
        else
        {
            p->setModuleParameter (2, uid, "amount", 100.0); p->setModuleParameter (2, uid, "target", 1000.0);
            p->setModuleParameter (2, uid, "anchor", 1); p->setModuleParameter (2, uid, "mode", mode);
        }
    }
    p->prepareToPlay (48000, 128);
    if (blockedEffect)
    {
        for (auto module : p->getModuleChain(2))
        {
            const auto status = p->getProcessor().getModuleResourceStatus (2,module["uid"].toString());
            REQUIRE (status.blocked == (module["type"].toString() == "SpectralDispersion"));
            if (status.blocked) REQUIRE (status.requiredBytes > status.availableBytes);
        }
    }
    juce::AudioBuffer<float> audio (2,128); juce::MidiBuffer midi; std::vector<float> output;
    for (int block = 0; block < 750; ++block)
    {
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n)
        {
            const double time = (block * 128 + n) / 48000.0;
            const double wave = std::sin (juce::MathConstants<double>::twoPi * 600 * time)
                + (twoTones ? std::sin (juce::MathConstants<double>::twoPi * 650 * time) : 0);
            audio.setSample (c,n,rightOnly && c == 0 ? 0.0f : .15f * (float) wave);
        }
        p->processBlock (audio,midi);
        if (block >= 375) output.insert (output.end(), audio.getReadPointer (rightOnly ? 1 : 0), audio.getReadPointer (rightOnly ? 1 : 0) + 128);
    }
    return output;
}
inline double amplitude (const std::vector<float>& audio, double hz)
{
    std::complex<double> sum {};
    for (size_t n = 0; n < audio.size(); ++n) sum += (double) audio[n] * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * (double)n / 48000.0);
    return 2 * std::abs(sum) / (double)audio.size();
}
}

TEST_CASE ("Partial resynthesis obeys rack order and supports a right-only energy anchor")
{
    for (const bool shiftFirst : {false,true}) for (const bool rightOnly : {false,true})
    {
        const auto audio = spectralUpgradeTests::renderPartialOrder (shiftFirst,1,rightOnly);
        const double expected = shiftFirst ? 1000.0 : 1234.375;
        const auto wanted = spectralUpgradeTests::amplitude (audio,expected);
        const auto wrongOrder = spectralUpgradeTests::amplitude (audio,shiftFirst ? 1234.375 : 1000.0);
        std::cout << "[PARTIAL ORDER] shiftFirst=" << shiftFirst << " rightOnly=" << rightOnly << " target=" << wanted << " other=" << wrongOrder << '\n';
        REQUIRE (wanted > .08); REQUIRE (wrongOrder < wanted * .05);
        for (const auto sample : audio) REQUIRE (std::isfinite(sample));
    }
}

TEST_CASE ("Scale retains a band's two tones while Resynth collapses it to one sine")
{
    const auto scale = spectralUpgradeTests::renderPartialOrder (false,0,false,true);
    const auto resynth = spectralUpgradeTests::renderPartialOrder (false,1,false,true);
    const auto lower = spectralUpgradeTests::amplitude (scale,1209.375), upper = spectralUpgradeTests::amplitude (scale,1259.375);
    const auto sine = spectralUpgradeTests::amplitude (resynth,1234.375);
    std::cout << "[PARTIAL TEXTURE] scale=" << lower << ',' << upper << " resynth=" << sine << '\n';
    REQUIRE (lower > .07); REQUIRE (upper > .07); REQUIRE (sine > .12);
    REQUIRE (spectralUpgradeTests::amplitude (resynth,1209.375) < sine * .05);
    REQUIRE (spectralUpgradeTests::amplitude (resynth,1259.375) < sine * .05);
}

TEST_CASE ("Memory budget bypasses only the expensive effect while partial audio continues")
{
    const auto reference = spectralUpgradeTests::renderPartialOrder (false,1);
    const auto protectedAudio = spectralUpgradeTests::renderPartialOrder (false,1,false,false,true);
    REQUIRE (reference.size() == protectedAudio.size());
    REQUIRE (spectralUpgradeTests::amplitude (protectedAudio,1234.375) > .14);
    for (size_t n = 0; n < reference.size(); ++n) REQUIRE (std::abs(reference[n]-protectedAudio[n]) < 1.0e-6f);
}

TEST_CASE ("Modulated slice counts reuse the prepared per-effect clone pool")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam(*p,id::oversample,0); setParam(*p,id::fftSizeExtended,6);
    setParam(*p,id::freqSplitNumSplits,4);
    p->addModule(1,"PartialStretcher"); p->addModule(1,"FrequencyRemap");
    p->assignMacro(0,0,{},id::freqSplitNumSplits);
    auto mapping = p->getMacroMappings().front(); mapping.setDepth(60.0/63.0,false); p->updateMacroMapping(mapping);
    p->prepareToPlay(48000,128);
    const auto allocations = p->getProcessor().getPartialCloneAllocationCount();
    const auto reserved = p->getProcessor().getEffectCacheBytes(); REQUIRE (allocations > 0);
    juce::AudioBuffer<float> audio(2,128); juce::MidiBuffer midi;
    for (int block = 0; block < 250; ++block)
    {
        setParam(*p,id::macros[0],(float)((block * 37)%101)); audio.clear(); p->processBlock(audio,midi);
        REQUIRE (p->getProcessor().getPartialCloneAllocationCount() == allocations);
        REQUIRE (p->getProcessor().getEffectCacheBytes() == reserved);
    }
    p->removeMacroMapping(0,{},id::freqSplitNumSplits);
    setParam(*p,id::freqSplitNumSplits,1); audio.clear(); p->processBlock(audio,midi);
    REQUIRE (p->getProcessor().getEffectCacheBytes() < reserved / 2);
    for (auto module : p->getModuleChain(1)) p->setModuleParameter(1,module["uid"].toString(),"enabled",false);
    audio.clear(); p->processBlock(audio,midi);
    REQUIRE (p->getProcessor().getEffectCacheBytes() == 0);
}

TEST_CASE ("New active effects take budget priority over unused future slice caches")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->getProcessor().setEffectCacheBudgetBytes(1024*1024);
    setParam(*p,id::oversample,0); setParam(*p,id::fftSizeExtended,4); setParam(*p,id::freqSplitNumSplits,4);
    p->addModule(1,"PartialStretcher"); p->addModule(1,"FrequencyRemap");
    p->assignMacro(0,0,{},id::freqSplitNumSplits);
    auto mapping = p->getMacroMappings().front(); mapping.setDepth(60.0/63.0,false); p->updateMacroMapping(mapping);
    p->prepareToPlay(48000,128);
    p->addModule(1,"TemporalSmear");
    juce::AudioBuffer<float> audio(2,128); audio.clear(); juce::MidiBuffer midi; p->processBlock(audio,midi);
    for (auto module : p->getModuleChain(1))
        REQUIRE (!p->getProcessor().getModuleResourceStatus(1,module["uid"].toString()).blocked);
    REQUIRE (p->getProcessor().getEffectCacheBytes() <= 1024*1024);
}

TEST_CASE ("Frequency Remap native inspector edits its saved curve")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    editor->addToDesktop (0); editor->setVisible (true); pump (60);
    auto* curve = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (*editor, "frequency-remap-curve"));
    REQUIRE (curve != nullptr); REQUIRE (curve->isVisible());
    const auto bounds = curve->getPlotBounds();
    curve->mouseDoubleClick (mouse (*curve, { bounds.getX() + bounds.getWidth() * .25f, bounds.getY() + bounds.getHeight() * .25f }));
    REQUIRE (scrr::params::TransferCurve::decode (p->getModuleChain (1).getChild (0)["remapCurve"].toString()).points.size() == 3);
    auto* reverse = findButton (*editor, "REVERSE MAP"); REQUIRE (reverse != nullptr); reverse->triggerClick(); pump();
    REQUIRE (std::abs (scrr::params::TransferCurve::decode (p->getModuleChain (1).getChild (0)["remapCurve"].toString()).evaluate (.25) - .75) < 1.0e-6);
}

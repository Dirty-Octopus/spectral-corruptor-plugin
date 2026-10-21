// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "modules/ModuleSpecs.h"
#include "RandomPreset.h"
#include <cmath>

namespace {
const juce::String chainNames[] = { "modules", "ch1", "ch2", "ch3", "ch4" };
constexpr int maxModulesPerChain = 32;
int stageInsertion (const juce::ValueTree& chain, const juce::ValueTree& module, int requested)
{
    if (module["type"].toString() == "Notes") return juce::jlimit (0, chain.getNumChildren(), requested < 0 ? chain.getNumChildren() : requested);
    int boundary = 0;
    while (boundary < chain.getNumChildren() && ! scrr::dsp::isGenericEffect (chain.getChild (boundary)["type"].toString())) ++boundary;
    const bool generic = scrr::dsp::isGenericEffect (module["type"].toString());
    return juce::jlimit (generic ? boundary : 0, generic ? chain.getNumChildren() : boundary,
                        requested < 0 ? (generic ? chain.getNumChildren() : boundary) : requested);
}
void orderStages (juce::ValueTree chain)
{
    std::vector<juce::ValueTree> originals, effects;
    for (auto child : chain) { originals.push_back (child); if (child["type"].toString() != "Notes") effects.push_back (child); }
    std::stable_partition (effects.begin(), effects.end(), [] (auto child) { return ! scrr::dsp::isGenericEffect (child["type"].toString()); });
    chain.removeAllChildren (nullptr); size_t index = 0;
    for (auto child : originals) chain.appendChild (child["type"].toString() == "Notes" ? child : effects[index++], nullptr);
}
juce::ValueTree cleanModule (const juce::ValueTree& source)
{
    const auto type = source["type"].toString();
    auto* spec = scrr::dsp::findModuleSpec (type);
    if (spec == nullptr) return {};
    auto result = scrr::dsp::createDefaultModuleState (type);
    result.setProperty ("uid", juce::Uuid().toString(), nullptr);
    result.setProperty ("enabled", (bool) source.getProperty ("enabled", false), nullptr);
    if (type == "FrequencyRemap") result.setProperty ("remapCurve", scrr::params::TransferCurve::decode (source["remapCurve"].toString()).encode(), nullptr);
    if (type == "Notes") result.setProperty ("notes", source["notes"].toString().substring (0, 65536), nullptr);
    if (type == "FrequencyRemap")
    {
        const auto formula = scrr::params::RemapFormula::decode (source["remapFormula"].toString());
        result.setProperty ("remapMode", (int) source.getProperty ("remapMode", 0) == 1 ? 1 : 0, nullptr);
        result.setProperty ("remapInvalidPolicy", (int) source.getProperty ("remapInvalidPolicy", 0) == 1 ? 1 : 0, nullptr);
        result.setProperty ("remapFormula", formula.encode(), nullptr);
        result.setProperty ("formulaView", source["formulaView"].toString().substring (0, 16384), nullptr);
        for (const auto& parameter : formula.parameters)
        {
            const double requested = (double) source.getProperty (parameter.property(), parameter.defaultValue);
            result.setProperty (parameter.property(), std::isfinite (requested)
                ? juce::jlimit (parameter.minimum, parameter.maximum, requested) : parameter.defaultValue, nullptr);
        }
    }
    for (const auto& p : spec->params)
    {
        auto value = (double) source.getProperty (p.id, p.defaultVal);
        if (! std::isfinite (value)) value = p.defaultVal;
        value = juce::jlimit ((double) p.minVal, (double) p.maxVal, value);
        result.setProperty (p.id, p.type == scrr::dsp::ParamSpec::Bool ? juce::var (value > 0.5)
                                    : p.type == scrr::dsp::ParamSpec::Float ? juce::var (value)
                                    : juce::var ((int) value), nullptr);
    }
    for (const auto* key : { "rangeLow", "rangeHigh", "moduleMix" })
    {
        const bool mix = juce::String (key) == "moduleMix";
        const double def = mix ? 100.0 : juce::String (key) == "rangeLow" ? 0.0 : 96000.0;
        double value = (double) source.getProperty (key, def);
        if (! std::isfinite (value)) value = def;
        result.setProperty (key, juce::jlimit (0.0, mix ? 100.0 : 96000.0, value), nullptr);
    }
    return result;
}
void syncFormulaParameters (juce::ValueTree module, const juce::ValueTree& clean)
{
    for (int i = module.getNumProperties(); --i >= 0;)
    {
        const auto property = module.getPropertyName (i);
        if (property.toString().startsWith ("formulaParam_") && ! clean.hasProperty (property)) module.removeProperty (property, nullptr);
    }
    for (int i = 0; i < clean.getNumProperties(); ++i)
    {
        const auto property = clean.getPropertyName (i);
        if (property.toString().startsWith ("formulaParam_")) module.setProperty (property, clean[property], nullptr);
    }
}
}

SpectralCrrptProcessor::SpectralCrrptProcessor (std::shared_ptr<scrr::licensing::LicenseManager> licence)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      license (licence != nullptr ? std::move (licence) : scrr::licensing::LicenseManager::shared()),
      apvts (*this, nullptr, "PARAMS", scrr::params::createParameterLayout())
{
    for (size_t i = 0; i < macroParameters.size(); ++i) macroParameters[i] = apvts.getRawParameterValue (scrr::params::id::macros[i]);
    for (const auto& name : chainNames) rackState.appendChild (juce::ValueTree (name), nullptr);
    publishRack (true);
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (ranged->paramID, this);
    recordUndoState (false);
}

SpectralCrrptProcessor::~SpectralCrrptProcessor()
{
    // Stop the last update worker while JUCE and its platform classes still live.
    updateService.reset();
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (ranged->paramID, this);
}

std::shared_ptr<scrr::updates::UpdateService> SpectralCrrptProcessor::getUpdateService()
{
    const juce::ScopedLock lock (stateLock);
    if (! updateService) updateService = scrr::updates::UpdateService::shared();
    return updateService;
}

void SpectralCrrptProcessor::parameterChanged (const juce::String&, float)
{
    if (! restoring.load()) { presetDirty.store (true); pendingUndoEdit.store (true); }
}

void SpectralCrrptProcessor::publishRack (bool structural)
{
    auto next = std::make_shared<RackSnapshot>();
    const auto previous = std::atomic_load (&publishedRack);
    for (int i = 0; i < 5; ++i)
    {
        next->chains[(size_t) i] = juce::ValueTree (chainNames[i]);
        for (auto module : rackState.getChildWithName (chainNames[i]))
            if (module["type"].toString() != "Notes")
            {
                next->chains[(size_t) i].appendChild (module.createCopy(), nullptr);
                if (module["type"].toString() == "FrequencyRemap" && (bool) module.getProperty ("enabled", true)
                    && (int) module.getProperty ("remapMode", 0) == 1)
                {
                    scrr::dsp::SpectralProcessor::FormulaProgramEntry entry;
                    entry.uid = module["uid"].toString(); entry.definition = module["remapFormula"].toString();
                    if (previous) for (const auto& old : previous->formulaPrograms)
                        if (old.uid == entry.uid && old.definition == entry.definition) { entry.program = old.program; break; }
                    if (! entry.program)
                    {
                        juce::String error;
                        const auto formula = scrr::params::RemapFormula::decode (entry.definition, &error);
                        auto program = std::make_shared<scrr::params::FormulaProgram> (scrr::params::FormulaProgram::compile (formula));
                        if (error.isNotEmpty()) { program->valid = false; program->error = error; }
                        entry.program = std::move (program);
                        // Moving canvas blocks or renaming a parameter does not
                        // invalidate an identical audio program/function table.
                        if (previous) for (const auto& old : previous->formulaPrograms)
                            if (old.uid == entry.uid && old.program && old.program->sameEvaluationAs (*entry.program))
                            { entry.program = old.program; break; }
                    }
                    next->formulaPrograms.push_back (std::move (entry));
                }
            }
    }
    for (auto& mapping : macroMappings) if (mapping.routeUid.isEmpty()) mapping.routeUid = juce::Uuid().toString();
    // Removing a point/route also removes only its coordinate bindings. Repeat
    // once owners have been removed; no vector indices identify user targets.
    for (;;)
    {
        std::vector<scrr::params::MacroMapping> valid; valid.reserve (macroMappings.size());
        for (auto mapping : macroMappings) if (describeMacroTarget (mapping)) valid.push_back (std::move (mapping));
        const bool complete = valid.size() == macroMappings.size(); macroMappings = std::move (valid);
        if (complete) break;
    }
    next->mappings = macroMappings; next->sources = modulators;
    std::atomic_store (&publishedRack, std::shared_ptr<const RackSnapshot> (std::move (next)));
    if (structural) structureRevision.fetch_add (1);
}

void SpectralCrrptProcessor::recordUndoState (bool beginsEdit)
{
    if (applyingUndoState) return;
    auto state = copyPresetState();
    if (undoHistory.empty())
    {
        undoHistory.push_back (state);
        undoPosition = 0;
    }
    else if (! undoHistory[undoPosition].isEquivalentTo (state))
    {
        undoHistory.resize (undoPosition + 1);
        undoHistory.push_back (state);
        if (undoHistory.size() > 101) undoHistory.erase (undoHistory.begin());
        undoPosition = undoHistory.size() - 1;
    }
    pendingUndoEdit.store (beginsEdit);
}

bool SpectralCrrptProcessor::undo()
{
    const juce::ScopedLock lock (stateLock);
    recordUndoState (false);
    if (undoPosition == 0) return false;
    --undoPosition;
    applyingUndoState = true;
    const bool restored = applyPreset (undoHistory[undoPosition]);
    applyingUndoState = false;
    return restored;
}

bool SpectralCrrptProcessor::redo()
{
    const juce::ScopedLock lock (stateLock);
    recordUndoState (false);
    if (undoPosition + 1 >= undoHistory.size()) return false;
    ++undoPosition;
    applyingUndoState = true;
    const bool restored = applyPreset (undoHistory[undoPosition]);
    applyingUndoState = false;
    return restored;
}

void SpectralCrrptProcessor::consumeRack()
{
    auto next = std::atomic_load (&publishedRack);
    if (next == appliedRack) return;
    for (int i = 0; i < 5; ++i)
        spectralProcessor.updateModuleChain (i, next->chains[(size_t) i]);
    spectralProcessor.setFormulaPrograms (next->formulaPrograms);
    spectralProcessor.setMacroMappings (next->mappings);
    modulationEngine.configure (next->sources, next->mappings);
    appliedRack = std::move (next);
}

juce::ValueTree SpectralCrrptProcessor::getModuleChain (int channel) const
{
    const juce::ScopedLock lock (stateLock);
    return rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]).createCopy();
}

void SpectralCrrptProcessor::addModule (int channel, const juce::String& type)
{
    const juce::ScopedLock lock (stateLock);
    auto chain = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]);
    auto module = cleanModule (scrr::dsp::createDefaultModuleState (type));
    if (! module.isValid() || chain.getNumChildren() >= maxModulesPerChain) return;
    recordUndoState();
    module.setProperty ("enabled", true, nullptr);
    chain.addChild (module, stageInsertion (chain, module, -1), nullptr);
    presetDirty.store (true);
    publishRack (true);
}

bool SpectralCrrptProcessor::randomizePreset (int effectCount)
{ return randomizePreset (effectCount, juce::Random::getSystemRandom().nextInt64()); }

bool SpectralCrrptProcessor::randomizePreset (int effectCount, juce::int64 seed)
{
    const juce::ScopedLock lock (stateLock);
    std::array<int, 4> capacity { maxModulesPerChain, maxModulesPerChain, maxModulesPerChain, maxModulesPerChain };
    for (int ch = 1; ch <= 4; ++ch)
        for (auto module : rackState.getChildWithName (chainNames[ch]))
            if (module["type"].toString() == "Notes") --capacity[(size_t) (ch - 1)];
    const int slices = juce::roundToInt (apvts.getRawParameterValue (scrr::params::id::freqSplitNumSplits)->load());
    const double rate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    auto generated = scrr::RandomPreset::generate (effectCount, seed, slices, juce::jmin ((double) getFrequencyLimit(), rate * .5), capacity);
    if (generated.effectCount == 0) return false;
    recordUndoState();
    for (int ch = 1; ch <= 4; ++ch)
    {
        auto chain = rackState.getChildWithName (chainNames[ch]);
        for (int i = chain.getNumChildren(); --i >= 0;)
            if (chain.getChild (i)["type"].toString() != "Notes") chain.removeChild (i, nullptr);
        // Copy because appending detaches a child from the generated chain.
        const auto& generatedChain = generated.chains[(size_t) (ch - 1)];
        for (auto module : generatedChain)
        {
            auto next = cleanModule (module);
            chain.addChild (next, stageInsertion (chain, next, -1), nullptr);
        }
    }
    presetDirty.store (true);
    // Validation removes old effect bindings and bindings to those routes, while
    // preserving global/modulator routes and all source/base values.
    publishRack (true);
    return true;
}

void SpectralCrrptProcessor::removeModule (int channel, int index)
{
    const juce::ScopedLock lock (stateLock);
    auto chain = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]);
    if (index < 0 || index >= chain.getNumChildren()) return;
    recordUndoState();
    const auto removedUid = chain.getChild (index)["uid"].toString();
    macroMappings.erase (std::remove_if (macroMappings.begin(), macroMappings.end(), [&] (const auto& m)
        { return m.channel == channel && m.uid == removedUid; }), macroMappings.end());
    chain.removeChild (index, nullptr);
    presetDirty.store (true);
    publishRack (true);
}

void SpectralCrrptProcessor::moveModule (int channel, int from, int to)
{
    const juce::ScopedLock lock (stateLock);
    auto chain = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]);
    if (from < 0 || from >= chain.getNumChildren()) return;
    transferModule (channel, chain.getChild (from)["uid"].toString(), channel, to > from ? to + 1 : to, false);
}
juce::String SpectralCrrptProcessor::transferModule (int source, const juce::String& uid, int destination, int insertion, bool duplicate)
{
    if (source < 1 || source > 4 || destination < 1 || destination > 4) return {};
    const juce::ScopedLock lock (stateLock);
    auto from = rackState.getChildWithName (chainNames[source]), to = rackState.getChildWithName (chainNames[destination]);
    const auto original = from.getChildWithProperty ("uid", uid);
    if (! original.isValid() || ((source != destination || duplicate) && to.getNumChildren() >= maxModulesPerChain)) return {};
    std::vector<scrr::params::MacroMapping> copied;
    if (duplicate)
    {
        for (auto m : macroMappings) if (m.channel == source && m.uid == uid) copied.push_back (m);
        const auto owners = copied;
        for (const auto& m : macroMappings) if (m.channel == scrr::params::MacroMapping::responseChannel)
            for (const auto& owner : owners) if (m.uid == owner.routeUid) { copied.push_back (m); break; }
        if (copied.size() + macroMappings.size() > 256) return {};
    }
    recordUndoState();
    auto module = duplicate ? original.createCopy() : original;
    const auto nextUid = duplicate ? juce::Uuid().toString() : uid;
    if (duplicate) module.setProperty ("uid", nextUid, nullptr);
    else
    {
        const int oldIndex = from.indexOf (module);
        if (source == destination && insertion > oldIndex) --insertion;
        from.removeChild (module, nullptr);
    }
    to.addChild (module, stageInsertion (to, module, insertion), nullptr);
    if (duplicate)
    {
        std::vector<std::pair<juce::String, juce::String>> routeIds;
        for (auto& m : copied)
        {
            const auto fresh = juce::Uuid().toString(); routeIds.emplace_back (m.routeUid, fresh); m.routeUid = fresh;
            if (m.channel != scrr::params::MacroMapping::responseChannel) { m.channel = destination; m.uid = nextUid; }
        }
        for (auto& m : copied)
        {
            if (m.channel == scrr::params::MacroMapping::responseChannel)
                for (const auto& pair : routeIds) if (m.uid == pair.first) { m.uid = pair.second; break; }
            macroMappings.push_back (std::move (m));
        }
    }
    else
        for (auto& m : macroMappings) if (m.channel == source && m.uid == uid) m.channel = destination;
    presetDirty.store (true); publishRack (true); return nextUid;
}
juce::String SpectralCrrptProcessor::copyModule (int channel, const juce::String& uid) const
{
    if (channel < 1 || channel > 4) return {};
    const juce::ScopedLock lock (stateLock);
    auto module = rackState.getChildWithName (chainNames[channel]).getChildWithProperty ("uid", uid);
    if (! module.isValid()) return {};
    juce::ValueTree copy ("SPECTRAL_EFFECT"); copy.setProperty ("version", 1, nullptr); copy.appendChild (module.createCopy(), nullptr);
    juce::ValueTree sources ("modulators");
    std::vector<juce::String> routeIds;
    for (const auto& m : macroMappings) if (m.channel == channel && m.uid == uid) routeIds.push_back (m.routeUid);
    for (const auto& m : macroMappings)
    {
        const bool direct = m.channel == channel && m.uid == uid;
        const bool response = m.channel == scrr::params::MacroMapping::responseChannel && std::find (routeIds.begin(), routeIds.end(), m.uid) != routeIds.end();
        if (! direct && ! response) continue;
        copy.appendChild (m.state(), nullptr);
        if (m.sourceUid.isNotEmpty() && ! sources.getChildWithProperty ("uid", m.sourceUid).isValid())
            for (const auto& source : modulators) if (source.uid == m.sourceUid) sources.appendChild (source.state(), nullptr);
    }
    copy.appendChild (sources, nullptr);
    return copy.toXmlString();
}
juce::String SpectralCrrptProcessor::pasteModule (int channel, const juce::String& clipboard)
{
    if (channel < 1 || channel > 4 || clipboard.length() > 1024 * 1024) return {};
    auto xml = juce::parseXML (clipboard); if (! xml) return {};
    auto copy = juce::ValueTree::fromXml (*xml);
    if (! copy.hasType ("SPECTRAL_EFFECT") || (int) copy["version"] != 1) return {};
    const juce::ScopedLock lock (stateLock);
    auto module = cleanModule (copy.getChildWithName ("module"));
    auto chain = rackState.getChildWithName (chainNames[channel]);
    if (! module.isValid() || chain.getNumChildren() >= maxModulesPerChain) return {};
    recordUndoState();
    const auto uid = module["uid"].toString();
    for (auto tree : copy.getChildWithName ("modulators"))
    {
        if (! tree.hasType ("SOURCE") || modulators.size() >= (size_t) scrr::params::maxModulators) continue;
        auto source = scrr::params::ModulationSource::read (tree);
        if (source.uid.isNotEmpty() && std::none_of (modulators.begin(), modulators.end(), [&] (const auto& m) { return m.uid == source.uid; }))
        { modulators.push_back (std::move (source)); modulatorRevision.fetch_add (1); }
    }
    chain.addChild (module, stageInsertion (chain, module, -1), nullptr);
    std::vector<std::pair<juce::String, juce::String>> routeIds;
    for (int pass = 0; pass < 2; ++pass)
      for (auto tree : copy) if (tree.hasType ("MAP") && macroMappings.size() < 256)
    {
        const bool response = (int) tree["channel"] == scrr::params::MacroMapping::responseChannel;
        if (response != (pass == 1)) continue;
        if (response && ! scrr::params::MacroMapping::hasUniqueSavedRouteId (copy, tree["uid"].toString())) continue;
        const auto parameter = tree["parameter"].toString();
        if (parameter.isEmpty() || parameter.length() > 128) continue;
        scrr::params::MacroMapping m; m.channel = response ? scrr::params::MacroMapping::responseChannel : channel;
        m.uid = uid; m.parameter = juce::Identifier (parameter);
        if (response)
        {
            m.uid.clear();
            for (const auto& pair : routeIds) if (pair.first == tree["uid"].toString()) { m.uid = pair.second; break; }
            if (m.uid.isEmpty()) continue;
        }
        m.routeUid = juce::Uuid().toString();
        m.sourceUid = tree["source"].toString();
        m.macro = (int) tree.getProperty ("macro", -1); m.mode = juce::jlimit (0, 3, (int) tree["mode"]);
        if (! describeMacroTarget (m)) continue;
        const double a = (double) tree["startOffset"], b = (double) tree["endOffset"];
        m.startOffset = std::isfinite (a) ? juce::jlimit (-1.0, 1.0, a) : 0;
        m.endOffset = std::isfinite (b) ? juce::jlimit (-1.0, 1.0, b) : 0;
        m.readResponseCurve (tree);
        m.centre = macroBase (m); m.syncRange();
        if (std::none_of (macroMappings.begin(), macroMappings.end(), [&] (const auto& existing) { return existing.sameTarget (m) && existing.sameSource (m); }))
        { routeIds.emplace_back (tree["routeUid"].toString(), m.routeUid); macroMappings.push_back (std::move (m)); }
    }
    presetDirty.store (true); publishRack (true); return uid;
}

void SpectralCrrptProcessor::setModuleParameter (int channel, const juce::String& uid,
                                               const juce::Identifier& key, const juce::var& value)
{
    const juce::ScopedLock lock (stateLock);
    auto chain = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]);
    for (auto module : chain)
        if (module["uid"].toString() == uid)
        {
            if (key == juce::Identifier ("type") || key == juce::Identifier ("uid")) return;
            uint32_t pointId {}; bool xAxis {};
            if (module["type"].toString() == "FrequencyRemap" && scrr::params::parseCurveCoordinateParameter (key, pointId, xAxis))
            {
                auto curve = scrr::params::TransferCurve::decode (module["remapCurve"].toString());
                if (! curve.setCoordinate (pointId, xAxis, (double) value)) return;
                const auto encoded = curve.encode(); if (encoded == module["remapCurve"].toString()) return;
                recordUndoState(); module.setProperty ("remapCurve", encoded, nullptr); presetDirty.store (true); publishRack (false); return;
            }
            auto candidate = module.createCopy();
            candidate.setProperty (key, value, nullptr);
            auto clean = cleanModule (candidate);
            if (! clean.hasProperty (key)) return;
            if (module[key] == clean[key]) return;
            recordUndoState();
            if (module["type"].toString() == "FrequencyRemap" && key == juce::Identifier ("remapFormula"))
            {
                // Graph, parameter definitions, removed values and range clamps
                // form one undo transaction; publication revalidates its routes.
                syncFormulaParameters (module, clean);
                module.setProperty (key, clean[key], nullptr);
                presetDirty.store (true); publishRack (true); return;
            }
            module.setProperty (key, clean[key], nullptr);
            presetDirty.store (true);
            if (key == juce::Identifier ("formulaView")) return;
            if (module["type"].toString() != "Notes" || key != juce::Identifier ("notes")) publishRack (false);
            return;
        }
}

void SpectralCrrptProcessor::setRemapFormulaState (int channel, const juce::String& uid,
                                                 const juce::String& formula, const juce::String& view)
{
    const juce::ScopedLock lock (stateLock);
    auto module = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]).getChildWithProperty ("uid", uid);
    if (module["type"].toString() != "FrequencyRemap") return;
    auto candidate = module.createCopy();
    candidate.setProperty ("remapFormula", formula, nullptr); candidate.setProperty ("formulaView", view, nullptr);
    const auto clean = cleanModule (candidate);
    if (module["remapFormula"] == clean["remapFormula"] && module["formulaView"] == clean["formulaView"]) return;
    recordUndoState(); syncFormulaParameters (module, clean);
    module.setProperty ("remapFormula", clean["remapFormula"], nullptr); module.setProperty ("formulaView", clean["formulaView"], nullptr);
    presetDirty.store (true); publishRack (true);
}

juce::ValueTree SpectralCrrptProcessor::copyPresetState()
{
    const juce::ScopedLock lock (stateLock);
    auto result = apvts.copyState();
    result.removeChild (result.getChildWithName ("presetRecovery"), nullptr);
    for (const auto& name : chainNames)
    {
        result.removeChild (result.getChildWithName (name), nullptr);
        result.appendChild (rackState.getChildWithName (name).createCopy(), nullptr);
    }
    result.removeChild (result.getChildWithName ("macroNames"), nullptr);
    juce::ValueTree names ("macroNames");
    for (int i = 0; i < 8; ++i)
    {
        juce::ValueTree name ("NAME"); name.setProperty ("index", i, nullptr);
        name.setProperty ("text", macroNames[(size_t) i], nullptr); names.appendChild (name, nullptr);
    }
    result.appendChild (names, nullptr);
    result.removeChild (result.getChildWithName ("macroMappings"), nullptr);
    juce::ValueTree mappings ("macroMappings");
    // Bases and ranges may change independently of their routes (including
    // host automation and custom Formula ranges). Save the same canonical
    // values restoration produces, so Undo does not invalidate its own Redo.
    for (const auto& m : getMacroMappings()) mappings.appendChild (m.state(), nullptr);
    result.appendChild (mappings, nullptr);
    result.removeChild (result.getChildWithName ("modulators"), nullptr);
    juce::ValueTree sources ("modulators");
    for (const auto& source : modulators) sources.appendChild (source.state(), nullptr);
    result.appendChild (sources, nullptr);
    return result;
}

bool SpectralCrrptProcessor::switchPreset (const juce::ValueTree& preset, const juce::String& name, bool keepRecovery)
{
    const juce::ScopedLock lock (stateLock);
    juce::ValueTree previous;
    if (keepRecovery)
    {
        previous = copyPresetState();
        previous.setProperty ("presetName", presetName, nullptr);
        previous.setProperty ("presetDirty", presetDirty.load(), nullptr);
    }
    if (! applyPreset (preset)) return false;
    if (keepRecovery) presetRecovery = previous;
    presetName = name;
    presetDirty.store (false);
    return true;
}

bool SpectralCrrptProcessor::hasPresetRecovery() const
{
    const juce::ScopedLock lock (stateLock);
    return presetRecovery.isValid();
}

juce::String SpectralCrrptProcessor::getPresetRecoveryName() const
{
    const juce::ScopedLock lock (stateLock);
    return presetRecovery.getProperty ("presetName", "Init").toString();
}

bool SpectralCrrptProcessor::restorePresetRecovery()
{
    const juce::ScopedLock lock (stateLock);
    if (! presetRecovery.isValid() || ! applyPreset (presetRecovery)) return false;
    presetName = presetRecovery.getProperty ("presetName", "Init").toString();
    presetDirty.store ((bool) presetRecovery.getProperty ("presetDirty", false));
    return true;
}

bool SpectralCrrptProcessor::applyPreset (const juce::ValueTree& preset)
{
    if (! preset.isValid() || (! preset.hasType ("PARAMS") && ! preset.getChildWithName ("modules").isValid()))
        return false;
    const juce::ScopedLock lock (stateLock);
    recordUndoState();
    auto nextRack = juce::ValueTree ("RACK");
    std::map<juce::String, juce::String> restoredIds;
    for (const auto& name : chainNames)
    {
        auto dest = juce::ValueTree (name);
        auto source = preset.getChildWithName (name);
        if (! applyingUndoState && name == "ch1" && source.getNumChildren() == 0)
            source = preset.getChildWithName ("modules");
        if (applyingUndoState || name != "modules")
            for (auto child : source)
            {
                auto module = cleanModule (child);
                if (module.isValid() && dest.getNumChildren() < maxModulesPerChain)
                {
                    // History is our own validated state. Retain identities so
                    // restored mappings and subsequent redo still match it.
                    if (applyingUndoState) module.setProperty ("uid", child["uid"], nullptr);
                    const auto key = (name == "modules" ? juce::String ("0") : name.substring (2)) + ":" + child["uid"].toString();
                    restoredIds.emplace (key, module["uid"].toString());
                    dest.appendChild (module, nullptr);
                }
            }
        orderStages (dest); nextRack.appendChild (dest, nullptr);
    }
    restoring.store (true);
    if (preset.hasType ("PARAMS"))
    {
        auto globals = preset.createCopy();
        for (const auto& name : chainNames) globals.removeChild (globals.getChildWithName (name), nullptr);
        globals.removeProperty ("presetName", nullptr);
        globals.removeProperty ("presetDirty", nullptr);
        globals.removeChild (globals.getChildWithName ("presetRecovery"), nullptr);
        globals.removeChild (globals.getChildWithName ("macroNames"), nullptr);
        globals.removeChild (globals.getChildWithName ("macroMappings"), nullptr);
        globals.removeChild (globals.getChildWithName ("modulators"), nullptr);
        for (const auto& macroId : scrr::params::id::macros)
            if (! globals.getChildWithProperty ("id", macroId).isValid())
            {
                juce::ValueTree parameter ("PARAM"); parameter.setProperty ("id", macroId, nullptr);
                parameter.setProperty ("value", 0.0, nullptr); globals.appendChild (parameter, nullptr);
            }
        auto defaultParameter = [&] (const juce::String& id, double value)
        {
            if (globals.getChildWithProperty ("id", id).isValid()) return;
            juce::ValueTree parameter ("PARAM"); parameter.setProperty ("id", id, nullptr);
            parameter.setProperty ("value", value, nullptr); globals.appendChild (parameter, nullptr);
        };
        defaultParameter (scrr::params::id::fftSizeExtended, 0);
        defaultParameter (scrr::params::id::frequencyLimit, 2);
        defaultParameter (scrr::params::id::windowTypeExtended, 0);
        defaultParameter (scrr::params::id::overlapFactor, 4);
        for (const auto& id : scrr::params::id::channelEnabled) defaultParameter (id, 1);
        apvts.replaceState (globals);
    }
    else
    {
        for (const auto& macroId : scrr::params::id::macros)
            apvts.getParameter (macroId)->setValueNotifyingHost (0.0f);
        auto* limit = apvts.getParameter (scrr::params::id::frequencyLimit);
        limit->setValueNotifyingHost (limit->getDefaultValue());
        for (const auto& id : { scrr::params::id::windowTypeExtended, scrr::params::id::overlapFactor })
            apvts.getParameter (id)->setValueNotifyingHost (apvts.getParameter (id)->getDefaultValue());
    }
    macroNames.fill ({});
    for (auto name : preset.getChildWithName ("macroNames"))
        if (name.hasType ("NAME")) setMacroName ((int) name.getProperty ("index", -1), name["text"].toString());
    macroNameRevision.fetch_add (1);
    modulators.clear();
    for (auto tree : preset.getChildWithName ("modulators"))
    {
        if (! tree.hasType ("SOURCE") || modulators.size() >= (size_t) scrr::params::maxModulators) continue;
        auto source = scrr::params::ModulationSource::read (tree);
        if (source.uid.isEmpty()) source.uid = juce::Uuid().toString();
        if (std::none_of (modulators.begin(), modulators.end(), [&] (const auto& m) { return m.uid == source.uid; })) modulators.push_back (std::move (source));
    }
    modulatorRevision.fetch_add (1);
    rackState = nextRack;
    restoreMacroMappings (preset.getChildWithName ("macroMappings"), restoredIds);
    // Replace retired per-effect seed/rate random switches with editable sources.
    for (int ch = 1; ch <= 4; ++ch)
    {
        auto oldChain = preset.getChildWithName (chainNames[ch]);
        if (ch == 1 && oldChain.getNumChildren() == 0) oldChain = preset.getChildWithName ("modules");
        for (auto oldModule : oldChain)
        {
            if (modulators.size() >= (size_t) scrr::params::maxModulators || macroMappings.size() >= 256) break;
            const auto found = restoredIds.find (juce::String (ch) + ":" + oldModule["uid"].toString());
            if (found == restoredIds.end()) continue;
            scrr::params::ModulationSource source; source.random = true; source.uid = juce::Uuid().toString();
            source.name = "RND / " + oldModule["type"].toString();
            const float oldRate = (float) oldModule.getProperty ("seedRate", 16.0);
            source.rate = std::isfinite (oldRate) ? juce::jlimit (.01f, 128.0f, oldRate) : 16;
            source.seed = juce::jlimit (0, 999999, (int) oldModule.getProperty ("seed", 0));
            bool added = false;
            for (const auto* parameter : { "seed", "rate" })
            {
                if (! (bool) oldModule.getProperty (juce::String (parameter) == "seed" ? "randomSeed" : "randomRate", false) || macroMappings.size() >= 256) continue;
                scrr::params::MacroMapping mapping; mapping.channel = ch; mapping.uid = found->second; mapping.parameter = juce::Identifier (parameter);
                if (! describeMacroTarget (mapping)) continue;
                if (! added) { modulators.push_back (source); added = true; }
                mapping.sourceUid = source.uid; describeMacroTarget (mapping); mapping.centre = macroBase (mapping);
                mapping.startOffset = -mapping.normalise (mapping.centre); mapping.endOffset = 1 + mapping.startOffset; mapping.syncRange();
                macroMappings.push_back (std::move (mapping));
            }
        }
    }
    publishRack (true);
    restoring.store (false);
    return true;
}

juce::String SpectralCrrptProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock lock (stateLock);
    return presetName;
}

void SpectralCrrptProcessor::setPresetName (const juce::String& name)
{
    const juce::ScopedLock lock (stateLock);
    presetName = name;
}

void SpectralCrrptProcessor::prepareToPlay (double sr, int blockSize)
{
    preparedBlockSize = juce::jmax (1, blockSize);
    bypassHistory.setSize (getMainBusNumOutputChannels(), 32768 + 4096); bypassHistory.clear(); bypassWrite = 0;
    spectralProcessor.prepare ({ sr, (juce::uint32) juce::jmax (1, blockSize),
                                (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) });
    appliedRack.reset();
    consumeRack();
    modulationEngine.prepare (sr);
    spectralProcessor.applyMacroValues (getModulationValues());
    spectralProcessor.updateGlobalParameters (apvts);
    spectralProcessor.applyPendingReconfigure();
    setLatencySamples (spectralProcessor.getLatencySamples());
    displayLatency.store (spectralProcessor.getLatencySamples());
}

bool SpectralCrrptProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in = layouts.getMainInputChannelSet();
    if (in != layouts.getMainOutputChannelSet() || (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.inputBuses[1];
        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::mono() && sidechain != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void SpectralCrrptProcessor::renderUnlicensed (juce::AudioBuffer<float>& buffer) noexcept
{
    auto main = getBusBuffer (buffer, false, 0);
    main.clear();
    for (int ch = 0; ch < juce::jmin (2, main.getNumChannels()); ++ch)
        for (int i = 0; i < main.getNumSamples(); ++i)
            main.setSample (ch, i, noise[(size_t) ch].next());
}

scrr::params::HostBeat SpectralCrrptProcessor::captureHostBeat() noexcept
{
    scrr::params::HostBeat beat; beat.bpm = lastHostBpm;
    if (auto* playHead = getPlayHead()) if (const auto position = playHead->getPosition())
    {
        const double bpm = position->getBpm().orFallback (lastHostBpm);
        if (std::isfinite (bpm) && bpm > 0) lastHostBpm = beat.bpm = bpm;
        beat.ppq = position->getPpqPosition().orFallback (0);
        beat.playing = position->getIsPlaying();
        beat.hasTimeline = position->getPpqPosition().hasValue() && std::isfinite (beat.ppq);
    }
    hostBeat.publish (beat); return beat;
}

void SpectralCrrptProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    captureHostBeat();
    if (! license->isActivated()) { renderUnlicensed (buffer); return; }
    juce::ignoreUnused (midi); hostBypassDelay (buffer, true);
}

void SpectralCrrptProcessor::hostBypassDelay (juce::AudioBuffer<float>& buffer, bool replace) noexcept
{
    const int capacity = bypassHistory.getNumSamples();
    if (capacity == 0) { if (replace) getBusBuffer (buffer, false, 0).clear(); return; }
    const int delay = juce::jlimit (0, capacity - 1, getLatencySamples());
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int read = (bypassWrite + capacity - delay) % capacity;
        for (int c = 0; c < getMainBusNumOutputChannels(); ++c)
        {
            const float input = buffer.getSample (c, i);
            bypassHistory.setSample (c, bypassWrite, std::isfinite (input) ? input : 0);
            if (replace) buffer.setSample (c, i, bypassHistory.getSample (c, read));
        }
        bypassWrite = (bypassWrite + 1) % capacity;
    }
}

void SpectralCrrptProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto beat = captureHostBeat();
    if (! license->isActivated()) { renderUnlicensed (buffer); return; }
    if (buffer.getNumSamples() == 0) return;
    hostBypassDelay (buffer, false);
    consumeRack();
    spectralProcessor.applyMacroValues (getModulationValues());
    spectralProcessor.updateGlobalParameters (apvts);
    spectralProcessor.applyPendingReconfigure();
    const double bpm = beat.bpm, ppq = beat.ppq;
    spectralProcessor.setTempo (bpm);
    const bool playing = beat.playing, timeline = beat.hasTimeline;
    // Bound modulation granularity independently of oversized offline host blocks.
    const int quantum = juce::jmin (preparedBlockSize, 64);
    auto mainInput = getBusBuffer (buffer, true, 0);
    auto sidechain = getBusBuffer (buffer, true, 1);
    for (int offset = 0; offset < buffer.getNumSamples(); offset += quantum)
    {
        const int count = juce::jmin (quantum, buffer.getNumSamples() - offset);
        modulationEngine.process (mainInput, offset, count, spectralProcessor.getPreBandLevels(), bpm, playing,
                                  ppq + (double) offset * bpm / (60 * getSampleRate()), timeline, &sidechain, getMacroValues());
        spectralProcessor.applyMacroValues (getModulationValues());
        spectralProcessor.updateGlobalParameters (apvts);
        float* channels[2] {};
        const int numChannels = getMainBusNumOutputChannels();
        for (int c = 0; c < numChannels; ++c) channels[c] = buffer.getWritePointer (c, offset);
        juce::AudioBuffer<float> block (channels, numChannels, count);
        spectralProcessor.process (block);
    }
    const int latency = spectralProcessor.getLatencySamples();
    if (latency != getLatencySamples()) setLatencySamples (latency);
    displayLatency.store (latency);
}

juce::AudioProcessorEditor* SpectralCrrptProcessor::createEditor() { return new SpectralCrrptEditor (*this); }

void SpectralCrrptProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const juce::ScopedLock lock (stateLock);
    auto state = copyPresetState();
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetDirty", presetDirty.load(), nullptr);
    if (presetRecovery.isValid())
    {
        juce::ValueTree recovery ("presetRecovery");
        recovery.appendChild (presetRecovery.createCopy(), nullptr);
        state.appendChild (recovery, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SpectralCrrptProcessor::setStateInformation (const void* data, int bytes)
{
    if (data == nullptr || bytes <= 0 || bytes > 16 * 1024 * 1024) return;
    if (auto xml = getXmlFromBinary (data, bytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        const juce::ScopedLock lock (stateLock);
        if (applyPreset (state))
        {
            presetName = state.getProperty ("presetName", "Init").toString();
            presetDirty.store ((bool) state.getProperty ("presetDirty", false));
            auto recovery = state.getChildWithName ("presetRecovery").getChild (0);
            presetRecovery = recovery.hasType ("PARAMS") ? recovery.createCopy() : juce::ValueTree();
            if (presetRecovery.isValid())
                presetRecovery.removeChild (presetRecovery.getChildWithName ("presetRecovery"), nullptr);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralCrrptProcessor(); }

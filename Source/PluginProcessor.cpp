// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "modules/ModuleSpecs.h"
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
    if (type == "Notes") result.setProperty ("notes", source["notes"].toString().substring (0, 65536), nullptr);
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
}

SpectralCrrptProcessor::~SpectralCrrptProcessor()
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (ranged->paramID, this);
}

void SpectralCrrptProcessor::parameterChanged (const juce::String&, float)
{
    if (! restoring.load()) presetDirty.store (true);
}

void SpectralCrrptProcessor::publishRack (bool structural)
{
    auto next = std::make_shared<RackSnapshot>();
    for (int i = 0; i < 5; ++i)
    {
        next->chains[(size_t) i] = juce::ValueTree (chainNames[i]);
        for (auto module : rackState.getChildWithName (chainNames[i]))
            if (module["type"].toString() != "Notes") next->chains[(size_t) i].appendChild (module.createCopy(), nullptr);
    }
    for (auto& mapping : macroMappings) describeMacroTarget (mapping);
    next->mappings = macroMappings; next->sources = modulators;
    std::atomic_store (&publishedRack, std::shared_ptr<const RackSnapshot> (std::move (next)));
    if (structural) structureRevision.fetch_add (1);
}

void SpectralCrrptProcessor::consumeRack()
{
    auto next = std::atomic_load (&publishedRack);
    if (next == appliedRack) return;
    for (int i = 0; i < 5; ++i)
        spectralProcessor.updateModuleChain (i, next->chains[(size_t) i]);
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
    module.setProperty ("enabled", true, nullptr);
    chain.addChild (module, stageInsertion (chain, module, -1), nullptr);
    presetDirty.store (true);
    publishRack (true);
}

void SpectralCrrptProcessor::removeModule (int channel, int index)
{
    const juce::ScopedLock lock (stateLock);
    auto chain = rackState.getChildWithName (chainNames[juce::jlimit (0, 4, channel)]);
    if (index < 0 || index >= chain.getNumChildren()) return;
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
        if (copied.size() + macroMappings.size() > 256) return {};
    }
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
        for (auto m : copied) { m.channel = destination; m.uid = nextUid; macroMappings.push_back (std::move (m)); }
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
    for (const auto& m : macroMappings) if (m.channel == channel && m.uid == uid)
    {
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
    const auto uid = module["uid"].toString();
    for (auto tree : copy.getChildWithName ("modulators"))
    {
        if (! tree.hasType ("SOURCE") || modulators.size() >= (size_t) scrr::params::maxModulators) continue;
        auto source = scrr::params::ModulationSource::read (tree);
        if (source.uid.isNotEmpty() && std::none_of (modulators.begin(), modulators.end(), [&] (const auto& m) { return m.uid == source.uid; }))
        { modulators.push_back (std::move (source)); modulatorRevision.fetch_add (1); }
    }
    chain.addChild (module, stageInsertion (chain, module, -1), nullptr);
    for (auto tree : copy) if (tree.hasType ("MAP") && macroMappings.size() < 256)
    {
        const auto parameter = tree["parameter"].toString();
        if (parameter.isEmpty() || parameter.length() > 128) continue;
        scrr::params::MacroMapping m; m.channel = channel; m.uid = uid; m.parameter = juce::Identifier (parameter);
        m.sourceUid = tree["source"].toString();
        m.macro = (int) tree.getProperty ("macro", -1); m.mode = juce::jlimit (0, 3, (int) tree["mode"]);
        if (! describeMacroTarget (m)) continue;
        const double a = (double) tree["startOffset"], b = (double) tree["endOffset"];
        m.startOffset = std::isfinite (a) ? juce::jlimit (-1.0, 1.0, a) : 0;
        m.endOffset = std::isfinite (b) ? juce::jlimit (-1.0, 1.0, b) : 0;
        m.centre = (double) module[m.parameter]; m.syncRange();
        if (std::none_of (macroMappings.begin(), macroMappings.end(), [&] (const auto& existing) { return existing.sameTarget (m) && existing.sameSource (m); }))
            macroMappings.push_back (std::move (m));
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
            auto candidate = module.createCopy();
            candidate.setProperty (key, value, nullptr);
            auto clean = cleanModule (candidate);
            if (! clean.hasProperty (key)) return;
            module.setProperty (key, clean[key], nullptr);
            presetDirty.store (true);
            if (module["type"].toString() != "Notes" || key != juce::Identifier ("notes")) publishRack (false);
            return;
        }
}

juce::ValueTree SpectralCrrptProcessor::copyPresetState()
{
    const juce::ScopedLock lock (stateLock);
    auto result = apvts.copyState();
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
    for (const auto& m : macroMappings) mappings.appendChild (m.state(), nullptr);
    result.appendChild (mappings, nullptr);
    result.removeChild (result.getChildWithName ("modulators"), nullptr);
    juce::ValueTree sources ("modulators");
    for (const auto& source : modulators) sources.appendChild (source.state(), nullptr);
    result.appendChild (sources, nullptr);
    return result;
}

bool SpectralCrrptProcessor::applyPreset (const juce::ValueTree& preset)
{
    if (! preset.isValid() || (! preset.hasType ("PARAMS") && ! preset.getChildWithName ("modules").isValid()))
        return false;
    const juce::ScopedLock lock (stateLock);
    auto nextRack = juce::ValueTree ("RACK");
    std::map<juce::String, juce::String> restoredIds;
    for (const auto& name : chainNames)
    {
        auto dest = juce::ValueTree (name);
        auto source = preset.getChildWithName (name);
        if (name == "ch1" && source.getNumChildren() == 0)
            source = preset.getChildWithName ("modules");
        if (name != "modules")
            for (auto child : source)
            {
                auto module = cleanModule (child);
                if (module.isValid() && dest.getNumChildren() < maxModulesPerChain)
                {
                    const auto key = name.substring (2) + ":" + child["uid"].toString();
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
        for (const auto& id : scrr::params::id::channelEnabled) defaultParameter (id, 1);
        apvts.replaceState (globals);
    }
    else
    {
        for (const auto& macroId : scrr::params::id::macros)
            apvts.getParameter (macroId)->setValueNotifyingHost (0.0f);
        auto* limit = apvts.getParameter (scrr::params::id::frequencyLimit);
        limit->setValueNotifyingHost (limit->getDefaultValue());
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
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralCrrptProcessor(); }

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "PluginProcessor.h"
#include "modules/ModuleSpecs.h"
#include <algorithm>

using scrr::params::MacroMapping;
namespace {
void boundRange (MacroMapping& m)
{
    const auto bound = [] (double v) { return std::isfinite (v) ? juce::jlimit (-1.0, 1.0, v) : 0.0; };
    m.startOffset = bound (m.startOffset); m.endOffset = bound (m.endOffset);
    m.centre = juce::jlimit (m.minimum, m.maximum, std::isfinite (m.centre) ? m.centre : m.minimum);
    m.mode = juce::jlimit (0, 3, m.mode); m.relative = true; m.syncRange();
}
}
juce::String SpectralCrrptProcessor::getMacroName (int index) const
{
    if (index < 0 || index >= 8) return {};
    const juce::ScopedLock lock (stateLock);
    const auto& name = macroNames[(size_t) index];
    return name.isEmpty() ? "M" + juce::String (index + 1) : name;
}
juce::String SpectralCrrptProcessor::getMacroLabel (int index) const
{
    const auto id = "M" + juce::String (index + 1), name = getMacroName (index);
    return name == id ? id : id + " / " + name;
}
void SpectralCrrptProcessor::setMacroName (int index, const juce::String& name)
{
    if (index < 0 || index >= 8) return;
    const juce::ScopedLock lock (stateLock);
    auto clean = name.replaceCharacters ("\r\n\t", "   ").trim().substring (0, 32);
    if (clean == "M" + juce::String (index + 1)) clean.clear();
    if (macroNames[(size_t) index] == clean) return;
    macroNames[(size_t) index] = clean; macroNameRevision.fetch_add (1); presetDirty.store (true);
}
scrr::params::MacroValues SpectralCrrptProcessor::getMacroValues() const noexcept
{
    scrr::params::MacroValues values {};
    for (size_t i = 0; i < values.size(); ++i) values[i] = macroParameters[i]->load() * .01f;
    return values;
}
bool SpectralCrrptProcessor::describeMacroTarget (MacroMapping& m) const
{
    if (m.channel < 0 || m.channel > MacroMapping::modulationChannel) return false;
    if (m.sourceUid.isEmpty()) { if (m.macro < 0 || m.macro >= 8) return false; m.sourceName = getMacroLabel (m.macro); }
    else
    {
        m.sourceIndex = -1;
        for (size_t i = 0; i < modulators.size(); ++i) if (modulators[i].uid == m.sourceUid)
        { m.sourceIndex = 8 + (int) i; m.sourceName = modulators[i].name; m.sourceEnabled = modulators[i].enabled; break; }
        if (m.sourceIndex < 0) return false;
    }
    if (m.channel == MacroMapping::modulationChannel)
    {
        // Source settings accept macros only, avoiding self-routing and cycles.
        if (m.sourceUid.isNotEmpty()) return false;
        for (const auto& source : modulators) if (source.uid == m.uid)
        {
            scrr::dsp::ParamSpec spec;
            const auto& key = m.parameter.toString();
            using scrr::dsp::makeFloat;
            if (! source.envelope && key == "rate") spec = makeFloat ("rate", "RATE", .01f, source.random ? 128.0f : 40.0f, 1, .01f, " Hz");
            else if (! source.envelope && ! source.random && key == "phase") spec = makeFloat ("phase", "PHASE", 0, 360, 0, 1, " deg");
            else if (source.random && key == "seed") spec = makeFloat ("seed", "SEED", 0, 999999, 1, 1, "");
            else if (source.random && key == "smooth") spec = makeFloat ("smooth", "SMOOTH", 0, 2000, 0, .1f, " ms");
            else if (source.envelope && key == "gain") spec = makeFloat ("gain", "GAIN", -24, 48, 0, .1f, " dB");
            else if (source.envelope && key == "attack") spec = makeFloat ("attack", "RISE", .1f, 2000, 10, .1f, " ms");
            else if (source.envelope && key == "release") spec = makeFloat ("release", "FALL", 1, 5000, 150, .1f, " ms");
            else return false;
            m.minimum = spec.minVal; m.maximum = spec.maxVal; m.step = spec.step; m.logarithmic = false;
            m.label = "MODULATION / " + source.name + " / " + spec.label; m.unit = spec.unit; return true;
        }
        return false;
    }
    if (m.channel == 0)
    {
        using namespace scrr::params;
        const auto paramId = m.parameter.toString();
        if (paramId != id::drywet && paramId != id::inputGain && paramId != id::outputGain
            && paramId != id::freqSplitNumSplits && paramId != id::freqSplitCrossfade) return false;
        auto* p = apvts.getParameter (paramId); if (p == nullptr) return false;
        m.uid.clear(); m.minimum = p->getNormalisableRange().start; m.maximum = p->getNormalisableRange().end;
        m.step = p->getNormalisableRange().interval; m.logarithmic = false;
        m.label = "MASTER / " + p->getName (64); m.unit = p->getLabel(); return true;
    }
    for (auto child : rackState.getChildWithName ("ch" + juce::String (m.channel)))
    {
        if (child["uid"].toString() != m.uid) continue;
        auto* spec = scrr::dsp::findModuleSpec (child["type"].toString()); if (! spec || spec->typeId == "Notes") return false;
        auto accept = [&] (const scrr::dsp::ParamSpec& p)
        {
            m.minimum = p.minVal; m.maximum = p.maxVal; m.step = p.step; m.logarithmic = p.logScale; m.unit = p.unit;
            m.label = "CH " + juce::String (m.channel) + " / " + spec->displayName + " / " + p.label; return true;
        };
        for (const auto& p : spec->params)
            if (m.parameter == juce::Identifier (p.id) && (p.type == scrr::dsp::ParamSpec::Float || p.type == scrr::dsp::ParamSpec::Int)) return accept (p);
        if (! scrr::dsp::isGenericEffect (spec->typeId))
        {
            if (m.parameter == juce::Identifier ("rangeLow")) return accept (scrr::dsp::makeFloat ("rangeLow", "LOW / Hz", 0, getFrequencyLimit(), 0, 1, " Hz"));
            if (m.parameter == juce::Identifier ("rangeHigh")) return accept (scrr::dsp::makeFloat ("rangeHigh", "HIGH / Hz", 0, getFrequencyLimit(), getFrequencyLimit(), 1, " Hz"));
            if (m.parameter == juce::Identifier ("moduleMix")) return accept (scrr::dsp::makeFloat ("moduleMix", "STAGE MIX", 0, 100, 100, .1f, " %"));
        }
        return false;
    }
    return false;
}
double SpectralCrrptProcessor::macroBase (const MacroMapping& m) const
{
    if (m.channel == MacroMapping::modulationChannel)
    {
        for (const auto& source : modulators) if (source.uid == m.uid) return source.parameterValue (m.parameter);
        return m.centre;
    }
    if (m.channel == 0) return apvts.getRawParameterValue (m.parameter.toString())->load();
    for (auto child : rackState.getChildWithName ("ch" + juce::String (m.channel)))
        if (child["uid"].toString() == m.uid) return juce::jlimit (m.minimum, m.maximum, (double) child[m.parameter]);
    return m.centre;
}
std::vector<MacroMapping> SpectralCrrptProcessor::getMacroMappings() const
{
    const juce::ScopedLock lock (stateLock); auto result = macroMappings;
    for (auto& m : result) { describeMacroTarget (m); m.centre = macroBase (m); m.syncRange(); }
    return result;
}
double SpectralCrrptProcessor::getEffectiveValue (int channel, const juce::String& uid, const juce::Identifier& parameter, double fallback) const
{
    const juce::ScopedLock lock (stateLock);
    const MacroMapping* target {}; double sum = 0;
    for (const auto& m : macroMappings)
        if (m.channel == channel && m.uid == uid && m.parameter == parameter && m.sourceEnabled)
        { target = &m; sum += m.offset (m.sourceUid.isEmpty() ? macroParameters[(size_t) m.macro]->load() * .01f : modulationEngine.value (m.sourceIndex - 8)); }
    if (! target) return fallback;
    auto current = *target;
    if (parameter == juce::Identifier ("rangeLow") || parameter == juce::Identifier ("rangeHigh")) current.maximum = getFrequencyLimit();
    return current.fromBase (macroBase (current), sum);
}
void SpectralCrrptProcessor::assignMacro (int macro, int channel, const juce::String& uid, const juce::Identifier& parameter)
{
    const juce::ScopedLock lock (stateLock);
    MacroMapping next; next.macro = macro; next.channel = channel; next.uid = uid; next.parameter = parameter;
    if (! describeMacroTarget (next) || macroMappings.size() >= 256) return;
    for (const auto& m : macroMappings) if (m.sameTarget (next) && m.sourceUid.isEmpty() && m.macro == macro) return;
    next.centre = macroBase (next); next.setDepth (next.normalise (next.centre) > .8 ? -.2 : .2, false);
    macroMappings.push_back (next); presetDirty.store (true); publishRack (true);
}
void SpectralCrrptProcessor::removeMacroMapping (int channel, const juce::String& uid, const juce::Identifier& parameter, int macro, const juce::String& source)
{
    const juce::ScopedLock lock (stateLock);
    macroMappings.erase (std::remove_if (macroMappings.begin(), macroMappings.end(), [&] (const auto& m)
        { return m.channel == channel && m.uid == uid && m.parameter == parameter && (source.isNotEmpty() ? m.sourceUid == source : macro < 0 || (m.sourceUid.isEmpty() && m.macro == macro)); }), macroMappings.end());
    presetDirty.store (true); publishRack (true);
}
void SpectralCrrptProcessor::updateMacroMapping (MacroMapping next)
{
    const juce::ScopedLock lock (stateLock);
    if (! describeMacroTarget (next)) return;
    boundRange (next);
    for (auto& m : macroMappings) if (m.sameTarget (next) && m.sameSource (next))
    {
        // Centre changes in the mapping list are ordinary base parameter edits.
        if (std::abs (macroBase (next) - next.centre) > juce::jmax (1.0e-6, next.step * .001))
        {
            if (next.channel == MacroMapping::modulationChannel)
            {
                for (auto& source : modulators) if (source.uid == next.uid) source.setParameterValue (next.parameter, next.centre);
            }
            else if (next.channel == 0)
            {
                auto* p = apvts.getParameter (next.parameter.toString());
                p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) next.centre)); p->endChangeGesture();
            }
            else for (auto child : rackState.getChildWithName ("ch" + juce::String (next.channel)))
                if (child["uid"].toString() == next.uid) child.setProperty (next.parameter, next.centre, nullptr);
        }
        m = std::move (next); presetDirty.store (true); publishRack (false); return;
    }
}
void SpectralCrrptProcessor::restoreMacroMappings (const juce::ValueTree& tree, const std::map<juce::String, juce::String>& ids)
{
    macroMappings.clear();
    for (auto child : tree)
    {
        if (! child.hasType ("MAP") || macroMappings.size() >= 256) continue;
        MacroMapping m; m.macro = (int) child.getProperty ("macro", -1); m.channel = (int) child.getProperty ("channel", -1);
        m.sourceUid = child["source"].toString();
        m.uid = child["uid"].toString(); const auto parameter = child["parameter"].toString();
        if (parameter.isEmpty() || parameter.length() > 128) continue;
        m.parameter = juce::Identifier (parameter);
        if (m.channel > 0 && m.channel != MacroMapping::modulationChannel)
        {
            const auto found = ids.find (juce::String (m.channel) + ":" + m.uid);
            if (found == ids.end()) continue;
            m.uid = found->second;
        }
        if (! describeMacroTarget (m)) continue;
        m.low = (double) child.getProperty ("low", m.minimum); m.high = (double) child.getProperty ("high", m.maximum);
        m.centre = (double) child.getProperty ("centre", (m.minimum + m.maximum) * .5); m.mode = (int) child["mode"];
        if ((int) child.getProperty ("version", 1) >= 2)
        {
            m.startOffset = (double) child["startOffset"]; m.endOffset = (double) child["endOffset"];
            m.centre = macroBase (m);
        }
        else
        {
            // Preserve old absolute endpoints; subsequent base edits move the range.
            auto clean = [&] (double value, double fallback) { return juce::jlimit (m.minimum, m.maximum, std::isfinite (value) ? value : fallback); };
            m.low = clean (m.low, m.minimum); m.high = clean (m.high, m.maximum);
            if (m.low > m.high) std::swap (m.low, m.high);
            m.centre = juce::jlimit (m.low, m.high, clean (m.centre, m.minimum));
            if (m.mode == MacroMapping::unipolarLeft || m.mode == MacroMapping::bipolarRightLow) std::swap (m.low, m.high);
            if (! m.bipolar()) m.centre = m.low;
            m.startOffset = m.normalise (m.low) - m.normalise (m.centre);
            m.endOffset = m.normalise (m.high) - m.normalise (m.centre);
            if (m.channel == MacroMapping::modulationChannel)
            {
                for (auto& source : modulators) if (source.uid == m.uid) source.setParameterValue (m.parameter, m.centre);
            }
            else if (m.channel == 0)
            {
                auto* p = apvts.getParameter (m.parameter.toString());
                p->setValueNotifyingHost (p->convertTo0to1 ((float) m.centre));
            }
            else for (auto module : rackState.getChildWithName ("ch" + juce::String (m.channel)))
                if (module["uid"].toString() == m.uid) module.setProperty (m.parameter, m.centre, nullptr);
        }
        boundRange (m);
        if (std::none_of (macroMappings.begin(), macroMappings.end(), [&] (const auto& existing) { return m.sameTarget (existing) && m.sameSource (existing); })) macroMappings.push_back (std::move (m));
    }
}

scrr::params::ModulationValues SpectralCrrptProcessor::getModulationValues() const noexcept
{
    scrr::params::ModulationValues result {}; const auto macros = getMacroValues();
    std::copy (macros.begin(), macros.end(), result.begin());
    for (int i = 0; i < scrr::params::maxModulators; ++i) result[(size_t) i + 8] = modulationEngine.value (i);
    return result;
}
std::vector<scrr::params::ModulationSource> SpectralCrrptProcessor::getModulators() const
{ const juce::ScopedLock lock (stateLock); return modulators; }
juce::String SpectralCrrptProcessor::addModulator (bool envelope, bool random)
{
    const juce::ScopedLock lock (stateLock);
    if (modulators.size() >= (size_t) scrr::params::maxModulators) return {};
    scrr::params::ModulationSource source; source.uid = juce::Uuid().toString(); source.envelope = envelope;
    source.random = random && ! envelope;
    if (source.random) source.seed = juce::Random::getSystemRandom().nextInt (1000000);
    for (int number = 1;; ++number)
    {
        source.name = (envelope ? "ENV " : source.random ? "RND " : "LFO ") + juce::String (number);
        if (std::none_of (modulators.begin(), modulators.end(), [&] (const auto& m) { return m.name == source.name; })) break;
    }
    modulators.push_back (source); modulatorRevision.fetch_add (1); presetDirty.store (true); publishRack (false); return source.uid;
}
void SpectralCrrptProcessor::removeModulator (const juce::String& uid)
{
    const juce::ScopedLock lock (stateLock);
    modulators.erase (std::remove_if (modulators.begin(), modulators.end(), [&] (const auto& m) { return m.uid == uid; }), modulators.end());
    macroMappings.erase (std::remove_if (macroMappings.begin(), macroMappings.end(), [&] (const auto& m) { return m.sourceUid == uid || (m.channel == MacroMapping::modulationChannel && m.uid == uid); }), macroMappings.end());
    for (auto& m : macroMappings) describeMacroTarget (m);
    modulatorRevision.fetch_add (1); presetDirty.store (true); publishRack (true);
}
void SpectralCrrptProcessor::updateModulator (scrr::params::ModulationSource source)
{
    const juce::ScopedLock lock (stateLock);
    for (auto& m : modulators) if (m.uid == source.uid)
    { m = scrr::params::ModulationSource::read (source.state()); presetDirty.store (true); publishRack (false); return; }
}
void SpectralCrrptProcessor::assignModulator (const juce::String& source, int channel, const juce::String& uid, const juce::Identifier& parameter)
{
    const juce::ScopedLock lock (stateLock);
    scrr::params::MacroMapping next; next.sourceUid = source; next.channel = channel; next.uid = uid; next.parameter = parameter;
    if (! describeMacroTarget (next) || macroMappings.size() >= 256) return;
    for (const auto& m : macroMappings) if (m.sameTarget (next) && m.sameSource (next)) return;
    next.centre = macroBase (next); next.setDepth (next.normalise (next.centre) > .8 ? -.2 : .2, false);
    macroMappings.push_back (next); presetDirty.store (true); publishRack (true);
}

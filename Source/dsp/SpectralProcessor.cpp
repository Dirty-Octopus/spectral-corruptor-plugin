// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectralProcessor.h"
#include "FrequencySplit.h"
#include "SpectralResourceBudget.h"
#include "../parameters/ParamIDs.h"
#include <cmath>
#include <algorithm>

namespace scrr::dsp {
namespace {
bool isPartialType (const juce::String& type) { return type.startsWith ("Partial") && findModuleSpec (type) != nullptr; }

double mappedValue (const scrr::params::MacroMapping& mapping, double base, double modulation, double maximum) noexcept
{
    const double minimum = mapping.minimum;
    maximum = std::max (minimum, maximum);
    if (maximum <= minimum) return minimum;
    base = juce::jlimit (minimum, maximum, base);
    const bool logarithmic = mapping.logarithmic && minimum > 0;
    const double normal = logarithmic ? std::log (base / minimum) / std::log (maximum / minimum)
                                      : (base - minimum) / (maximum - minimum);
    const double t = juce::jlimit (0.0, 1.0, normal + modulation);
    double result = logarithmic ? minimum * std::pow (maximum / minimum, t) : minimum + t * (maximum - minimum);
    if (mapping.step > 0) result = minimum + std::round ((result - minimum) / mapping.step) * mapping.step;
    return juce::jlimit (minimum, maximum, result);
}
}

SpectralProcessor::SpectralProcessor() {}

void SpectralProcessor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate   = spec.sampleRate;
    displaySampleRate.store (sampleRate);
    numChannels  = (int) spec.numChannels;
    maxBlockSize = (int) spec.maximumBlockSize;

    wetBuffer.setSize (numChannels, maxBlockSize);
    osBuffer.setSize (numChannels, maxBlockSize * 8);
    for (auto& route : routeAudio) route.setSize (numChannels, maxBlockSize * 4);

    oversampler.prepare (sampleRate, maxBlockSize, numChannels, oversampleFactor);
    inputGain.prepare  (sampleRate, maxBlockSize, numChannels);
    outputGain.prepare (sampleRate, maxBlockSize, numChannels);
    dryWet.prepare (sampleRate, maxBlockSize * 8, numChannels);

    reconfigure();
    fft.prepare (sampleRate * oversampleFactor, maxBlockSize * oversampleFactor, numChannels);

    const int bins = fft.getNumBins();
    moduleDry.resize ((size_t) bins);
    sliceBuffer.resize ((size_t) bins);
    displayOversample.store (oversampleFactor);

    for (auto& buf : channelBuffers)
        buf.assign ((size_t) bins, std::complex<float> { 0.0f, 0.0f });

    splitWeightsDirty = true;

    dryWet.setWetDelay (getLatencySamples());
    pendingOversampleFactor = oversampleFactor;
    pendingFftSize = fftSize;
    pendingWindowType = windowType;
    pendingOverlapFactor = overlapFactor;
    pendingReconfigure = false;
    syncSliceModules (true);
    reset();
}

void SpectralProcessor::reconfigure()
{
    // Scale FFT size by oversample factor to maintain frequency resolution
    // at base sample rate. With 4x oversample and 2048 FFT, actual FFT = 8192.
    const int actualFftSize = fftSize * oversampleFactor;
    hopSize = juce::jlimit (1, actualFftSize, (int) std::llround ((double) actualFftSize / overlapFactor));
    fft.configure (actualFftSize, hopSize, windowType, legacyNormalization);
    displayFFTSize.store (actualFftSize);
    displayHopSize.store (hopSize);
    double power = 0;
    for (const auto value : fft.getAnalysisWindow()) power += (double) value * value;
    bandPowerCalibration = 2.0 / ((double) actualFftSize * std::max (1.0e-20, power));
    rebuildOccupiedSliceMasks();
    refreshPoolPlan();
}

void SpectralProcessor::applyPendingReconfigure()
{
    if (! pendingReconfigure) return;

    const bool osChanged = (pendingOversampleFactor != oversampleFactor);
    oversampleFactor = pendingOversampleFactor;
    fftSize = pendingFftSize;
    windowType = pendingWindowType;
    overlapFactor = pendingOverlapFactor;
    legacyNormalization = pendingLegacyNormalization;

    if (osChanged)
        oversampler.prepare (sampleRate, maxBlockSize, numChannels, oversampleFactor);

    reconfigure();
    fft.prepare (sampleRate * oversampleFactor, maxBlockSize * oversampleFactor, numChannels);

    const int bins = fft.getNumBins();
    moduleDry.resize ((size_t) bins);
    sliceBuffer.resize ((size_t) bins);
    displayOversample.store (oversampleFactor);

    for (auto& buf : channelBuffers)
        buf.assign ((size_t) bins, std::complex<float> { 0.0f, 0.0f });

    splitWeightsDirty = true;
    syncSliceModules (true);
    dryWet.setWetDelay (getLatencySamples());
    pendingReconfigure = false;
}

void SpectralProcessor::reset()
{
    fft.reset();
    oversampler.reset();
    dryWet.reset();
    inputGain.reset();
    outputGain.reset();
    wetBuffer.clear();
    osBuffer.clear();
    for (auto& route : routeAudio) route.clear();
    for (auto& level : channelLevels) level.store (0);
    for (auto& chain : chains)
        for (auto& slot : chain)
            {
                if (slot.module) slot.module->reset();
                if (slot.partial) slot.partial->reset();
                for (auto& clone : slot.sliceModules) if (clone) clone->reset();
            }
}

void SpectralProcessor::updateGlobalParameters (juce::AudioProcessorValueTreeState& apvts)
{
    using namespace scrr::params;

    frequencyLimit = frequencyLimitValue (apvts);
    const int newOversample = oversampleFactorFromChoice (apvts);
    const int newFftSize    = fftSizeValue (apvts);
    const int extendedWindow = (int) apvts.getRawParameterValue (id::windowTypeExtended)->load();
    const int newWindowIdx = extendedWindow > 0 ? extendedWindow - 1
        : (int) apvts.getRawParameterValue (id::windowType)->load();
    const auto newWindow = static_cast<WindowType> (juce::jlimit (0, 8, newWindowIdx));

    pendingOversampleFactor = newOversample;
    pendingFftSize = newFftSize;
    pendingWindowType = newWindow;
    pendingOverlapFactor = juce::jlimit (2.0, 16.0, (double) apvts.getRawParameterValue (id::overlapFactor)->load());
    pendingLegacyNormalization = extendedWindow == 0 && std::abs (pendingOverlapFactor - 4.0) < 1.0e-8;
    pendingReconfigure = (pendingOversampleFactor != oversampleFactor
                       || pendingFftSize != fftSize
                       || pendingWindowType != windowType || pendingLegacyNormalization != legacyNormalization
                       || std::abs (pendingOverlapFactor - overlapFactor) > 1.0e-8);

    dryWetMix = globalValue (id::drywet, apvts.getRawParameterValue (id::drywet)->load()) / 100.0f;
    bypass = apvts.getRawParameterValue (id::bypass)->load() > 0.5f;
    dryWet.setMix (bypass ? 0.0f : dryWetMix);
    inputGain.setGainDb  (globalValue (id::inputGain, apvts.getRawParameterValue (id::inputGain)->load()));
    outputGain.setGainDb (globalValue (id::outputGain, apvts.getRawParameterValue (id::outputGain)->load()));

    for (size_t c = 0; c < 4; ++c) channelEnabled[c] = apvts.getRawParameterValue (id::channelEnabled[c])->load() > .5f;
    soloMode = apvts.getRawParameterValue (id::freqSplitSolo)->load() > 0.5f;
    postPreview = apvts.getRawParameterValue (id::freqSplitPostPreview)->load() > 0.5f;

    const int rawSplits = juce::jlimit (1, 64, (int) apvts.getRawParameterValue (id::freqSplitNumSplits)->load());
    const bool poolPlanChanged = ! globalParametersReceived || rawSplits != baseNumSplits;
    globalParametersReceived = true;
    baseNumSplits = rawSplits;
    if (poolPlanChanged) refreshPoolPlan();
    const int newNumSplits = juce::jlimit (1, 64, (int) globalValue (id::freqSplitNumSplits, (float) baseNumSplits));
    const float newCrossfade = globalValue (id::freqSplitCrossfade, apvts.getRawParameterValue (id::freqSplitCrossfade)->load());

    const bool sliceCountChanged = newNumSplits != numSplits;
    if (sliceCountChanged
        || std::fabs (newCrossfade - crossfadePct) > 0.01f)
    {
        numSplits = newNumSplits;
        crossfadePct = newCrossfade;
        splitWeightsDirty = true;
    }
    const bool previewChanged = poolFullPreview != (soloMode && ! postPreview) || poolSoloChannel != soloChannel.load();
    if (previewChanged) resourceConfigurationDirty = true;
    if (sliceCountChanged || poolPlanChanged || previewChanged) syncSliceModules();
}

bool SpectralProcessor::chainNeedsRebuild (int channel, const juce::ValueTree& chainState) const
{
    const auto& slots = chains[(size_t) juce::jlimit (0, 4, channel)];
    if (chainState.getNumChildren() != (int) slots.size())
        return true;

    for (int i = 0; i < chainState.getNumChildren(); ++i)
    {
        auto child = chainState.getChild (i);
        const juce::String type = child.getProperty ("type", "");
        if (type != slots[(size_t) i].typeId || child["uid"] != slots[(size_t) i].state["uid"])
            return true;
    }
    return false;
}

void SpectralProcessor::rebuildChain (int channel, const juce::ValueTree& chainState)
{
    resourceConfigurationDirty = true;
    auto& slots = getChain (channel);
    auto previous = std::move (slots);
    slots.clear();
    slots.reserve ((size_t) chainState.getNumChildren());

    for (int i = 0; i < chainState.getNumChildren(); ++i)
    {
        auto child = chainState.getChild (i);
        const juce::String typeId = child.getProperty ("type", "");

        const auto existing = std::find_if (previous.begin(), previous.end(), [&] (const auto& slot)
        { return slot.typeId == typeId && slot.state["uid"] == child["uid"]; });
        if (existing != previous.end())
        {
            slots.push_back (std::move (*existing));
            continue;
        }

        ModuleSlot slot;
        slot.typeId = typeId;
        slot.state = child.createCopy();
        slot.enabled = (bool) child.getProperty ("enabled", true);
        if (slot.enabled) slot.module = createModule (typeId);
        slot.resource = std::make_shared<ResourceEntry>();
        slot.resource->channel = channel;
        slot.resource->uid = child["uid"].toString();

        slots.push_back (std::move (slot));
    }
    publishResourceEntries();
}

void SpectralProcessor::updateModuleChain (int channel, const juce::ValueTree& chainState)
{
    if (chainNeedsRebuild (channel, chainState))
        rebuildChain (channel, chainState);

    auto& slots = getChain (channel);
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (i < (size_t) chainState.getNumChildren())
        {
            // Audio-owned state: macro automation must never mutate a published snapshot.
            slots[i].baseState = chainState.getChild ((int) i).createCopy();
            slots[i].state = slots[i].baseState.createCopy();
            slots[i].enabled = (bool) slots[i].state.getProperty ("enabled", true);
            slots[i].parametersDirty = true;
            if (slots[i].module)
                slots[i].module->updateParameters (slots[i].state);
            if (slots[i].partial) configurePartial (slots[i], true);
        }
    }
    // The complete rack snapshot is followed by setMacroMappings(), which
    // plans resources once after every rack has received its current state.
}

void SpectralProcessor::preparePartial (ModuleSlot& slot)
{
    if (! slot.partial) return;
    const int frames = (maxBlockSize * oversampleFactor + hopSize - 1) / hopSize + 2;
    slot.partial->prepare (sampleRate * oversampleFactor, fft.getFFTSize(), hopSize, numChannels,
                           fft.getAnalysisWindow(), frames, partialWorkspace);
}

void SpectralProcessor::setFormulaPrograms (const FormulaPrograms& programs)
{
    for (auto& chain : chains) for (auto& slot : chain)
    {
        if (slot.typeId != "FrequencyRemap") continue;
        const auto uid = slot.state["uid"].toString();
        const auto found = std::find_if (programs.begin(), programs.end(), [&] (const auto& entry) { return entry.uid == uid; });
        slot.formulaProgram = found != programs.end() ? found->program : nullptr;
        if (slot.formulaRuntime)
        {
            slot.formulaRuntime->setProgram (slot.formulaProgram);
            slot.formulaRuntime->updateParameters (slot.state);
        }
    }
}

void SpectralProcessor::configurePartial (ModuleSlot& slot, bool rebuild)
{
    if (! slot.partial) return;
    const double reference = (double) slot.state.getProperty ("reference", 110.0);
    slot.partial->setFundamentalHz (reference);
    slot.partial->setAnchorMode ((int) slot.state.getProperty ("anchor", 0) == 1
        ? PartialProcessor::AnchorMode::EnergyCentroid : PartialProcessor::AnchorMode::BandCentre);
    const double amount = (double) slot.state.getProperty ("amount", 0.0);
    double pitch = (double) slot.state.getProperty ("pitch", 12.0);
    if (slot.typeId == "PartialCentroid")
        pitch = 12.0 * std::log2 (std::max (1.0, (double) slot.state.getProperty ("target", 1000.0)) / std::max (1.0, reference));
    if (! rebuild)
    {
        slot.partial->updateStageParameters (0, amount, pitch);
        return;
    }
    PartialProcessor::Stage stage;
    stage.uid = slot.state["uid"].toString().toStdString();
    if (slot.typeId == "PartialStiffString") stage.type = PartialProcessor::Type::StiffString;
    else if (slot.typeId == "PartialCentroid") stage.type = PartialProcessor::Type::Centroid;
    else if (slot.typeId == "PartialPGrid") stage.type = PartialProcessor::Type::PGrid;
    else if (slot.typeId == "PartialFGrid") stage.type = PartialProcessor::Type::FGrid;
    stage.mode = (int) slot.state.getProperty ("mode", 0) == 1 ? PartialProcessor::Mode::Resynth : PartialProcessor::Mode::Scale;
    stage.shape = static_cast<PartialProcessor::Shape> (juce::jlimit (0, 3, (int) slot.state.getProperty ("shape", 0)));
    stage.amount = amount;
    stage.pitch = pitch;
    slot.partial->setStages ({ stage });
}

SpectralProcessor::ModuleResourceStatus SpectralProcessor::getModuleResourceStatus (int channel, const juce::String& uid) const noexcept
{
    const auto entries = std::atomic_load (&publishedResources);
    if (entries)
        for (const auto& entry : *entries)
            if (entry->channel == channel && entry->uid == uid)
                return { entry->blocked.load(), entry->requiredBytes.load(),
                         entry->availableBytes.load(), entry->budgetBytes.load() };
    return {};
}

void SpectralProcessor::publishResourceEntries()
{
    auto entries = std::make_shared<ResourceEntries>();
    for (const auto& chain : chains)
        for (const auto& slot : chain)
            if (slot.resource) entries->push_back (slot.resource);
    std::atomic_store (&publishedResources, std::shared_ptr<const ResourceEntries> (std::move (entries)));
}

void SpectralProcessor::rebuildOccupiedSliceMasks()
{
    // Conservative geometric occupancy at the widest crossfade. This costs at
    // most 4096 interval tests when FFT geometry changes, rather than rescanning
    // every bin at every 64-sample modulation update. Endpoints belong to slice0.
    const double binHz = sampleRate * oversampleFactor / fft.getFFTSize();
    const double ratio = std::max (40.0, sampleRate * .5) / 20.0;
    for (int count = 1; count <= 64; ++count)
    {
        auto& mask = occupiedSliceMasks[(size_t) count];
        mask.fill (false);
        mask[0] = true;
        for (int slice = 1; slice < count; ++slice)
        {
            const double low = 20.0 * std::pow (ratio, ((double) slice - .5) / count);
            const double high = slice == count - 1 ? sampleRate * oversampleFactor
                : 20.0 * std::pow (ratio, ((double) slice + 1.5) / count);
            const int first = std::max (1, (int) std::ceil (low / binHz));
            const int last = std::min (fft.getNumBins() - 2, (int) std::floor (high / binHz));
            mask[(size_t) slice] = first <= last;
        }
    }
}

void SpectralProcessor::refreshPoolPlan()
{
    const scrr::params::MacroMapping* target = nullptr;
    double lowOffset = 0, highOffset = 0;
    for (const auto& mapping : globalMappings)
        if (mapping.parameter.toString() == scrr::params::id::freqSplitNumSplits
            && mapping.sourceEnabled && mapping.valueIndex() >= 0
            && (size_t) mapping.valueIndex() < macroValues.size())
        {
            target = &mapping;
            lowOffset += std::min ({ 0.0, mapping.startOffset, mapping.endOffset });
            highOffset += std::max ({ 0.0, mapping.startOffset, mapping.endOffset });
        }
    const int minimum = juce::jlimit (1, 64, target ? (int) target->fromBase (baseNumSplits, lowOffset) : baseNumSplits);
    const int maximum = juce::jlimit (minimum, 64, target ? (int) target->fromBase (baseNumSplits, highOffset) : baseNumSplits);
    plannedSliceMask.fill (false);
    for (int count = minimum; count <= maximum; ++count)
        for (size_t slice = 0; slice < plannedSliceMask.size(); ++slice)
            plannedSliceMask[slice] = plannedSliceMask[slice] || occupiedSliceMasks[(size_t) count][slice];
}

void SpectralProcessor::syncSliceModules (bool reprepare)
{
    const int bins = fft.getNumBins();
    const double actualRate = sampleRate * oversampleFactor;
    poolFullPreview = soloMode && ! postPreview;
    poolSoloChannel = soloChannel.load();
    const int frames = (maxBlockSize * oversampleFactor + hopSize - 1) / hopSize + 2;
    const bool planNeedsPartial = std::any_of (plannedSliceMask.begin() + 1, plannedSliceMask.end(), [] (bool value) { return value; });
    const bool splitPermanentlyOff = globalParametersReceived && numSplits <= 1 && ! planNeedsPartial;
    auto clearSlot = [] (ModuleSlot& slot)
    {
        for (auto& clone : slot.sliceModules) clone.reset();
        slot.scratch.reset();
        slot.partial.reset();
        slot.module.reset();
        slot.formulaRuntime.reset();
        slot.primaryPrepared = false;
        slot.ownedBytes = 0;
        slot.primaryBytes = 0;
    };
    for (auto& chain : chains)
        for (auto& slot : chain)
        {
            if (reprepare || ! slot.enabled || (splitPermanentlyOff && isPartialType (slot.typeId))) clearSlot (slot);
            slot.resourceBlocked = false;
        }
    // If no enabled partial remains in a rack, its ordinary clones are no
    // longer useful. A modulated count of one alone never destroys the pool.
    for (int rack = 1; rack <= 4; ++rack)
    {
        auto& chain = chains[(size_t) rack];
        const bool hasPartial = std::any_of (chain.begin(), chain.end(), [] (const auto& slot)
        { return slot.enabled && isPartialType (slot.typeId); });
        if (! hasPartial || splitPermanentlyOff)
            for (auto& slot : chain)
                if (! isPartialType (slot.typeId))
                {
                    for (auto& clone : slot.sliceModules) clone.reset();
                    slot.scratch.reset();
                    slot.ownedBytes = slot.primaryBytes;
                }
    }
    bool hasPreparedPartial = false;
    for (const auto& chain : chains) for (const auto& slot : chain)
        hasPreparedPartial = hasPreparedPartial || slot.partial != nullptr;
    if (! hasPreparedPartial) partialWorkspace.reset();
    bool waveformsRequested = false;
    if (globalParametersReceived && (numSplits > 1 || planNeedsPartial))
        for (size_t rack = 1; rack < chains.size(); ++rack) for (const auto& slot : chains[rack])
            waveformsRequested = waveformsRequested || (slot.enabled && isPartialType (slot.typeId)
                && (int) slot.state.getProperty ("mode", 0) == 1 && (int) slot.state.getProperty ("shape", 0) > 0);
    if (partialWorkspace && ! waveformsRequested) partialWorkspace->releaseWaveforms();
    size_t used = partialWorkspace ? PartialProcessor::Workspace::estimatedBytes (fft.getFFTSize())
        + (partialWorkspace->hasWaveforms() ? PartialProcessor::Workspace::estimatedWaveformBytes() : 0) : 0;
    for (const auto& chain : chains) for (const auto& slot : chain) used += slot.ownedBytes;
    const auto availableFor = [&] (const ModuleSlot& slot)
    { const size_t other = used - slot.ownedBytes; return other < effectCacheBudget ? effectCacheBudget - other : 0; };
    // Snapshot/configuration admission may reclaim future-count caches, but
    // count-only modulation never evicts/recreates its retained clone pool.
    const auto reclaimUnusedClones = [&]
    {
        if (! resourceConfigurationDirty || reprepare) return false;
        bool reclaimed = false;
        for (size_t rack = 1; rack < chains.size(); ++rack)
        {
            const bool currentSlicePath = ! poolFullPreview && numSplits > 1
                && std::any_of (chains[rack].begin(), chains[rack].end(), [] (const auto& slot)
                    { return slot.enabled && isPartialType (slot.typeId); });
            for (auto& slot : chains[rack])
            {
                if (isPartialType (slot.typeId)) continue;
                const size_t cloneBytes = estimateSpectralCloneBytes (slot.typeId, bins, true);
                bool retainsClone = false;
                for (int slice = 0; slice < 64; ++slice)
                {
                    auto& clone = slot.sliceModules[(size_t) slice];
                    if (! clone) continue;
                    if (currentSlicePath && occupiedSliceMasks[(size_t) numSplits][(size_t) slice])
                    { retainsClone = true; continue; }
                    clone.reset();
                    slot.ownedBytes -= cloneBytes;
                    used -= cloneBytes;
                    reclaimed = true;
                }
                if (! retainsClone && slot.scratch)
                {
                    const size_t workspaceBytes = estimateSpectralWorkspaceBytes (slot.typeId, bins);
                    slot.scratch.reset();
                    slot.ownedBytes -= workspaceBytes;
                    used -= workspaceBytes;
                    reclaimed = true;
                }
            }
        }
        return reclaimed;
    };
    const auto setStatus = [&] (ModuleSlot& slot, bool blocked, size_t required, size_t available)
    {
        slot.resourceBlocked = blocked;
        if (slot.resource)
        {
            slot.resource->requiredBytes.store (required);
            slot.resource->availableBytes.store (available);
            slot.resource->budgetBytes.store (effectCacheBudget);
            slot.resource->blocked.store (blocked);
        }
    };
    // Admit partial stages first. A rejected ordinary effect must not disable
    // the movement of its slices or any other effect in the same rack.
    for (size_t rack = 0; rack < chains.size(); ++rack)
        for (auto& slot : chains[rack])
        {
            if (! isPartialType (slot.typeId)) continue;
            if (! slot.enabled || rack == 0 || ! globalParametersReceived
                || (numSplits <= 1 && ! planNeedsPartial))
            {
                setStatus (slot, false, 0, availableFor (slot));
                continue;
            }
            const size_t privateBytes = PartialProcessor::estimatedPrivateBytes (frames);
            const bool wantsWaveform = (int) slot.state.getProperty ("mode", 0) == 1 && (int) slot.state.getProperty ("shape", 0) > 0;
            const size_t workspaceAddition = partialWorkspace ? 0 : PartialProcessor::Workspace::estimatedBytes (fft.getFFTSize());
            const size_t waveformAddition = wantsWaveform && (! partialWorkspace || ! partialWorkspace->hasWaveforms())
                ? PartialProcessor::Workspace::estimatedWaveformBytes() : 0;
            const size_t sharedBytes = workspaceAddition + waveformAddition;
            const size_t available = availableFor (slot);
            if (privateBytes > available || sharedBytes > available - privateBytes)
            {
                if (reclaimUnusedClones())
                {
                    resourceConfigurationDirty = false;
                    syncSliceModules();
                    return;
                }
                setStatus (slot, true, privateBytes + sharedBytes, available);
                continue;
            }
            if (! partialWorkspace)
            {
                partialWorkspace = std::make_shared<PartialProcessor::Workspace>();
                partialWorkspace->prepare (fft.getFFTSize(), fft.getAnalysisWindow());
                used += workspaceAddition;
            }
            if (waveformAddition > 0)
            {
                partialWorkspace->prepareWaveforms();
                used += waveformAddition;
            }
            if (! slot.partial)
            {
                if (! slot.module) slot.module = createModule (slot.typeId);
                slot.partial = std::make_unique<PartialProcessor>();
                configurePartial (slot, true);
                preparePartial (slot);
                slot.primaryPrepared = true;
                slot.ownedBytes = privateBytes;
                used += privateBytes;
            }
            setStatus (slot, false, privateBytes + sharedBytes, available);
        }
    for (int rack = 1; rack <= 4; ++rack)
        sliceChains[(size_t) (rack - 1)] = numSplits > 1
            && std::any_of (chains[(size_t) rack].begin(), chains[(size_t) rack].end(), [] (const auto& slot)
                { return slot.enabled && slot.partial && ! slot.resourceBlocked; });

    const auto addClone = [&] (ModuleSlot& slot, int slice, size_t cloneBytes)
    {
        auto& clone = slot.sliceModules[(size_t) slice];
        if (clone) return;
        clone = createModule (slot.typeId);
        if (! clone) return;
        clone->setSharedScratchWorkspace (slot.scratch.get());
        if (slot.typeId == "FrequencyRemap") static_cast<FrequencyRemapModule&> (*clone).setFormulaRuntime (slot.formulaRuntime.get());
        clone->prepare (actualRate, bins);
        clone->setSpectrumCalibration (fft.getWindowAmplitudeSum());
        clone->setHopSize (hopSize);
        clone->updateParameters (slot.state);
        slot.ownedBytes += cloneBytes;
        used += cloneBytes;
        cloneAllocationCount.fetch_add (1);
    };
    for (size_t rack = 0; rack < chains.size(); ++rack)
        for (auto& slot : chains[rack])
        {
            if (isPartialType (slot.typeId)) continue;
            if (! slot.enabled)
            {
                setStatus (slot, false, 0, availableFor (slot));
                continue;
            }
            const bool wantsFormula = slot.typeId == "FrequencyRemap" && (int) slot.state.getProperty ("remapMode", 0) == 1;
            if (! wantsFormula && slot.formulaRuntime)
            {
                slot.formulaRuntime.reset();
                slot.primaryBytes -= estimateRemapFormulaBytes();
                slot.ownedBytes -= estimateRemapFormulaBytes();
                used -= estimateRemapFormulaBytes();
                if (slot.module) static_cast<FrequencyRemapModule&> (*slot.module).setFormulaRuntime (nullptr);
                for (auto& clone : slot.sliceModules) if (clone) static_cast<FrequencyRemapModule&> (*clone).setFormulaRuntime (nullptr);
            }
            const size_t formulaBytes = wantsFormula ? estimateRemapFormulaBytes() : 0;
            const size_t originalBytes = estimateOriginalModuleBytes (slot.typeId, bins, actualRate) + formulaBytes;
            const size_t available = availableFor (slot);
            if (! slot.module) slot.module = createModule (slot.typeId);
            if (! slot.module) continue;
            const bool slicePath = rack > 0 && ! poolFullPreview && sliceChains[rack - 1] && ! slot.module->isTimeDomain();
            const bool plannedSlicePath = rack > 0 && planNeedsPartial && ! slot.module->isTimeDomain()
                && std::any_of (chains[rack].begin(), chains[rack].end(), [] (const auto& other)
                    { return other.enabled && other.partial && ! other.resourceBlocked; });
            const bool poolNeeded = slicePath || plannedSlicePath;
            const bool primaryNeeded = poolFullPreview && rack > 0
                ? (int) rack == poolSoloChannel + 1 : ! slicePath;
            // Metadata-only original instances do not own per-bin histories.
            // Two random modules construct a small fixed source table eagerly.
            const size_t metadataBytes = slot.typeId == "Notes" ? 0 : (slot.typeId == "PhaseNoise" || slot.typeId == "ComplexRotation"
                ? 4096 + 2 * 4096 * sizeof (uint32_t) : 4096) + formulaBytes;
            const size_t wantedPrimaryBytes = primaryNeeded || slot.primaryPrepared ? originalBytes : metadataBytes;
            if (wantedPrimaryBytes > available)
            {
                if (reclaimUnusedClones())
                {
                    resourceConfigurationDirty = false;
                    syncSliceModules();
                    return;
                }
                setStatus (slot, true, wantedPrimaryBytes, available);
                continue;
            }
            const size_t cloneBytes = poolNeeded ? estimateSpectralCloneBytes (slot.typeId, bins, true) : 0;
            const size_t workspaceBytes = poolNeeded ? estimateSpectralWorkspaceBytes (slot.typeId, bins) : 0;
            size_t requiredClones = 0, missingClones = 0;
            if (slicePath)
                for (int slice = (int) rack - 1; slice < numSplits; slice += 4)
                    if (occupiedSliceMasks[(size_t) numSplits][(size_t) slice])
                    {
                        ++requiredClones;
                        if (! slot.sliceModules[(size_t) slice]) ++missingClones;
                    }
            const size_t primaryAddition = wantedPrimaryBytes > slot.primaryBytes ? wantedPrimaryBytes - slot.primaryBytes : 0;
            const size_t workspaceAddition = slicePath && workspaceBytes > 0 && ! slot.scratch ? workspaceBytes : 0;
            const size_t fixed = slot.ownedBytes + primaryAddition + workspaceAddition;
            const bool fits = fixed <= available && (missingClones == 0
                || cloneBytes <= (available - fixed) / missingClones);
            const size_t required = cloneBytes == std::numeric_limits<size_t>::max() ? cloneBytes
                : wantedPrimaryBytes + (slicePath ? workspaceBytes : 0) + requiredClones * cloneBytes;
            if (! fits)
            {
                if (reclaimUnusedClones())
                {
                    resourceConfigurationDirty = false;
                    syncSliceModules();
                    return;
                }
                setStatus (slot, true, required, available);
                continue;
            }
            if (wantsFormula)
            {
                if (! slot.formulaRuntime) slot.formulaRuntime = std::make_unique<RemapFormulaRuntime>();
                slot.formulaRuntime->setProgram (slot.formulaProgram);
                slot.formulaRuntime->updateParameters (slot.state);
                static_cast<FrequencyRemapModule&> (*slot.module).setFormulaRuntime (slot.formulaRuntime.get());
                for (auto& clone : slot.sliceModules) if (clone) static_cast<FrequencyRemapModule&> (*clone).setFormulaRuntime (slot.formulaRuntime.get());
            }
            if (primaryNeeded && ! slot.primaryPrepared)
            {
                slot.module->updateParameters (slot.state);
                slot.module->prepare (actualRate, bins);
                slot.module->setSpectrumCalibration (fft.getWindowAmplitudeSum());
                slot.module->setHopSize (hopSize);
                slot.module->updateParameters (slot.state);
                slot.primaryPrepared = true;
            }
            if (slot.parametersDirty) slot.module->updateParameters (slot.state);
            slot.primaryBytes = wantedPrimaryBytes;
            slot.ownedBytes += primaryAddition;
            used += primaryAddition;
            if (workspaceAddition > 0)
            {
                slot.scratch = std::make_unique<SpectralScratchWorkspace>();
                slot.ownedBytes += workspaceAddition;
                used += workspaceAddition;
            }
            if (slicePath)
                for (int slice = (int) rack - 1; slice < numSplits; slice += 4)
                    if (occupiedSliceMasks[(size_t) numSplits][(size_t) slice]) addClone (slot, slice, cloneBytes);
            if (slot.parametersDirty)
                for (auto& clone : slot.sliceModules) if (clone) clone->updateParameters (slot.state);
            slot.parametersDirty = false;
            setStatus (slot, false, required, available);
        }
    // Only after every currently requested effect has been admitted may spare
    // capacity prewarm the known modulation range. An early slot's speculative
    // future count must never displace a later slot's current audio processing.
    if (planNeedsPartial)
        for (size_t rack = 1; rack < chains.size(); ++rack)
        {
            const bool hasPartial = std::any_of (chains[rack].begin(), chains[rack].end(), [] (const auto& slot)
                { return slot.enabled && slot.partial && ! slot.resourceBlocked; });
            if (! hasPartial) continue;
            for (auto& slot : chains[rack])
            {
                if (! slot.enabled || slot.resourceBlocked || ! slot.module || slot.module->isTimeDomain()
                    || isPartialType (slot.typeId)) continue;
                const size_t cloneBytes = estimateSpectralCloneBytes (slot.typeId, bins, true);
                const size_t workspaceBytes = ! slot.scratch ? estimateSpectralWorkspaceBytes (slot.typeId, bins) : 0;
                size_t missing = 0;
                for (int slice = (int) rack - 1; slice < 64; slice += 4)
                    if (plannedSliceMask[(size_t) slice] && ! slot.sliceModules[(size_t) slice]) ++missing;
                if (missing == 0 || used > effectCacheBudget || workspaceBytes > effectCacheBudget - used
                    || cloneBytes > (effectCacheBudget - used - workspaceBytes) / missing) continue;
                if (workspaceBytes > 0)
                {
                    slot.scratch = std::make_unique<SpectralScratchWorkspace>();
                    slot.ownedBytes += workspaceBytes;
                    used += workspaceBytes;
                }
                for (int slice = (int) rack - 1; slice < 64; slice += 4)
                    if (plannedSliceMask[(size_t) slice]) addClone (slot, slice, cloneBytes);
            }
        }
    // Admission and prewarming change the space available to other rows.
    // Refresh these scalar status fields after the final global accounting.
    for (auto& chain : chains)
        for (auto& slot : chain)
            if (slot.resource) slot.resource->availableBytes.store (availableFor (slot));
    resourceConfigurationDirty = false;
    displayEffectCacheBytes.store (used);
}

void SpectralProcessor::setMacroMappings (const std::vector<scrr::params::MacroMapping>& mappings)
{
    resourceConfigurationDirty = true;
    globalMappings.clear();
    for (auto& chain : chains) for (auto& slot : chain)
    {
        slot.mappings.clear();
        if (slot.typeId == "FrequencyRemap")
        {
            slot.remapBaseCurve = scrr::params::TransferCurve::decode (slot.baseState["remapCurve"].toString());
            slot.remapEffectiveCurve = slot.remapBaseCurve; slot.remapEffectiveCurve.points.reserve (64);
        }
    }
    for (const auto& m : mappings)
    {
        if (m.channel == scrr::params::MacroMapping::modulationChannel) continue;
        if (m.channel == 0 && m.uid.isEmpty()) { globalMappings.push_back (m); globalMappings.back().bindResponseMappings (mappings); continue; }
        if (m.channel < 0 || m.channel > 4) continue;
        for (auto& slot : chains[(size_t) m.channel])
            if (slot.state["uid"].toString() == m.uid) { slot.mappings.push_back (m); slot.mappings.back().bindResponseMappings (mappings); break; }
    }
    refreshPoolPlan();
    syncSliceModules();
}

void SpectralProcessor::applyMacroValues (const scrr::params::ModulationValues& values)
{
    macroValues = values;
    for (auto& chain : chains) for (auto& slot : chain)
    {
        if (! slot.module || (slot.mappings.empty() && slot.typeId != "FrequencyRemap")) continue;
        if (slot.typeId == "FrequencyRemap")
            std::copy (slot.remapBaseCurve.points.begin(), slot.remapBaseCurve.points.end(), slot.remapEffectiveCurve.points.begin());
        for (size_t i = 0; i < slot.mappings.size(); ++i)
        {
            const auto& m = slot.mappings[i]; bool already = false;
            const bool frequencyRange = m.parameter.toString() == "rangeLow" || m.parameter.toString() == "rangeHigh";
            for (size_t j = 0; j < i; ++j) if (slot.mappings[j].parameter == m.parameter) { already = true; break; }
            if (already) continue;
            double sum = 0;
            for (const auto& route : slot.mappings)
                if (route.parameter == m.parameter && route.sourceEnabled && route.valueIndex() >= 0 && (size_t) route.valueIndex() < values.size())
                    sum += route.offset (values[(size_t) route.valueIndex()], &values);
            if (m.coordinatePointId != 0)
            {
                const int point = slot.remapBaseCurve.pointIndex (m.coordinatePointId);
                if (point >= 0)
                {
                    const auto base = slot.remapBaseCurve.points[(size_t) point]; auto& effective = slot.remapEffectiveCurve.points[(size_t) point];
                    (m.coordinateXAxis ? effective.x : effective.y) = (float) m.fromBase (m.coordinateXAxis ? base.x : base.y, sum);
                }
                continue;
            }
            const double value = frequencyRange ? mappedValue (m, (double) slot.baseState[m.parameter], sum, frequencyLimit)
                                                 : m.fromBase ((double) slot.baseState[m.parameter], sum);
            slot.state.setProperty (m.parameter, value, nullptr);
        }
        slot.module->updateParameters (slot.state);
        if (slot.formulaRuntime) slot.formulaRuntime->updateParameters (slot.state);
        if (slot.typeId == "FrequencyRemap")
        {
            scrr::params::TransferCurve::constrainCoordinates (slot.remapEffectiveCurve.points.data(), slot.remapEffectiveCurve.points.size(), false, false, slot.remapBaseCurve.points.data());
            if (auto* remap = dynamic_cast<FrequencyRemapModule*> (slot.module.get())) remap->setEffectiveCurve (slot.remapEffectiveCurve);
        }
        if (slot.partial) configurePartial (slot, false);
        for (auto& clone : slot.sliceModules) if (clone)
        {
            clone->updateParameters (slot.state);
            if (auto* remap = dynamic_cast<FrequencyRemapModule*> (clone.get())) remap->setEffectiveCurve (slot.remapEffectiveCurve);
        }
    }
}
float SpectralProcessor::globalValue (const juce::String& id, float base) const noexcept
{
    const scrr::params::MacroMapping* target {}; double sum = 0;
    for (const auto& m : globalMappings)
        if (m.parameter.toString() == id && m.sourceEnabled && m.valueIndex() >= 0 && (size_t) m.valueIndex() < macroValues.size())
        { target = &m; sum += m.offset (macroValues[(size_t) m.valueIndex()], &macroValues); }
    return target ? (float) target->fromBase (base, sum) : base;
}
void SpectralProcessor::recomputeSplitWeights (int numBins)
{
    for (auto& w : splitWeights)
        w.assign ((size_t) numBins, 0.0f);
    individualWeights.resize ((size_t) numBins);
    sliceFirst.fill (numBins);
    sliceLast.fill (-1);

    const FrequencySplit mapping (sampleRate, numSplits, crossfadePct);
    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto weights = mapping.weightsForBin (bin, numBins, sampleRate * oversampleFactor);
        const auto individual = mapping.sliceWeightsForBin (bin, numBins, sampleRate * oversampleFactor);
        individualWeights[(size_t) bin] = individual;
        auto include = [&] (int slice, float weight)
        {
            if (slice < 0 || weight <= 0) return;
            sliceFirst[(size_t) slice] = std::min (sliceFirst[(size_t) slice], bin);
            sliceLast[(size_t) slice] = std::max (sliceLast[(size_t) slice], bin);
        };
        include (individual.first, individual.firstWeight);
        include (individual.second, individual.secondWeight);
        for (size_t ch = 0; ch < splitWeights.size(); ++ch)
            splitWeights[ch][(size_t) bin] = weights[ch];
    }

    splitWeightsDirty = false;
}

void SpectralProcessor::processChain (std::vector<ModuleSlot>& chain,
    std::complex<float>* spectrum, int bins, double sr)
{
    for (auto& slot : chain)
        if (slot.module && ! slot.partial) processSlot (slot, *slot.module, spectrum, bins, sr);
}

void SpectralProcessor::processSlot (ModuleSlot& slot, SpectralModule& module,
    std::complex<float>* spectrum, int bins, double sr, PartialProcessor::SliceContext* context)
{
    if (! slot.enabled || slot.resourceBlocked
        || (&module == slot.module.get() && ! slot.primaryPrepared) || module.isTimeDomain() || (slot.partial && context == nullptr)) return;
    const float mix = (float) slot.state.getProperty ("moduleMix", 100.0) * 0.01f;
    if (mix <= 0.0f) return;
    const float low = (float) slot.state.getProperty ("rangeLow", 0.0);
    const float high = (float) slot.state.getProperty ("rangeHigh", 96000.0);
    std::copy (spectrum, spectrum + bins, moduleDry.begin());
    // Keep the out-of-range signal dry, and do not let it feed shifts or
    // other spectral transforms back into the permitted processing band.
    const double binHz = sr / (2.0 * (bins - 1));
    for (int b = 0; b < bins; ++b)
        if ((double) b * binHz > frequencyLimit) spectrum[b] = {};
    const bool remap = slot.typeId == "FrequencyRemap";
    if (remap) static_cast<FrequencyRemapModule&> (module).setFrequencyLimit (frequencyLimit);
    if (slot.partial && context) slot.partial->processSlice (spectrum, bins, *context, frequencyLimit, currentAudioChannel);
    else module.processSpectrum (spectrum, bins, sr);
    for (int b = 0; b < bins; ++b)
    {
        const float hz = (float) ((double) b * sr / (2.0 * (bins - 1)));
        const bool selected = hz <= frequencyLimit && (remap || (low <= high ? (hz >= low && hz <= high) : (hz <= high || hz >= low)));
        const auto dry = moduleDry[(size_t) b];
        spectrum[b] = selected ? dry + (spectrum[b] - dry) * mix : dry;
        if (! std::isfinite (spectrum[b].real()) || ! std::isfinite (spectrum[b].imag()))
            spectrum[b] = {};
    }
    if (context && (! slot.partial || mix < 1.0f || low > 0.0f || high < frequencyLimit)) context->singleSine = false;
}

void SpectralProcessor::processSliceChain (int rack, const std::complex<float>* source,
    std::complex<float>* destination, int bins, double sr, int audioChannel)
{
    juce::ignoreUnused (audioChannel);
    std::fill_n (destination, bins, std::complex<float> {});
    for (int slice = rack - 1; slice < numSplits; slice += 4)
    {
        if (sliceFirst[(size_t) slice] > sliceLast[(size_t) slice]) continue;
        std::fill (sliceBuffer.begin(), sliceBuffer.end(), std::complex<float> {});
        for (int b = sliceFirst[(size_t) slice]; b <= sliceLast[(size_t) slice]; ++b)
        {
            const auto& weight = individualWeights[(size_t) b];
            const float amount = weight.first == slice ? weight.firstWeight : (weight.second == slice ? weight.secondWeight : 0.0f);
            sliceBuffer[(size_t) b] = source[b] * amount;
        }
        const double centre = 20.0 * std::pow (std::max (40.0, sampleRate * 0.5) / 20.0, ((double) slice + 0.5) / numSplits);
        PartialProcessor::SliceContext context { slice, centre, centre, false };
        for (auto& slot : chains[(size_t) rack])
        {
            if (! slot.module || ! slot.enabled || slot.module->isTimeDomain()) continue;
            auto* module = slot.partial ? slot.module.get() : slot.sliceModules[(size_t) slice].get();
            if (module) processSlot (slot, *module, sliceBuffer.data(), bins, sr, &context);
        }
        for (int b = 0; b < bins; ++b)
        {
            const double real = (double) destination[b].real() + sliceBuffer[(size_t) b].real();
            const double imaginary = (double) destination[b].imag() + sliceBuffer[(size_t) b].imag();
            destination[b] = { (float) (std::isfinite (real) ? juce::jlimit (-1.0e12, 1.0e12, real) : 0.0),
                               (float) (std::isfinite (imaginary) ? juce::jlimit (-1.0e12, 1.0e12, imaginary) : 0.0) };
        }
    }
}

void SpectralProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();

    wetBuffer.makeCopyOf (buffer, true);
    inputGain.process (buffer);

    auto inBlock = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, (size_t) n);
    auto osBlock = oversampler.processUp (inBlock);
    const int osN = (int) osBlock.getNumSamples();
    const int bins = fft.getNumBins();

    if (splitWeightsDirty)
        recomputeSplitWeights (bins);

    const int solo = soloChannel.load();
    auto callback = [this, solo, sr = this->sampleRate * oversampleFactor]
                    (const std::complex<float>* spectrum, FFTEngine::Routes routes, int count)
    {
        for (int c = 0; c < 4; ++c)
        {
            auto* dest = routes[(size_t) c];
            double power = 0;
            for (int bin = 1; bin < count - 1; ++bin) power += std::norm (spectrum[bin] * splitWeights[(size_t) c][(size_t) bin]);
            preBandLevels[(size_t) c] = legacyNormalization
                ? (float) (std::sqrt (power * (16.0 / 3.0)) / (2 * (count - 1)))
                : (float) std::sqrt (power * bandPowerCalibration);
            if (! channelEnabled[(size_t) c] || (soloMode && c != solo)) { std::fill_n (dest, count, std::complex<float> {}); continue; }
            const bool full = soloMode && ! postPreview;
            if (sliceChains[(size_t) c] && ! full)
                processSliceChain (c + 1, spectrum, dest, count, sr, currentAudioChannel);
            else
            {
                for (int i = 0; i < count; ++i) dest[i] = spectrum[i] * (full ? 1.0f : splitWeights[(size_t) c][(size_t) i]);
                processChain (chains[(size_t) c + 1], dest, count, sr);
            }
        }
    };
    for (auto& chain : chains) for (auto& slot : chain) if (slot.partial) slot.partial->beginBlock();
    for (int c = 0; c < ch; ++c)
    {
        currentAudioChannel = c;
        for (auto& chain : chains) for (auto& slot : chain)
        {
            if (slot.module) slot.module->beginChannel (c);
            for (auto& clone : slot.sliceModules) if (clone) clone->beginChannel (c);
        }
        std::array<float*, 4> outputs {};
        for (size_t r = 0; r < 4; ++r) outputs[r] = routeAudio[r].getWritePointer (c);
        fft.processRouted (osBlock.getChannelPointer ((size_t) c), outputs, osN, c, callback);
    }
    osBlock.clear();
    for (size_t r = 0; r < 4; ++r)
    {
        float* pointers[2] {};
        for (int c = 0; c < ch; ++c) pointers[c] = routeAudio[r].getWritePointer (c);
        juce::AudioBuffer<float> route (pointers, ch, osN);
        const bool active = channelEnabled[r] && (! soloMode || (int) r == solo);
        if (active)
        {
            for (auto& slot : chains[r + 1]) if (slot.module && slot.enabled && slot.primaryPrepared && ! slot.resourceBlocked && slot.module->isTimeDomain())
            {
                slot.module->setTempo (tempo);
                slot.module->processBuffer (route, sampleRate * oversampleFactor);
            }
        }
        else
        {
            for (auto& slot : chains[r + 1]) if (slot.module && slot.module->isTimeDomain() && channelGain[r] > 0)
                slot.module->reset();
        }
        float peak = 0;
        // Short ramps avoid a discontinuity when toggling a channel. Once off,
        // its entire band (including reverb/delay tails) is silent.
        const float ramp = 1.0f / (float) (sampleRate * oversampleFactor * .005);
        for (int i = 0; i < osN; ++i)
        {
            channelGain[r] = active ? juce::jmin (1.0f, channelGain[r] + ramp) : juce::jmax (0.0f, channelGain[r] - ramp);
            for (int c = 0; c < ch; ++c)
            {
                float sample = route.getSample (c, i) * channelGain[r];
                sample = std::isfinite (sample) ? juce::jlimit (-8.0f, 8.0f, sample) : 0;
                osBlock.getChannelPointer ((size_t) c)[i] += sample; peak = juce::jmax (peak, std::abs (sample));
            }
        }
        channelLevels[r].store (active ? juce::jmax (peak, channelLevels[r].load() * .92f) : 0);
    }
    oversampler.processDown (inBlock);

    dryWet.process (wetBuffer, buffer, buffer);
    outputGain.process (buffer);

    for (int c = 0; c < ch; ++c)
    {
        auto* data = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (data[i]))
                data[i] = 0.0f;
            else
                data[i] = juce::jlimit (-8.0f, 8.0f, data[i]);
        }
    }
}

int SpectralProcessor::getLatencySamples() const noexcept
{
    const int fftLatencyBase = (fft.getLatencySamples() + oversampleFactor - 1) / oversampleFactor;
    return fftLatencyBase + oversampler.getLatencySamples();
}

} // namespace scrr::dsp

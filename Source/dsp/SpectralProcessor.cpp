// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectralProcessor.h"
#include "FrequencySplit.h"
#include "../parameters/ParamIDs.h"
#include <cmath>
#include <algorithm>

namespace scrr::dsp {

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
    displayOversample.store (oversampleFactor);
    for (auto& chain : chains)
        for (auto& slot : chain)
            if (slot.module)
                slot.module->prepare (sampleRate * oversampleFactor, bins);

    for (auto& buf : channelBuffers)
        buf.assign ((size_t) bins, std::complex<float> { 0.0f, 0.0f });

    splitWeightsDirty = true;

    dryWet.setWetDelay (getLatencySamples());
    pendingOversampleFactor = oversampleFactor;
    pendingFftSize = fftSize;
    pendingWindowType = windowType;
    pendingReconfigure = false;
    reset();
}

void SpectralProcessor::reconfigure()
{
    // Scale FFT size by oversample factor to maintain frequency resolution
    // at base sample rate. With 4x oversample and 2048 FFT, actual FFT = 8192.
    const int actualFftSize = fftSize * oversampleFactor;
    hopSize = actualFftSize / 4;
    fft.configure (actualFftSize, hopSize, windowType);
}

void SpectralProcessor::applyPendingReconfigure()
{
    if (! pendingReconfigure) return;

    const bool osChanged = (pendingOversampleFactor != oversampleFactor);
    oversampleFactor = pendingOversampleFactor;
    fftSize = pendingFftSize;
    windowType = pendingWindowType;

    if (osChanged)
        oversampler.prepare (sampleRate, maxBlockSize, numChannels, oversampleFactor);

    reconfigure();
    fft.prepare (sampleRate * oversampleFactor, maxBlockSize * oversampleFactor, numChannels);

    const int bins = fft.getNumBins();
    moduleDry.resize ((size_t) bins);
    displayOversample.store (oversampleFactor);
    for (auto& chain : chains)
        for (auto& slot : chain)
            if (slot.module)
                slot.module->prepare (sampleRate * oversampleFactor, bins);

    for (auto& buf : channelBuffers)
        buf.assign ((size_t) bins, std::complex<float> { 0.0f, 0.0f });

    splitWeightsDirty = true;
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
            if (slot.module)
                slot.module->reset();
}

void SpectralProcessor::updateGlobalParameters (juce::AudioProcessorValueTreeState& apvts)
{
    using namespace scrr::params;

    frequencyLimit = frequencyLimitValue (apvts);
    const int newOversample = oversampleFactorFromChoice (apvts);
    const int newFftSize    = fftSizeValue (apvts);
    const int newWindowIdx  = (int) apvts.getRawParameterValue (id::windowType)->load();
    const WindowType newWindow = [newWindowIdx]() {
        switch (newWindowIdx) {
            case 1:  return WindowType::Hamming;
            case 2:  return WindowType::Blackman;
            case 3:  return WindowType::Rectangular;
            default: return WindowType::Hann;
        }
    }();

    pendingOversampleFactor = newOversample;
    pendingFftSize = newFftSize;
    pendingWindowType = newWindow;
    pendingReconfigure = (pendingOversampleFactor != oversampleFactor
                       || pendingFftSize != fftSize
                       || pendingWindowType != windowType);

    dryWetMix = globalValue (id::drywet, apvts.getRawParameterValue (id::drywet)->load()) / 100.0f;
    bypass = apvts.getRawParameterValue (id::bypass)->load() > 0.5f;
    dryWet.setMix (bypass ? 0.0f : dryWetMix);
    inputGain.setGainDb  (globalValue (id::inputGain, apvts.getRawParameterValue (id::inputGain)->load()));
    outputGain.setGainDb (globalValue (id::outputGain, apvts.getRawParameterValue (id::outputGain)->load()));

    for (size_t c = 0; c < 4; ++c) channelEnabled[c] = apvts.getRawParameterValue (id::channelEnabled[c])->load() > .5f;
    soloMode = apvts.getRawParameterValue (id::freqSplitSolo)->load() > 0.5f;
    postPreview = apvts.getRawParameterValue (id::freqSplitPostPreview)->load() > 0.5f;

    const int newNumSplits = juce::jlimit (1, 64, (int) globalValue (id::freqSplitNumSplits, apvts.getRawParameterValue (id::freqSplitNumSplits)->load()));
    const float newCrossfade = globalValue (id::freqSplitCrossfade, apvts.getRawParameterValue (id::freqSplitCrossfade)->load());

    if (newNumSplits != numSplits
        || std::fabs (newCrossfade - crossfadePct) > 0.01f)
    {
        numSplits = newNumSplits;
        crossfadePct = newCrossfade;
        splitWeightsDirty = true;
    }
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
    auto& slots = getChain (channel);
    slots.clear();

    for (int i = 0; i < chainState.getNumChildren(); ++i)
    {
        auto child = chainState.getChild (i);
        const juce::String typeId = child.getProperty ("type", "");

        ModuleSlot slot;
        slot.typeId = typeId;
        slot.state = child.createCopy();
        slot.enabled = (bool) child.getProperty ("enabled", true);
        slot.module = createModule (typeId);

        if (slot.module)
        {
            const int bins = fft.getNumBins();
            slot.module->prepare (sampleRate * oversampleFactor, bins);
        }

        slots.push_back (std::move (slot));
    }
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
            if (slots[i].module)
                slots[i].module->updateParameters (slots[i].state);
        }
    }
}

void SpectralProcessor::setMacroMappings (const std::vector<scrr::params::MacroMapping>& mappings)
{
    globalMappings.clear();
    for (auto& chain : chains) for (auto& slot : chain) slot.mappings.clear();
    for (const auto& m : mappings)
    {
        if (m.channel == scrr::params::MacroMapping::modulationChannel) continue;
        if (m.channel == 0) { globalMappings.push_back (m); continue; }
        for (auto& slot : chains[(size_t) m.channel])
            if (slot.state["uid"].toString() == m.uid) { slot.mappings.push_back (m); break; }
    }
}
void SpectralProcessor::applyMacroValues (const scrr::params::ModulationValues& values)
{
    macroValues = values;
    for (auto& chain : chains) for (auto& slot : chain)
    {
        if (slot.mappings.empty() || ! slot.module) continue;
        for (size_t i = 0; i < slot.mappings.size(); ++i)
        {
            auto m = slot.mappings[i]; bool already = false;
            if (m.parameter.toString() == "rangeLow" || m.parameter.toString() == "rangeHigh") m.maximum = frequencyLimit;
            for (size_t j = 0; j < i; ++j) if (slot.mappings[j].parameter == m.parameter) { already = true; break; }
            if (already) continue;
            double sum = 0;
            for (const auto& route : slot.mappings) if (route.parameter == m.parameter && route.sourceEnabled) sum += route.offset (values[(size_t) route.valueIndex()]);
            slot.state.setProperty (m.parameter, m.fromBase ((double) slot.baseState[m.parameter], sum), nullptr);
        }
        slot.module->updateParameters (slot.state);
    }
}
float SpectralProcessor::globalValue (const juce::String& id, float base) const noexcept
{
    const scrr::params::MacroMapping* target {}; double sum = 0;
    for (const auto& m : globalMappings)
        if (m.parameter.toString() == id && m.sourceEnabled) { target = &m; sum += m.offset (macroValues[(size_t) m.valueIndex()]); }
    return target ? (float) target->fromBase (base, sum) : base;
}
void SpectralProcessor::recomputeSplitWeights (int numBins)
{
    for (auto& w : splitWeights)
        w.assign ((size_t) numBins, 0.0f);

    const FrequencySplit mapping (sampleRate, numSplits, crossfadePct);
    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto weights = mapping.weightsForBin (bin, numBins, sampleRate * oversampleFactor);
        for (size_t ch = 0; ch < splitWeights.size(); ++ch)
            splitWeights[ch][(size_t) bin] = weights[ch];
    }

    splitWeightsDirty = false;
}

void SpectralProcessor::processChain (std::vector<ModuleSlot>& chain,
    std::complex<float>* spectrum, int bins, double sr)
{
    for (auto& slot : chain)
    {
        if (! slot.module || ! slot.enabled || slot.module->isTimeDomain()) continue;
        const float mix = (float) slot.state.getProperty ("moduleMix", 100.0) * 0.01f;
        if (mix <= 0.0f) continue;
        const float low = (float) slot.state.getProperty ("rangeLow", 0.0);
        const float high = (float) slot.state.getProperty ("rangeHigh", 96000.0);
        std::copy (spectrum, spectrum + bins, moduleDry.begin());
        // Keep the out-of-range signal dry, and do not let it feed shifts or
        // other spectral transforms back into the permitted processing band.
        const double binHz = sr / (2.0 * (bins - 1));
        for (int b = 0; b < bins; ++b)
            if ((double) b * binHz > frequencyLimit) spectrum[b] = {};
        slot.module->processSpectrum (spectrum, bins, sr);
        for (int b = 0; b < bins; ++b)
        {
            const float hz = (float) ((double) b * sr / (2.0 * (bins - 1)));
            const bool selected = hz <= frequencyLimit && (low <= high ? (hz >= low && hz <= high) : (hz <= high || hz >= low));
            const auto dry = moduleDry[(size_t) b];
            spectrum[b] = selected ? dry + (spectrum[b] - dry) * mix : dry;
            if (! std::isfinite (spectrum[b].real()) || ! std::isfinite (spectrum[b].imag()))
                spectrum[b] = {};
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
            preBandLevels[(size_t) c] = (float) (std::sqrt (power * (16.0 / 3.0)) / (2 * (count - 1)));
            if (! channelEnabled[(size_t) c] || (soloMode && c != solo)) { std::fill_n (dest, count, std::complex<float> {}); continue; }
            const bool full = soloMode && ! postPreview;
            for (int i = 0; i < count; ++i) dest[i] = spectrum[i] * (full ? 1.0f : splitWeights[(size_t) c][(size_t) i]);
            processChain (chains[(size_t) c + 1], dest, count, sr);
        }
    };
    for (int c = 0; c < ch; ++c)
    {
        for (auto& chain : chains) for (auto& slot : chain) if (slot.module) slot.module->beginChannel (c);
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
            for (auto& slot : chains[r + 1]) if (slot.module && slot.enabled && slot.module->isTimeDomain())
                slot.module->processBuffer (route, sampleRate * oversampleFactor);
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

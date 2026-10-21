// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../parameters/MacroMapping.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace scrr::dsp {
class ModulationEngine
{
    struct Runtime { scrr::params::ModulationSource source, effective; std::vector<scrr::params::MacroMapping> mappings; double phase {}; float envelope {}, randomSmooth {}; bool smoothReady {}; };
public:
    void configure (const std::vector<scrr::params::ModulationSource>& sources, const std::vector<scrr::params::MacroMapping>& mappings = {})
    {
        std::vector<Runtime> next; next.reserve (sources.size());
        std::array<float, scrr::params::maxModulators> nextOutputs {}, nextPhases {};
        for (const auto& s : sources)
        {
            Runtime r; r.source = r.effective = s;
            for (const auto& m : mappings)
                if (m.channel == scrr::params::MacroMapping::modulationChannel && m.uid == s.uid)
                { r.mappings.push_back (m); r.mappings.back().bindResponseMappings (mappings); }
            for (size_t oldIndex = 0; oldIndex < runtime.size(); ++oldIndex)
            {
                const auto& old = runtime[oldIndex];
                if (old.source.uid != s.uid) continue;
                r.phase = old.phase; r.envelope = old.envelope; r.randomSmooth = old.randomSmooth; r.smoothReady = old.smoothReady;
                if (s.enabled)
                {
                    nextOutputs[next.size()] = outputs[oldIndex].load();
                    if (! s.envelope) nextPhases[next.size()] = phases[oldIndex].load();
                }
                break;
            }
            next.push_back (std::move (r));
        }
        runtime = std::move (next);
        processOrder.clear(); std::vector<bool> ready (runtime.size(), false);
        for (size_t pass = 0; pass < runtime.size(); ++pass)
            for (size_t i = 0; i < runtime.size(); ++i) if (! ready[i])
            {
                bool dependenciesReady = true;
                const auto check = [&] (const auto& route)
                { if (route.sourceUid.isNotEmpty() && route.sourceIndex >= 8 && (size_t) (route.sourceIndex - 8) < ready.size() && ! ready[(size_t) route.sourceIndex - 8]) dependenciesReady = false; };
                for (const auto& route : runtime[i].mappings) { check (route); for (const auto& response : route.responseMappings) check (response); }
                if (dependenciesReady) { ready[i] = true; processOrder.push_back (i); }
            }
        // Malformed external snapshots cannot feed a stale value around a
        // cycle. The editor rejects these routes before they reach runtime.
        for (size_t i = 0; i < ready.size(); ++i) if (! ready[i]) { nextOutputs[i] = 0; nextPhases[i] = 0; }
        // These UI-visible values follow the same stable UIDs as runtime state.
        // Capture all old slots before publishing, so moving one cannot overwrite
        // a later source's value. Followers and disabled sources have no phase.
        for (size_t i = 0; i < outputs.size(); ++i) { outputs[i].store (nextOutputs[i]); phases[i].store (nextPhases[i]); }
    }
    void prepare (double sr) { sampleRate = sr; wasPlaying = false; for (auto& r : runtime) { r.phase = 0; r.envelope = 0; r.randomSmooth = 0; r.smoothReady = false; } }
    void process (const juce::AudioBuffer<float>& input, int offset, int count, const std::array<float, 4>& bands,
                  double bpm, bool playing, double ppq, bool hasTimeline, const juce::AudioBuffer<float>* sidechain = nullptr,
                  const scrr::params::MacroValues& macros = {})
    {
        bpm = std::isfinite (bpm) ? juce::jlimit (1.0, 1000.0, bpm) : 120;
        scrr::params::ModulationValues values {}; std::copy (macros.begin(), macros.end(), values.begin());
        for (size_t i = 0; i < runtime.size(); ++i) values[i + 8] = outputs[i].load();
        for (const auto index : processOrder)
        {
            auto& r = runtime[index]; auto& s = r.effective;
            std::copy (r.source.points.begin(), r.source.points.end(), s.points.begin());
            for (size_t i = 0; i < r.mappings.size(); ++i)
            {
                const auto& m = r.mappings[i]; bool already = false;
                for (size_t j = 0; j < i; ++j) if (r.mappings[j].parameter == m.parameter) { already = true; break; }
                if (already) continue;
                double sum = 0;
                for (const auto& route : r.mappings)
                    if (route.parameter == m.parameter && route.sourceEnabled && route.valueIndex() >= 0 && (size_t) route.valueIndex() < values.size())
                        sum += route.offset (values[(size_t) route.valueIndex()], &values);
                if (m.coordinatePointId != 0)
                {
                    for (size_t point = 0; point < r.source.points.size(); ++point) if (r.source.points[point].id == m.coordinatePointId)
                    {
                        const auto base = r.source.points[point];
                        (m.coordinateXAxis ? s.points[point].x : s.points[point].y) = (float) m.fromBase (m.coordinateXAxis ? base.x : base.y, sum);
                    }
                }
                else s.setParameterValue (m.parameter, m.fromBase (r.source.parameterValue (m.parameter), sum));
            }
            scrr::params::TransferCurve::constrainCoordinates (s.points.data(), s.points.size(), false, false, r.source.points.data());
            if (! s.enabled) { outputs[index].store (0); phases[index].store (0); values[index + 8] = 0; continue; }
            float output = 0;
            if (s.envelope)
            {
                const float gain = juce::Decibels::decibelsToGain (s.gain);
                const float attack = (float) std::exp (-1 / (sampleRate * s.attack * .001));
                const float release = (float) std::exp (-1 / (sampleRate * s.release * .001));
                for (int i = 0; i < count; ++i)
                {
                    float level = s.input >= 1 && s.input <= 4 ? bands[(size_t) s.input - 1] : 0;
                    const auto* audio = s.input == 0 ? &input : s.input == 5 ? sidechain : nullptr;
                    if (audio && offset + i < audio->getNumSamples()) for (int c = 0; c < juce::jmin (2, audio->getNumChannels()); ++c) level = juce::jmax (level, std::abs (audio->getSample (c, offset + i)));
                    level = std::isfinite (level) ? juce::jlimit (0.0f, 8.0f, level * gain) : 0;
                    const float coefficient = level > r.envelope ? attack : release;
                    r.envelope = coefficient * r.envelope + (1 - coefficient) * level;
                }
                output = juce::jlimit (0.0f, 1.0f, r.envelope);
                phases[index].store (0);
            }
            else
            {
                const double cycleBeats = scrr::params::ModulationSource::beats[(size_t) s.division];
                if (s.retrigger && playing && ! wasPlaying) { r.phase = 0; r.smoothReady = false; }
                if (s.sync && ! s.retrigger && playing && hasTimeline) r.phase = ppq / cycleBeats;
                const double rate = s.sync ? bpm / (60 * cycleBeats) : s.rate;
                const double displayPhase = r.phase + s.phase / 360.0;
                output = s.valueAt (displayPhase);
                if (s.random)
                {
                    // Smooth is a time constant in milliseconds, independent of
                    // Rate/Sync. Zero keeps both legacy random shapes unchanged.
                    if (! r.smoothReady || s.smooth <= 0) r.randomSmooth = output;
                    else if (count > 0)
                    {
                        const float amount = (float) -std::expm1 (-(double) count / (sampleRate * s.smooth * .001));
                        r.randomSmooth += amount * (output - r.randomSmooth);
                    }
                    r.smoothReady = true; output = juce::jlimit (0.0f, 1.0f, r.randomSmooth);
                }
                phases[index].store ((float) (displayPhase - std::floor (displayPhase)));
                r.phase += rate * count / sampleRate;
                if (! s.random) r.phase -= std::floor (r.phase);
            }
            outputs[index].store (output);
            values[index + 8] = output;
        }
        wasPlaying = playing;
    }
    float value (int index) const noexcept { return index >= 0 && index < (int) outputs.size() ? outputs[(size_t) index].load() : 0; }
    float phase (int index) const noexcept { return index >= 0 && index < (int) phases.size() ? phases[(size_t) index].load() : 0; }
private:
    std::vector<Runtime> runtime;
    std::vector<size_t> processOrder;
    std::array<std::atomic<float>, scrr::params::maxModulators> outputs {}, phases {};
    double sampleRate { 44100 }; bool wasPlaying {};
};
}

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
        for (const auto& s : sources)
        {
            Runtime r; r.source = r.effective = s;
            for (const auto& m : mappings)
                if (m.channel == scrr::params::MacroMapping::modulationChannel && m.uid == s.uid && m.sourceUid.isEmpty()) r.mappings.push_back (m);
            for (const auto& old : runtime) if (old.source.uid == s.uid) { r.phase = old.phase; r.envelope = old.envelope; r.randomSmooth = old.randomSmooth; r.smoothReady = old.smoothReady; break; }
            next.push_back (std::move (r));
        }
        runtime = std::move (next);
        for (size_t i = runtime.size(); i < outputs.size(); ++i) { outputs[i].store (0); phases[i].store (0); }
    }
    void prepare (double sr) { sampleRate = sr; wasPlaying = false; for (auto& r : runtime) { r.phase = 0; r.envelope = 0; r.randomSmooth = 0; r.smoothReady = false; } }
    void process (const juce::AudioBuffer<float>& input, int offset, int count, const std::array<float, 4>& bands,
                  double bpm, bool playing, double ppq, bool hasTimeline, const juce::AudioBuffer<float>* sidechain = nullptr,
                  const scrr::params::MacroValues& macros = {})
    {
        bpm = std::isfinite (bpm) ? juce::jlimit (1.0, 1000.0, bpm) : 120;
        for (size_t index = 0; index < runtime.size(); ++index)
        {
            auto& r = runtime[index]; auto& s = r.effective;
            for (size_t i = 0; i < r.mappings.size(); ++i)
            {
                const auto& m = r.mappings[i]; bool already = false;
                for (size_t j = 0; j < i; ++j) if (r.mappings[j].parameter == m.parameter) { already = true; break; }
                if (already) continue;
                double sum = 0;
                for (const auto& route : r.mappings) if (route.parameter == m.parameter) sum += route.offset (macros[(size_t) route.macro]);
                s.setParameterValue (m.parameter, m.fromBase (r.source.parameterValue (m.parameter), sum));
            }
            if (! s.enabled) { outputs[index].store (0); continue; }
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
        }
        wasPlaying = playing;
    }
    float value (int index) const noexcept { return index >= 0 && index < (int) outputs.size() ? outputs[(size_t) index].load() : 0; }
    float phase (int index) const noexcept { return index >= 0 && index < (int) phases.size() ? phases[(size_t) index].load() : 0; }
private:
    std::vector<Runtime> runtime;
    std::array<std::atomic<float>, scrr::params::maxModulators> outputs {}, phases {};
    double sampleRate { 44100 }; bool wasPlaying {};
};
}

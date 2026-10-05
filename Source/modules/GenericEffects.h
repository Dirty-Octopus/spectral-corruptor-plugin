// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "SpectralModule.h"
#include "GenericCurves.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace scrr::dsp {
inline float effectValue (const juce::ValueTree& state, const char* id, float fallback, float low, float high)
{
    const float value = (float) state.getProperty (id, fallback);
    return std::isfinite (value) ? juce::jlimit (low, high, value) : fallback;
}
class TimeEffect : public SpectralModule
{
public:
    bool isTimeDomain() const noexcept override { return true; }
    void processSpectrum (std::complex<float>*, int, double) override {}
protected:
    static float safe (float x) noexcept { return std::isfinite (x) ? juce::jlimit (-8.0f, 8.0f, x) : 0; }
};
// Fractional stereo delay. All storage is allocated at prepare, never per sample/block.
class GenericDelay : public TimeEffect
{
public:
    const char* getName() const noexcept override { return "Delay"; }
    void prepare (double sr, int) override
    {
        rate = sr;
        for (auto& channel : ring) channel.assign ((size_t) std::ceil (sr * 2.01) + 4, 0);
        smoothing = (float) (1 - std::exp (-1 / (sr * .025))); reset();
    }
    void reset() override { for (auto& c : ring) std::fill (c.begin(), c.end(), 0); write = 0; currentTime = timeMs; }
    void updateParameters (const juce::ValueTree& s) override
    {
        mode = (int) effectValue (s, "mode", 0, 0, 2);
        timeMs = effectValue (s, "time", 300, 1, 2000);
        feedback = effectValue (s, "feedback", 35, 0, 95) * .01f;
        mix = effectValue (s, "mix", 30, 0, 100) * .01f;
    }
    void processBuffer (juce::AudioBuffer<float>& b, double) override
    {
        const int channels = juce::jmin (2, b.getNumChannels());
        if (ring[0].empty()) return;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            currentTime += smoothing * (timeMs - currentTime);
            std::array<float, 2> input {}, delayed {}, wet {};
            for (int c = 0; c < channels; ++c)
            {
                input[(size_t) c] = safe (b.getSample (c, i));
                delayed[(size_t) c] = read (c, currentTime);
                wet[(size_t) c] = mode == 1 ? .55f * read (c, currentTime * .333333f) + .3f * read (c, currentTime * .666667f) + .15f * delayed[(size_t) c] : delayed[(size_t) c];
            }
            for (int c = 0; c < channels; ++c)
            {
                const int other = channels == 2 ? 1 - c : c;
                const float feed = mode == 2 ? delayed[(size_t) other] : delayed[(size_t) c];
                // Ping-pong injects mono input on the left, then alternates echoes.
                const float source = mode == 2 && channels == 2 ? (c == 0 ? (input[0] + input[1]) * .5f : 0) : input[(size_t) c];
                ring[(size_t) c][write] = safe (source + feed * feedback);
                b.setSample (c, i, safe (input[(size_t) c] * (1 - mix) + wet[(size_t) c] * mix));
            }
            write = (write + 1) % ring[0].size();
        }
    }
private:
    float read (int c, float ms) const noexcept
    {
        const auto& data = ring[(size_t) c];
        double index = (double) write - juce::jlimit (1.0, (double) data.size() - 2, (double) ms * rate * .001);
        if (index < 0) index += (double) data.size();
        const size_t a = (size_t) index, next = (a + 1) % data.size(); const float t = (float) (index - (double) a);
        return data[a] + t * (data[next] - data[a]);
    }
    std::array<std::vector<float>, 2> ring;
    size_t write {}; double rate { 44100 };
    int mode {}; float timeMs { 300 }, currentTime { 300 }, smoothing {}, feedback { .35f }, mix { .3f };
};

// Small stereo feedback-delay network. Topologies are voiced separately for
// dense plate, longer hall and short room responses; no convolution assets.
class GenericReverb : public TimeEffect
{
public:
    const char* getName() const noexcept override { return "Reverb"; }
    void prepare (double sr, int) override
    {
        rate = sr;
        for (auto& line : lines) line.assign ((size_t) std::ceil (sr * .18) + 4, 0);
        for (auto& channel : predelay) channel.assign ((size_t) std::ceil (sr * .201) + 4, 0);
        reset(); coefficients();
    }
    void reset() override
    {
        for (auto& line : lines) std::fill (line.begin(), line.end(), 0);
        for (auto& channel : predelay) std::fill (channel.begin(), channel.end(), 0);
        damp.fill (0); heads.fill (0); preHead = 0;
    }
    void updateParameters (const juce::ValueTree& s) override
    {
        const int nextMode = (int) effectValue (s, "mode", 0, 0, 2);
        const float nextSize = effectValue (s, "size", 60, 0, 100) * .01f;
        decay = effectValue (s, "decay", 2.5f, .1f, 12);
        damping = effectValue (s, "damping", 45, 0, 100) * .01f;
        preMs = effectValue (s, "predelay", 20, 0, 200);
        mix = effectValue (s, "mix", 25, 0, 100) * .01f;
        // Length changes reset only this reverb's tail, avoiding invalid history.
        if (nextMode != mode) { mode = nextMode; reset(); }
        size = nextSize;
        coefficients();
    }
    void processBuffer (juce::AudioBuffer<float>& b, double) override
    {
        if (lines[0].empty()) return;
        const int channels = juce::jmin (2, b.getNumChannels());
        const float pole = (float) std::exp (-juce::MathConstants<double>::twoPi * (18000 - damping * 17000) / rate);
        const size_t preSamples = (size_t) (preMs * .001 * rate);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float input[2] { safe (b.getSample (0, i)), safe (b.getSample (channels - 1, i)) };
            float source[2] {};
            for (size_t c = 0; c < 2; ++c)
            {
                predelay[c][preHead] = input[c];
                source[c] = predelay[c][(preHead + predelay[c].size() - preSamples) % predelay[c].size()];
            }
            std::array<float, 8> taps {}; float sum = 0;
            for (size_t j = 0; j < 8; ++j)
            { taps[j] = lines[j][heads[j]]; damp[j] = taps[j] * (1 - pole) + damp[j] * pole; sum += damp[j]; }
            float wet[2] {};
            for (size_t j = 0; j < 8; ++j)
            {
                // Householder matrix is energy preserving; RT60 gains are < 1.
                const float reflected = damp[j] - sum * .25f;
                const float injection = (j % 2 == 0 ? source[0] : source[1]) * (j < 4 ? .25f : -.25f);
                lines[j][heads[j]] = safe (injection + reflected * gains[j]);
                heads[j] = (heads[j] + 1) % lengths[j];
                wet[j % 2] += taps[j] * (j < 4 ? .7f : -.7f);
            }
            preHead = (preHead + 1) % predelay[0].size();
            for (int c = 0; c < channels; ++c) b.setSample (c, i, safe (input[c] * (1 - mix) + wet[c] * mix));
        }
    }
private:
    void coefficients()
    {
        if (lines[0].empty()) return;
        static constexpr double times[3][8] {
            { .0131, .0179, .0233, .0277, .0319, .0371, .0413, .0479 },
            { .0311, .0373, .0437, .0533, .0617, .0719, .0833, .0971 },
            { .0079, .0113, .0137, .0173, .0199, .0239, .0293, .0317 } };
        for (size_t j = 0; j < 8; ++j)
        {
            lengths[j] = juce::jlimit ((size_t) 2, lines[j].size(), (size_t) (times[mode][j] * (.55 + size * 1.1) * rate));
            heads[j] %= lengths[j];
            gains[j] = (float) std::pow (.001, (double) lengths[j] / (rate * decay));
        }
    }
    std::array<std::vector<float>, 8> lines;
    std::array<std::vector<float>, 2> predelay;
    std::array<size_t, 8> heads {}, lengths {};
    std::array<float, 8> gains {}, damp {};
    size_t preHead {}; double rate { 44100 };
    int mode {}; float size { .6f }, decay { 2.5f }, damping { .45f }, preMs { 20 }, mix { .25f };
};

class GenericCompressor : public TimeEffect
{
public:
    const char* getName() const noexcept override { return "Compressor"; }
    void prepare (double sr, int) override { rate = sr; reset(); }
    void reset() override { envelope = 0; }
    void updateParameters (const juce::ValueTree& s) override
    {
        threshold = effectValue (s, "threshold", -18, -60, 0); ratio = effectValue (s, "ratio", 4, 1, 20);
        attack = effectValue (s, "attack", 10, .1f, 200); release = effectValue (s, "release", 100, 10, 2000);
        knee = effectValue (s, "knee", 6, 0, 24); makeup = juce::Decibels::decibelsToGain (effectValue (s, "makeup", 0, 0, 24));
        mix = effectValue (s, "mix", 100, 0, 100) * .01f;
    }
    void processBuffer (juce::AudioBuffer<float>& b, double) override
    {
        const float rise = (float) std::exp (-1 / (rate * attack * .001)), fall = (float) std::exp (-1 / (rate * release * .001));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float peak = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) peak = juce::jmax (peak, std::abs (safe (b.getSample (c, i))));
            const float coefficient = peak > envelope ? rise : fall;
            envelope = coefficient * envelope + (1 - coefficient) * peak;
            const float reduction = compressorReduction (juce::Decibels::gainToDecibels (envelope, -120.0f), threshold, ratio, knee);
            const float gain = (1 - mix) + mix * makeup * juce::Decibels::decibelsToGain (reduction);
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, safe (b.getSample (c, i) * gain));
        }
    }
private:
    double rate { 44100 }; float envelope {}, threshold { -18 }, ratio { 4 }, attack { 10 }, release { 100 }, knee { 6 }, makeup { 1 }, mix { 1 };
};
class GenericDistortion : public TimeEffect
{
public:
    const char* getName() const noexcept override { return "Distortion"; }
    void prepare (double sr, int) override { rate = sr; reset(); }
    void reset() override { toneState.fill (0); dcIn.fill (0); dcOut.fill (0); }
    void updateParameters (const juce::ValueTree& s) override
    {
        mode = (int) effectValue (s, "mode", 0, 0, 3);
        drive = juce::Decibels::decibelsToGain (effectValue (s, "drive", 6, 0, 36));
        bias = effectValue (s, "bias", 0, -1, 1); tone = effectValue (s, "tone", 16000, 200, 20000);
        output = juce::Decibels::decibelsToGain (effectValue (s, "output", -6, -36, 12));
        mix = effectValue (s, "mix", 100, 0, 100) * .01f;
    }
    void processBuffer (juce::AudioBuffer<float>& b, double) override
    {
        const float pole = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin ((double) tone, rate * .45) / rate);
        const float dcPole = (float) std::exp (-juce::MathConstants<double>::twoPi * 15 / rate);
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c) for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float dry = safe (b.getSample (c, i)), x = dry * drive + bias;
            const float wet = distortionShape (x, mode);
            auto& z = toneState[(size_t) c]; z = wet * (1 - pole) + z * pole;
            const float filtered = z - dcIn[(size_t) c] + dcPole * dcOut[(size_t) c];
            dcIn[(size_t) c] = z; dcOut[(size_t) c] = filtered;
            b.setSample (c, i, safe (dry * (1 - mix) + filtered * output * mix));
        }
    }
private:
    int mode {}; double rate { 44100 }; float drive { 2 }, bias {}, tone { 16000 }, output { .5f }, mix { 1 };
    std::array<float, 2> toneState {}, dcIn {}, dcOut {};
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "SpectralModule.h"
#include <cmath>

namespace scrr::dsp {

class UtilityModule : public SpectralModule
{
public:
    bool isTimeDomain() const noexcept override { return true; }
    const char* getName() const noexcept override { return "Utility"; }

    void updateParameters (const juce::ValueTree& state) override
    {
        enabled = (bool) state.getProperty ("enabled", false);
        invert = (int) state.getProperty ("invert", 0);
        channelMode = (int) state.getProperty ("mode", 0);
        swap = (bool) state.getProperty ("swap", false);
        widthPct = (float) state.getProperty ("width", 100.0f);
        mono = (bool) state.getProperty ("mono", false);
        gainDb = (float) state.getProperty ("gain", 0.0f);
        balance = (float) state.getProperty ("balance", 0.0f);
    }

    void processSpectrum (std::complex<float>*, int, double) override {}

    void processBuffer (juce::AudioBuffer<float>& buffer, double sampleRate) override
    {
        if (! enabled) return;
        juce::ignoreUnused (sampleRate);
        const int ch = buffer.getNumChannels();
        const int n = buffer.getNumSamples();

        // Swap L/R
        if (swap && ch >= 2)
        {
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
                std::swap (l[i], r[i]);
        }

        // Channel mode: Left Only / Right Only
        if (channelMode == 1 && ch >= 2) // Left Only
        {
            auto* r = buffer.getWritePointer (1);
            auto* l = buffer.getReadPointer (0);
            std::memcpy (r, l, (size_t) n * sizeof (float));
        }
        else if (channelMode == 2 && ch >= 2) // Right Only
        {
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getReadPointer (1);
            std::memcpy (l, r, (size_t) n * sizeof (float));
        }

        // Phase invert
        if (invert != 0 && ch >= 1)
        {
            if (invert == 1 || invert == 3) // Left or Both
            {
                auto* l = buffer.getWritePointer (0);
                for (int i = 0; i < n; ++i) l[i] = -l[i];
            }
            if ((invert == 2 || invert == 3) && ch >= 2) // Right or Both
            {
                auto* r = buffer.getWritePointer (1);
                for (int i = 0; i < n; ++i) r[i] = -r[i];
            }
        }

        // Mono
        if (mono && ch >= 2)
        {
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float m = (l[i] + r[i]) * 0.5f;
                l[i] = m;
                r[i] = m;
            }
        }

        // Width (Mid/Side)
        if (ch >= 2 && std::fabs (widthPct - 100.0f) > 0.01f)
        {
            const float w = widthPct / 100.0f;
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float mid = (l[i] + r[i]) * 0.5f;
                const float side = (l[i] - r[i]) * 0.5f;
                l[i] = mid + side * w;
                r[i] = mid - side * w;
            }
        }

        // Gain
        if (std::fabs (gainDb) > 0.001f)
        {
            const float gain = std::pow (10.0f, gainDb / 20.0f);
            for (int c = 0; c < ch; ++c)
            {
                auto* data = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                    data[i] *= gain;
            }
        }

        // Balance
        if (std::fabs (balance) > 0.01f && ch >= 2)
        {
            const float b = balance / 100.0f;
            const float leftGain = juce::jlimit (0.0f, 1.0f, 1.0f - b);
            const float rightGain = juce::jlimit (0.0f, 1.0f, 1.0f + b);
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                l[i] *= leftGain;
                r[i] *= rightGain;
            }
        }
    }

private:
    int invert { 0 };
    int channelMode { 0 };
    bool swap { false };
    float widthPct { 100.0f };
    bool mono { false };
    float gainDb { 0.0f };
    float balance { 0.0f };
    bool enabled { false };
};

} // namespace scrr::dsp

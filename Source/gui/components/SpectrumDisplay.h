// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Theme.h"
#include "SpectrumScale.h"
#include "../../PluginProcessor.h"
#include <vector>

namespace scrr::gui {
class SpectrogramDisplay : public juce::Component, private juce::Timer
{
public:
    SpectrogramDisplay (SpectralCrrptProcessor&, bool input);
    ~SpectrogramDisplay() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void setRange (float low, float high, bool active = true)
    { lowHz = low; highHz = high; rangeColour = active ? Theme::yellow() : Theme::inactive(); repaint(); }
    void setPaused (bool value) { paused = value; repaint(); }
private:
    void timerCallback() override;
    void updateChannelBands();
    SpectralCrrptProcessor& processor;
    const bool input;
    bool paused {};
    double spectrumRate { 44100.0 };
    float lowHz {}, highHz { 96000 }, hoverHz {};
    juce::Colour rangeColour { Theme::yellow() };
    std::vector<float> magnitudes;
    juce::Image history;
    juce::Image channelBands;
    struct BandRegion { int channel, left, right; };
    std::vector<BandRegion> bandRegions;
    double bandHostRate {}, bandFftRate {};
    int bandSlices {}, bandBins {};
    float bandCrossfade { -1 };
    juce::Rectangle<int> plot;
};
} // namespace scrr::gui

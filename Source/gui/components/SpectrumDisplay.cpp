// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "SpectrumDisplay.h"
#include "../../dsp/FrequencySplit.h"
#include <cmath>
#include <cstring>
namespace scrr::gui {
SpectrogramDisplay::SpectrogramDisplay (SpectralCrrptProcessor& p, bool isInput) : processor (p), input (isInput)
{
    setComponentID (input ? "input-spectrum" : "output-spectrum");
    setOpaque (true); startTimerHz (24);
}
void SpectrogramDisplay::resized()
{
    plot = getLocalBounds().withTrimmedTop (30).withTrimmedBottom (24).reduced (10, 0);
    const int w = juce::jlimit (64, 640, plot.getWidth()), h = juce::jlimit (32, 180, plot.getHeight());
    if (history.getWidth() != w || history.getHeight() != h)
    {
        // History is rewritten from the CPU every tick. Keep it in software
        // storage so native rendering cannot retain the mutable pixel buffer
        // (CoreGraphics) or depend on a GPU bitmap surviving device loss.
        history = history.isNull() ? juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType()) : history.rescaled (w, h);
    }
}
void SpectrogramDisplay::timerCallback()
{
    if (! isShowing()) return;
    updateChannelBands();
    if (paused) return;
    if (! processor.getProcessor().copyLastMagnitudes (magnitudes, &spectrumRate, input) || magnitudes.size() < 2 || history.isNull()) return;
    const int w = history.getWidth(), h = history.getHeight();
    {
        juce::Image::BitmapData bitmap (history, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < h - 1; ++y)
            std::memcpy (bitmap.getLinePointer (y), bitmap.getLinePointer (y + 1), (size_t) bitmap.lineStride);
        const float normalise = 4.0f / (float) (2 * (magnitudes.size() - 1));
        for (int x = 0; x < w; ++x)
        {
            const float frequency = SpectrumScale::frequency ((float) x / (float) (w - 1));
            const int bin = SpectrumScale::bin (frequency, (int) magnitudes.size(), spectrumRate);
            const float db = juce::Decibels::gainToDecibels (magnitudes[(size_t) bin] * normalise, -96.0f);
            const float t = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 84.0f);
            auto colour = t < .45f ? Theme::field().interpolatedWith (Theme::blue(), t / .45f)
                         : t < .8f ? Theme::blue().interpolatedWith (Theme::yellow(), (t - .45f) / .35f)
                         : Theme::yellow().interpolatedWith (Theme::red(), (t - .8f) / .2f);
            bitmap.setPixelColour (x, h - 1, colour.withAlpha (juce::jlimit (0.0f, 1.0f, t * 2.0f)));
        }
    } // Finish pixel writes before scheduling a paint or uploading a native image.
    repaint();
}
void SpectrogramDisplay::mouseDown (const juce::MouseEvent&) { paused = ! paused; repaint(); }
void SpectrogramDisplay::mouseMove (const juce::MouseEvent& event)
{
    const float t = juce::jlimit (0.0f, 1.0f, (float) (event.x - plot.getX()) / (float) juce::jmax (1, plot.getWidth()));
    hoverHz = SpectrumScale::frequency (t); repaint();
}
void SpectrogramDisplay::mouseExit (const juce::MouseEvent&) { hoverHz = 0; repaint(); }
void SpectrogramDisplay::updateChannelBands()
{
    const auto& state = processor.getAPVTS();
    const int slices = (int) processor.getEffectiveValue (0, {}, scrr::params::id::freqSplitNumSplits,
        state.getRawParameterValue (scrr::params::id::freqSplitNumSplits)->load());
    const float crossfade = (float) processor.getEffectiveValue (0, {}, scrr::params::id::freqSplitCrossfade,
        state.getRawParameterValue (scrr::params::id::freqSplitCrossfade)->load());
    const double hostRate = processor.getProcessor().getSampleRate();
    const int factor = scrr::params::oversampleFactorFromChoice (state);
    const int bins = magnitudes.size() >= 2 ? (int) magnitudes.size() : scrr::params::fftSizeValue (state) * factor / 2 + 1;
    const double fftRate = magnitudes.size() >= 2 ? spectrumRate : hostRate * factor;
    if (slices <= 1 || plot.isEmpty())
    {
        if (! channelBands.isNull()) { channelBands = {}; bandRegions.clear(); repaint(); }
        bandSlices = 0; return;
    }
    if (bandSlices == slices && bandBins == bins && juce::approximatelyEqual (bandHostRate, hostRate) && juce::approximatelyEqual (bandFftRate, fftRate)
        && juce::approximatelyEqual (bandCrossfade, crossfade) && channelBands.getWidth() == plot.getWidth() && channelBands.getHeight() == plot.getHeight()) return;
    bandSlices = slices; bandBins = bins; bandHostRate = hostRate; bandFftRate = fftRate; bandCrossfade = crossfade;
    channelBands = juce::Image (juce::Image::RGB, plot.getWidth(), plot.getHeight(), false);
    repaint();
    bandRegions.clear(); juce::Graphics g (channelBands);
    const scrr::dsp::FrequencySplit mapping (hostRate, slices, crossfade);
    const int columns = history.getWidth();
    for (int x = 0; x < columns; ++x)
    {
        const auto frequency = SpectrumScale::frequency ((float) x / (float) (columns - 1));
        const auto weights = mapping.weightsForBin (SpectrumScale::bin (frequency, bins, fftRate), bins, fftRate);
        float red = 0, green = 0, blue = 0; int strongest = 0;
        for (int ch = 0; ch < 4; ++ch)
        {
            const auto colour = Theme::channelColour (ch); const auto weight = weights[(size_t) ch];
            red += colour.getFloatRed() * weight; green += colour.getFloatGreen() * weight; blue += colour.getFloatBlue() * weight;
            if (weight > weights[(size_t) strongest]) strongest = ch;
        }
        const auto colour = juce::Colour::fromFloatRGBA (red, green, blue, 1);
        const int left = x * plot.getWidth() / columns, right = (x + 1) * plot.getWidth() / columns;
        if (bandRegions.empty() || bandRegions.back().channel != strongest) bandRegions.push_back ({ strongest, left, right });
        else bandRegions.back().right = right;
        juce::ColourGradient tint (Theme::field().interpolatedWith (colour, .18f), 0, 3,
                                  Theme::field(), 0, 3 + (float) plot.getHeight() * .58f, false);
        tint.addColour (.32, Theme::field().interpolatedWith (colour, .07f));
        g.setGradientFill (tint); g.fillRect (left, 3, right - left, plot.getHeight() - 3);
        g.setColour (colour); g.fillRect (left, 0, right - left, 3);
    }
}
void SpectrogramDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::field());
    auto title = getLocalBounds().removeFromTop (27); g.setColour (Theme::blue()); g.fillRect (title);
    Theme::text (g, input ? "01 / INPUT SPECTRUM" : "02 / OUTPUT SPECTRUM", title.reduced (10, 0), 12, Theme::white(), true);
    Theme::text (g, paused ? "PAUSED" : "LIVE / L", title.reduced (10, 0), 10, Theme::white(), false, juce::Justification::centredRight);
    const auto position = [this] (float hz) { return (float) plot.getX() + (float) plot.getWidth() * SpectrumScale::position (hz); };
    updateChannelBands();
    if (! channelBands.isNull()) g.drawImageAt (channelBands, plot.getX(), plot.getY());
    if (! history.isNull()) g.drawImage (history, plot.toFloat());
    // Redraw the pure colour strip over spectrum history; keep the dark fade behind it.
    if (! channelBands.isNull())
    {
        g.drawImage (channelBands, plot.getX(), plot.getY(), plot.getWidth(), 3, 0, 0, plot.getWidth(), 3);
        for (const auto& band : bandRegions)
            if (band.right - band.left >= 35)
                Theme::text (g, "CH " + juce::String (band.channel + 1),
                    { plot.getX() + band.left + 4, plot.getY() + 5, band.right - band.left - 8, 14 }, 9,
                    band.channel == 2 ? Theme::white() : Theme::channelColour (band.channel), true);
    }
    for (float hz : { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f })
    {
        const int x = (int) position (hz); g.setColour (Theme::line().withAlpha (.5f)); g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
        auto label = hz < 1000 ? juce::String ((int) hz) : juce::String ((int) hz / 1000) + "k";
        Theme::text (g, label, { juce::jlimit (2, getWidth() - 32, x - 15), plot.getBottom() + 3, 32, 18 }, 10, Theme::dim(), false, juce::Justification::centred);
    }
    const float limit = processor.getFrequencyLimit();
    if (lowHz > 20 || highHz < 20000 || limit < 20000)
    {
        const auto highlight = [&] (float from, float to)
        {
            to = juce::jmin (to, limit);
            if (to <= from) return;
            const float a = position (from), b = position (to);
            g.setColour (rangeColour.withAlpha (.12f));
            g.fillRect (a, (float) plot.getY(), b - a, (float) plot.getHeight());
            g.setColour (rangeColour);
            g.drawVerticalLine ((int) a, (float) plot.getY(), (float) plot.getBottom());
            g.drawVerticalLine ((int) b, (float) plot.getY(), (float) plot.getBottom());
        };
        if (lowHz <= highHz) highlight (lowHz, highHz);
        else { highlight (0, highHz); highlight (lowHz, limit); }
    }
    if (hoverHz > 0)
    {
        const int midi = (int) std::round (69.0 + 12.0 * std::log2 (hoverHz / 440.0f));
        auto readout = juce::String (hoverHz, 1) + " Hz  /  " + juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, midi), true, true, 3);
        if (bandSlices > 1 && bandCrossfade > 0)
        {
            const scrr::dsp::FrequencySplit mapping (bandHostRate, bandSlices, bandCrossfade);
            const auto weights = mapping.weightsForBin (SpectrumScale::bin (hoverHz, bandBins, bandFftRate), bandBins, bandFftRate);
            for (int ch = 0; ch < 4; ++ch)
                if (weights[(size_t) ch] > .005f)
                    readout += "  /  CH " + juce::String (ch + 1) + " " + juce::String ((int) std::round (weights[(size_t) ch] * 100)) + "%";
        }
        else if (bandSlices > 1)
        {
            const int x = (int) (SpectrumScale::position (hoverHz) * (float) (plot.getWidth() - 1));
            for (const auto& band : bandRegions)
                if (x >= band.left && x < band.right)
                {
                    const float low = SpectrumScale::frequency ((float) band.left / (float) plot.getWidth());
                    const float high = SpectrumScale::frequency ((float) band.right / (float) plot.getWidth());
                    readout += "  /  CH " + juce::String (band.channel + 1) + "  " + juce::String ((int) low) + "-" + juce::String ((int) high) + " Hz";
                    break;
                }
        }
        const int width = juce::jmin (plot.getWidth() - 10, bandSlices > 1 ? 380 : 174);
        g.setColour (Theme::yellow()); g.fillRect (plot.getX() + 5, plot.getY() + 5, width, 22);
        Theme::text (g, readout, { plot.getX() + 11, plot.getY() + 5, width - 12, 22 }, 11, Theme::black(), true);
    }
    g.setColour (Theme::blue()); g.drawRect (getLocalBounds(), 2);
}
} // namespace scrr::gui

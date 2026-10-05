// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "FFTEngine.h"

namespace scrr::dsp {

FFTEngine::FFTEngine()
{
    configure (2048, 512, WindowType::Hann);
}

void FFTEngine::configure (int newFftSize, int newHopSize, WindowType wt)
{
    jassert (newFftSize >= kMinFFTSize && newFftSize <= kMaxFFTSize);
    jassert ((newFftSize & (newFftSize - 1)) == 0 && "FFT size must be power of 2");
    jassert (newHopSize > 0 && newFftSize % newHopSize == 0 && "FFT size must be multiple of hop");

    fftSize    = newFftSize;
    hopSize    = newHopSize;
    windowType = wt;

    int order = 0;
    for (int n = fftSize; n > 1; n >>= 1) ++order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    updateWindow();

    const int bins = fftSize / 2 + 1;
    fftReal.assign  ((size_t) (2 * fftSize), 0.0f); // 2x for in-place real FFT
    spectrum.assign ((size_t) bins, std::complex<float>{});
    lastMag.assign  ((size_t) bins, 0.0f);
    for (auto& route : routedSpectra) route.assign ((size_t) bins, {});
    magnitudeBuffer.resize ((size_t) bins);
    inputMagnitudeBuffer.resize ((size_t) bins);
}

float FFTEngine::windowSample (WindowType wt, int n, int size) noexcept
{
    const float a = 2.0f * juce::MathConstants<float>::pi * (float) n / (float) (size - 1);
    switch (wt)
    {
        case WindowType::Hann:        return 0.5f  - 0.5f  * std::cos (a);
        case WindowType::Hamming:     return 0.54f - 0.46f * std::cos (a);
        case WindowType::Blackman:    return 0.42f - 0.5f  * std::cos (a) + 0.08f * std::cos (2.0f * a);
        case WindowType::Rectangular:
        default:                      return 1.0f;
    }
}

void FFTEngine::updateWindow()
{
    window.resize ((size_t) fftSize);
    windowSum = 0.0f;
    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = windowSample (windowType, n, fftSize);
        windowSum += window[(size_t) n] * window[(size_t) n];
    }
    // COLA normalization: reconstruction factor = windowSum / hop -> inverse = hop / windowSum.
    norm = (windowSum > 0.0f) ? (float) hopSize / windowSum : 1.0f;
}

void FFTEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    analysisSampleRate = sampleRate;
    juce::ignoreUnused (maxBlockSize);
    channels.resize ((size_t) numChannels);
    for (auto& c : channels)
    {
        c.inFIFO.assign   ((size_t) fftSize, 0.0f);
        for (auto& out : c.outputs)
        { out.accum.assign ((size_t) fftSize, 0); out.fifo.assign ((size_t) (2 * fftSize), 0); }

    }
    reset();
}

void FFTEngine::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.inFIFO.begin(),   c.inFIFO.end(),   0.0f);
        for (auto& out : c.outputs)
        { std::fill (out.accum.begin(), out.accum.end(), 0); std::fill (out.fifo.begin(), out.fifo.end(), 0); out.read = out.write = out.fill = 0; }
        c.inWrite = c.inFill = 0;
    }
    std::fill (fftReal.begin(),  fftReal.end(),  0.0f);
    std::fill (spectrum.begin(), spectrum.end(), std::complex<float>{});
    std::fill (lastMag.begin(),  lastMag.end(),  0.0f);
    if (! lastMag.empty())
        magnitudeBuffer.write (lastMag.data(), lastMag.size(), analysisSampleRate);
}

void FFTEngine::process (const float* input, float* output, int samples, int channel,
                         std::function<void(std::complex<float>*, int, float)> callback)
{
    processOutputs (input, { output, nullptr, nullptr, nullptr }, samples, channel, 1,
        [&] (const std::complex<float>* source, Routes routes, int bins)
        { std::copy_n (source, bins, routes[0]); if (callback) callback (routes[0], bins, 0); });
}
void FFTEngine::processRouted (const float* input, const std::array<float*, 4>& output, int samples, int channel, const RouteCallback& callback)
{ processOutputs (input, output, samples, channel, 4, callback); }
void FFTEngine::processOutputs (const float* input, const std::array<float*, 4>& output, int samples, int channel, int routeCount, const RouteCallback& callback)
{
    auto& c = channels[(size_t) channel]; const int bins = getNumBins();
    Routes routes {}; for (size_t r = 0; r < routes.size(); ++r) routes[r] = routedSpectra[r].data();
    for (int sample = 0; sample < samples; ++sample)
    {
        c.inFIFO[(size_t) c.inWrite] = input[sample]; c.inWrite = (c.inWrite + 1) % fftSize; ++c.inFill;
        if (c.inFill >= fftSize)
        {
            const int start = (c.inWrite - c.inFill + 2 * fftSize) % fftSize;
            for (int n = 0; n < fftSize; ++n) fftReal[(size_t) n] = c.inFIFO[(size_t) ((start + n) % fftSize)] * window[(size_t) n];
            std::fill (fftReal.begin() + fftSize, fftReal.end(), 0);
            fft->performRealOnlyForwardTransform (fftReal.data(), true);
            for (int b = 0; b < bins; ++b) spectrum[(size_t) b] = { fftReal[(size_t) (2 * b)], fftReal[(size_t) (2 * b + 1)] };
            if (channel == 0)
            {
                for (int b = 0; b < bins; ++b) lastMag[(size_t) b] = std::abs (spectrum[(size_t) b]);
                inputMagnitudeBuffer.write (lastMag.data(), (size_t) bins, analysisSampleRate);
            }
            callback (spectrum.data(), routes, bins);
            if (channel == 0)
            {
                for (int b = 0; b < bins; ++b)
                {
                    std::complex<float> sum {};
                    for (int r = 0; r < routeCount; ++r) sum += routes[(size_t) r][b];
                    lastMag[(size_t) b] = std::abs (sum);
                }
                magnitudeBuffer.write (lastMag.data(), (size_t) bins, analysisSampleRate);
            }
            for (int r = 0; r < routeCount; ++r)
            {
                auto& out = c.outputs[(size_t) r]; const auto* route = routes[(size_t) r];
                for (int b = 0; b < bins; ++b)
                { fftReal[(size_t) (2 * b)] = route[b].real(); fftReal[(size_t) (2 * b + 1)] = route[b].imag(); }
                std::fill (fftReal.begin() + 2 * bins, fftReal.end(), 0);
                fft->performRealOnlyInverseTransform (fftReal.data());
                for (int n = 0; n < fftSize; ++n) out.accum[(size_t) n] += fftReal[(size_t) n] * window[(size_t) n] * norm;
                for (int n = 0; n < hopSize; ++n)
                {
                    out.fifo[(size_t) out.write] = out.accum[(size_t) n];
                    out.write = (out.write + 1) % (2 * fftSize); ++out.fill;
                }
                std::move (out.accum.begin() + hopSize, out.accum.end(), out.accum.begin());
                std::fill (out.accum.end() - hopSize, out.accum.end(), 0);
            }
            c.inFill -= hopSize;
        }
        for (int r = 0; r < routeCount; ++r)
        {
            auto& out = c.outputs[(size_t) r]; float value = 0;
            if (out.fill > 0) { value = out.fifo[(size_t) out.read]; out.read = (out.read + 1) % (2 * fftSize); --out.fill; }
            output[(size_t) r][sample] = value;
        }
    }
}
} // namespace scrr::dsp

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "FFTEngine.h"
#include <stdexcept>

namespace scrr::dsp {

FFTEngine::FFTEngine()
{
    configure (2048, 512, WindowType::Hann);
}

void FFTEngine::configure (int newFftSize, int newHopSize, WindowType wt, bool legacyNormalization)
{
    if (newFftSize < kMinFFTSize || newFftSize > kMaxFFTSize
        || (newFftSize & (newFftSize - 1)) != 0
        || newHopSize <= 0 || newHopSize > newFftSize)
        throw std::invalid_argument ("Invalid FFT size or hop size");

    fftSize    = newFftSize;
    hopSize    = newHopSize;
    windowType = wt;
    useLegacyNormalization = legacyNormalization;

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
        case WindowType::BlackmanHarris:
            return 0.35875f - 0.48829f * std::cos (a) + 0.14128f * std::cos (2.0f * a)
                - 0.01168f * std::cos (3.0f * a);
        case WindowType::Gaussian:
        {
            // Sigma is 0.4 times the half-window length.
            const double position = (2.0 * n / (size - 1) - 1.0) / 0.4;
            return (float) std::exp (-0.5 * position * position);
        }
        case WindowType::FlatTop:
            return 0.21557895f - 0.41663158f * std::cos (a) + 0.277263158f * std::cos (2.0f * a)
                - 0.083578947f * std::cos (3.0f * a) + 0.006947368f * std::cos (4.0f * a);
        case WindowType::Welch:
        {
            const double position = 2.0 * n / (size - 1) - 1.0;
            return (float) (1.0 - position * position);
        }
        case WindowType::Triangle:
            return (float) (1.0 - std::abs (2.0 * n / (size - 1) - 1.0));
        case WindowType::Rectangular:
        default:                      return 1.0f;
    }
}

void FFTEngine::updateWindow()
{
    window.resize ((size_t) fftSize);
    synthesisWindow.resize ((size_t) fftSize);
    float windowSum = 0.0f; // Keep the legacy accumulation order for old projects.
    std::vector<double> overlapEnergy ((size_t) hopSize, 0.0);
    windowAmplitudeSum = 0.0;
    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = windowSample (windowType, n, fftSize);
        windowSum += window[(size_t) n] * window[(size_t) n];
        windowAmplitudeSum += (double) window[(size_t) n];
        overlapEnergy[(size_t) (n % hopSize)] += (double) window[(size_t) n] * window[(size_t) n];
    }
    // A dual synthesis window normalizes each overlap position separately.
    // Residue classes also cover hops which do not divide the FFT size.
    for (const double energy : overlapEnergy)
        if (! std::isfinite (energy) || energy <= 1.0e-12)
            throw std::invalid_argument ("Window and hop leave uncovered samples");
    legacyNorm = (float) hopSize / windowSum;
    for (int n = 0; n < fftSize; ++n)
        synthesisWindow[(size_t) n] = useLegacyNormalization
            ? window[(size_t) n] * legacyNorm
            : (float) (window[(size_t) n] / overlapEnergy[(size_t) (n % hopSize)]);
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
                for (int n = 0; n < fftSize; ++n)
                    out.accum[(size_t) n] += useLegacyNormalization
                        ? fftReal[(size_t) n] * window[(size_t) n] * legacyNorm
                        : fftReal[(size_t) n] * synthesisWindow[(size_t) n];
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

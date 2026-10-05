// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include <functional>
#include "TripleBuffer.h"

namespace scrr::dsp {

enum class WindowType { Hann, Hamming, Blackman, Rectangular };

/**
    Real-time STFT engine using juce::dsp::FFT.

    Pipeline per channel:
      inFIFO -> [frame fftSize] -> window -> FFT -> spectralCallback -> IFFT
              -> window -> overlap-add into outFIFO -> shift by hopSize.

    Default: 2048 FFT, 512 hop, Hann, 75% overlap (4x).

    Design goals (phase 1):
      - No clicks/pops; constant-overlap-add normalization makes bypass transparent.
      - Reports algorithmic latency = fftSize - 1 to the host.
      - Variable block sizes handled via internal FIFOs.
*/
class FFTEngine
{
public:
    static constexpr int kMinFFTSize = 128;
    static constexpr int kMaxFFTSize = 131072; // 32768 at 4x oversampling

    FFTEngine();

    /** Reconfigure FFT size / hop / window. Call from prepareResources(). */
    void configure (int fftSize, int hopSize, WindowType window);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Process one channel. numSamples <= maxBlockSize passed to prepare(). */
    void process (const float* input, float* output, int numSamples, int channel,
                  std::function<void(std::complex<float>*, int, float)> spectralCallback);

    using Routes = std::array<std::complex<float>*, 4>;
    using RouteCallback = std::function<void(const std::complex<float>*, Routes, int)>;
    void processRouted (const float*, const std::array<float*, 4>&, int numSamples, int channel, const RouteCallback&);

    int  getLatencySamples() const noexcept { return fftSize - 1; }
    int  getFFTSize() const noexcept { return fftSize; }
    int  getHopSize() const noexcept { return hopSize; }
    int  getNumBins() const noexcept { return fftSize / 2 + 1; }

    /** Copy the most recently analysed magnitude spectrum (linear) into `out`.
        Returns true if a new snapshot was available since the last call; false
        means `out` is left untouched (caller keeps its previous snapshot). */
    bool copyLastMagnitudes (std::vector<float>& out, double* rate = nullptr, bool input = false) const
    {
        return (input ? inputMagnitudeBuffer : magnitudeBuffer).read (out, rate);
    }

private:
    void updateWindow();
    static float windowSample (WindowType wt, int n, int size) noexcept;

    int fftSize   { 2048 };
    int hopSize   { 512 };
    WindowType windowType { WindowType::Hann };

    std::unique_ptr<juce::dsp::FFT> fft;

    struct OutputData
    {
        std::vector<float> accum, fifo;
        int read {}, write {}, fill {};
    };
    struct ChannelData
    {
        std::vector<float> inFIFO;
        int inWrite {}, inFill {};
        std::array<OutputData, 4> outputs;
    };
    void processOutputs (const float*, const std::array<float*, 4>&, int, int, int, const RouteCallback&);
    std::array<std::vector<std::complex<float>>, 4> routedSpectra;
    std::vector<ChannelData> channels;

    std::vector<float>               window;
    float                            windowSum { 1.0f }; // sum of window^2
    float                            norm      { 1.0f };  // hop / windowSum

    std::vector<float>               fftReal;
    std::vector<std::complex<float>> spectrum;
    std::vector<float>               lastMag;       // producer scratch (audio thread only)
    double analysisSampleRate { 44100.0 };
    TripleBuffer inputMagnitudeBuffer;
    TripleBuffer                     magnitudeBuffer; // lock-free snapshot channel to GUI

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTEngine)
};

} // namespace scrr::dsp

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <array>
#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace juce::dsp { class FFT; }

namespace scrr::dsp {

/** Transforms individual split slices before they are folded into the four racks.

    Scale translates a complete slice, preserving the frequency offsets inside it.
    Resynth replaces its energy with one continuous band-limited oscillator. Once a
    stage has resynthesized a slice, later Scale stages move that oscillator.
    Transform equations are independent implementations inspired by the named
    dissonance effects; they do not reproduce another product's engine.
*/
class PartialProcessor
{
public:
    static constexpr int maxSlices = 64;
    static constexpr int maxStages = 64;
    enum class Type { Stretcher, StiffString, Centroid, PGrid, FGrid };
    enum class Mode { Scale, Resynth };
    enum class Shape { Sine, Triangle, Saw, Square };
    enum class AnchorMode { BandCentre, EnergyCentroid };

    struct Stage
    {
        std::string uid;
        Type type { Type::Stretcher };
        bool enabled { true };
        Mode mode { Mode::Scale };
        Shape shape { Shape::Sine };
        // Stretcher: semitones. Others: percent (0..100).
        double amount {};
        // Centroid: semitones above the reference; PGrid: semitone spacing;
        // FGrid: frequency spacing in Hz. Unused by the other stage types.
        double pitch { 12.0 };
    };

    struct SliceContext
    {
        int index {};
        double originalCentreHz {}, currentAnchorHz {};
        bool singleSine {}; // Legacy field name: true for every oscillator shape.
        Shape resynthShape { Shape::Sine };
    };

    using Routes = std::array<std::complex<float>*, 4>;

    // Serial rack stages share their FFT plan, analysis window and temporary
    // frames. Oscillator phase and movement histories remain instance-owned.
    struct Workspace
    {
        Workspace();
        ~Workspace();
        void prepare (int fftSize, const std::vector<float>& analysisWindow, bool allRoutes = false);
        static size_t estimatedBytes (int fftSize) noexcept;
        // Control-thread preparation only, shared by serial partial stages.
        // Default Sine processing never allocates the optional waveform bank.
        void prepareWaveforms();
        void releaseWaveforms() { std::vector<float>().swap (waveforms); }
        bool hasWaveforms() const noexcept { return ! waveforms.empty(); }
        static size_t estimatedWaveformBytes() noexcept;
    private:
        friend class PartialProcessor;
        std::vector<float> window;
        std::array<std::vector<float>, 4> oscillatorFrames;
        std::vector<std::complex<float>> sliceInput;
        std::unique_ptr<juce::dsp::FFT> synthesisFFT;
        std::vector<float> waveforms;
        std::array<double, 33> waveformCorrelations {};
        const float* waveform (Shape, int maxHarmonic) const noexcept;
        void renderWaveform (std::vector<float>& frame, Shape, double amplitude,
                             double phase, double frequency, double limit, double sampleRate,
                             bool accumulate) const noexcept;
        double windowPower { 1 }, windowBinPhase {};
    };

    PartialProcessor();
    ~PartialProcessor();
    PartialProcessor (const PartialProcessor&) = delete;
    PartialProcessor& operator= (const PartialProcessor&) = delete;

    // Allocate only during engine preparation. A shared workspace must already
    // be prepared for this size/window; it supports processSlice only. Private
    // workspaces also support the whole-frame/four-route processing API.
    void prepare (double actualSampleRate, int fftSize, int hopSize,
                  int numChannels, const std::vector<float>& analysisWindow,
                  int maxFramesPerBlock = 4096,
                  std::shared_ptr<Workspace> sharedWorkspace = {});
    // Reservation for a rack instance with exactly one configured stage.
    static size_t estimatedPrivateBytes (int maxFramesPerBlock) noexcept;
    void reset() noexcept;
    // Call once before processing the block's channels in ascending order. The
    // Whole-frame processing links movement to the left channel. Rack slice
    // processing links Band Centre anchors; Energy Centroid follows each audio
    // channel independently, including right-only input. Amplitude/phase remain
    // separate. Prepare ceil(blockSize / hop) + 1 or more history frames.
    void beginBlock() noexcept;
    // Configuration updates never reset running slice phases, including reorder
    // and same-UID parameter edits. Slice identity is independent of stage order.
    void setStages (const std::vector<Stage>&);
    void updateStageParameters (size_t index, double amount, double pitch) noexcept;
    void setAnchorMode (AnchorMode mode) noexcept { anchorMode = mode; }
    void setFundamentalHz (double hz) noexcept;
    bool hasActiveStages() const noexcept;
    const std::array<double, maxSlices>& getMappedAnchors() const noexcept { return mappedAnchors; }

    // Clears/fills all four destinations. Source must not alias a destination.
    // With one slice or no enabled stages this is exactly the existing routing.
    // DC, Nyquist and input above frequencyLimit are retained at their original
    // frequencies. Transformed material outside the valid interior is dropped.
    // Colliding slices add complex values; they are never overwritten.
    void process (const std::complex<float>* source, Routes destinations, int bins,
                  int slices, float crossfade, double hostSampleRate,
                  float frequencyLimit, int channel) noexcept;
    // Rack integration: the caller has already isolated this named slice and
    // carries its anchor through intervening ordinary spectral stages. In-place
    // processing is supported; no allocations occur here.
    void processSlice (std::complex<float>* spectrum, int bins, SliceContext&,
                       float frequencyLimit, int channel) noexcept;

    // Public mathematical contract for the editor, previews and focused tests.
    static double mapFrequency (double frequencyHz, double referenceHz, const Stage&) noexcept;

private:
    struct Assignment { int first {}, second { -1 }; float a { 1 }, b {}; };
    struct SliceSpan { int first {}, last {}; double centre {}; };
    struct SliceState
    {
        double shiftPhase {}, sinePhase {}, smoothedShift {}, smoothedDestination {}, lastAnchor {};
        bool shiftReady {}, sineReady {};
    };
    struct Movement { double anchor {}, shift {}, destination {}; };

    void updateRouting (int slices, float crossfade, double hostRate) noexcept;
    double sampleRate { 44100.0 }, referenceHz { 110.0 }, windowPower { 1.0 }, windowBinPhase {};
    int fftSize {}, hopSize {}, channels { 2 }, cachedSlices {};
    double cachedHostRate {};
    float cachedCrossfade {};
    AnchorMode anchorMode { AnchorMode::BandCentre };
    std::vector<Stage> stages;
    std::vector<Assignment> assignments;
    std::array<SliceSpan, maxSlices> spans {};
    std::array<std::array<SliceState, maxSlices>, 2> state {};
    std::vector<std::array<Movement, maxSlices>> movementHistory;
    std::array<size_t, 2> frameCursors {};
    std::array<std::array<size_t, maxSlices>, 2> sliceFrameCursors {};
    size_t recordedFrames {};
    std::array<double, maxSlices> mappedAnchors {};
    std::shared_ptr<Workspace> workspace;
    bool ownsWorkspace {};
};

} // namespace scrr::dsp

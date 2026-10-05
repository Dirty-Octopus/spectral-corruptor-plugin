// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SpectralProcessor.h"
#include "dsp/ModulationEngine.h"
#include "parameters/ParamIDs.h"
#include "parameters/HostBeat.h"
#include "parameters/MacroMapping.h"
#include "licensing/LicenseManager.h"
#include "licensing/PinkNoise.h"
#include <atomic>
#include <memory>
#include <map>

class SpectralCrrptProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit SpectralCrrptProcessor (std::shared_ptr<scrr::licensing::LicenseManager> = scrr::licensing::LicenseManager::shared());
    ~SpectralCrrptProcessor() override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    scrr::dsp::SpectralProcessor& getProcessor() noexcept { return spectralProcessor; }
    // Returned trees are detached copies. All rack writes go through these methods.
    juce::ValueTree getModuleChain (int channel) const;
    juce::ValueTree copyPresetState();
    void addModule (int channel, const juce::String& typeId);
    void removeModule (int channel, int index);
    void moveModule (int channel, int fromIndex, int toIndex);
    juce::String transferModule (int source, const juce::String& uid, int destination, int insertion, bool duplicate);
    juce::String copyModule (int channel, const juce::String& uid) const;
    juce::String pasteModule (int channel, const juce::String& clipboard);
    void setModuleParameter (int channel, const juce::String& uid,
                             const juce::Identifier& key, const juce::var& value);
    bool applyPreset (const juce::ValueTree&);
    juce::String getCurrentPresetName() const;
    bool isPresetDirty() const { return presetDirty.load(); }
    void setPresetName (const juce::String&);
    void setPresetDirty (bool dirty) { presetDirty.store (dirty); }
    bool readHostBeat (scrr::params::HostBeat& beat) const noexcept { return hostBeat.read (beat); }
    float getFrequencyLimit() const noexcept { return scrr::params::frequencyLimitValue (apvts); }
    int getDisplayLatency() const noexcept { return displayLatency.load(); }
    uint64_t getStructureRevision() const noexcept { return structureRevision.load(); }
    scrr::licensing::LicenseManager& getLicense() const noexcept { return *license; }
    juce::String getMacroName (int index) const;
    juce::String getMacroLabel (int index) const;
    void setMacroName (int index, const juce::String& name);
    uint64_t getMacroNameRevision() const noexcept { return macroNameRevision.load(); }
    std::vector<scrr::params::MacroMapping> getMacroMappings() const;
    void assignMacro (int macro, int channel, const juce::String& uid, const juce::Identifier& parameter);
    void removeMacroMapping (int channel, const juce::String& uid, const juce::Identifier& parameter, int macro = -1, const juce::String& source = {});
    void updateMacroMapping (scrr::params::MacroMapping);
    double getEffectiveValue (int channel, const juce::String& uid, const juce::Identifier& parameter, double fallback) const;
    scrr::params::MacroValues getMacroValues() const noexcept;
    scrr::params::ModulationValues getModulationValues() const noexcept;
    std::vector<scrr::params::ModulationSource> getModulators() const;
    juce::String addModulator (bool envelope, bool random = false);
    void removeModulator (const juce::String&);
    void updateModulator (scrr::params::ModulationSource);
    void assignModulator (const juce::String& source, int channel, const juce::String& uid, const juce::Identifier& parameter);
    uint64_t getModulatorRevision() const noexcept { return modulatorRevision.load(); }
    float getModulatorPhase (int index) const noexcept { return modulationEngine.phase (index); }

private:
    std::shared_ptr<scrr::licensing::LicenseManager> license;
    std::array<scrr::licensing::PinkNoise, 2> noise { scrr::licensing::PinkNoise (0x529423u), scrr::licensing::PinkNoise (0x735ab3u) };
    void renderUnlicensed (juce::AudioBuffer<float>&) noexcept;
    scrr::params::HostBeatMailbox hostBeat;
    double lastHostBpm { 120 };
    scrr::params::HostBeat captureHostBeat() noexcept;
    std::atomic<int> displayLatency { 0 };
    int preparedBlockSize { 512 };
    juce::AudioBuffer<float> bypassHistory;
    int bypassWrite {};
    void hostBypassDelay (juce::AudioBuffer<float>&, bool replace) noexcept;
    juce::AudioProcessorValueTreeState apvts;
    scrr::dsp::SpectralProcessor spectralProcessor;
    scrr::dsp::ModulationEngine modulationEngine;
    std::vector<scrr::params::ModulationSource> modulators;
    std::atomic<uint64_t> modulatorRevision { 0 };
    mutable juce::CriticalSection stateLock;
    juce::ValueTree rackState { "RACK" };
    juce::String presetName { "Init" };
    std::atomic<bool> presetDirty { false }, restoring { false };
    std::atomic<uint64_t> structureRevision { 0 };
    std::vector<scrr::params::MacroMapping> macroMappings;
    std::array<juce::String, 8> macroNames;
    std::atomic<uint64_t> macroNameRevision {};
    std::array<std::atomic<float>*, 8> macroParameters {};
    double macroBase (const scrr::params::MacroMapping&) const;
    bool describeMacroTarget (scrr::params::MacroMapping&) const;
    void restoreMacroMappings (const juce::ValueTree&, const std::map<juce::String, juce::String>&);
    struct RackSnapshot { std::array<juce::ValueTree, 5> chains; std::vector<scrr::params::MacroMapping> mappings; std::vector<scrr::params::ModulationSource> sources; };
    std::shared_ptr<const RackSnapshot> publishedRack, appliedRack;
    void publishRack (bool structureChanged);
    void consumeRack();
    void parameterChanged (const juce::String&, float) override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralCrrptProcessor)
};

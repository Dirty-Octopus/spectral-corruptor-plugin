// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "HostRegistration.h"
#include <juce_cryptography/juce_cryptography.h>
namespace scrr::updates {
class UpdateService : private juce::Thread
{
public:
    enum class State { idle, checking, current, available, downloading, ready, scheduled, failed };
    struct Snapshot { State state { State::idle }; juce::String version, message; float progress {}; };
    static std::shared_ptr<UpdateService> shared();
    inline static bool testing { false };
    static int compareVersions (juce::String a, juce::String b);
    static bool trustedAsset (const juce::String& url);
    UpdateService();
    ~UpdateService() override;
    Snapshot snapshot() const;
    void check (bool force = true);
    void download();
    bool schedule (bool projectsSaved);
    bool cancelScheduled();
    static juce::String installScript (int hostPid, const juce::File& hosts, const juce::File& cancel, const juce::File& installer, const juce::String& sha256);
    static bool writeInstallScript (const juce::File&, const juce::String&);
private:
    void run() override;
    void performCheck();
    void performDownload();
    bool restoreScheduled();
    bool persistScheduled();
    void clearScheduledRecord();
    juce::String readText (const juce::String&, int64_t limit);
    void setStatus (State, juce::String = {}, float progress = 0);
    mutable juce::CriticalSection lock;
    Snapshot status;
    juce::String installerUrl, checksumUrl, assetName;
    juce::File downloaded, scheduledDirectory;
    int operation {};
    bool forceCheck {};
    juce::String expectedHash;
    juce::InterProcessLock updateLock { "SpectralCorruptorUpdate" };
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace scrr::licensing {
// Public integration contract. No official machine identity, keys, verification,
// licence format or activation storage implementation is distributed here.
class LicenseManager
{
public:
    LicenseManager();
    static std::shared_ptr<LicenseManager> shared();
    static juce::String machineCode();
    static juce::File defaultFile();
    const juce::String& getMachineCode() const noexcept { return machine; }
    bool isActivated() const noexcept { return activated.load (std::memory_order_acquire); }
    bool canProcessRealtime() const noexcept { return isActivated(); }
    bool isDemoActive() const noexcept { return false; }
    int64_t demoRemainingSeconds() const noexcept { return 0; }
    juce::String demoStatusText() const { return "Demo is unavailable in the source placeholder."; }
    juce::Result validate (const juce::String&) const;
    juce::Result activate (const juce::String&);
private:
    const juce::String machine;
    std::atomic<bool> activated { false };
};
}

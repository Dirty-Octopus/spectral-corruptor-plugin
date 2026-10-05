// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "LicenseManager.h"

namespace scrr::licensing {
// Intentionally unimplemented. Replace this backend with your own implementation.
// This is neither an official-code verifier nor an example that unlocks processing.
LicenseManager::LicenseManager() : machine (machineCode()) {}
juce::String LicenseManager::machineCode() { return "CUSTOM LICENSE BACKEND REQUIRED"; }
juce::File LicenseManager::defaultFile() { return {}; }
std::shared_ptr<LicenseManager> LicenseManager::shared()
{
    static auto instance = std::make_shared<LicenseManager>();
    return instance;
}
juce::Result LicenseManager::validate (const juce::String&) const
{
    return juce::Result::fail ("Implement your own licensing backend before using this source build.");
}
juce::Result LicenseManager::activate (const juce::String& code)
{
    return validate (code); // Always fails in the supplied placeholder; no files are read or written.
}
}

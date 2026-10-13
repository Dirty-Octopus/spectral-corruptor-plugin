// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "PromptLanguage.h"

namespace scrr::gui {
// Machine preference, independent of sound presets and host automation.
class UiScale
{
public:
    static int sanitise (int percent) noexcept
    { return percent >= 25 && percent <= 200 && percent % 25 == 0 ? percent : 100; }
    static juce::File preferenceFile()
    { return PromptLanguage::preferenceFile().getSiblingFile ("scale.txt"); }
    static int read (const juce::File& file)
    { return file.getSize() <= 16 ? sanitise (file.loadFileAsString().trim().getIntValue()) : 100; }
    static bool write (int percent, const juce::File& file)
    {
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary (file);
        return temporary.getFile().replaceWithText (juce::String (sanitise (percent)))
            && temporary.overwriteTargetFileWithTemporary();
    }
    static int get() { return current().load(); }
    static void set (int percent) { current().store (sanitise (percent)); }
private:
    static std::atomic<int>& current()
    { static std::atomic<int> percent { read (preferenceFile()) }; return percent; }
};
}

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "PromptLanguage.h"

namespace scrr::gui {
// Local preference shared by plugin instances; never a sound/automation parameter.
class RandomPresetSettings
{
public:
    static constexpr int defaultCount = 6;
    static int sanitise (int count) noexcept { return juce::jlimit (1, 12, count); }
    static juce::File preferenceFile() { return PromptLanguage::preferenceFile().getSiblingFile ("random-preset-count.txt"); }
    static int read (const juce::File& file)
    {
        if (file.getSize() > 16) return defaultCount;
        const auto text = file.loadFileAsString().trim(); const int value = text.getIntValue();
        return text.isNotEmpty() && text.containsOnly ("0123456789") && value >= 1 && value <= 12 ? value : defaultCount;
    }
    static bool write (int count, const juce::File& file)
    {
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary (file);
        return temporary.getFile().replaceWithText (juce::String (sanitise (count))) && temporary.overwriteTargetFileWithTemporary();
    }
    static int get() { return current().load(); }
    static void set (int count) { current().store (sanitise (count)); }
private:
    static std::atomic<int>& current() { static std::atomic<int> count { read (preferenceFile()) }; return count; }
};
} // namespace scrr::gui

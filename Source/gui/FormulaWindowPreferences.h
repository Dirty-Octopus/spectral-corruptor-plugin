// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "PromptLanguage.h"

namespace scrr::gui {
// Shared local presentation preference. It never becomes preset or audio state.
class FormulaWindowPreferences
{
public:
    enum class Mode { native = 1, contained = 2 };
    static juce::File preferenceFile() { return PromptLanguage::preferenceFile().getSiblingFile ("formula-window.txt"); }
    static Mode read (const juce::File& file)
    {
        return file.getSize() <= 32 && file.loadFileAsString().trim() == "contained" ? Mode::contained : Mode::native;
    }
    static bool write (Mode mode, const juce::File& file)
    {
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary (file);
        return temporary.getFile().replaceWithText (mode == Mode::contained ? "contained" : "native")
            && temporary.overwriteTargetFileWithTemporary();
    }
    static Mode get() { return current().load(); }
    static void set (Mode mode) { current().store (mode == Mode::contained ? Mode::contained : Mode::native); }
private:
    static std::atomic<Mode>& current() { static std::atomic<Mode> mode { read (preferenceFile()) }; return mode; }
};
} // namespace scrr::gui

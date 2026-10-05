// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace scrr::gui {

/**
    Centralized theme: colors, fonts, asset paths.
    Designed to be swapped at runtime so Aseprite sprite sheets can replace
    the default look without touching component code.
*/
struct Skin
{
    juce::Colour background  { 0xfff2e8d6 };
    juce::Colour panel       { 0xfffff8ec };
    juce::Colour panelLight  { 0xffead8bc };
    juce::Colour accent      { 0xffbf6a34 };
    juce::Colour accentDim   { 0xffc7925e };
    juce::Colour text        { 0xff3d2b1f };
    juce::Colour textDim     { 0xff7d6047 };
    juce::Colour warn        { 0xffa3462c };
    juce::Colour spectrumLow  { 0xff6d4c36 };
    juce::Colour spectrumHigh { 0xffd8813e };

    juce::String assetDir { "assets" }; // relative; resolved by AssetCache

    static Skin& get();
};

} // namespace scrr::gui

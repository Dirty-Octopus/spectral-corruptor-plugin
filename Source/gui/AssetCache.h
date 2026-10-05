// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>

namespace scrr::gui {

/**
    Loads and caches PNG images (single frames or Aseprite sprite sheets).
    Returns nullptr when an asset is missing so the UI can fall back to
    procedural drawing. Designed for later swap-in of pixel-art skins.
*/
class AssetCache
{
public:
    AssetCache();

    /** Resolve `assetDir/<name>`; returns nullptr if not found/loaded. */
    juce::Image* getImage (const juce::String& name);

    /** Load a sprite sheet: name -> Image with metadata via getFrameCount etc. (later) */
    void setAssetDirectory (const juce::File& dir);

private:
    juce::File directory;
    std::unordered_map<juce::String, juce::Image> cache;
};

} // namespace scrr::gui

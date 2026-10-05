// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "AssetCache.h"
#include "Skin.h"

namespace scrr::gui {

AssetCache::AssetCache()
{
    directory = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                    .getParentDirectory().getChildFile (Skin::get().assetDir);
}

void AssetCache::setAssetDirectory (const juce::File& dir)
{
    directory = dir;
    cache.clear();
}

juce::Image* AssetCache::getImage (const juce::String& name)
{
    auto it = cache.find (name);
    if (it != cache.end())
        return &it->second;

    const auto file = directory.getChildFile (name);
    if (! file.existsAsFile())
        return nullptr;

    juce::Image img = juce::ImageFileFormat::loadFrom (file);
    if (img.isNull())
        return nullptr;

    auto [inserted, _] = cache.emplace (name, std::move (img));
    juce::ignoreUnused (_);
    return &inserted->second;
}

} // namespace scrr::gui

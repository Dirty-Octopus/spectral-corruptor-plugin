// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "FactoryPresets.h"

namespace scrr {

struct PresetEntry
{
    juce::String category;   // "Factory" or "User"
    juce::String folder;     // subfolder name (e.g. "FriendID1"), or "" for root
    juce::String name;       // display name
    juce::String data;       // XML content (factory) or file path (user)
    bool isFactory { true };
};

class PresetManager
{
public:
    explicit PresetManager (juce::File directory = {})
    {
        if (directory != juce::File())
        {
            userPresetDir = directory; userPresetDir.createDirectory(); rescan(); return;
        }
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        userPresetDir = base.getChildFile ("SpectralCorruptor/Presets");
        userPresetDir.createDirectory();
        rescan();
    }

    void rescan()
    {
        allPresets.clear();
        scanFactory();
        scanUser();
    }

    juce::File getUserDir() const { return userPresetDir; }

    juce::ValueTree loadPreset (int flatIndex) const
    {
        if (flatIndex < 0 || flatIndex >= allPresets.size())
            return {};

        const auto& entry = allPresets[flatIndex];
        juce::String xmlContent;

        if (entry.isFactory)
        {
            xmlContent = entry.data;
        }
        else
        {
            if (auto xml = juce::XmlDocument::parse (juce::File (entry.data)))
                return juce::ValueTree::fromXml (*xml);
            return {};
        }

        if (auto xml = juce::XmlDocument::parse (xmlContent))
            return juce::ValueTree::fromXml (*xml);
        return {};
    }

    juce::ValueTree loadFromFile (const juce::File& file) const
    {
        if (auto xml = juce::XmlDocument::parse (file))
            return juce::ValueTree::fromXml (*xml);
        return {};
    }

    juce::Result saveToUser (const juce::String& name, const juce::ValueTree& state)
    {
        const auto safeName = juce::File::createLegalFileName (name.trim());
        if (safeName.isEmpty() || safeName != name.trim() || safeName == "." || safeName == "..")
            return juce::Result::fail ("Use a preset name without path separators.");
        auto result = saveToFile (userPresetDir.getChildFile (safeName + ".scpreset"), state);
        if (result.wasOk()) rescan();
        return result;
    }

    juce::Result saveToFile (const juce::File& file, const juce::ValueTree& state) const
    {
        auto xml = state.createXml();
        if (xml == nullptr) return juce::Result::fail ("Invalid preset state.");
        juce::TemporaryFile temporary (file);
        if (! temporary.getFile().replaceWithText (xml->toString()))
            return juce::Result::fail ("Could not write preset. Check folder permissions.");
        if (! temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not replace preset. Original file was preserved.");
        return juce::Result::ok();
    }

    int getFactoryCount() const
    {
        int count = 0;
        for (const auto& p : allPresets)
            if (p.isFactory) ++count;
        return count;
    }

    int getUserCount() const
    {
        int count = 0;
        for (const auto& p : allPresets)
            if (! p.isFactory) ++count;
        return count;
    }

    int getTotalCount() const { return allPresets.size(); }

    const PresetEntry& getEntry (int flatIndex) const { return allPresets.getReference (flatIndex); }

    juce::Array<PresetEntry> getAllEntries() const { return allPresets; }

    int findFlatIndex (const juce::String& category, const juce::String& folder, const juce::String& name) const
    {
        for (int i = 0; i < allPresets.size(); ++i)
            if (allPresets[i].category == category && allPresets[i].folder == folder && allPresets[i].name == name)
                return i;
        return -1;
    }

    int getFirstIndexInCategory (bool factory) const
    {
        for (int i = 0; i < allPresets.size(); ++i)
            if (allPresets[i].isFactory == factory)
                return i;
        return -1;
    }

    int getLastIndexInCategory (bool factory) const
    {
        for (int i = allPresets.size() - 1; i >= 0; --i)
            if (allPresets[i].isFactory == factory)
                return i;
        return -1;
    }

    int getNextInCategory (int currentIndex, bool factory) const
    {
        for (int i = currentIndex + 1; i < allPresets.size(); ++i)
            if (allPresets[i].isFactory == factory)
                return i;
        return getFirstIndexInCategory (factory);
    }

    int getPrevInCategory (int currentIndex, bool factory) const
    {
        for (int i = currentIndex - 1; i >= 0; --i)
            if (allPresets[i].isFactory == factory)
                return i;
        return getLastIndexInCategory (factory);
    }

private:
    void scanFactory()
    {
        auto& entries = factoryPresets::getEntries();
        for (const auto& e : entries)
        {
            PresetEntry pe;
            pe.isFactory = true;
            pe.category = "Factory";
            pe.folder = e.folder;
            pe.name = e.name;
            pe.data = e.data;
            allPresets.add (pe);
        }
    }

    void scanUser()
    {
        auto files = userPresetDir.findChildFiles (juce::File::findFiles, false, "*.scpreset");
        files.sort();
        for (const auto& f : files)
        {
            PresetEntry pe;
            pe.isFactory = false;
            pe.category = "User";
            pe.folder = "";
            pe.name = f.getFileNameWithoutExtension();
            pe.data = f.getFullPathName();
            allPresets.add (pe);
        }
    }

    juce::File userPresetDir;
    juce::Array<PresetEntry> allPresets;
};

} // namespace scrr

// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "PromptLanguage.h"
#include <array>
#include <map>

namespace scrr::gui {
// UI-thread preferences, shared by editors in this process; never part of audio state.
class ThemeProfiles
{
public:
    static constexpr int count = 10;
    using Colours = std::array<juce::uint32, count>;
    inline static constexpr const char* labels[count] { "Background", "Panel", "Field", "Border", "Text", "Muted text", "Inactive", "Red accent", "Yellow accent", "Blue accent" };
    static Colours defaults() { return { 0xff101010, 0xff202020, 0xff080808, 0xff575757, 0xfff6f6ef, 0xffb8b8b0, 0xff979797, 0xffff392b, 0xffffdf00, 0xff164cff }; }
    static ThemeProfiles& get() { static ThemeProfiles value; return value; }
    juce::Colour colour (int slot) const { return juce::Colour (colours[(size_t) slot]); }
    uint64_t revision() const { return generation; }
    const Colours& values() const { return colours; }
    juce::String selected() const { return active; }
    juce::StringArray names() const { juce::StringArray result { "Default" }; for (auto& p : profiles) result.add (p.first); return result; }
    static bool parse (juce::String text, juce::uint32& value)
    {
        text = text.trim(); if (text.startsWithChar ('#')) text = text.substring (1);
        if (text.length() != 6 || ! text.containsOnly ("0123456789abcdefABCDEF")) return false;
        value = 0xff000000u | (juce::uint32) text.getHexValue32(); return true;
    }
    bool save (juce::String name, const Colours& values)
    {
        PreferenceLock lock;
        if (! lock.acquired) return false;
        reloadBeforeMutation();
        name = name.trim().substring (0, 48);
        if (name.isEmpty() || name.equalsIgnoreCase ("Default") || (profiles.find (name) == profiles.end() && profiles.size() >= 64)) return false;
        const auto old = profiles; const auto oldActive = active; const auto oldColours = colours;
        profiles[name] = values; active = name; colours = values;
        if (! write()) { profiles = old; active = oldActive; colours = oldColours; return false; }
        ++generation; return true;
    }
    bool select (const juce::String& name)
    {
        PreferenceLock lock;
        if (! lock.acquired) return false;
        reloadBeforeMutation();
        if (name != "Default" && profiles.find (name) == profiles.end()) return false;
        const auto before = active; const auto previous = colours;
        active = name; colours = name == "Default" ? defaults() : profiles.at (name);
        if (! write()) { active = before; colours = previous; return false; }
        ++generation; return true;
    }
    void refreshFromDisk()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastDiskPoll < 1000.0) return;
        lastDiskPoll = now;
        const auto stamp = file().getLastModificationTime();
        if (stamp != modified) { load(); ++generation; }
    }
    static juce::File file() { return PromptLanguage::preferenceFile().getSiblingFile ("colours.json"); }
private:
    struct PreferenceLock
    {
        juce::InterProcessLock lock { "SpectralCorruptor.colours." + juce::String::toHexString (file().getFullPathName().hashCode64()) };
        bool acquired { lock.enter (500) };
        ~PreferenceLock() { if (acquired) lock.exit(); }
    };
    std::map<juce::String, Colours> profiles;
    Colours colours { defaults() };
    juce::String active { "Default" };
    uint64_t generation {};
    double lastDiskPoll { -1000 };
    juce::Time modified;
    ThemeProfiles() { load(); }
    void reloadBeforeMutation()
    {
        const auto previousProfiles = profiles;
        const auto previousActive = active;
        const auto previousColours = colours;
        load();
        if (profiles != previousProfiles || active != previousActive || colours != previousColours) ++generation;
    }
    void load()
    {
        modified = file().getLastModificationTime();
        if (file().getSize() > 128 * 1024) return;
        auto root = juce::JSON::parse (file());
        auto entries = root["profiles"];
        if (! entries.isObject()) return;
        std::map<juce::String, Colours> next;
        for (auto& p : entries.getDynamicObject()->getProperties())
        {
            auto* array = p.value.getArray(); if (! array || array->size() != count || next.size() >= 64) continue;
            Colours candidate; bool valid = true;
            for (int i = 0; i < count; ++i) valid = parse ((*array)[i].toString(), candidate[(size_t) i]) && valid;
            if (valid && p.name.toString() != "Default") next[p.name.toString()] = candidate;
        }
        profiles = std::move (next); active = root["selected"].toString();
        if (profiles.find (active) == profiles.end()) active = "Default";
        colours = active == "Default" ? defaults() : profiles.at (active);
    }
    bool write()
    {
        juce::DynamicObject::Ptr entries = new juce::DynamicObject();
        for (auto& p : profiles)
        {
            juce::Array<juce::var> a;
            for (auto c : p.second) a.add (juce::String::toHexString ((int) (c & 0xffffffu)).paddedLeft ('0', 6).toUpperCase());
            entries->setProperty (p.first, a);
        }
        juce::DynamicObject::Ptr root = new juce::DynamicObject(); root->setProperty ("version", 1);
        root->setProperty ("selected", active); root->setProperty ("profiles", juce::var (entries.get()));
        if (file().getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temp (file());
        if (! temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root.get()))) || ! temp.overwriteTargetFileWithTemporary()) return false;
        modified = file().getLastModificationTime(); return true;
    }
};
}

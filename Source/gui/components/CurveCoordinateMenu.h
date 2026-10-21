// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Theme.h"
#include <functional>

namespace scrr::gui {
inline void showCurveCoordinateMenu (juce::Component& target, bool allowX, bool allowY,
                                     const juce::String& xLabel, const juce::String& yLabel,
                                     std::function<void(bool)> chooseAxis)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.addSectionHeader ("NODE COORDINATES");
    menu.addItem (1, "Assign X / " + xLabel, allowX);
    menu.addItem (2, "Assign Y / " + yLabel, allowY);
    const juce::Component::SafePointer<juce::Component> safe (&target);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
        [safe, callback = std::move (chooseAxis)] (int result)
        {
            if (! safe || (result != 1 && result != 2) || ! callback) return;
            // Finish dismantling this popup before opening the source menu.
            // Otherwise JUCE's completion focus restore can close the new menu.
            juce::MessageManager::callAsync ([safe, callback, result]
            { if (safe) callback (result == 1); });
        });
}
}

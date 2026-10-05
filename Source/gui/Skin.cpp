// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "Skin.h"

namespace scrr::gui {

Skin& Skin::get()
{
    static Skin instance;
    return instance;
}

} // namespace scrr::gui

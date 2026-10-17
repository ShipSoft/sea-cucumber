// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  Checks ViewConfig: styleForVolume rule lookup, the deterministic fallback
//  colour, and TOML parsing (style transparency inheriting
//  default_transparency, region window pair validation).
// =============================================================================

#include <fstream>
#include <string>

#include "TestUtil.h"
#include "ViewConfig.h"

int main() {
    using testutil::check;

    // Built-in defaults: rule lookup and unmatched fallback.
    const shipdisp::ViewConfig def = shipdisp::DefaultViewConfig();
    const auto* straw = def.styleForVolume("a_Straw_tube");
    check(straw != nullptr, "Straw matches a default rule");
    if (straw) check(straw->color == "#20428A", "Straw rule colour");
    check(def.styleForVolume("no_such_subsystem") == nullptr, "unmatched volume -> nullptr");

    const std::string f1 = def.fallbackColorForVolume("no_such_subsystem");
    check(f1 == def.fallbackColorForVolume("no_such_subsystem"), "fallback colour deterministic");
    check(f1.size() == 7 && f1[0] == '#', "fallback colour is #RRGGBB");

    // Parse a view file. The style without a transparency must inherit
    // default_transparency (regression: it used to hardcode 55).
    const std::string path = testutil::tempPath("view.toml");
    std::ofstream(path) << R"(
hit_scale = 0.5

[geometry]
default_transparency = 70

[[geometry.style]]
match = "Magnet"
color = "#101010"

[[region]]
name = "R"
zmin = 10.0
zmax = 20.0
xmin = 5.0  # lone value, no xmax: must be ignored with a warning
)";
    const shipdisp::ViewConfig v = shipdisp::LoadViewConfig(path);
    check(v.hit_scale == 0.5, "hit_scale parsed");
    const auto* mag = v.styleForVolume("BigMagnet_1");
    check(mag != nullptr, "style rule parsed");
    if (mag) {
        check(mag->color == "#101010", "style colour parsed");
        check(mag->transparency == 70, "style inherits default_transparency");
    }
    check(v.regions.size() == 1, "one region parsed");
    if (!v.regions.empty()) {
        check(v.regions[0].has_window[2] && v.regions[0].wmin[2] == 10.0 &&
                  v.regions[0].wmax[2] == 20.0,
              "z window parsed");
        check(!v.regions[0].has_window[0], "lone xmin ignored");
    }

    return testutil::summary("test_view_config");
}

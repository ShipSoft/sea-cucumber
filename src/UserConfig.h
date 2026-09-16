// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_USERCONFIG_H
#define SHIPDISP_USERCONFIG_H

// =============================================================================
//  UserConfig.h
//
//  The *user's* configuration, as opposed to the view configuration. A view
//  file (views/*.toml) says what to draw and where the zoom regions sit, and is
//  meant to be shared; this one says how the display should look to you, and
//  lives on your machine. It is found along the usual paths, highest precedence
//  first:
//
//    --config <path>                               explicit; must exist
//    $SEA_CUCUMBER_CONFIG                          explicit; warns if missing
//    ./sea_cucumber.toml                           project-local
//    $XDG_CONFIG_HOME/sea_cucumber/config.toml     default ~/.config
//    <dir>/sea_cucumber/config.toml for each dir in $XDG_CONFIG_DIRS (/etc/xdg)
//    $CONDA_PREFIX/share/sea_cucumber/config.toml  shipped baseline
//
//  Every file that exists is applied, higher-precedence ones overwriting the
//  keys they set -- per key, so a config setting only [ui.fonts] menu leaves
//  the other categories alone. The whole stack lands on top of the view config,
//  so a user config wins over a view file's [ui] block.
//
//  Only [ui]/[ui.fonts] and [defaults] are honoured; anything else is named on
//  stderr and skipped (see OverlayViewConfigFile in ViewConfig.h).
//
//  The web frontend adds one more layer on top of this one, in the browser --
//  see web/js/prefs.js. The browser and the machine running make_web_data need
//  not be the same host, which is why per-viewer settings cannot live here.
// =============================================================================

#include <string>
#include <vector>

#include "ViewConfig.h"

namespace shipdisp {

/// The paths probed, LOWEST precedence first, so applying them in order is
/// correct. Pure string work -- nothing is opened -- so the order stays
/// testable without a filesystem. `cliPath` is --config and may be empty.
std::vector<std::string> UserConfigCandidates(const std::string& cliPath);

/// Those candidates that exist, lowest precedence first. Throws
/// std::runtime_error when `cliPath` is non-empty and is not a file: an
/// explicit --config that is not there is a mistake worth stopping for.
std::vector<std::string> ResolveUserConfigFiles(const std::string& cliPath);

/// What a user config may set that is needed *before* the view config loads.
/// Empty means "not set"; command-line flags still win.
struct UserDefaults {
    std::string view;      ///< [defaults] view     -- stands in for --view
    std::string geometry;  ///< [defaults] geometry -- stands in for --geometry
};

/// Read only [defaults] from the resolved stack. `skip` (from --no-config)
/// returns an empty result without touching the filesystem.
UserDefaults LoadUserDefaults(const std::string& cliPath, bool skip = false);

/// Layer the resolved stack's [ui] block onto `c`.
void ApplyUserConfig(const std::string& cliPath, ViewConfig& c, bool skip = false);

}  // namespace shipdisp

#endif  // SHIPDISP_USERCONFIG_H

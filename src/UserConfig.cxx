// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  UserConfig.cxx -- see header. Parsing is OverlayViewConfigFile's job; this
//  file only decides *which* files to hand it, and in what order.
// =============================================================================

#include "UserConfig.h"

#include <toml++/toml.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace shipdisp {

namespace {

constexpr const char* kConfigName = "config.toml";       // inside sea_cucumber/
constexpr const char* kLocalName = "sea_cucumber.toml";  // in the CWD

/// getenv, but an empty value counts as unset -- the XDG spec says so.
const char* Env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

/// Split a `:`-separated directory list. XDG requires absolute paths and says
/// to ignore anything else, so relative and empty entries are dropped.
std::vector<std::string> SplitDirs(const std::string& list) {
    std::vector<std::string> out;
    std::string::size_type from = 0;
    for (;;) {
        const auto to = list.find(':', from);
        const auto end = (to == std::string::npos) ? list.size() : to;
        std::string dir = list.substr(from, end - from);
        if (!dir.empty() && dir.front() == '/') out.push_back(dir);
        if (to == std::string::npos) break;
        from = to + 1;
    }
    return out;
}

}  // namespace

std::vector<std::string> UserConfigCandidates(const std::string& cliPath) {
    std::vector<std::string> out;  // lowest precedence first

    // Shipped baseline, mirroring how LoadViewConfig finds views/default.toml.
    if (const char* prefix = Env("CONDA_PREFIX")) {
        out.push_back(std::string(prefix) + "/share/sea_cucumber/" + kConfigName);
    }
    // System-wide. XDG_CONFIG_DIRS is most-important-first, and we build the
    // list least-important-first, so walk it backwards.
    {
        const char* dirs = Env("XDG_CONFIG_DIRS");
        const auto list = SplitDirs(dirs ? dirs : "/etc/xdg");
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            out.push_back(*it + "/sea_cucumber/" + kConfigName);
        }
    }
    // Per-user.
    const char* xdgHome = Env("XDG_CONFIG_HOME");
    if (xdgHome && xdgHome[0] == '/') {
        out.push_back(std::string(xdgHome) + "/sea_cucumber/" + kConfigName);
    } else if (const char* home = Env("HOME")) {
        out.push_back(std::string(home) + "/.config/sea_cucumber/" + kConfigName);
    }
    // Project-local. pixi runs from the repo root, the same assumption
    // LoadViewConfig makes when it probes views/default.toml.
    out.push_back(kLocalName);
    // Explicit, in increasing order of "the user meant it".
    if (const char* fromEnv = Env("SEA_CUCUMBER_CONFIG")) out.push_back(fromEnv);
    if (!cliPath.empty()) out.push_back(cliPath);
    return out;
}

std::vector<std::string> ResolveUserConfigFiles(const std::string& cliPath) {
    namespace fs = std::filesystem;
    if (!cliPath.empty() && !fs::is_regular_file(cliPath)) {
        throw std::runtime_error("--config '" + cliPath + "': no such file");
    }
    // A stale $SEA_CUCUMBER_CONFIG in a long-lived shell should not break every
    // run, so unlike --config it only warns and falls through.
    if (const char* fromEnv = Env("SEA_CUCUMBER_CONFIG")) {
        if (!fs::is_regular_file(fromEnv)) {
            std::cerr << "[UserConfig] $SEA_CUCUMBER_CONFIG='" << fromEnv
                      << "': no such file -- ignoring it\n";
        }
    }
    std::vector<std::string> out;
    for (const auto& cand : UserConfigCandidates(cliPath)) {
        if (fs::is_regular_file(cand)) out.push_back(cand);
    }
    return out;
}

UserDefaults LoadUserDefaults(const std::string& cliPath, bool skip) {
    UserDefaults d;
    if (skip) return d;
    for (const auto& path : ResolveUserConfigFiles(cliPath)) {
        toml::table tbl;
        try {
            tbl = toml::parse_file(path);
        } catch (const toml::parse_error&) {
            continue;  // ApplyUserConfig reports it; no need to say it twice
        }
        if (const auto* t = tbl["defaults"].as_table()) {
            d.view = (*t)["view"].value_or(d.view);
            d.geometry = (*t)["geometry"].value_or(d.geometry);
        }
    }
    return d;
}

void ApplyUserConfig(const std::string& cliPath, ViewConfig& c, bool skip) {
    if (skip) {
        std::cout << "[UserConfig] --no-config: using the view config alone\n";
        return;
    }
    for (const auto& path : ResolveUserConfigFiles(cliPath)) {
        if (OverlayViewConfigFile(path, c, /*ui_only=*/true)) {
            std::cout << "[UserConfig] applied '" << path << "'\n";
        }
    }
}

}  // namespace shipdisp

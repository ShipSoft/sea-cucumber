// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Checks the user-config search path: the order of the candidates, which of
//  them actually get applied, and that a user config layers onto a view config
//  per key rather than wholesale.
//
//  Every relevant environment variable is pinned to a scratch directory first.
//  Without that the test would pick up the developer's real ~/.config and the
//  CONDA_PREFIX pixi always sets, and fail on one machine but not another.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "UserConfig.h"
#include "ViewConfig.h"

namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

void write(const fs::path& p, const std::string& body) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

/// Index of `needle` in `hay`, or -1. Used to assert relative precedence
/// without hard-coding the full candidate list.
int indexOf(const std::vector<std::string>& hay, const std::string& needle) {
    for (std::size_t i = 0; i < hay.size(); ++i) {
        if (hay[i] == needle) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

int main() {
    using shipdisp::ApplyUserConfig;
    using shipdisp::ResolveUserConfigFiles;
    using shipdisp::UserConfigCandidates;

    const fs::path root = fs::temp_directory_path() / "sc_user_config";
    fs::remove_all(root);
    const fs::path home = root / "home";
    const fs::path xdgHome = root / "xdg";
    const fs::path sysA = root / "etc-a";
    const fs::path sysB = root / "etc-b";
    const fs::path prefix = root / "prefix";

    setenv("HOME", home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", xdgHome.c_str(), 1);
    setenv("XDG_CONFIG_DIRS", (sysA.string() + ":" + sysB.string()).c_str(), 1);
    setenv("CONDA_PREFIX", prefix.c_str(), 1);
    unsetenv("SEA_CUCUMBER_CONFIG");

    const std::string userCfg = (xdgHome / "sea_cucumber" / "config.toml").string();
    const std::string sysACfg = (sysA / "sea_cucumber" / "config.toml").string();
    const std::string sysBCfg = (sysB / "sea_cucumber" / "config.toml").string();
    const std::string prefixCfg = (prefix / "share" / "sea_cucumber" / "config.toml").string();
    const std::string localCfg = "sea_cucumber.toml";

    // --- candidate order (pure string work, nothing is opened) --------------
    {
        const auto c = UserConfigCandidates("/tmp/explicit.toml");
        // Lowest precedence first, so a later index wins.
        check(indexOf(c, prefixCfg) >= 0, "CONDA_PREFIX candidate present");
        check(indexOf(c, sysBCfg) > indexOf(c, prefixCfg), "XDG_CONFIG_DIRS beats CONDA_PREFIX");
        check(indexOf(c, sysACfg) > indexOf(c, sysBCfg), "earlier XDG_CONFIG_DIRS entry wins");
        check(indexOf(c, userCfg) > indexOf(c, sysACfg), "XDG_CONFIG_HOME beats XDG_CONFIG_DIRS");
        check(indexOf(c, localCfg) > indexOf(c, userCfg), "CWD beats XDG_CONFIG_HOME");
        check(indexOf(c, "/tmp/explicit.toml") == static_cast<int>(c.size()) - 1,
              "--config has the last word");
    }

    // XDG_CONFIG_HOME unset falls back to $HOME/.config.
    {
        unsetenv("XDG_CONFIG_HOME");
        const auto c = UserConfigCandidates("");
        check(indexOf(c, (home / ".config" / "sea_cucumber" / "config.toml").string()) >= 0,
              "$HOME/.config fallback");
        // A relative XDG_CONFIG_HOME must be ignored, per the spec.
        setenv("XDG_CONFIG_HOME", "relative/path", 1);
        const auto c2 = UserConfigCandidates("");
        check(indexOf(c2, "relative/path/sea_cucumber/config.toml") < 0,
              "relative XDG_CONFIG_HOME ignored");
        setenv("XDG_CONFIG_HOME", xdgHome.c_str(), 1);
    }

    // --- resolution: only existing files, still lowest-first ----------------
    write(sysACfg, "[ui]\ncolor_scheme = \"apprentice\"\n");
    write(userCfg, "[ui]\ncolor_scheme = \"dracula\"\n");
    {
        const auto files = ResolveUserConfigFiles("");
        check(files.size() == 2, "two configs found");
        check(!files.empty() && files.front() == sysACfg, "system config applied first");
        check(!files.empty() && files.back() == userCfg, "user config applied last");
    }

    // An explicit --config that is not there is a mistake worth stopping for.
    {
        bool threw = false;
        try {
            ResolveUserConfigFiles((root / "nope.toml").string());
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "missing --config throws");
    }

    // A stale $SEA_CUCUMBER_CONFIG only warns, and the rest still applies.
    {
        setenv("SEA_CUCUMBER_CONFIG", (root / "gone.toml").c_str(), 1);
        const auto files = ResolveUserConfigFiles("");
        check(files.size() == 2, "missing $SEA_CUCUMBER_CONFIG falls through");
        unsetenv("SEA_CUCUMBER_CONFIG");
    }

    // --- layering onto a view config ----------------------------------------
    const fs::path viewFile = root / "view.toml";
    write(viewFile,
          "[ui]\n"
          "font_scale = 1.0\n"
          "color_scheme = \"ship_original\"\n"
          "sidebar_width = 232\n"
          "[ui.fonts]\n"
          "menu = 13\n"
          "brand = 15\n");
    // The user config sets some of those keys and one table it may not touch.
    write(userCfg,
          "[ui]\n"
          "color_scheme = \"dracula\"\n"
          "[ui.fonts]\n"
          "menu = 16\n"
          "[geometry]\n"
          "db_file = \"not_yours.db\"\n");
    fs::remove(sysACfg);
    {
        shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile.string());
        const std::string dbBefore = view.geometry.db_file;
        ApplyUserConfig("", view);
        check(view.ui_color_scheme == "dracula", "user config overrides the view's color_scheme");
        check(view.ui_fonts["menu"] == 16, "user config overrides one font category");
        check(view.ui_fonts["brand"] == 15, "font categories merge per key");
        check(view.ui_font_scale == 1.0, "untouched [ui] keys keep the view's value");
        check(view.ui_sidebar_width == 232, "untouched sidebar_width keeps the view's value");
        check(view.geometry.db_file == dbBefore, "a user config may not set [geometry]");
    }

    // --no-config skips the search entirely.
    {
        shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile.string());
        ApplyUserConfig("", view, /*skip=*/true);
        check(view.ui_color_scheme == "ship_original", "--no-config leaves the view config alone");
    }

    // [defaults] is read separately, before the view config loads.
    {
        write(userCfg, "[defaults]\nview = \"v.toml\"\ngeometry = \"g.db\"\n");
        const auto d = shipdisp::LoadUserDefaults("");
        check(d.view == "v.toml", "[defaults] view");
        check(d.geometry == "g.db", "[defaults] geometry");
        check(shipdisp::LoadUserDefaults("", /*skip=*/true).view.empty(),
              "--no-config skips [defaults]");
    }

    // A malformed config is skipped without taking the earlier layers with it.
    {
        write(userCfg, "this is not = = toml\n");
        shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile.string());
        ApplyUserConfig("", view);
        check(view.ui_color_scheme == "ship_original", "unparsable config is skipped");
    }

    fs::remove_all(root);

    if (failures == 0) std::cout << "test_user_config: OK\n";
    return failures == 0 ? 0 : 1;
}

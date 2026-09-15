// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  make_geometry_cache -- one-off conversion of a GeoModel .db into a compact
//  sea_cucumber display cache, so the display starts without re-reading the
//  ~1M-volume database every launch.
//
//  This captures BOTH passes the display needs -- the shallow whole-detector
//  envelope pass and the deeper per-region pass -- into two cache files, since
//  they use different walk options. The display picks up each automatically.
//
//  Usage:
//    make_geometry_cache --geometry ship.db [--view views/default.toml]
//                        [--out-prefix geocache]
//  Produces <prefix>.main.root and <prefix>.region.root next to the .db's CWD.
// =============================================================================

#include <cstdlib>
#include <iostream>
#include <string>

#include "GeoModelGeometrySource.h"
#include "GeometryCache.h"
#include "ViewConfig.h"

int main(int argc, char* argv[]) {
    std::string geometry, viewFile, outPrefix = "geocache";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nxt = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--geometry")
            geometry = nxt();
        else if (a == "--view")
            viewFile = nxt();
        else if (a == "--out-prefix")
            outPrefix = nxt();
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: make_geometry_cache --geometry ship.db "
                         "[--view v.toml] [--out-prefix geocache]\n";
            return 0;
        }
    }
    if (geometry.empty()) {
        std::cerr << "error: --geometry is required\n";
        return 2;
    }

    const shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile);

    // Mirror the display's two walks exactly, so the caches are drop-in.
    shipdisp::GeoLoadOptions mainOpt;
    mainOpt.include = view.geometry.include;
    mainOpt.exclude = view.geometry.exclude;
    mainOpt.max_depth = view.geometry.max_depth;
    mainOpt.stop_at_match = view.geometry.stop_at_match;
    mainOpt.length_scale = view.hit_scale;

    shipdisp::GeoLoadOptions regionOpt = mainOpt;
    regionOpt.stop_at_match = false;
    regionOpt.max_depth = view.region_depth;
    if (view.has_region_exclude) regionOpt.exclude = view.region_exclude;
    regionOpt.exclude.insert(regionOpt.exclude.end(), {"Cavern", "World", "Rock", "Hall"});
    // The cache walks the whole detector with no z window (so one cache serves
    // every view), which means the DEFAULT 200k cap can truncate it before the
    // downstream detectors. Raise it well above the full deep-walk size; the
    // cap is only a runaway guard here.
    regionOpt.max_shapes = 5000000;

    try {
        shipdisp::GeoModelGeometrySource mainSrc(geometry, mainOpt);
        shipdisp::WriteGeometryCache(mainSrc, outPrefix + ".main.root", view.hit_scale,
                                     "main envelope pass of " + geometry);

        shipdisp::GeoModelGeometrySource regionSrc(geometry, regionOpt);
        shipdisp::WriteGeometryCache(
            regionSrc, outPrefix + ".region.root", view.hit_scale,
            "region deep pass (depth " + std::to_string(regionOpt.max_depth) + ") of " + geometry);
    } catch (const std::exception& e) {
        std::cerr << "[make_geometry_cache] failed: " << e.what() << "\n";
        return 1;
    }
    std::cout << "[make_geometry_cache] done: " << outPrefix << ".main.root, " << outPrefix
              << ".region.root\n";
    return 0;
}

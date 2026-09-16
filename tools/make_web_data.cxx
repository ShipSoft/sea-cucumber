// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  make_web_data -- produce the display files the web frontend consumes.
//
//  This is the C++ "producer" half of the ALICE-style split. It reuses the very
//  same geometry walk and event source the REve display uses, so the browser
//  shows the same data -- it only changes the OUTPUT: instead of REve elements,
//  it writes JSON.
//
//    geometry.json   tessellated meshes (vertices + triangle indices), colours
//                    and transparency resolved exactly as the REve path does
//    event_<i>.json  one file per event: hits (+ energy, pdg) and truth vertex
//    manifest.json   { nEvents, geometry, unit_mm_per_scene }
//
//  Tessellation: every TGeoShape can produce a triangle mesh through ROOT's
//  TBuffer3D (the same buffer the OpenGL viewer uses), so booleans, pcons, etc.
//  all work without special cases. We transform each mesh's vertices into the
//  world frame with the volume's matrix, and emit native millimetres -- the
//  frontend scales mm -> scene units itself (manifest.unit_mm_per_scene).
//
//  Usage:
//    make_web_data --geometry ship.db --data events.root [--view v.toml]
//                  [--out web/data] [--events all|<i>]
// =============================================================================

#include <TBuffer3D.h>
#include <TBuffer3DTypes.h>
#include <TGeoMatrix.h>
#include <TGeoShape.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "GeoModelGeometrySource.h"
#include "IEventSource.h"
#include "RNTupleEventSource.h"
#include "UserConfig.h"
#include "ViewConfig.h"

namespace {

// Minimal JSON string escaping (names can contain '/', quotes are unlikely but
// handled). Avoids a JSON dependency for this write-only tool.
std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                o += c;
        }
    }
    return o;
}

// Tessellate one shape and append its world-frame mesh to `out` as a JSON
// object. Returns false if the shape yielded no triangles.
bool writeMesh(std::ofstream& out, const std::string& name, TGeoShape* shape, const TGeoHMatrix& g,
               const std::string& colorHex, int transparency, bool first) {
    if (!shape) return false;

    // Use MakeBuffer3D(), NOT GetBuffer3D(): the latter fills a shared static
    // buffer via the geometry painter and dereferences painter/manager state
    // that a standalone shape doesn't have (it segfaults in FillBuffer3D even
    // for a plain box). MakeBuffer3D() allocates and fills its own buffer with
    // no painter dependency -- this is how extracted shapes are tessellated
    // without an active TGeoManager. We own the returned buffer.
    std::unique_ptr<TBuffer3D> buf(shape->MakeBuffer3D());
    if (!buf) return false;
    const TBuffer3D& b = *buf;
    const UInt_t nPts = b.NbPnts();
    const UInt_t nPols = b.NbPols();
    if (nPts == 0 || nPols == 0) return false;

    // Points are a flat [x0,y0,z0,x1,...] array in the shape's local frame; move
    // each into the world frame with the volume matrix.
    std::vector<double> vx;
    vx.reserve(nPts * 3);
    for (UInt_t i = 0; i < nPts; ++i) {
        Double_t local[3] = {b.fPnts[3 * i], b.fPnts[3 * i + 1], b.fPnts[3 * i + 2]};
        Double_t world[3];
        g.LocalToMaster(local, world);
        vx.push_back(world[0]);
        vx.push_back(world[1]);
        vx.push_back(world[2]);
    }

    // Triangulate. fPols is: [colour, nSegs, seg0, seg1, ..., colour, nSegs, ...].
    // A polygon's segments form a closed loop but are NOT given head-to-tail,
    // so we must reconstruct the vertex order by walking the edges as a graph:
    // each segment is an edge between two point indices, every vertex in a
    // simple polygon has exactly two incident edges, so we start anywhere and
    // follow "the neighbour we didn't just come from" until the loop closes.
    std::vector<int> idx;
    const Int_t* segs = b.fSegs;
    const Int_t* pols = b.fPols;
    UInt_t q = 0;
    for (UInt_t p = 0; p < nPols; ++p) {
        const Int_t nSeg = pols[q + 1];
        // Edges of this polygon (pairs of point indices).
        std::vector<std::pair<int, int>> edges;
        edges.reserve(nSeg);
        for (Int_t s = 0; s < nSeg; ++s) {
            const Int_t segIdx = pols[q + 2 + s];
            edges.emplace_back(segs[3 * segIdx + 1], segs[3 * segIdx + 2]);
        }
        q += 2 + nSeg;

        // Walk the edges into an ordered loop.
        std::vector<int> loop;
        loop.reserve(nSeg);
        std::vector<char> used(edges.size(), 0);
        // Start from the first edge.
        loop.push_back(edges[0].first);
        int cur = edges[0].second;
        used[0] = 1;
        loop.push_back(cur);
        for (Int_t step = 1; step < nSeg; ++step) {
            bool found = false;
            for (std::size_t e = 0; e < edges.size(); ++e) {
                if (used[e]) continue;
                int nxt = -1;
                if (edges[e].first == cur)
                    nxt = edges[e].second;
                else if (edges[e].second == cur)
                    nxt = edges[e].first;
                if (nxt >= 0) {
                    used[e] = 1;
                    cur = nxt;
                    found = true;
                    break;
                }
            }
            if (!found) break;               // open/broken polygon; stop
            if (cur == loop.front()) break;  // closed the loop
            loop.push_back(cur);
        }

        // Fan-triangulate the ordered loop, skipping degenerate triangles.
        for (std::size_t t = 1; t + 1 < loop.size(); ++t) {
            const int a = loop[0], c1 = loop[t], c2 = loop[t + 1];
            if (a == c1 || c1 == c2 || a == c2) continue;
            idx.push_back(a);
            idx.push_back(c1);
            idx.push_back(c2);
        }
    }
    if (idx.empty()) return false;

    if (!first) out << ",\n";
    out << "  {\"name\":\"" << jsonEscape(name) << "\",\"color\":\"" << colorHex
        << "\",\"transparency\":" << transparency << ",\"vertices\":[";
    for (std::size_t i = 0; i < vx.size(); ++i) {
        if (i) out << ',';
        out << vx[i];
    }
    out << "],\"indices\":[";
    for (std::size_t i = 0; i < idx.size(); ++i) {
        if (i) out << ',';
        out << idx[i];
    }
    out << "]}";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string geometry, dataFile, viewFile, configFile, outDir = "web/data", eventsArg = "all";
    bool noConfig = false;
    int webDepth = 4;                  // deeper than the envelope pass
    std::size_t webMaxShapes = 20000;  // keep geometry.json a sane size
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        // A flag with no value is an error, not an empty string: "--config" with
        // nothing after it would otherwise fall back to the config search, and
        // "--depth" would reach std::stoi(""). Same as next() in sea_cucumber.cxx.
        auto nxt = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--geometry")
            geometry = nxt("--geometry");
        else if (a == "--data")
            dataFile = nxt("--data");
        else if (a == "--view")
            viewFile = nxt("--view");
        else if (a == "--config")
            configFile = nxt("--config");
        else if (a == "--no-config")
            noConfig = true;
        else if (a == "--out")
            outDir = nxt("--out");
        else if (a == "--events")
            eventsArg = nxt("--events");
        else if (a == "--depth")
            webDepth = std::stoi(nxt("--depth"));
        else if (a == "--max-shapes")
            webMaxShapes = static_cast<std::size_t>(std::stoll(nxt("--max-shapes")));
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: make_web_data --geometry ship.db --data events.root "
                         "[--view v.toml] [--config c.toml] [--no-config] "
                         "[--out web/data] [--events all|<i>] "
                         "[--depth 4] [--max-shapes 20000]\n"
                         "  --config     user config to apply over the view's [ui] block;\n"
                         "               without it the usual paths are searched\n"
                         "               ($SEA_CUCUMBER_CONFIG, ./sea_cucumber.toml,\n"
                         "               $XDG_CONFIG_HOME/sea_cucumber/config.toml, ...)\n"
                         "  --no-config  skip that search entirely\n";
            return 0;
        }
    }

    // The user config can supply --view / --geometry, so it is read first; an
    // explicit flag still wins. Its [ui] block then lands on top of the view
    // config, and make_web_data bakes the result into manifest.json.
    shipdisp::UserDefaults defaults;
    shipdisp::ViewConfig view;
    try {
        defaults = shipdisp::LoadUserDefaults(configFile, noConfig);
        if (viewFile.empty()) viewFile = defaults.view;
        view = shipdisp::LoadViewConfig(viewFile);
        shipdisp::ApplyUserConfig(configFile, view, noConfig);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
    if (geometry.empty()) geometry = defaults.geometry;
    if (geometry.empty() || dataFile.empty()) {
        std::cerr << "error: --geometry and --data are required\n";
        return 2;
    }

    // Make the output directory (portable: shell out to mkdir -p).
    // Best-effort: copy the square logo next to the served page so the sidebar
    // and favicon find it at web/logo/sc.png. outDir is typically web/data, so
    // the web root is its parent.
    {
        std::string webRoot = outDir;
        const std::string tail = "/data";
        if (webRoot.size() >= tail.size() &&
            webRoot.compare(webRoot.size() - tail.size(), tail.size(), tail) == 0) {
            webRoot = webRoot.substr(0, webRoot.size() - tail.size());
        }
        std::system(("mkdir -p '" + webRoot + "/logo' && cp -f logo/sc.png '" + webRoot +
                     "/logo/sc.png' 2>/dev/null")
                        .c_str());
        // Copy view-setup configs (default_setup etc.) so the served copy under
        // web/configs stays in sync with the repo's configs/.
        std::system(("mkdir -p '" + webRoot + "/configs' && cp -f configs/*.json '" + webRoot +
                     "/configs/' 2>/dev/null")
                        .c_str());
        // Keep the served VERSION in sync with the repo's.
        std::system(("cp -f VERSION '" + webRoot + "/VERSION' 2>/dev/null").c_str());
    }
    std::string mk = "mkdir -p '" + outDir + "'";
    if (std::system(mk.c_str()) != 0) {
        std::cerr << "error: cannot create output dir '" << outDir << "'\n";
        return 1;
    }

    // --- geometry.json ------------------------------------------------------
    // Walk exactly as the display's main pass does (envelopes), honouring the
    // view's geometry include/exclude/depth so the browser shows the same set.
    // Walk deeper than the REve "envelope" pass: for the web view we want real
    // detector geometry, not just the outer shells. Descend past envelopes
    // (stop_at_match = false) to a bounded depth, drop the big cavern/world
    // shells, and cap the shape count so the JSON stays a sane size.
    shipdisp::GeoLoadOptions opt;
    opt.include = view.geometry.include;
    opt.exclude = view.geometry.exclude;
    if (view.has_region_exclude)
        opt.exclude.insert(opt.exclude.end(), view.region_exclude.begin(),
                           view.region_exclude.end());
    opt.exclude.insert(opt.exclude.end(), {"Cavern", "World", "Rock", "Hall"});
    opt.max_depth = webDepth;
    opt.stop_at_match = false;
    opt.max_shapes = webMaxShapes;
    opt.length_scale = 1.0;  // emit native millimetres; the frontend scales

    const std::string geoPath = outDir + "/geometry.json";
    std::ofstream gj(geoPath);
    if (!gj) {
        std::cerr << "error: cannot write '" << geoPath << "'\n";
        return 1;
    }
    gj << "{\"meshes\":[\n";
    std::size_t nMesh = 0, nSkipped = 0;
    bool firstMesh = true;
    try {
        shipdisp::GeoModelGeometrySource src(geometry, opt);
        src.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int) {
            // Colour: an explicit per-volume match colour from the config wins;
            // otherwise pick from a curated palette by hashing the name. It is
            // blue-dominant (six blues) with a cream and a muted purple worked
            // in for variety, and deliberately excludes the bright hit pink
            // (#C64284) so hits stay clearly visible against the geometry.
            static const char* kPalette[] = {"#0A1F44", "#12305F", "#20428A", "#3A6BBF",
                                             "#6E97D6", "#A9C4E8", "#E8CFA0", "#7A6A9E"};
            const std::string matched = view.colorForVolume(name);
            std::string color = matched;
            if (matched == view.geometry.default_color) {
                std::size_t h = 1469598103934665603ull;  // FNV-1a over the name
                for (char c : name) {
                    h ^= static_cast<unsigned char>(c);
                    h *= 1099511628211ull;
                }
                color = kPalette[h % (sizeof(kPalette) / sizeof(kPalette[0]))];
            }
            const int transp = view.transparencyForVolume(name);
            if (writeMesh(gj, name, shape, g, color, transp, firstMesh)) {
                firstMesh = false;
                ++nMesh;
            } else {
                ++nSkipped;
            }
            delete shape;  // emit contract transfers ownership to us here
        });
    } catch (const std::exception& e) {
        std::cerr << "error: geometry walk failed: " << e.what() << "\n";
        return 1;
    }
    gj << "\n]}\n";
    gj.close();
    std::cout << "[make_web_data] geometry.json: " << nMesh << " meshes (" << nSkipped
              << " empty/skipped)\n";

    // --- events -------------------------------------------------------------
    shipdisp::RNTupleEventSource source(dataFile, view.ntuple);
    const std::int64_t nEv = source.numEvents();
    std::int64_t lo = 0, hi = nEv;
    if (eventsArg != "all") {
        const std::int64_t one = std::stoll(eventsArg);
        lo = one;
        hi = one + 1;
    }
    std::int64_t written = 0;
    for (std::int64_t i = lo; i < hi && i < nEv; ++i) {
        if (!source.loadEvent(i)) continue;
        const std::string ep = outDir + "/event_" + std::to_string(i) + ".json";
        std::ofstream ej(ep);
        if (!ej) {
            std::cerr << "warning: cannot write '" << ep << "'\n";
            continue;
        }
        ej << "{\"event\":" << i << ",\"hits\":[";
        const auto& hits = source.hits();
        for (std::size_t h = 0; h < hits.size(); ++h) {
            const auto& sh = hits[h];
            if (h) ej << ',';
            ej << "{\"x\":" << sh.position[0] << ",\"y\":" << sh.position[1]
               << ",\"z\":" << sh.position[2] << ",\"e\":" << sh.energyDeposit
               << ",\"pdg\":" << sh.pdgCode << "}";
        }
        ej << "]";

        // Truth decay vertex: first MC particle that has a mother.
        const auto& mc = source.mcParticles();
        for (const auto& p : mc) {
            if (p.motherId >= 0) {
                ej << ",\"vertex\":{\"x\":" << p.vertex[0] << ",\"y\":" << p.vertex[1]
                   << ",\"z\":" << p.vertex[2] << "}";
                break;
            }
        }
        ej << "}\n";
        ++written;
    }
    std::cout << "[make_web_data] wrote " << written << " event file(s)\n";

    // --- manifest.json ------------------------------------------------------
    // Include the region definitions so the web client can build the three zoom
    // panels (Spectrometer / Calorimeter / SND), mirroring the REve views.
    // Only explicit windows are emitted; name-derived (`match`) regions would
    // need the geometry scan and are left for the client to ignore for now.
    const std::string mp = outDir + "/manifest.json";
    std::ofstream mj(mp);
    mj << "{\"nEvents\":" << nEv << ",\"geometry\":\"geometry.json\","
       << "\"unit_mm_per_scene\":1000,\"ui\":{\"font_scale\":" << view.ui_font_scale;
    if (view.ui_sidebar_width > 0) mj << ",\"sidebar_width\":" << view.ui_sidebar_width;
    if (!view.ui_color_scheme.empty())
        mj << ",\"color_scheme\":\"" << jsonEscape(view.ui_color_scheme) << "\"";
    mj << ",\"fonts\":{";
    {
        bool firstF = true;
        for (const auto& [k, v] : view.ui_fonts) {
            if (!firstF) mj << ",";
            firstF = false;
            mj << "\"" << jsonEscape(k) << "\":" << v;
        }
    }
    mj << "}},\"regions\":[";
    bool firstR = true;
    for (const auto& r : view.regions) {
        if (!r.hasAnyWindow()) continue;  // client can't place a match-only region
        if (!firstR) mj << ",";
        firstR = false;
        mj << "{\"name\":\"" << jsonEscape(r.name) << "\",\"camera\":\"" << jsonEscape(r.camera)
           << "\",\"window\":{";
        static const char* kAx[3] = {"x", "y", "z"};
        bool firstAx = true;
        for (int ax = 0; ax < 3; ++ax) {
            if (!r.has_window[ax]) continue;
            if (!firstAx) mj << ",";
            firstAx = false;
            mj << "\"" << kAx[ax] << "\":[" << r.wmin[ax] << "," << r.wmax[ax] << "]";
        }
        mj << "}";  // close window
        // Optional panel layout (CSS px). Emitted only when set, so the client
        // can fall back to a default cascade otherwise.
        if (r.panel_x >= 0 || r.panel_y >= 0 || r.panel_w >= 0 || r.panel_h >= 0) {
            mj << ",\"panel\":{";
            bool firstP = true;
            auto emitP = [&](const char* k, double val) {
                if (val < 0) return;
                if (!firstP) mj << ",";
                firstP = false;
                mj << "\"" << k << "\":" << val;
            };
            emitP("x", r.panel_x);
            emitP("y", r.panel_y);
            emitP("w", r.panel_w);
            emitP("h", r.panel_h);
            mj << "}";
        }
        mj << "}";  // close region
    }
    mj << "]}\n";
    mj.close();
    std::cout << "[make_web_data] manifest.json: " << nEv << " events, geometry.json, "
              << "regions emitted\n"
              << "[make_web_data] done -> " << outDir << " (serve with: pixi run web)\n";
    return 0;
}

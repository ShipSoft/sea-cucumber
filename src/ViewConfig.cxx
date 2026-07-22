// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  ViewConfig.cxx -- see header. Uses tomlplusplus (this package ships
//  toml++/toml.h).
// =============================================================================

#include "ViewConfig.h"

#include <toml++/toml.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace shipdisp {

namespace {

// SHiP outreach palette.
constexpr const char* kNavy = "#081B3C";
constexpr const char* kBlue = "#20428A";
constexpr const char* kPink = "#C64284";
constexpr const char* kCream = "#F1DEBC";

// Templated so it accepts both node_view<node> (from a mutable table) and
// node_view<const node> (from `const toml::table*`, e.g. inside an
// array-of-tables loop) -- these are distinct types in toml++.
template <typename NodeView>
std::vector<std::string> toStringVec(const NodeView& n) {
    std::vector<std::string> out;
    if (const auto* arr = n.as_array()) {
        for (const auto& e : *arr) {
            // `e`'s type is dependent now that this is a template, so the
            // `template` disambiguator is required before value<>.
            if (auto s = e.template value<std::string>()) out.push_back(*s);
        }
    }
    return out;
}

}  // namespace

ViewConfig DefaultViewConfig() {
    ViewConfig c;
    c.geometry.styles = {
        // Specific before broad; first match wins.
        {"DecayVolume", kCream, 55}, {"Vessel", kCream, 55},
        {"Decay", kCream, 60},       {"Straw", kBlue, 55},
        {"Tracker", kBlue, 55},      {"Magnet", kBlue, 60},
        {"Timing", kBlue, 50},       {"UpstreamTagger", kBlue, 60},
        {"SBT", kBlue, 80},          {"ECAL", kNavy, 45},
        {"HCAL", kNavy, 50},         {"Calorimeter", kNavy, 50},
        {"MuonShield", kNavy, 60},   {"Muon", kNavy, 60},
        {"Target", kPink, 40},       {"Neutrino", kNavy, 55},
        {"SND", kNavy, 55},          {"Cavern", kNavy, 90},
    };
    c.geometry.default_color = kBlue;
    c.geometry.default_transparency = 45;

    // The three zoomed viewers, in screen order (top, middle, bottom right).
    RegionView spectro;
    spectro.name = "Spectrometer";
    spectro.title = "Spectrometer (side view)";
    spectro.has_window[2] = true;
    spectro.wmin[2] = 83000.0;
    spectro.wmax[2] = 93000.0;
    spectro.camera = "xz";

    RegionView calo;
    calo.name = "Calorimeter";
    calo.title = "Calorimeter (side view)";
    calo.has_window[2] = true;
    calo.wmin[2] = 96000.0;
    calo.wmax[2] = 98000.0;
    calo.camera = "xz";

    // No hardcoded z: derive the SND window from the geometry by name, so this
    // works on any geometry that labels its neutrino detector sensibly.
    RegionView snd;
    snd.name = "SND";
    snd.title = "Scattering and Neutrino Detector (side view)";
    snd.match = "SND|Neutrino|Scattering|Emulsion";
    snd.camera = "xz";

    c.regions = {spectro, calo, snd};
    return c;
}

const std::string& ViewConfig::colorForVolume(const std::string& name) const {
    for (const auto& s : geometry.styles) {
        if (name.find(s.match) != std::string::npos) return s.color;
    }
    return geometry.default_color;
}

int ViewConfig::transparencyForVolume(const std::string& name) const {
    for (const auto& s : geometry.styles) {
        if (name.find(s.match) != std::string::npos) return s.transparency;
    }
    return geometry.default_transparency;
}

ViewConfig LoadViewConfig(const std::string& path) {
    ViewConfig c = DefaultViewConfig();
    if (path.empty()) return c;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        std::cerr << "[ViewConfig] could not parse '" << path << "': " << e.description()
                  << " -- using defaults\n";
        return c;
    }

    c.hit_scale = tbl["hit_scale"].value_or(c.hit_scale);
    c.scan_depth = tbl["scan_depth"].value_or(c.scan_depth);
    c.region_depth = tbl["region_depth"].value_or(c.region_depth);
    c.ntuple = tbl["ntuple"].value_or(c.ntuple);

    if (auto g = tbl["geometry"]; g.is_table()) {
        c.geometry.db_file = g["db_file"].value_or(c.geometry.db_file);
        c.geometry.max_depth = g["max_depth"].value_or(c.geometry.max_depth);
        c.geometry.stop_at_match = g["stop_at_match"].value_or(c.geometry.stop_at_match);
        c.geometry.default_color = g["default_color"].value_or(c.geometry.default_color);
        c.geometry.default_transparency =
            g["default_transparency"].value_or(c.geometry.default_transparency);
        if (auto inc = g["include"]; inc) c.geometry.include = toStringVec(inc);
        if (auto exc = g["exclude"]; exc) c.geometry.exclude = toStringVec(exc);

        if (const auto* styles = g["style"].as_array()) {
            c.geometry.styles.clear();
            for (const auto& st : *styles) {
                if (const auto* t = st.as_table()) {
                    SubsystemStyle s;
                    s.match = (*t)["match"].value_or(std::string{});
                    s.color = (*t)["color"].value_or(c.geometry.default_color);
                    s.transparency = (*t)["transparency"].value_or(55);
                    if (!s.match.empty()) c.geometry.styles.push_back(s);
                }
            }
        }
    }

    if (auto h = tbl["hits"]; h.is_table()) {
        c.hits.marker_style = h["marker_style"].value_or(c.hits.marker_style);
        c.hits.marker_size = static_cast<float>(h["marker_size"].value_or(c.hits.marker_size));
        c.hits.color_by_energy = h["color_by_energy"].value_or(c.hits.color_by_energy);
        c.hits.color_low = h["color_low"].value_or(c.hits.color_low);
        c.hits.color_high = h["color_high"].value_or(c.hits.color_high);
    }

    if (auto m = tbl["decay"]; m.is_table()) {
        c.decay.draw = m["draw"].value_or(c.decay.draw);
        c.decay.color = m["color"].value_or(c.decay.color);
        c.decay.marker_style = m["marker_style"].value_or(c.decay.marker_style);
        c.decay.marker_size = static_cast<float>(m["marker_size"].value_or(c.decay.marker_size));
    }

    if (auto re = tbl["region_exclude"]; re) {
        c.region_exclude = toStringVec(re);
        c.has_region_exclude = true;
    }

    // [[region]] array-of-tables replaces the built-in three when present.
    if (const auto* regions = tbl["region"].as_array()) {
        std::vector<RegionView> rv;
        for (const auto& r : *regions) {
            const auto* t = r.as_table();
            if (!t) continue;
            RegionView v;
            v.name = (*t)["name"].value_or(v.name);
            v.title = (*t)["title"].value_or(v.name);
            v.match = (*t)["match"].value_or(v.match);
            v.camera = (*t)["camera"].value_or(v.camera);
            if (auto ex = (*t)["exclude"]; ex) v.exclude = toStringVec(ex);
            if (auto in = (*t)["include"]; in) v.include = toStringVec(in);
            v.max_shapes = static_cast<std::size_t>(
                (*t)["max_shapes"].value_or(static_cast<std::int64_t>(v.max_shapes)));
            v.max_transparency = (*t)["max_transparency"].value_or(v.max_transparency);
            v.margin_frac = (*t)["margin_frac"].value_or(v.margin_frac);
            v.recenter = (*t)["recenter"].value_or(v.recenter);
            v.hit_marker_size =
                static_cast<float>((*t)["hit_marker_size"].value_or(v.hit_marker_size));
            v.draw_decay = (*t)["draw_decay"].value_or(v.draw_decay);
            v.decay_clip = (*t)["decay_clip"].value_or(v.decay_clip);
            v.decay_marker_size =
                static_cast<float>((*t)["decay_marker_size"].value_or(v.decay_marker_size));
            v.offset_x = (*t)["offset_x"].value_or(v.offset_x);
            v.offset_y = (*t)["offset_y"].value_or(v.offset_y);
            v.offset_z = (*t)["offset_z"].value_or(v.offset_z);
            // Per-axis windows: xmin/xmax, ymin/ymax, zmin/zmax (any subset).
            static const char* kMinKey[3] = {"xmin", "ymin", "zmin"};
            static const char* kMaxKey[3] = {"xmax", "ymax", "zmax"};
            for (int ax = 0; ax < 3; ++ax) {
                const auto a = (*t)[kMinKey[ax]].template value<double>();
                const auto b = (*t)[kMaxKey[ax]].template value<double>();
                if (a && b) {
                    v.wmin[ax] = std::min(*a, *b);
                    v.wmax[ax] = std::max(*a, *b);
                    v.has_window[ax] = true;
                } else if (a || b) {
                    std::cerr << "[ViewConfig] region '" << v.name << "': " << kMinKey[ax]
                              << "/" << kMaxKey[ax]
                              << " must be given as a pair -- ignoring the lone value\n";
                }
            }

            // Sanity-check the window against the camera. This is a warning,
            // never an error: we always use whatever windows were supplied,
            // preferring z when it is present.
            const bool front = (v.camera == "xy" || v.camera == "yx");
            if (front && !v.has_window[0] && !v.has_window[1]) {
                if (v.has_window[2]) {
                    std::cerr << "[ViewConfig] region '" << v.name
                              << "': camera \"" << v.camera
                              << "\" is a front view (looking along z), which normally selects "
                                 "an x or y slab, but only zmin/zmax were given -- using the z "
                                 "window\n";
                } else if (v.match.empty()) {
                    std::cerr << "[ViewConfig] region '" << v.name
                              << "': camera \"" << v.camera
                              << "\" but no window at all -- set xmin/xmax (or ymin/ymax, or "
                                 "zmin/zmax), or a `match` pattern\n";
                }
            }
            if (!front && !v.has_window[2] && (v.has_window[0] || v.has_window[1])) {
                std::cerr << "[ViewConfig] region '" << v.name << "': camera \"" << v.camera
                          << "\" normally selects a z slab, but only an x/y window was given "
                             "-- using it\n";
            }
            rv.push_back(v);
        }
        if (!rv.empty()) c.regions = rv;
    }

    std::cout << "[ViewConfig] loaded '" << path << "'\n";
    return c;
}

}  // namespace shipdisp

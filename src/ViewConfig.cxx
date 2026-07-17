// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  ViewConfig.cxx -- see header. Uses tomlplusplus (conda-forge tomlplusplus;
//  this package ships toml++/toml.h).
// =============================================================================

#include "ViewConfig.h"

#include <toml++/toml.h>

#include <iostream>

namespace shipdisp {

namespace {

// SHiP outreach palette.
constexpr const char* kNavy = "#081B3C";
constexpr const char* kBlue = "#20428A";
constexpr const char* kPink = "#C64284";
constexpr const char* kCream = "#F1DEBC";

std::vector<std::string> toStringVec(const toml::node_view<toml::node>& n) {
    std::vector<std::string> out;
    if (const auto* arr = n.as_array())
        for (const auto& e : *arr)
            if (auto s = e.value<std::string>()) out.push_back(*s);
    return out;
}

}  // namespace

ViewConfig DefaultViewConfig() {
    ViewConfig c;
    c.geometry.styles = {
        // Specific before broad; first match wins.
        {"DecayVolume", kCream, 55}, {"Vessel", kCream, 55},        {"Decay", kCream, 60},
        {"Straw", kBlue, 55},        {"Tracker", kBlue, 55},        {"Magnet", kBlue, 60},
        {"Timing", kBlue, 50},       {"UpstreamTagger", kBlue, 60}, {"SBT", kBlue, 80},
        {"ECAL", kNavy, 45},         {"HCAL", kNavy, 50},           {"Calorimeter", kNavy, 50},
        {"MuonShield", kNavy, 60},   {"Muon", kNavy, 60},           {"Target", kPink, 40},
        {"Neutrino", kNavy, 65},     {"Cavern", kNavy, 90},
    };
    c.geometry.default_color = kBlue;
    c.geometry.default_transparency = 45;
    return c;
}

const std::string& ViewConfig::colorForVolume(const std::string& name) const {
    for (const auto& s : geometry.styles)
        if (name.find(s.match) != std::string::npos) return s.color;
    return geometry.default_color;
}

int ViewConfig::transparencyForVolume(const std::string& name) const {
    for (const auto& s : geometry.styles)
        if (name.find(s.match) != std::string::npos) return s.transparency;
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
    c.ntuple = tbl["ntuple"].value_or(c.ntuple);

    if (auto d = tbl["downstream"]; d.is_table()) {
        c.downstream.auto_window = d["auto"].value_or(c.downstream.auto_window);
        c.downstream.zmin = d["zmin"].value_or(c.downstream.zmin);
        c.downstream.zmax = d["zmax"].value_or(c.downstream.zmax);
    }

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

    std::cout << "[ViewConfig] loaded '" << path << "'\n";
    return c;
}

}  // namespace shipdisp

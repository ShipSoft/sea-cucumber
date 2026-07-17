// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  ViewConfig.cxx -- see header.
//
//  Uses tomlplusplus (conda-forge `tomlplusplus`). If your install exposes a
//  different include path, only the include line below changes.
// =============================================================================

#include "ViewConfig.h"

#include <toml++/toml.hpp>

#include <iostream>

namespace shipdisp {

namespace {

// ROOT colour indices for the SHiP subsystems. Plain ints so ViewConfig has no
// ROOT dependency; the core maps them to Color_t. Values are ROOT's named
// colours (kAzure=860, kPink=900, kOrange=800, kCyan=432, kSpring=820,
// kViolet=880, kTeal=840, kYellow=400, kGray+1=921, kRed=632, kGreen=416).
constexpr int kAzure = 860, kPink = 900, kOrange = 800, kCyan = 432, kSpring = 820,
              kViolet = 880, kTeal = 840, kGrayP1 = 921, kGreen = 416, kRed = 632;

std::vector<std::string> toStringVec(const toml::node_view<const toml::node>& n) {
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
        // First match wins; keep the specific ones before broad ones.
        {"Straw", kAzure, 55},        {"Tracker", kAzure, 60},
        {"ECAL", kPink, 45},          {"HCAL", kOrange, 50},
        {"Calorimeter", kPink, 55},   {"SBT", kCyan, 75},
        {"DecayVolume", kCyan, 80},   {"Magnet", kSpring, 65},
        {"MuonShield", kViolet, 60},  {"Muon", kViolet, 60},
        {"Target", kRed, 40},         {"UpstreamTagger", kTeal, 60},
        {"Timing", kGreen, 55},       {"Neutrino", kGrayP1, 65},
        {"Cavern", kGrayP1, 88},
    };
    c.geometry.default_color = kGrayP1;
    c.geometry.default_transparency = 70;
    return c;
}

int ViewConfig::colorForVolume(const std::string& name) const {
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

    if (auto g = tbl["geometry"]; g.is_table()) {
        c.geometry.db_file = g["db_file"].value_or(c.geometry.db_file);
        c.geometry.max_depth = g["max_depth"].value_or(c.geometry.max_depth);
        c.geometry.stop_at_match = g["stop_at_match"].value_or(c.geometry.stop_at_match);
        c.geometry.default_transparency =
            g["default_transparency"].value_or(c.geometry.default_transparency);
        if (auto inc = g["include"]; inc) c.geometry.include = toStringVec(inc);
        if (auto exc = g["exclude"]; exc) c.geometry.exclude = toStringVec(exc);

        // [[geometry.style]] array-of-tables overrides the default palette.
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
    }

    if (auto pt = tbl["particles"]; pt.is_table()) {
        c.particles.draw_mc = pt["draw_mc"].value_or(c.particles.draw_mc);
        c.particles.draw_sim = pt["draw_sim"].value_or(c.particles.draw_sim);
        c.particles.draw_rec = pt["draw_rec"].value_or(c.particles.draw_rec);
        c.particles.direction_length_mm =
            static_cast<float>(pt["direction_length_mm"].value_or(c.particles.direction_length_mm));
    }

    std::cout << "[ViewConfig] loaded '" << path << "'\n";
    return c;
}

}  // namespace shipdisp

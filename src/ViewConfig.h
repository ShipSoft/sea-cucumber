// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_VIEWCONFIG_H
#define SHIPDISP_VIEWCONFIG_H

// =============================================================================
//  ViewConfig.h
//
//  The display's *view* configuration: which volumes to draw and in what
//  colour, how to style hits, where the "downstream" zoom region is, and the
//  palette. Colours are hex strings ("#RRGGBB"), resolved to ROOT colours in
//  the core via TColor::GetColor, so ViewConfig stays free of any ROOT include.
//  Lengths are millimetres (EDM/geometry native).
//
//  Default palette (SHiP outreach):
//    #081B3C dark navy   #20428A blue   #C64284 pink   #F1DEBC cream
// =============================================================================

#include <string>
#include <vector>

namespace shipdisp {

/// Any volume whose name contains `match` is drawn with `color` (hex) at
/// `transparency` (0..100). First matching rule wins.
struct SubsystemStyle {
    std::string match;
    std::string color = "#20428A";
    int transparency = 55;
};

struct GeometryConfig {
    std::string db_file = "ship_geometry.db";
    std::vector<std::string> include;  // ECMAScript regex; empty => all
    std::vector<std::string> exclude;
    int max_depth = -1;
    bool stop_at_match = true;
    std::vector<SubsystemStyle> styles;  // first match wins
    std::string default_color = "#20428A";
    int default_transparency = 45;
};

struct HitStyle {
    int marker_style = 20;
    float marker_size = 2.0f;
    // If true, colour hits on a low->high energy ramp between color_low and
    // color_high; else colour every hit color_high.
    bool color_by_energy = true;
    std::string color_low = "#F1DEBC";   // cream (low energy)
    std::string color_high = "#C64284";  // pink (high energy)
};

// Truth decay-vertex marker (the one piece of MC truth the demo shows).
struct DecayMarker {
    bool draw = true;
    std::string color = "#F1DEBC";
    int marker_style = 29;  // filled star
    float marker_size = 3.0f;
};

// The downstream sub-detector window the right-hand viewers zoom onto (mm, z).
struct Downstream {
    bool auto_window = true;  // derive the window from the geometry (agnostic)
    double zmin = 82000.0;    // used only when auto_window = false
    double zmax = 95000.0;
};

struct ViewConfig {
    double hit_scale = 0.01;
    std::string ntuple = "events";
    GeometryConfig geometry;
    HitStyle hits;
    DecayMarker decay;
    Downstream downstream;

    const std::string& colorForVolume(const std::string& name) const;
    int transparencyForVolume(const std::string& name) const;
};

ViewConfig LoadViewConfig(const std::string& path);
ViewConfig DefaultViewConfig();

}  // namespace shipdisp

#endif  // SHIPDISP_VIEWCONFIG_H

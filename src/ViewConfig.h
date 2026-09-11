// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_VIEWCONFIG_H
#define SHIPDISP_VIEWCONFIG_H

// =============================================================================
//  ViewConfig.h
//
//  The display's *view* configuration: which volumes to draw and in what
//  colour, how to style hits, and which sub-detector REGIONS get their own
//  zoomed viewer. Colours are hex strings ("#RRGGBB"), resolved to ROOT colours
//  in the core via TColor::GetColor, so ViewConfig stays free of any ROOT
//  include. Lengths are millimetres (EDM/geometry native).
//
//  Palette (SHiP outreach):
//    #081B3C dark navy   #20428A blue   #C64284 pink   #F1DEBC cream
// =============================================================================

#include <cstddef>
#include <map>
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
    float marker_size = 2.5f;
    // If true, energy is encoded by marker SIZE (colour stays `color_high`).
    bool color_by_energy = true;
    std::string color_low = "#F1DEBC";
    std::string color_high = "#C64284";
};

/// Truth decay-vertex marker.
struct DecayMarker {
    bool draw = true;
    std::string color = "#F1DEBC";
    int marker_style = 29;  // filled star
    float marker_size = 3.0f;
};

/// One zoomed viewer onto a sub-detector region.
///
/// The z window is either given explicitly (zmin/zmax, mm) or derived from the
/// geometry by matching volume names against `match` (an ECMAScript regex, so
/// alternations like "SND|Neutrino" work). Explicit wins when both are set;
/// name-matching keeps the display geometry-agnostic for detectors whose
/// position we don't want to hardcode.
struct RegionView {
    std::string name = "Region";
    std::string title;

    // Selection window, per axis (0 = x, 1 = y, 2 = z), in millimetres.
    // A volume/hit is kept only if it falls inside EVERY axis window that is
    // set; unset axes are unconstrained. This is deliberately independent of
    // `camera`: what you SELECT and what you LOOK ALONG are different choices.
    // A side view ("xz") is normally a z slab; a front view ("xy") is normally
    // an x or y slab, but any combination is legal.
    bool has_window[3] = {false, false, false};
    double wmin[3] = {0.0, 0.0, 0.0};
    double wmax[3] = {0.0, 0.0, 0.0};

    // Derive the window from the geometry by matching volume names
    // (ECMAScript regex or glob, case-insensitive). Used only for axes with no
    // explicit window.
    std::string match;

    // "xz" side view (looking along y) | "yz" along the beam (looking along x)
    // | "xy" transverse/front view (looking along z) | "3d" perspective
    std::string camera = "xz";

    // Web panel placement as VIEWPORT PERCENTAGES (0..100) for this region's
    // view, so a layout is resolution-independent. < 0 means "not set" -> the
    // web client falls back to a default cascade. views/default.toml is the
    // single source of the web layout (model A): edit these to arrange panels.
    double panel_x = -1.0;
    double panel_y = -1.0;
    double panel_w = -1.0;
    double panel_h = -1.0;

    // Volumes to drop from / restrict to THIS view only. Patterns may be
    // regex (".*ms.*") or glob ("*ms*"), matched case-insensitively as a
    // substring. exclude wins over include.
    std::vector<std::string> exclude;
    std::vector<std::string> include;

    // Rendering budget: too many tessellated, semi-transparent meshes make the
    // web viewer stall on pan/zoom. Transparency is the most expensive part.
    std::size_t max_shapes = 1200;
    int max_transparency = 30;
    // Margin added around a name-derived window, as a fraction of its length.
    double margin_frac = 0.05;
    // Re-express this region's contents in a frame centred on the region.
    // Eve7's web client auto-fits its camera about the ORIGIN, so a region
    // sitting tens of metres downstream renders far off to one side. Shifting
    // the contents is what centres the view; there is no reliable C++ API to
    // move the camera itself. Set false to keep global coordinates.
    bool recenter = true;
    // --- per-view event styling ---------------------------------------------
    // Marker size for hits in THIS view; < 0 means "use the global [hits]
    // size". Zoomed views usually want smaller markers than the full-detector
    // view, since the same hits cover far more screen area.
    float hit_marker_size = -1.0f;
    // Show the truth decay vertex in this view.
    bool draw_decay = true;
    // Only draw it when it actually falls inside this view's window. Leave
    // true for zoom views: a vertex tens of metres upstream would otherwise
    // stretch the camera auto-fit and undo the zoom.
    bool decay_clip = true;
    // Marker size for the decay vertex here; < 0 means use the global [decay].
    float decay_marker_size = -1.0f;

    // Optional manual nudge (mm) applied on top of the automatic centring.
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;

    /// True when at least one axis window is set explicitly.
    bool hasAnyWindow() const {
        return has_window[0] || has_window[1] || has_window[2];
    }
};

struct ViewConfig {
    double hit_scale = 0.01;  // the single mm -> scene-unit factor
    double ui_font_scale = 1.0;  // web UI text scale (see [ui] font_scale)
    double ui_sidebar_width = -1.0;  // web menu width in px ([ui] sidebar_width); <0 = default
    std::string ui_color_scheme;     // web colour scheme name ([ui] color_scheme); empty = default
    // Per-category base font sizes (px) for the web UI, from [ui.fonts]. Keys
    // are category names (window_title, menu, heading, dialog, brand).
    std::map<std::string, double> ui_fonts;
    std::string ntuple = "events";
    GeometryConfig geometry;
    HitStyle hits;
    DecayMarker decay;
    std::vector<RegionView> regions;
    // Depth limit for the name scan that resolves `match`-based region windows.
    // Subsystem envelopes sit near the top of the tree, so a shallow scan finds
    // them instantly; an unbounded scan walks the whole geometry (~1M volumes)
    // and dominates start-up. Raise only if a `match` fails to resolve.
    int scan_depth = 4;
    // Depth limit for the walk that builds the zoom regions' geometry. Higher
    // shows finer internal structure at the cost of start-up time and render
    // load; lower is faster and coarser.
    int region_depth = 5;
    // Exclude list for the deeper region walk. Defaults to geometry.exclude,
    // but can be set separately: geometry.exclude often hides subsystems (for
    // speed in the big view) that a zoom region actually needs to show.
    std::vector<std::string> region_exclude;
    bool has_region_exclude = false;

    const std::string& colorForVolume(const std::string& name) const;
    int transparencyForVolume(const std::string& name) const;
};

ViewConfig LoadViewConfig(const std::string& path);
ViewConfig DefaultViewConfig();

}  // namespace shipdisp

#endif  // SHIPDISP_VIEWCONFIG_H

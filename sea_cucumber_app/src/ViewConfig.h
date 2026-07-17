// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_VIEWCONFIG_H
#define SHIPDISP_VIEWCONFIG_H

// =============================================================================
//  ViewConfig.h
//
//  The display's *view* configuration -- the small amount of semantic mapping
//  a geometry-and-EDM-agnostic display needs to turn a pile of numbers into a
//  readable picture: which volumes to draw and in what colour, and how to style
//  hits and particles. This is the config that keeps sea_cucumber's core
//  independent of any particular detector: swap the TOML, not the code.
//
//  Everything has a sensible default, so the app runs with no config at all
//  (or a partial one). Lengths referenced here are millimetres (EDM-native).
// =============================================================================

#include <string>
#include <vector>

namespace shipdisp {

/// Colour rule: any volume whose name contains `match` (case-sensitive
/// substring) is drawn with this ROOT colour index and transparency (0..100).
struct SubsystemStyle {
    std::string match;
    int color = 921;       // kGray+1-ish default; resolved as a ROOT Color_t
    int transparency = 55;
};

struct GeometryConfig {
    std::string db_file = "ship_geometry.db";  // bare name resolved aegir-style
    std::vector<std::string> include;          // ECMAScript regex; empty => all
    std::vector<std::string> exclude;
    int max_depth = -1;
    bool stop_at_match = true;
    std::vector<SubsystemStyle> styles;        // first match wins
    int default_color = 921;
    int default_transparency = 60;
};

struct HitStyle {
    int marker_style = 20;
    float marker_size = 2.0f;
    // If true, colour hits on an energy heat-map; else colour categorically by
    // detectorId (one colour per distinct id). Both are useful; energy is the
    // friendlier default for a generic display.
    bool color_by_energy = true;
};

struct ParticleStyle {
    bool draw_mc = true;    // MCParticle: vertex + momentum direction
    bool draw_sim = true;   // SimParticle: vertex -> endpoint
    bool draw_rec = true;   // RecParticle: vertex + momentum direction
    float direction_length_mm = 500.0f;  // length of momentum-direction stubs
};

struct ViewConfig {
    double hit_scale = 0.01;   // the single mm -> scene-unit factor
    std::string ntuple = "events";
    GeometryConfig geometry;
    HitStyle hits;
    ParticleStyle particles;

    /// Colour for a volume name: first matching style, else default_color.
    int colorForVolume(const std::string& name) const;
    /// Transparency for a volume name: first matching style, else default.
    int transparencyForVolume(const std::string& name) const;
};

/// Load a view config from a TOML file. Missing file or parse errors are
/// non-fatal: a warning is printed and defaults (with the built-in SHiP
/// subsystem palette) are returned. Never throws.
ViewConfig LoadViewConfig(const std::string& path);

/// The built-in defaults (SHiP subsystem palette) used when no file is given.
ViewConfig DefaultViewConfig();

}  // namespace shipdisp

#endif  // SHIPDISP_VIEWCONFIG_H

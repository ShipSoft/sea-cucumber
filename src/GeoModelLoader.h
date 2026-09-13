// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_GEOMODEL_LOADER_H
#define SHIPDISP_GEOMODEL_LOADER_H

// =============================================================================
//  GeoModelLoader.h
//
//  Native GeoModel SQLite (.db) -> ROOT TGeo bridge for the SHiP event
//  display.  This replaces the GDML path (TGeoManager::Import) used by the
//  sea_cucumber prototype: the official ShipSoft geometry is authored in
//  GeoModel and shipped as a .db file (built by ShipSoft/Geometry's
//  build_geometry, viewable in gmex), and TGeoManager::Import does NOT
//  understand GeoModel .db.
//
//  What this module does
//  ---------------------
//    * Opens a .db with GMDBManager and rebuilds the GeoModel tree with
//      GeoModelIO::ReadGeoModel, yielding the world GeoVPhysVol.
//    * Walks the physical-volume tree.  GeoModel's getChildVol()/
//      getXToChildVol() accessors present an already-EXPANDED view: a
//      GeoSerialTransformer holding N copies (e.g. 9600 straw tubes) shows
//      up as N children, each with its own transform.  That is convenient
//      but dangerous for a web display -- see the filtering notes below.
//    * Converts each GeoModel GeoShape into an equivalent TGeoShape,
//      converting GeoModel's native millimetres to TGeo's native
//      centimetres (factor 0.1) on every length.
//    * Emits (name, TGeoShape*, global-transform, depth) through a caller
//      supplied callback.  The display wraps each emission in a
//      REveGeoShape and colours it -- exactly what BuildGeoShapes already
//      does for TGeoNodes, so the existing colour/opacity code is reused.
//
//  Why a callback instead of building REve elements here
//  -----------------------------------------------------
//  Keeping this module free of any REve/EVE include makes it unit-testable
//  without a running EVE manager, and keeps the shape-translation logic in
//  one place independent of how the display chooses to render it.  The only
//  ROOT types in this header are TGeoShape / TGeoHMatrix.
//
//  Units and frame
//  ---------------
//  Emitted shapes and transforms are scaled by GeoLoadOptions::length_scale
//  (mm/scene-unit). With the default 1.0 they are in native millimetres in the
//  GeoModel world frame (origin = SHiP target front face, +z downstream, +y up;
//  see ShipSoft/Geometry README). The display sets length_scale to its single
//  mm->scene factor so geometry and the event-data hits (also mm on disk) share
//  ONE uniform scale -- no per-element rescaling at render time and no frame
//  mismatch between detector and hits.
//
//  Filtering -- important for performance
//  --------------------------------------
//  The SHiP geometry has very heavy replicated structures (~9600 straw
//  tubes, ~16300 UT tiles, calorimeter fibres, 330 timing bars).  Emitting
//  one mesh per copy will overwhelm a web-REve client.  Use `include`
//  (and optionally `exclude`) regexes to keep only what you want, and lean
//  on `stop_at_match` (default true) so a matched *envelope* volume is
//  emitted as a single shape and its dense interior is NOT descended into
//  -- the same "keep a few dozen shapes" trick the prototype used for the
//  ECAL layer envelopes.  `max_shapes` is a hard safety cap.
// =============================================================================

#include <cstddef>
#include <functional>
#include <regex>
#include <string>
#include <vector>

class TGeoShape;
class TGeoHMatrix;
class GeoVPhysVol;  // GeoModel world / physical volume (from SHiPGeometryService or direct open)

namespace shipdisp {

struct GeoLoadOptions {
    // ECMAScript regexes.  A volume is a candidate iff it matches at least
    // one `include` pattern (or `include` is empty, which means MATCH ALL --
    // note this differs from the prototype's TOML, where empty meant "load
    // nothing"; match-all is the friendlier default for a general display).
    std::vector<std::string> include;
    // Optional veto: a volume matching any `exclude` pattern is dropped even
    // if it matched `include`.  Handy to blacklist a dense subsystem.
    std::vector<std::string> exclude;

    // Traversal depth cap (root world = depth 0).  -1 = unlimited.
    int max_depth = -1;

    // If true, a matched volume is emitted and NOT descended into (envelope
    // behaviour -- what you almost always want for dense subsystems).  If
    // false, matched volumes are emitted AND their children still visited.
    bool stop_at_match = true;

    // Hard cap on emitted shapes; loading stops once reached (with a warning
    // to std::cerr).  Guards against an over-broad include pattern melting
    // the web client.
    std::size_t max_shapes = 200000;

    // If true, print the first handful of volume names encountered when the
    // include pattern matches nothing -- the same diagnostic the prototype
    // prints, so it's obvious what to put in the regex.
    bool verbose = true;

    // Optional z gate (millimetres, GeoModel world frame). When enabled, only
    // volumes whose origin lies inside [z_window_min, z_window_max] are
    // CONVERTED and emitted; the walk still descends through volumes outside
    // it, because a parent envelope may sit at z~0 while its children are far
    // downstream. On a ~1M-volume geometry this is the difference between
    // building a million TGeoShapes and building a few thousand.
    // Match volume names case-insensitively (geometries are inconsistent about
    // capitalisation, e.g. "neutrino_detector" vs "NeutrinoDetector").
    bool icase = false;

    bool use_z_window = false;
    double z_window_min = 0.0;
    double z_window_max = 0.0;

    // Length scale applied to every emitted dimension and translation, in
    // mm/scene-unit. The display sets this to its single mm->scene factor
    // (e.g. 0.01) so geometry shares one uniform scale with the hits and no
    // per-element rescaling happens at render time. Default 1.0 = native mm.
    double length_scale = 1.0;
};

// Callback signature.  `shape` is a freshly-allocated TGeoShape dimensioned
// in cm; OWNERSHIP IS TRANSFERRED to the callback (e.g. hand it straight to
// REveGeoShape::SetShape, which takes ownership).  `global` is the volume's
// transform in the cm world frame.  `name` is the GeoModel logical-volume
// name with a copy index suffix appended for uniqueness.  `depth` is the
// tree depth (world = 0).
using GeoEmit = std::function<void(const std::string& name, TGeoShape* shape,
                                   const TGeoHMatrix& global, int depth)>;

// Walk an already-loaded GeoModel world, emitting matched shapes through
// `emit`. This is the display-specific half (GeoModel tree -> TGeo/REve) and
// is deliberately separate from *how* the world was obtained: pass a world
// from SHiPGeometryService (the aegir way) or from the direct-open helper
// below -- either works. Returns the number of shapes emitted.
std::size_t WalkGeoModelWorld(const GeoVPhysVol* world, const GeoLoadOptions& opt,
                              const GeoEmit& emit);

// Compile a volume-name pattern.
//
// Patterns are ECMAScript regexes, matched as substrings (so "ms" already means
// "contains ms"). If a pattern is NOT a valid regex -- the common case being
// glob syntax like "*ms*", where a leading '*' is a regex error -- it is
// translated from glob (* -> .*, ? -> .) and compiled again. That way both
// styles work and neither surprises the user.
std::regex CompileNamePattern(const std::string& pattern, bool icase = true);

// Cheap reconnaissance pass: reports (name, world z in mm, depth) for every
// volume WITHOUT converting any shape. Shape conversion dominates the cost of a
// walk, so this is orders of magnitude faster than LoadGeoModelDB and is the
// right way to answer "where in z is the subsystem called X?" before deciding
// what to actually build. Honours include/exclude and max_depth; ignores
// max_shapes and the z window. Honours stop_at_match: when true the scan does
// NOT descend into a volume it has already reported, which is what you want
// when locating a subsystem envelope (its interior can hold ~100k volumes).
// `dz_mm` is the world-frame z half-extent, or -1 when it can't be measured.
using GeoScan = std::function<void(const std::string& name, double z_mm, double dz_mm, int depth)>;

std::size_t ScanGeoModelDB(const std::string& db_path, const GeoLoadOptions& opt,
                           const GeoScan& scan);

// Open `db_path` once and cache the resulting GeoModel world for the lifetime
// of the process. Reading a large .db (~1M physical volumes) takes seconds, so
// callers that walk the same geometry more than once (e.g. a whole-detector
// pass plus a per-region pass) should go through this rather than reopening.
// Returns nullptr on failure. The returned world stays valid until exit.
const GeoVPhysVol* GetCachedGeoModelWorld(const std::string& db_path);

// Convenience: open `db_path` directly with GeoModelIO (GMDBManager +
// ReadGeoModel), then WalkGeoModelWorld. This is the fallback path for when
// SHiPGeometryService is not available; when it is, prefer obtaining the world
// from the service and calling WalkGeoModelWorld yourself.
// Returns the number of shapes emitted.
// Throws std::runtime_error if the .db cannot be opened or the GeoModel
// tree cannot be rebuilt.
std::size_t LoadGeoModelDB(const std::string& db_path, const GeoLoadOptions& opt,
                           const GeoEmit& emit);

}  // namespace shipdisp

#endif  // SHIPDISP_GEOMODEL_LOADER_H

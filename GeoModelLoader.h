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
//  Emitted shapes are dimensioned in CENTIMETRES and the emitted transform
//  is in centimetres in the GeoModel world frame (origin = SHiP target
//  front face, +z downstream, +y up; see ShipSoft/Geometry README).  The
//  event-data reader MUST feed hit/vertex positions in the SAME cm world
//  frame so geometry and hits align.  (The display's fHitScale / fOff scene
//  transforms then apply uniformly to both, unchanged.)
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

#include <string>
#include <vector>
#include <cstddef>
#include <functional>

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
};

// Callback signature.  `shape` is a freshly-allocated TGeoShape dimensioned
// in cm; OWNERSHIP IS TRANSFERRED to the callback (e.g. hand it straight to
// REveGeoShape::SetShape, which takes ownership).  `global` is the volume's
// transform in the cm world frame.  `name` is the GeoModel logical-volume
// name with a copy index suffix appended for uniqueness.  `depth` is the
// tree depth (world = 0).
using GeoEmit = std::function<void(const std::string& name,
                                   TGeoShape*         shape,
                                   const TGeoHMatrix& global,
                                   int                depth)>;

// Walk an already-loaded GeoModel world, emitting matched shapes through
// `emit`. This is the display-specific half (GeoModel tree -> TGeo/REve) and
// is deliberately separate from *how* the world was obtained: pass a world
// from SHiPGeometryService (the aegir way) or from the direct-open helper
// below -- either works. Returns the number of shapes emitted.
std::size_t WalkGeoModelWorld(const GeoVPhysVol*    world,
                              const GeoLoadOptions& opt,
                              const GeoEmit&        emit);

// Convenience: open `db_path` directly with GeoModelIO (GMDBManager +
// ReadGeoModel), then WalkGeoModelWorld. This is the fallback path for when
// SHiPGeometryService is not available; when it is, prefer obtaining the world
// from the service and calling WalkGeoModelWorld yourself.
// Returns the number of shapes emitted.
// Throws std::runtime_error if the .db cannot be opened or the GeoModel
// tree cannot be rebuilt.
std::size_t LoadGeoModelDB(const std::string&    db_path,
                           const GeoLoadOptions& opt,
                           const GeoEmit&        emit);

}  // namespace shipdisp

#endif  // SHIPDISP_GEOMODEL_LOADER_H

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_GDML_GEOMETRYSOURCE_H
#define SHIPDISP_GDML_GEOMETRYSOURCE_H

// =============================================================================
//  GdmlGeometrySource.h
//
//  The GDML geometry provider -- the sea_cucumber analogue of aegir's
//  `geometry_gdml_provider`. It parses a .gdml file with ROOT's TGDMLParse into
//  a TGeo volume tree and walks it with EXACTLY the same semantics as
//  WalkGeoModelWorld, so everything downstream (REve display, geometry cache,
//  web producer, region windows, --inspect) behaves identically whichever
//  backend produced the shapes:
//
//    * include / exclude regexes (or globs), icase, max_depth, stop_at_match,
//      max_shapes, the z window with whole-subtree pruning, length_scale;
//    * emitted names are "<logical volume name>#<daughter index>";
//    * emitted shapes are NEW TGeoShapes owned by the callback;
//    * lengths are emitted in mm * length_scale, GeoModel-world-style.
//
//  Units: TGeo stores lengths in cm (ROOT units) unless the process switched to
//  Geant4 units (mm) before parsing. The provider records which was active when
//  the file was parsed and converts to mm itself, so callers never see cm.
//
//  Assemblies (<assembly> in GDML, TGeoVolumeAssembly in TGeo) have no solid of
//  their own. They are never emitted; the walk always descends through them.
//  If an assembly MATCHES the include patterns, its descendants inherit the
//  match -- so an assembly named "Spectrometer" behaves like a GeoModel
//  envelope called "Spectrometer" (with stop_at_match, its first solid
//  children are emitted as the envelopes).
//
//  The parsed geometry is cached per path for the process lifetime (like
//  GetCachedGeoModelWorld): the display walks the same geometry several times
//  (whole-detector pass, name scan, region pass) and must not re-parse.
// =============================================================================

#include <cstddef>
#include <string>

#include "GeoModelLoader.h"  // GeoLoadOptions, GeoEmit, GeoScan
#include "IGeometrySource.h"

class TGeoShape;
class TGeoVolume;

namespace shipdisp {

class GdmlGeometrySource : public IGeometrySource {
   public:
    /// @p gdmlFile  bare filename (resolved with the same CWD ->
    ///              $SHIPGEOMETRY_ROOT/share/geometry convention as .db files)
    ///              or a path to a .gdml file.
    /// @p opt       include/exclude regexes, depth cap, envelope behaviour, etc.
    GdmlGeometrySource(std::string gdmlFile, GeoLoadOptions opt);

    std::size_t provide(const GeoEmit& emit) override;

    /// The path the constructor resolved @p gdmlFile to (for logging/errors).
    const std::string& resolvedPath() const { return resolved_; }

   private:
    std::string gdmlFile_;
    std::string resolved_;
    GeoLoadOptions opt_;
};

/// A parsed GDML world plus the length unit TGeo stored it in.
struct GdmlWorld {
    const TGeoVolume* world = nullptr;
    double mm_per_unit = 10.0;  // 10 for ROOT units (cm), 1 for Geant4 units (mm)
};

/// Parse @p gdml_path once and cache the world for the process lifetime.
/// Returns a GdmlWorld with world == nullptr if the file cannot be parsed.
/// Does not disturb an existing gGeoManager (or the shapes registered in it).
GdmlWorld GetCachedGdmlWorld(const std::string& gdml_path);

/// Walk an already-parsed GDML world, emitting matched shapes (see header
/// comment for semantics). Returns the number of shapes emitted.
std::size_t WalkGdmlWorld(const GdmlWorld& world, const GeoLoadOptions& opt, const GeoEmit& emit);

/// Parse (cached) + walk. Throws std::runtime_error if the file can't be read.
std::size_t LoadGdml(const std::string& gdml_path, const GeoLoadOptions& opt, const GeoEmit& emit);

/// Shape-free reconnaissance pass, the GDML twin of ScanGeoModelDB: reports
/// (name, world z in mm, world z half-extent in mm, depth) without converting
/// any shape. Assemblies ARE reported here (with the extent of their contents),
/// since locating a subsystem is exactly what an assembly is for.
/// Returns the number of volumes visited. Throws if the file can't be read.
std::size_t ScanGdml(const std::string& gdml_path, const GeoLoadOptions& opt, const GeoScan& scan);

/// Deep-copy @p shape with every length multiplied by @p factor (angles and
/// dimensionless parameters unchanged). Booleans are rebuilt recursively with
/// scaled operand placements. Shapes without a dedicated case are wrapped in a
/// TGeoScaledShape that REFERENCES (does not own) the original, which must
/// therefore outlive the copy -- true for shapes of a cached GDML world.
/// Returns nullptr for shapes that can't be drawn (e.g. TGeoHalfSpace).
TGeoShape* CloneScaledShape(const TGeoShape* shape, double factor);

}  // namespace shipdisp

#endif  // SHIPDISP_GDML_GEOMETRYSOURCE_H

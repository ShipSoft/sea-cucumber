// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_GEOMODEL_GEOMETRYSOURCE_H
#define SHIPDISP_GEOMODEL_GEOMETRYSOURCE_H

// =============================================================================
//  GeoModelGeometrySource.h
//
//  The GeoModel .db geometry provider -- the sea_cucumber analogue of aegir's
//  `geometry_geomodel_provider`. It obtains the GeoModel world the aegir way
//  (via SHiPGeometryService when available) and walks it into REve shapes with
//  WalkGeoModelWorld.
//
//  DB path resolution follows the aegir convention exactly: a bare filename is
//  resolved against the current working directory first, then against
//  $SHIPGEOMETRY_ROOT/share/geometry/. An absolute path is used as-is.
//
//  Build-time switch SHIP_USE_GEOMETRY_SERVICE selects the world source:
//    * defined  -> obtain the world from SHiPGeometryService (aegir way)
//    * undefined-> direct GeoModelIO open (GMDBManager + ReadGeoModel)
//  Both feed the identical WalkGeoModelWorld translation, so the display looks
//  the same either way; the service simply centralises .db reading (and any
//  caching / Geant4 conversion) across the ShipSoft stack.
// =============================================================================

#include <string>

#include "IGeometrySource.h"

namespace shipdisp {

class GeoModelGeometrySource : public IGeometrySource {
   public:
    /// @p dbFile  bare filename (resolved per the aegir convention) or an
    ///            absolute path to a GeoModel .db.
    /// @p opt     include/exclude regexes, depth cap, envelope behaviour, etc.
    GeoModelGeometrySource(std::string dbFile, GeoLoadOptions opt);

    std::size_t provide(const GeoEmit& emit) override;

    /// The path the constructor resolved @p dbFile to (for logging/errors).
    const std::string& resolvedPath() const { return resolved_; }

   private:
    std::string dbFile_;
    std::string resolved_;
    GeoLoadOptions opt_;
};

/// Resolve a GeoModel .db path the aegir way: absolute -> as-is; else CWD;
/// else $SHIPGEOMETRY_ROOT/share/geometry/<file>. Returns @p dbFile unchanged
/// if nothing matched, so the caller's open emits the not-found error.
std::string ResolveGeometryDbPath(const std::string& dbFile);

}  // namespace shipdisp

#endif  // SHIPDISP_GEOMODEL_GEOMETRYSOURCE_H

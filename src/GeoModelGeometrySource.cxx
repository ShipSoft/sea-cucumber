// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  GeoModelGeometrySource.cxx -- see header.
// =============================================================================

#include "GeoModelGeometrySource.h"

#include <cstdlib>       // std::getenv
#include <filesystem>
#include <iostream>
#include <utility>

#ifdef SHIP_USE_GEOMETRY_SERVICE
// The aegir way. NOTE: SHiPGeometryService is not part of the Geometry repo I
// was given; its exact header/API needs confirming against the `shipgeometry`
// package (or aegir's geometry_geomodel_provider). The include and the single
// load call below are the ONLY things to adjust once that header is in hand --
// everything downstream (WalkGeoModelWorld) is already final. The expected
// shape is "give it a .db path, get back the GeoModel world physical volume".
//
//   #include <SHiPGeometry/SHiPGeometryService.h>   // <-- confirm path
//   const GeoVPhysVol* world = SHiPGeometry::SHiPGeometryService{}.load(path);
#endif

#include "GeoModelLoader.h"

namespace shipdisp {

std::string ResolveGeometryDbPath(const std::string& dbFile) {
    namespace fs = std::filesystem;
    if (!dbFile.empty() && dbFile.front() == '/') return dbFile;  // absolute
    if (fs::is_regular_file(dbFile)) return dbFile;               // CWD

    if (const char* root = std::getenv("SHIPGEOMETRY_ROOT")) {
        const fs::path cand = fs::path(root) / "share" / "geometry" / dbFile;
        if (fs::is_regular_file(cand)) return cand.string();
    }
    return dbFile;  // unchanged -> the open below emits the not-found error
}

GeoModelGeometrySource::GeoModelGeometrySource(std::string dbFile, GeoLoadOptions opt)
    : dbFile_(std::move(dbFile)), resolved_(ResolveGeometryDbPath(dbFile_)), opt_(std::move(opt)) {}

std::size_t GeoModelGeometrySource::provide(const GeoEmit& emit) {
#ifdef SHIP_USE_GEOMETRY_SERVICE
    // Obtain the world from SHiPGeometryService, then walk it. Bind the one
    // call above; the line below is the intended shape.
    //   const GeoVPhysVol* world = SHiPGeometry::SHiPGeometryService{}.load(resolved_);
    //   return WalkGeoModelWorld(world, opt_, emit);
    std::cerr << "[GeoModelGeometrySource] SHIP_USE_GEOMETRY_SERVICE is defined but the "
                 "SHiPGeometryService call is not yet bound -- falling back to direct open.\n";
    return LoadGeoModelDB(resolved_, opt_, emit);
#else
    // Direct GeoModelIO open. Identical GeoModel calls to what the service uses
    // internally, so the rendered result matches the aegir path.
    return LoadGeoModelDB(resolved_, opt_, emit);
#endif
}

}  // namespace shipdisp

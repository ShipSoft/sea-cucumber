// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_GEOMETRYSOURCEFACTORY_H
#define SHIPDISP_GEOMETRYSOURCEFACTORY_H

// =============================================================================
//  GeometrySourceFactory.h
//
//  One place that turns a `--geometry <file>` argument into the right
//  IGeometrySource, so the tools never hard-code a backend:
//
//    GeoModel SQLite  (.db)    -> GeoModelGeometrySource
//    GDML             (.gdml)  -> GdmlGeometrySource
//
//  The format is chosen by extension first (.gdml / .db / .sqlite, case
//  insensitive) and, for anything else, by sniffing the file's first bytes
//  ("SQLite format 3" vs an XML document). Unknown files default to GeoModel,
//  which keeps the historical behaviour and its error message.
//
//  Path resolution is shared by both formats: absolute paths as-is, then the
//  CWD, then $SHIPGEOMETRY_ROOT/share/geometry/ (the aegir convention).
// =============================================================================

#include <cstddef>
#include <memory>
#include <string>

#include "GeoModelLoader.h"  // GeoLoadOptions, GeoScan
#include "IGeometrySource.h"

namespace shipdisp {

enum class GeometryFormat { GeoModelDb, Gdml };

/// Short human-readable name ("GeoModel .db" / "GDML") for log lines.
const char* GeometryFormatName(GeometryFormat f);

/// Decide the format of @p path (see header comment). Does not resolve the
/// path; pass an already-resolved one if you want content sniffing to work for
/// files found via $SHIPGEOMETRY_ROOT.
GeometryFormat DetectGeometryFormat(const std::string& path);

/// Resolve @p file with the shared CWD -> $SHIPGEOMETRY_ROOT convention.
/// (Same as ResolveGeometryDbPath, named for what it now covers.)
std::string ResolveGeometryPath(const std::string& file);

/// Build the provider for @p file (bare name or path, .db or .gdml).
std::unique_ptr<IGeometrySource> MakeGeometrySource(const std::string& file, GeoLoadOptions opt);

/// Shape-free name/position scan of @p resolved_path, dispatched on format.
/// Same contract as ScanGeoModelDB. Returns the number of volumes visited.
std::size_t ScanGeometry(const std::string& resolved_path, const GeoLoadOptions& opt,
                         const GeoScan& scan);

}  // namespace shipdisp

#endif  // SHIPDISP_GEOMETRYSOURCEFACTORY_H

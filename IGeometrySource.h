#ifndef SHIPDISP_IGEOMETRYSOURCE_H
#define SHIPDISP_IGEOMETRYSOURCE_H

// =============================================================================
//  IGeometrySource.h
//
//  Provider abstraction for detector geometry, mirroring aegir's
//  `IGeometrySource` provider pattern (builtin / GDML / GeoModel). The display
//  core asks a provider to emit shapes and stays agnostic of the backend.
//
//  Providers reuse the GeoModelLoader emit callback (name, TGeoShape*,
//  transform, depth) so the same REve-wrapping + colouring code serves every
//  backend. GeoModelGeometrySource is the one that matches the official stack;
//  a GDML provider (mirroring aegir's geometry_gdml_provider) is a thin
//  variant over TGeoManager::Import + the prototype's existing shape walk.
// =============================================================================

#include <cstddef>

#include "GeoModelLoader.h"  // shipdisp::GeoEmit, GeoLoadOptions

namespace shipdisp {

class IGeometrySource {
   public:
    virtual ~IGeometrySource() = default;

    /// Emit all selected shapes for this geometry through @p emit.
    /// Returns the number of shapes emitted. Throws on unrecoverable load
    /// errors (missing/corrupt geometry input).
    virtual std::size_t provide(const GeoEmit& emit) = 0;
};

}  // namespace shipdisp

#endif  // SHIPDISP_IGEOMETRYSOURCE_H

// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SHIPDISP_SHAPEMESH_H
#define SHIPDISP_SHAPEMESH_H

// =============================================================================
//  ShapeMesh.h -- TGeoShape -> triangle mesh, for display files (geometry.json).
//
//  Plain shapes (boxes, tubes, pcons, xtrus, scaled shapes, ...) are meshed
//  from their own TBuffer3D. Booleans (TGeoCompositeShape, also when wrapped in
//  a TGeoScaledShape) have no buffer of their own -- MakeBuffer3D() returns
//  nullptr -- so they are meshed with ROOT's CSG library (RootCsg, libRCsg),
//  mirroring REve's MakeGeoMesh in REveGeoPolyShape.cxx. That is why the REve
//  display always drew booleans while the web producer used to drop them.
// =============================================================================

#include <vector>

class TGeoShape;

namespace shipdisp {

/// Triangulate @p shape in its LOCAL frame (same units as the shape).
/// @p vertices receives flat [x0,y0,z0,x1,...] coordinates, @p triangles
/// receives three vertex indices per triangle (degenerate ones skipped).
/// Returns false, leaving both vectors empty, if the shape yields no triangles
/// (null shape, unsupported shape, or an empty boolean result).
bool TessellateShape(const TGeoShape* shape, std::vector<double>& vertices,
                     std::vector<int>& triangles);

}  // namespace shipdisp

#endif  // SHIPDISP_SHAPEMESH_H

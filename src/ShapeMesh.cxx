// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  ShapeMesh.cxx -- see header.
// =============================================================================

#include "ShapeMesh.h"

#include <CsgOps.h>
#include <TBuffer3D.h>
#include <TBuffer3DTypes.h>
#include <TGeoBoolNode.h>
#include <TGeoCompositeShape.h>
#include <TGeoMatrix.h>
#include <TGeoScaledShape.h>
#include <TGeoShape.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace shipdisp {

namespace {

// A polygon mesh in the shape's LOCAL frame: flat [x0,y0,z0,x1,...] vertices
// and each polygon as an ordered loop of vertex indices.
struct LocalMesh {
    std::vector<double> vertices;
    std::vector<std::vector<int>> polygons;
};

// Plain (non-boolean) shape -> mesh, straight from its TBuffer3D.
//
// Use MakeBuffer3D(), NOT GetBuffer3D(): the latter fills a shared static
// buffer via the geometry painter and dereferences painter/manager state that
// a standalone shape doesn't have (it segfaults in FillBuffer3D even for a
// plain box). MakeBuffer3D() allocates and fills its own buffer with no
// painter dependency. We own the returned buffer.
bool meshFromBuffer(const TGeoShape* shape, LocalMesh& mesh) {
    std::unique_ptr<TBuffer3D> buf(shape->MakeBuffer3D());
    if (!buf) return false;
    const TBuffer3D& b = *buf;
    const UInt_t nPts = b.NbPnts();
    const UInt_t nPols = b.NbPols();
    if (nPts == 0 || nPols == 0) return false;

    mesh.vertices.assign(b.fPnts, b.fPnts + 3 * nPts);

    // fPols is: [colour, nSegs, seg0, seg1, ..., colour, nSegs, ...]. A
    // polygon's segments form a closed loop but are NOT given head-to-tail, so
    // we must reconstruct the vertex order by walking the edges as a graph:
    // each segment is an edge between two point indices, every vertex in a
    // simple polygon has exactly two incident edges, so we start anywhere and
    // follow "the neighbour we didn't just come from" until the loop closes.
    const Int_t* segs = b.fSegs;
    const Int_t* pols = b.fPols;
    UInt_t q = 0;
    for (UInt_t p = 0; p < nPols; ++p) {
        const Int_t nSeg = pols[q + 1];
        std::vector<std::pair<int, int>> edges;
        edges.reserve(nSeg);
        for (Int_t s = 0; s < nSeg; ++s) {
            const Int_t segIdx = pols[q + 2 + s];
            edges.emplace_back(segs[3 * segIdx + 1], segs[3 * segIdx + 2]);
        }
        q += 2 + nSeg;
        if (edges.empty()) continue;

        std::vector<int> loop;
        loop.reserve(nSeg);
        std::vector<char> used(edges.size(), 0);
        loop.push_back(edges[0].first);
        int cur = edges[0].second;
        used[0] = 1;
        loop.push_back(cur);
        for (Int_t step = 1; step < nSeg; ++step) {
            bool found = false;
            for (std::size_t e = 0; e < edges.size(); ++e) {
                if (used[e]) continue;
                int nxt = -1;
                if (edges[e].first == cur)
                    nxt = edges[e].second;
                else if (edges[e].second == cur)
                    nxt = edges[e].first;
                if (nxt >= 0) {
                    used[e] = 1;
                    cur = nxt;
                    found = true;
                    break;
                }
            }
            if (!found) break;               // open/broken polygon; stop
            if (cur == loop.front()) break;  // closed the loop
            loop.push_back(cur);
        }
        mesh.polygons.push_back(std::move(loop));
    }
    return true;
}

// Booleans. TGeoCompositeShape::MakeBuffer3D() returns nullptr -- a boolean
// only becomes triangles after a mesh-level CSG step -- so without this every
// composite volume was silently dropped from geometry.json (the REve display
// was unaffected because it runs this same step). This mirrors REve's own
// MakeGeoMesh (REveGeoPolyShape.cxx): mesh each leaf, place it with the
// accumulated operand matrix, and combine with ROOT's RootCsg (libRCsg).
// Everything is done in the composite's LOCAL frame; the volume placement is
// applied afterwards, like any other shape, which keeps the CSG arithmetic at
// the shape's own scale.
std::unique_ptr<RootCsg::TBaseMesh> csgMesh(const TGeoHMatrix& m, const TGeoShape* shape) {
    if (const auto* comp = dynamic_cast<const TGeoCompositeShape*>(shape)) {
        const TGeoBoolNode* node = comp->GetBoolNode();
        if (!node) return nullptr;
        TGeoHMatrix ml(m), mr(m);
        if (const TGeoMatrix* lm = node->GetLeftMatrix()) ml.Multiply(lm);
        if (const TGeoMatrix* rm = node->GetRightMatrix()) mr.Multiply(rm);
        auto left = csgMesh(ml, node->GetLeftShape());
        auto right = csgMesh(mr, node->GetRightShape());
        if (!left || !right) return nullptr;
        RootCsg::TBaseMesh* out = nullptr;
        switch (node->GetBooleanOperator()) {
            case TGeoBoolNode::kGeoUnion:
                out = RootCsg::BuildUnion(left.get(), right.get());
                break;
            case TGeoBoolNode::kGeoIntersection:
                out = RootCsg::BuildIntersection(left.get(), right.get());
                break;
            case TGeoBoolNode::kGeoSubtraction:
                out = RootCsg::BuildDifference(left.get(), right.get());
                break;
            default:
                break;
        }
        return std::unique_ptr<RootCsg::TBaseMesh>(out);
    }
    // A scale wrapper around a boolean (e.g. a GDML <scaledSolid> of a union):
    // its own buffer would come from the boolean, i.e. be null, so fold the
    // scale into the placement and recurse.
    if (const auto* sc = dynamic_cast<const TGeoScaledShape*>(shape)) {
        if (dynamic_cast<const TGeoCompositeShape*>(sc->GetShape())) {
            const Double_t* k = sc->GetScale()->GetScale();
            const Double_t diag[9] = {k[0], 0, 0, 0, k[1], 0, 0, 0, k[2]};
            TGeoHMatrix s;
            s.SetRotation(diag);  // a linear map; LocalToMaster applies it as-is
            TGeoHMatrix ms(m);
            ms.Multiply(&s);
            return csgMesh(ms, sc->GetShape());
        }
    }
    // Leaf: its own buffer, moved into the composite's frame.
    std::unique_ptr<TBuffer3D> b(shape->MakeBuffer3D());
    if (!b || b->NbPnts() == 0 || b->NbPols() == 0) return nullptr;
    Double_t* v = b->fPnts;
    for (UInt_t i = 0; i < b->NbPnts(); ++i) {
        const Double_t local[3] = {v[3 * i], v[3 * i + 1], v[3 * i + 2]};
        m.LocalToMaster(local, &v[3 * i]);
    }
    return std::unique_ptr<RootCsg::TBaseMesh>(RootCsg::ConvertToMesh(*b));
}

bool needsCsg(const TGeoShape* shape) {
    if (dynamic_cast<const TGeoCompositeShape*>(shape)) return true;
    const auto* sc = dynamic_cast<const TGeoScaledShape*>(shape);
    return sc && dynamic_cast<const TGeoCompositeShape*>(sc->GetShape());
}

bool meshFromCsg(const TGeoShape* shape, LocalMesh& mesh) {
    const auto csg = csgMesh(TGeoHMatrix(), shape);
    if (!csg) return false;
    const UInt_t nv = csg->NumberOfVertices();
    const UInt_t np = csg->NumberOfPolys();
    if (nv == 0 || np == 0) return false;  // e.g. an empty intersection
    mesh.vertices.reserve(3 * nv);
    for (UInt_t i = 0; i < nv; ++i) {
        const Double_t* p = csg->GetVertex(i);
        mesh.vertices.insert(mesh.vertices.end(), p, p + 3);
    }
    mesh.polygons.reserve(np);
    for (UInt_t i = 0; i < np; ++i) {
        std::vector<int> loop(csg->SizeOfPoly(i));
        for (UInt_t j = 0; j < loop.size(); ++j) loop[j] = csg->GetVertexIndex(i, j);
        mesh.polygons.push_back(std::move(loop));
    }
    return true;
}

}  // namespace

bool TessellateShape(const TGeoShape* shape, std::vector<double>& vertices,
                     std::vector<int>& triangles) {
    vertices.clear();
    triangles.clear();
    if (!shape) return false;

    LocalMesh mesh;
    const bool ok = needsCsg(shape) ? meshFromCsg(shape, mesh) : meshFromBuffer(shape, mesh);
    if (!ok) return false;

    // Fan-triangulate each ordered loop, skipping degenerate triangles.
    const int nVert = static_cast<int>(mesh.vertices.size() / 3);
    for (const auto& loop : mesh.polygons) {
        for (std::size_t t = 1; t + 1 < loop.size(); ++t) {
            const int a = loop[0], c1 = loop[t], c2 = loop[t + 1];
            if (a == c1 || c1 == c2 || a == c2) continue;
            if (a < 0 || c1 < 0 || c2 < 0 || a >= nVert || c1 >= nVert || c2 >= nVert) continue;
            triangles.push_back(a);
            triangles.push_back(c1);
            triangles.push_back(c2);
        }
    }
    if (triangles.empty()) return false;
    vertices = std::move(mesh.vertices);
    return true;
}

}  // namespace shipdisp

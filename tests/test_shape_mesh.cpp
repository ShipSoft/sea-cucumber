// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Checks TessellateShape (the geometry.json mesher): plain shapes via their
//  TBuffer3D, and booleans -- which have no TBuffer3D of their own and used to
//  be dropped from the web display -- via RootCsg. Verifies triangles come out
//  and that the vertex extents match the boolean's true shape.
// =============================================================================

#include <TGeoBBox.h>
#include <TGeoBoolNode.h>
#include <TGeoCompositeShape.h>
#include <TGeoMatrix.h>
#include <TGeoScaledShape.h>
#include <TGeoTube.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "ShapeMesh.h"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
bool near(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
}

struct Extent {
    std::array<double, 3> lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
};

// Extent over the vertices actually used by triangles.
Extent extentOf(const std::vector<double>& v, const std::vector<int>& tri) {
    Extent e;
    for (int i : tri) {
        for (int a = 0; a < 3; ++a) {
            e.lo[a] = std::min(e.lo[a], v[3 * i + a]);
            e.hi[a] = std::max(e.hi[a], v[3 * i + a]);
        }
    }
    return e;
}

bool extentIs(const Extent& e, double xl, double xh, double yl, double yh, double zl, double zh) {
    return near(e.lo[0], xl) && near(e.hi[0], xh) && near(e.lo[1], yl) && near(e.hi[1], yh) &&
           near(e.lo[2], zl) && near(e.hi[2], zh);
}

bool indicesValid(const std::vector<double>& v, const std::vector<int>& tri) {
    const int n = static_cast<int>(v.size() / 3);
    if (tri.size() % 3 != 0) return false;
    for (int i : tri) {
        if (i < 0 || i >= n) return false;
    }
    return true;
}

// Two 100 mm cubes, the second shifted +80 mm in z (same as the GDML test).
TGeoCompositeShape* makeUnion() {
    auto* a = new TGeoBBox(50., 50., 50.);
    auto* b = new TGeoBBox(50., 50., 50.);
    auto* shift = new TGeoTranslation(0., 0., 80.);
    return new TGeoCompositeShape("", new TGeoUnion(a, b, nullptr, shift));
}
}  // namespace

int main() {
    std::vector<double> v;
    std::vector<int> t;

    // --- plain shape: unchanged TBuffer3D path -----------------------------------
    {
        TGeoBBox box(10., 20., 30.);
        check(shipdisp::TessellateShape(&box, v, t), "box tessellates");
        check(t.size() == 36 && indicesValid(v, t), "box -> 12 valid triangles");
        check(extentIs(extentOf(v, t), -10, 10, -20, 20, -30, 30), "box extent");
    }

    // --- union (MakeBuffer3D returns nullptr for this) -----------------------------
    {
        TGeoCompositeShape* u = makeUnion();
        check(shipdisp::TessellateShape(u, v, t), "union tessellates (was dropped before)");
        check(!t.empty() && indicesValid(v, t), "union -> valid triangles");
        check(extentIs(extentOf(v, t), -50, 50, -50, 50, -50, 130),
              "union extent covers both cubes (z -50..130)");
    }

    // --- subtraction: a box with a tube drilled through it -------------------------
    {
        auto* box = new TGeoBBox(50., 50., 50.);
        auto* hole = new TGeoTube(0., 10., 60.);
        auto* s = new TGeoCompositeShape("", new TGeoSubtraction(box, hole, nullptr, nullptr));
        check(shipdisp::TessellateShape(s, v, t), "subtraction tessellates");
        check(indicesValid(v, t), "subtraction -> valid triangles");
        check(extentIs(extentOf(v, t), -50, 50, -50, 50, -50, 50),
              "subtraction extent is the outer box");
        check(t.size() > 36, "subtraction has more triangles than the plain box (the bore)");
    }

    // --- intersection of overlapping boxes -----------------------------------------
    {
        auto* a = new TGeoBBox(50., 50., 50.);
        auto* b = new TGeoBBox(50., 50., 50.);
        auto* shift = new TGeoTranslation(0., 0., 80.);
        auto* i = new TGeoCompositeShape("", new TGeoIntersection(a, b, nullptr, shift));
        check(shipdisp::TessellateShape(i, v, t), "intersection tessellates");
        check(extentIs(extentOf(v, t), -50, 50, -50, 50, 30, 50),
              "intersection extent is the overlap slab (z 30..50)");
    }

    // --- nested boolean: (union) minus a box ---------------------------------------
    {
        auto* cut = new TGeoBBox(60., 60., 20.);
        auto* place = new TGeoTranslation(0., 0., 130.);  // trims the top 20 mm
        auto* n = new TGeoCompositeShape("", new TGeoSubtraction(makeUnion(), cut, nullptr, place));
        check(shipdisp::TessellateShape(n, v, t), "nested boolean tessellates");
        check(extentIs(extentOf(v, t), -50, 50, -50, 50, -50, 110),
              "nested boolean extent (top trimmed to z=110)");
    }

    // --- boolean inside a scale wrapper (e.g. GDML scaledSolid of a union) ---------
    {
        auto* sc = new TGeoScaledShape(makeUnion(), new TGeoScale(2., 2., 2.));
        check(shipdisp::TessellateShape(sc, v, t), "scaled boolean tessellates");
        check(extentIs(extentOf(v, t), -100, 100, -100, 100, -100, 260),
              "scaled boolean extent is doubled");
    }

    // --- empty results are reported, not emitted -----------------------------------
    {
        auto* a = new TGeoBBox(10., 10., 10.);
        auto* b = new TGeoBBox(10., 10., 10.);
        auto* far = new TGeoTranslation(0., 0., 100.);
        auto* empty = new TGeoCompositeShape("", new TGeoIntersection(a, b, nullptr, far));
        check(!shipdisp::TessellateShape(empty, v, t) && v.empty() && t.empty(),
              "disjoint intersection -> no mesh");
        check(!shipdisp::TessellateShape(nullptr, v, t), "null shape -> no mesh");
    }

    if (failures == 0) std::cout << "test_shape_mesh: OK\n";
    return failures == 0 ? 0 : 1;
}

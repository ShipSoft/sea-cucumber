// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Parses a small hand-written GDML file and checks the GDML provider honours
//  the same contract as the GeoModel one: names, mm output (whatever unit TGeo
//  stored), length_scale, include/exclude/glob, stop_at_match through
//  assemblies, depth cap, z window, the shape-free scan, format detection,
//  the factory, and a round-trip through the geometry cache. Also checks that
//  loading GDML leaves the caller's gGeoManager alone. No .db or GeoModel
//  needed.
// =============================================================================

#include <TGeoBBox.h>
#include <TGeoBoolNode.h>
#include <TGeoCompositeShape.h>
#include <TGeoManager.h>
#include <TGeoMatrix.h>
#include <TGeoPcon.h>
#include <TGeoScaledShape.h>
#include <TGeoShape.h>
#include <TGeoSphere.h>
#include <TGeoXtru.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "GdmlGeometrySource.h"
#include "GeometryCache.h"
#include "GeometrySourceFactory.h"

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
void writeFile(const std::string& path, const std::string& text) { std::ofstream(path) << text; }

// World (40 m long) holding, in daughter order:
//   0  Target        box 200x100x50 mm           at z = 100 mm   (position via <define>)
//   1  Magnet        tube rmax 50 cm, length 4 m at z = 3 m      (cm / m units)
//   2  Spectrometer  ASSEMBLY of two 10 mm Planes at local z = -/+500 mm, placed at z = 8 m
//   3  Joint         union of two 100 mm cubes, second shifted +80 mm in z,
//                    placed at x = 1 m, z = -1 m
// Mixed units on purpose: every length must come out in mm.
const char* kGdml = R"GDML(<?xml version="1.0" encoding="UTF-8"?>
<gdml xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:noNamespaceSchemaLocation="http://service-spi.web.cern.ch/service-spi/app/releases/GDML/schema/gdml.xsd">
  <define>
    <position name="targetPos" x="0" y="0" z="100" unit="mm"/>
  </define>
  <materials>
    <material name="Vacuum" Z="1">
      <D unit="g/cm3" value="1e-25"/>
      <atom unit="g/mole" value="1.00794"/>
    </material>
  </materials>
  <solids>
    <box name="WorldBox" x="20" y="20" z="40" lunit="m"/>
    <box name="TargetBox" x="200" y="100" z="50" lunit="mm"/>
    <tube name="MagnetTube" rmin="10" rmax="50" z="400" startphi="0" deltaphi="360"
          aunit="deg" lunit="cm"/>
    <box name="PlaneBox" x="1000" y="1000" z="10" lunit="mm"/>
    <box name="CubeA" x="100" y="100" z="100" lunit="mm"/>
    <box name="CubeB" x="100" y="100" z="100" lunit="mm"/>
    <union name="JointSolid">
      <first ref="CubeA"/>
      <second ref="CubeB"/>
      <position name="jointShift" x="0" y="0" z="80" unit="mm"/>
    </union>
  </solids>
  <structure>
    <volume name="Target">
      <materialref ref="Vacuum"/>
      <solidref ref="TargetBox"/>
    </volume>
    <volume name="Magnet">
      <materialref ref="Vacuum"/>
      <solidref ref="MagnetTube"/>
    </volume>
    <volume name="Plane">
      <materialref ref="Vacuum"/>
      <solidref ref="PlaneBox"/>
    </volume>
    <volume name="Joint">
      <materialref ref="Vacuum"/>
      <solidref ref="JointSolid"/>
    </volume>
    <assembly name="Spectrometer">
      <physvol>
        <volumeref ref="Plane"/>
        <position name="plane1Pos" x="0" y="0" z="-500" unit="mm"/>
      </physvol>
      <physvol>
        <volumeref ref="Plane"/>
        <position name="plane2Pos" x="0" y="0" z="500" unit="mm"/>
      </physvol>
    </assembly>
    <volume name="World">
      <materialref ref="Vacuum"/>
      <solidref ref="WorldBox"/>
      <physvol>
        <volumeref ref="Target"/>
        <positionref ref="targetPos"/>
      </physvol>
      <physvol>
        <volumeref ref="Magnet"/>
        <position name="magnetPos" x="0" y="0" z="3" unit="m"/>
      </physvol>
      <physvol>
        <volumeref ref="Spectrometer"/>
        <position name="specPos" x="0" y="0" z="8000" unit="mm"/>
      </physvol>
      <physvol>
        <volumeref ref="Joint"/>
        <position name="jointPos" x="1000" y="0" z="-1000" unit="mm"/>
      </physvol>
    </volume>
  </structure>
  <setup name="Default" version="1.0">
    <world ref="World"/>
  </setup>
</gdml>
)GDML";

struct Rec {
    std::string name;
    double x, y, z;
    double dx, dy, dz;
    std::string type;
    int depth;
};

std::vector<Rec> collect(shipdisp::IGeometrySource& src) {
    std::vector<Rec> out;
    src.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int depth) {
        const Double_t* t = g.GetTranslation();
        Rec r{name, t[0], t[1], t[2], 0, 0, 0, shape ? shape->ClassName() : "", depth};
        if (const auto* bb = dynamic_cast<const TGeoBBox*>(shape)) {
            r.dx = bb->GetDX();
            r.dy = bb->GetDY();
            r.dz = bb->GetDZ();
        }
        out.push_back(r);
        delete shape;  // ownership is ours
    });
    return out;
}

std::vector<Rec> collect(const std::string& file, const shipdisp::GeoLoadOptions& opt) {
    auto src = shipdisp::MakeGeometrySource(file, opt);
    return collect(*src);
}

const Rec* find(const std::vector<Rec>& v, const std::string& name) {
    for (const auto& r : v) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

shipdisp::GeoLoadOptions quiet() {
    shipdisp::GeoLoadOptions o;
    o.verbose = false;
    return o;
}
}  // namespace

int main() {
    using shipdisp::GeometryFormat;

    // A geometry the caller already owns must survive the GDML load: building
    // a TGeoManager normally deletes gGeoManager and every registered shape.
    auto* pre = new TGeoManager("pre_existing", "must survive the GDML load");
    auto* keep = new TGeoBBox(1, 2, 3);

    const std::string gdml = "sc_test_geometry.gdml";
    const std::string gdmlXml = "sc_test_geometry_as.xml";
    const std::string fakeDb = "sc_test_fake_sqlite.bin";
    writeFile(gdml, kGdml);
    writeFile(gdmlXml, kGdml);
    writeFile(fakeDb, std::string("SQLite format 3\0junk", 20));

    // --- format detection -------------------------------------------------------
    check(shipdisp::DetectGeometryFormat("a/b/ship.gdml") == GeometryFormat::Gdml, "ext .gdml");
    check(shipdisp::DetectGeometryFormat("SHIP.GDML") == GeometryFormat::Gdml, "ext .GDML");
    check(shipdisp::DetectGeometryFormat("ship.db") == GeometryFormat::GeoModelDb, "ext .db");
    check(shipdisp::DetectGeometryFormat(gdmlXml) == GeometryFormat::Gdml, "sniffed GDML (.xml)");
    check(shipdisp::DetectGeometryFormat(fakeDb) == GeometryFormat::GeoModelDb, "sniffed SQLite");
    check(shipdisp::DetectGeometryFormat("missing.bin") == GeometryFormat::GeoModelDb,
          "unknown -> GeoModel default");

    // --- parse once, cached, units recorded, caller's geometry untouched --------
    const shipdisp::GdmlWorld w = shipdisp::GetCachedGdmlWorld(gdml);
    check(w.world != nullptr, "GDML parses");
    if (!w.world) {
        std::remove(gdml.c_str());
        std::remove(gdmlXml.c_str());
        std::remove(fakeDb.c_str());
        return 1;
    }
    check(gGeoManager == pre, "caller's gGeoManager restored after parsing");
    check(keep->GetDX() == 1.0 && keep->GetDZ() == 3.0, "caller's shapes still alive");
    const double expectMmPerUnit =
        (TGeoManager::GetDefaultUnits() == TGeoManager::kG4Units) ? 1.0 : 10.0;
    check(near(w.mm_per_unit, expectMmPerUnit), "unit factor matches TGeo default units");
    check(shipdisp::GetCachedGdmlWorld(gdml).world == w.world, "parsed once and cached");

    // --- factory picks the GDML provider ----------------------------------------
    {
        auto src = shipdisp::MakeGeometrySource(gdml, quiet());
        check(dynamic_cast<shipdisp::GdmlGeometrySource*>(src.get()) != nullptr,
              "MakeGeometrySource(.gdml) -> GdmlGeometrySource");
        auto srcXml = shipdisp::MakeGeometrySource(gdmlXml, quiet());
        check(dynamic_cast<shipdisp::GdmlGeometrySource*>(srcXml.get()) != nullptr,
              "MakeGeometrySource(GDML content, .xml) -> GdmlGeometrySource");
    }

    // --- full walk: names, mm, depths, assembly transparency --------------------
    {
        const auto all = collect(gdml, quiet());
        check(all.size() == 5, "full walk emits 5 shapes (assembly itself not emitted), got " +
                                   std::to_string(all.size()));
        const Rec* t = find(all, "Target#0");
        check(t && near(t->z, 100) && near(t->dx, 100) && near(t->dy, 50) && near(t->dz, 25) &&
                  t->depth == 0,
              "Target: z=100 mm, half-lengths 100/50/25 mm, depth 0");
        const Rec* m = find(all, "Magnet#1");
        check(
            m && near(m->z, 3000) && near(m->dx, 500) && near(m->dz, 2000) && m->type == "TGeoTube",
            "Magnet: cm/m units converted (z=3000, rmax=500, half-length 2000 mm)");
        const Rec* p0 = find(all, "Plane#0");
        const Rec* p1 = find(all, "Plane#1");
        check(p0 && p1 && near(p0->z, 7500) && near(p1->z, 8500) && p0->depth == 1,
              "Planes placed through the assembly at z=7500/8500 mm, depth 1");
        check(!find(all, "Spectrometer#2"), "assembly is not emitted as a shape");
        const Rec* j = find(all, "Joint#3");
        check(j && j->type == "TGeoCompositeShape" && near(j->x, 1000) && near(j->z, -1000) &&
                  near(j->dx, 50) && near(j->dz, 90),
              "Joint: boolean rebuilt in mm (box half-z 90 = union of offset cubes)");
    }

    // --- boolean operand placement is scaled too --------------------------------
    {
        auto o = quiet();
        o.include = {"^Joint$"};
        auto src = shipdisp::MakeGeometrySource(gdml, o);
        double shiftZ = -1;
        src->provide([&](const std::string&, TGeoShape* shape, const TGeoHMatrix&, int) {
            if (const auto* c = dynamic_cast<const TGeoCompositeShape*>(shape)) {
                if (const TGeoMatrix* rm = c->GetBoolNode()->GetRightMatrix())
                    shiftZ = rm->GetTranslation()[2];
            }
            delete shape;
        });
        check(near(shiftZ, 80), "union's second operand shifted by 80 mm");
    }

    // --- length_scale ------------------------------------------------------------
    {
        auto o = quiet();
        o.length_scale = 0.01;
        const auto all = collect(gdml, o);
        const Rec* t = find(all, "Target#0");
        check(t && near(t->z, 1.0) && near(t->dx, 1.0), "length_scale applied to shape and pose");
    }

    // --- include through an assembly (envelope semantics) -----------------------
    {
        auto o = quiet();
        o.include = {"Spectrometer"};
        const auto v = collect(gdml, o);
        check(v.size() == 2 && find(v, "Plane#0") && find(v, "Plane#1"),
              "matching assembly hands its match to its children");
    }

    // --- exclude, glob include, depth cap ----------------------------------------
    {
        auto o = quiet();
        o.exclude = {"Magnet"};
        const auto v = collect(gdml, o);
        check(v.size() == 4 && !find(v, "Magnet#1"), "exclude drops Magnet");
    }
    {
        auto o = quiet();
        o.include = {"*agn*"};  // glob, not a regex
        const auto v = collect(gdml, o);
        check(v.size() == 1 && find(v, "Magnet#1"), "glob include");
    }
    {
        auto o = quiet();
        o.max_depth = 0;
        const auto v = collect(gdml, o);
        check(v.size() == 3 && !find(v, "Plane#0"), "max_depth 0 stops at the world's children");
    }

    // --- z window with subtree pruning -------------------------------------------
    {
        auto o = quiet();
        o.use_z_window = true;
        o.z_window_min = 7000;
        o.z_window_max = 9000;
        const auto v = collect(gdml, o);
        check(v.size() == 2 && find(v, "Plane#0") && find(v, "Plane#1"),
              "z window keeps only the spectrometer planes");
    }

    // --- shape-free scan (what --inspect and name-derived regions use) ---------
    {
        auto o = quiet();
        o.include = {"Spectrometer"};
        o.stop_at_match = true;
        std::vector<Rec> hits;
        const std::size_t visited =
            shipdisp::ScanGeometry(shipdisp::ResolveGeometryPath(gdml), o,
                                   [&](const std::string& n, double z, double dz, int d) {
                                       hits.push_back({n, 0, 0, z, 0, 0, dz, "", d});
                                   });
        check(visited >= 4, "scan visits the world's children");
        check(hits.size() == 1 && hits[0].name == "Spectrometer" && near(hits[0].z, 8000) &&
                  near(hits[0].dz, 505),
              "scan reports the assembly with its content's z span (8000 +/- 505 mm)");
    }

    // --- round-trip through the display cache ------------------------------------
    {
        const std::string cachePath = "sc_test_gdml_cache.root";
        auto src = shipdisp::MakeGeometrySource(gdml, quiet());
        const std::size_t n = shipdisp::WriteGeometryCache(*src, cachePath, "gdml unit test");
        check(n == 5, "cache captured 5 shapes");
        shipdisp::CachedGeometrySource cache(cachePath);
        const auto back = collect(cache);
        const Rec* t = find(back, "Target#0");
        check(back.size() == 5 && t && near(t->z, 100) && near(t->dz, 25),
              "cache replays GDML shapes in mm");
        std::remove(cachePath.c_str());
    }

    // --- CloneScaledShape on shapes the fixture doesn't cover ---------------------
    {
        auto* pc = new TGeoPcon(0., 360., 2);
        pc->DefineSection(0, -10, 0, 5);
        pc->DefineSection(1, 20, 1, 6);
        auto* p = dynamic_cast<TGeoPcon*>(shipdisp::CloneScaledShape(pc, 10));
        check(p && p->GetNz() == 2 && near(p->GetZ(0), -100) && near(p->GetZ(1), 200) &&
                  near(p->GetRmin(1), 10) && near(p->GetRmax(1), 60) && near(p->GetDphi(), 360),
              "pcon sections scaled, angles kept");
        delete p;
    }
    {
        auto* x = new TGeoXtru(2);
        Double_t xv[3] = {0, 10, 0}, yv[3] = {0, 0, 10};
        x->DefinePolygon(3, xv, yv);
        x->DefineSection(0, -5, 0, 0, 1.0);
        x->DefineSection(1, 5, 1, 2, 0.5);
        auto* c = dynamic_cast<TGeoXtru*>(shipdisp::CloneScaledShape(x, 10));
        check(c && near(c->GetX(1), 100) && near(c->GetZ(1), 50) && near(c->GetXOffset(1), 10) &&
                  near(c->GetYOffset(1), 20) && near(c->GetScale(1), 0.5),
              "xtru polygon/offsets scaled, section scale kept");
        delete c;
    }
    {
        auto* sph = new TGeoSphere(0., 10.);
        auto* ell = new TGeoScaledShape(sph, new TGeoScale(1, 2, 3));  // GDML ellipsoid
        auto* c = dynamic_cast<TGeoScaledShape*>(shipdisp::CloneScaledShape(ell, 10));
        const Double_t* k = c ? c->GetScale()->GetScale() : nullptr;
        check(c && c->GetShape() == sph && k && near(k[0], 10) && near(k[1], 20) &&
                  near(k[2], 30) && near(c->GetDZ(), 300),
              "scaled shape folds the length factor into its scale");
        delete c;
    }

    // --- missing file is a clean error --------------------------------------------
    {
        bool threw = false;
        try {
            auto src = shipdisp::MakeGeometrySource("sc_does_not_exist.gdml", quiet());
            src->provide(
                [](const std::string&, TGeoShape* s, const TGeoHMatrix&, int) { delete s; });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "missing .gdml throws std::runtime_error");
    }

    check(gGeoManager == pre, "caller's gGeoManager still current at the end");

    std::remove(gdml.c_str());
    std::remove(gdmlXml.c_str());
    std::remove(fakeDb.c_str());
    if (failures == 0) std::cout << "test_gdml_geometry: OK\n";
    return failures == 0 ? 0 : 1;
}

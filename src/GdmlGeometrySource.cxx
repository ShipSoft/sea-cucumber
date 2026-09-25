// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  GdmlGeometrySource.cxx -- GDML -> TGeo -> emitted shapes (see header).
//
//  Build dependency: ROOT Geom + Gdml (already required by sea_cucumber_core).
//
//  Two ROOT behaviours shape this file:
//
//   1. Constructing a TGeoManager DELETES the current gGeoManager, and a
//      TGeoManager's destructor deletes every shape registered with it. Shapes
//      emitted earlier (e.g. held by REve) are registered there, so a naive
//      TGeoManager::Import would free them under the display's feet. We park
//      gGeoManager while parsing and restore it afterwards, and keep our own
//      manager alive privately for the process lifetime.
//
//   2. TGeoManager::Import only recognises GDML by a literal ".gdml" in the
//      path. We drive TGDMLParse directly, so a GDML file detected by content
//      (e.g. "detector.xml") loads too.
// =============================================================================

#include "GdmlGeometrySource.h"

#include <TGDMLParse.h>
#include <TGeoArb8.h>
#include <TGeoBBox.h>
#include <TGeoBoolNode.h>
#include <TGeoCompositeShape.h>
#include <TGeoCone.h>
#include <TGeoEltu.h>
#include <TGeoHalfSpace.h>
#include <TGeoHype.h>
#include <TGeoManager.h>
#include <TGeoMatrix.h>
#include <TGeoNode.h>
#include <TGeoPara.h>
#include <TGeoParaboloid.h>
#include <TGeoPcon.h>
#include <TGeoPgon.h>
#include <TGeoScaledShape.h>
#include <TGeoShape.h>
#include <TGeoShapeAssembly.h>
#include <TGeoSphere.h>
#include <TGeoTorus.h>
#include <TGeoTrd1.h>
#include <TGeoTrd2.h>
#include <TGeoTube.h>
#include <TGeoVolume.h>
#include <TGeoXtru.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <typeinfo>
#include <utility>
#include <vector>

#include "GeoModelGeometrySource.h"  // ResolveGeometryDbPath (same convention)

namespace shipdisp {

namespace {

// -----------------------------------------------------------------------------
//  Small helpers
// -----------------------------------------------------------------------------

// Copy of @p m with its translation multiplied by @p f (rotation, including any
// reflection, is dimensionless and kept as-is). A null matrix means identity.
TGeoHMatrix scaledTranslation(const TGeoMatrix* m, double f) {
    TGeoHMatrix out = m ? TGeoHMatrix(*m) : TGeoHMatrix();
    const Double_t* t = out.GetTranslation();
    Double_t st[3] = {t[0] * f, t[1] * f, t[2] * f};
    out.SetTranslation(st);
    return out;
}

void warnUnsupportedOnce(const TGeoShape* s) {
    static std::set<std::string> warned;
    const std::string type = s ? s->ClassName() : "<null>";
    if (warned.insert(type).second) {
        std::cerr << "[GdmlGeometrySource] cannot draw TGeo shape '" << type
                  << "' -- volumes using it are skipped (add a case in CloneScaledShape())\n";
    }
}

// Rebuild a boolean with both operands (and their placements) scaled.
TGeoShape* cloneComposite(const TGeoCompositeShape* c, double f) {
    const TGeoBoolNode* node = c->GetBoolNode();
    if (!node) return nullptr;
    TGeoShape* left = CloneScaledShape(node->GetLeftShape(), f);
    TGeoShape* right = CloneScaledShape(node->GetRightShape(), f);
    if (!left || !right) {
        delete left;
        delete right;
        return nullptr;
    }
    auto* lm = new TGeoHMatrix(scaledTranslation(node->GetLeftMatrix(), f));
    auto* rm = new TGeoHMatrix(scaledTranslation(node->GetRightMatrix(), f));
    lm->RegisterYourself();
    rm->RegisterYourself();

    TGeoBoolNode* out = nullptr;
    switch (node->GetBooleanOperator()) {
        case TGeoBoolNode::kGeoUnion:
            out = new TGeoUnion(left, right, lm, rm);
            break;
        case TGeoBoolNode::kGeoIntersection:
            out = new TGeoIntersection(left, right, lm, rm);
            break;
        case TGeoBoolNode::kGeoSubtraction:
            out = new TGeoSubtraction(left, right, lm, rm);
            break;
        default:
            delete left;
            delete right;
            return nullptr;
    }
    return new TGeoCompositeShape("", out);
}

TGeoShape* cloneXtru(const TGeoXtru* x, double f) {
    const Int_t nv = x->GetNvert();
    const Int_t nz = x->GetNz();
    if (nv < 3 || nz < 2) return nullptr;
    std::vector<Double_t> xv(nv), yv(nv);
    for (Int_t i = 0; i < nv; ++i) {
        xv[i] = x->GetX(i) * f;
        yv[i] = x->GetY(i) * f;
    }
    auto* out = new TGeoXtru(nz);
    if (!out->DefinePolygon(nv, xv.data(), yv.data())) {
        delete out;
        return nullptr;
    }
    // Section scale factors are dimensionless; offsets and z are lengths.
    for (Int_t iz = 0; iz < nz; ++iz) {
        out->DefineSection(iz, x->GetZ(iz) * f, x->GetXOffset(iz) * f, x->GetYOffset(iz) * f,
                           x->GetScale(iz));
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Extents
// -----------------------------------------------------------------------------

// World-frame z centre and z half-extent (TGeo length units) of a volume's
// bounding box, placed with @p toWorld. Uses the box origin, so shapes whose
// box is off-centre (booleans, assemblies, asymmetric pcons) are handled.
bool worldZExtent(const TGeoVolume* v, const TGeoHMatrix& toWorld, double& zc, double& dz) {
    const auto* bb = v ? dynamic_cast<const TGeoBBox*>(v->GetShape()) : nullptr;
    if (!bb) return false;
    const double dx = bb->GetDX(), dy = bb->GetDY(), dzl = bb->GetDZ();
    if (!(std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dzl))) return false;
    if (dx < 0 || dy < 0 || dzl < 0) return false;  // run-time/undefined shape
    Double_t o[3] = {0, 0, 0};
    if (const Double_t* org = bb->GetOrigin()) {
        o[0] = org[0];
        o[1] = org[1];
        o[2] = org[2];
    }
    Double_t w[3];
    toWorld.LocalToMaster(o, w);
    const Double_t* R = toWorld.GetRotationMatrix();  // row-major 3x3
    zc = w[2];
    dz = std::abs(R[6]) * dx + std::abs(R[7]) * dy + std::abs(R[8]) * dzl;
    return true;
}

// An assembly's box is derived from its daughters; make sure it is current
// before anyone reads it.
void ensureAssemblyBBox(const TGeoVolume* v) {
    if (v && v->IsAssembly() && v->GetShape()) v->GetShape()->ComputeBBox();
}

bool matchesAny(const std::string& name, const std::vector<std::regex>& pats) {
    for (const auto& p : pats) {
        if (std::regex_search(name, p)) return true;
    }
    return false;
}

std::vector<std::regex> compilePatterns(const std::vector<std::string>& pats, bool icase) {
    std::vector<std::regex> out;
    out.reserve(pats.size());
    for (const auto& s : pats) out.emplace_back(CompileNamePattern(s, icase));
    return out;
}

// -----------------------------------------------------------------------------
//  Emitting walk -- mirrors GeoModelLoader.cxx's walk() rule for rule.
// -----------------------------------------------------------------------------
struct WalkState {
    const std::vector<std::regex>& include;
    const std::vector<std::regex>& exclude;
    const GeoLoadOptions& opt;
    const GeoEmit& emit;
    double toMm;      // TGeo length unit -> mm
    double outScale;  // TGeo length unit -> emitted unit (mm * length_scale)
    std::size_t emitted = 0;
    std::size_t visited = 0;
    std::size_t pruned = 0;
    bool capped = false;
};

// @p inherited: the parent was an assembly that matched `include`, so its
// children count as matched too (an assembly is the GDML way of naming a
// subsystem envelope that has no solid of its own).
void walk(const TGeoVolume* vol, const TGeoHMatrix& parentToWorld, int depth, bool inherited,
          WalkState& st) {
    if (!vol || st.capped) return;
    if (st.opt.max_depth >= 0 && depth > st.opt.max_depth) return;

    const Int_t nChild = vol->GetNdaughters();
    for (Int_t i = 0; i < nChild; ++i) {
        if (st.emitted >= st.opt.max_shapes) {
            if (!st.capped) {
                std::cerr << "[GdmlGeometrySource] hit max_shapes=" << st.opt.max_shapes
                          << " -- stopping (tighten include/exclude regexes)\n";
                st.capped = true;
            }
            return;
        }

        const TGeoNode* node = vol->GetNode(i);
        const TGeoVolume* child = node ? node->GetVolume() : nullptr;
        if (!child) continue;
        TGeoHMatrix childToWorld(parentToWorld);
        childToWorld.Multiply(node->GetMatrix());

        const std::string name = child->GetName();
        const bool assembly = child->IsAssembly();
        ++st.visited;

        // Cheap z gate first, exactly as the GeoModel walk: a volume whose
        // bounding box misses the window cannot contain anything inside it,
        // so the whole subtree is pruned. Assemblies are never pruned by box
        // (their box is derived, and they are cheap to walk through).
        bool inZ = true;
        if (st.opt.use_z_window) {
            double zc = 0, dz = 0;
            if (!assembly && worldZExtent(child, childToWorld, zc, dz)) {
                zc *= st.toMm;
                dz *= st.toMm;
                if (zc + dz < st.opt.z_window_min || zc - dz > st.opt.z_window_max) {
                    ++st.pruned;
                    continue;  // skip this volume AND its whole subtree
                }
            }
            const double zOrigin = childToWorld.GetTranslation()[2] * st.toMm;
            inZ = (zOrigin >= st.opt.z_window_min && zOrigin <= st.opt.z_window_max);
        }

        const bool incl = inherited || st.include.empty() || matchesAny(name, st.include);
        const bool excl = !st.exclude.empty() && matchesAny(name, st.exclude);
        if (excl) continue;  // prune: neither emit nor descend into the subtree

        if (assembly) {
            // Nothing to draw; walk through, handing the match down.
            walk(child, childToWorld, depth + 1, incl, st);
            continue;
        }

        if (incl && inZ) {
            TGeoShape* shape = CloneScaledShape(child->GetShape(), st.outScale);
            if (shape) {
                const TGeoHMatrix global = scaledTranslation(&childToWorld, st.outScale);
                st.emit(name + "#" + std::to_string(i), shape, global, depth);
                ++st.emitted;
            } else {
                warnUnsupportedOnce(child->GetShape());
            }
            if (st.opt.stop_at_match) continue;  // envelope: don't descend
        }
        walk(child, childToWorld, depth + 1, false, st);
    }
}

// -----------------------------------------------------------------------------
//  Shape-free scan -- mirrors GeoModelLoader.cxx's scanWalk().
// -----------------------------------------------------------------------------
void scanWalk(const TGeoVolume* vol, const TGeoHMatrix& parentToWorld, int depth,
              const GeoLoadOptions& opt, const std::vector<std::regex>& include,
              const std::vector<std::regex>& exclude, const GeoScan& scan, double toMm,
              std::size_t& visited) {
    if (!vol) return;
    if (opt.max_depth >= 0 && depth > opt.max_depth) return;

    const Int_t nChild = vol->GetNdaughters();
    for (Int_t i = 0; i < nChild; ++i) {
        const TGeoNode* node = vol->GetNode(i);
        const TGeoVolume* child = node ? node->GetVolume() : nullptr;
        if (!child) continue;
        TGeoHMatrix childToWorld(parentToWorld);
        childToWorld.Multiply(node->GetMatrix());
        const std::string name = child->GetName();
        ++visited;

        if (!exclude.empty() && matchesAny(name, exclude)) continue;  // prune subtree
        const bool matched = include.empty() || matchesAny(name, include);
        if (matched) {
            ensureAssemblyBBox(child);
            // Report the box centre (not the volume origin) so [z-dz, z+dz]
            // is the true span even for off-centre boxes such as assemblies.
            double zc = 0, dz = 0;
            if (worldZExtent(child, childToWorld, zc, dz)) {
                scan(name, zc * toMm, dz * toMm, depth);
            } else {
                scan(name, childToWorld.GetTranslation()[2] * toMm, -1.0, depth);
            }
            if (opt.stop_at_match) continue;
        }
        scanWalk(child, childToWorld, depth + 1, opt, include, exclude, scan, toMm, visited);
    }
}

// -----------------------------------------------------------------------------
//  Parsed-world cache
// -----------------------------------------------------------------------------
struct GdmlHolder {
    TGeoManager* manager = nullptr;  // owns the volumes/shapes; never deleted
    GdmlWorld world;
};

std::map<std::string, GdmlHolder>& gdmlCache() {
    static std::map<std::string, GdmlHolder> cache;
    return cache;
}

}  // namespace

// =============================================================================
//  Public entry points
// =============================================================================

TGeoShape* CloneScaledShape(const TGeoShape* s, double f) {
    if (!s) return nullptr;
    const std::type_info& t = typeid(*s);  // exact type: subclasses need their own case

    if (t == typeid(TGeoBBox)) {
        const auto* b = static_cast<const TGeoBBox*>(s);
        const Double_t* o = b->GetOrigin();
        Double_t so[3] = {o[0] * f, o[1] * f, o[2] * f};
        return new TGeoBBox(b->GetDX() * f, b->GetDY() * f, b->GetDZ() * f, so);
    }
    if (t == typeid(TGeoTube)) {
        const auto* x = static_cast<const TGeoTube*>(s);
        return new TGeoTube(x->GetRmin() * f, x->GetRmax() * f, x->GetDz() * f);
    }
    if (t == typeid(TGeoTubeSeg)) {
        const auto* x = static_cast<const TGeoTubeSeg*>(s);
        return new TGeoTubeSeg(x->GetRmin() * f, x->GetRmax() * f, x->GetDz() * f, x->GetPhi1(),
                               x->GetPhi2());
    }
    if (t == typeid(TGeoCtub)) {
        const auto* x = static_cast<const TGeoCtub*>(s);
        const Double_t* lo = x->GetNlow();  // unit normals: dimensionless
        const Double_t* hi = x->GetNhigh();
        return new TGeoCtub(x->GetRmin() * f, x->GetRmax() * f, x->GetDz() * f, x->GetPhi1(),
                            x->GetPhi2(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    }
    if (t == typeid(TGeoEltu)) {
        const auto* x = static_cast<const TGeoEltu*>(s);
        return new TGeoEltu(x->GetA() * f, x->GetB() * f, x->GetDz() * f);
    }
    if (t == typeid(TGeoHype)) {
        const auto* x = static_cast<const TGeoHype*>(s);
        return new TGeoHype(x->GetRmin() * f, x->GetStIn(), x->GetRmax() * f, x->GetStOut(),
                            x->GetDz() * f);
    }
    if (t == typeid(TGeoCone)) {
        const auto* x = static_cast<const TGeoCone*>(s);
        return new TGeoCone(x->GetDz() * f, x->GetRmin1() * f, x->GetRmax1() * f, x->GetRmin2() * f,
                            x->GetRmax2() * f);
    }
    if (t == typeid(TGeoConeSeg)) {
        const auto* x = static_cast<const TGeoConeSeg*>(s);
        return new TGeoConeSeg(x->GetDz() * f, x->GetRmin1() * f, x->GetRmax1() * f,
                               x->GetRmin2() * f, x->GetRmax2() * f, x->GetPhi1(), x->GetPhi2());
    }
    if (t == typeid(TGeoPcon)) {
        const auto* x = static_cast<const TGeoPcon*>(s);
        const Int_t nz = x->GetNz();
        auto* p = new TGeoPcon(x->GetPhi1(), x->GetDphi(), nz);
        for (Int_t i = 0; i < nz; ++i)
            p->DefineSection(i, x->GetZ(i) * f, x->GetRmin(i) * f, x->GetRmax(i) * f);
        return p;
    }
    if (t == typeid(TGeoPgon)) {
        const auto* x = static_cast<const TGeoPgon*>(s);
        const Int_t nz = x->GetNz();
        auto* p = new TGeoPgon(x->GetPhi1(), x->GetDphi(), x->GetNedges(), nz);
        for (Int_t i = 0; i < nz; ++i)
            p->DefineSection(i, x->GetZ(i) * f, x->GetRmin(i) * f, x->GetRmax(i) * f);
        return p;
    }
    if (t == typeid(TGeoTrd1)) {
        const auto* x = static_cast<const TGeoTrd1*>(s);
        return new TGeoTrd1(x->GetDx1() * f, x->GetDx2() * f, x->GetDy() * f, x->GetDz() * f);
    }
    if (t == typeid(TGeoTrd2)) {
        const auto* x = static_cast<const TGeoTrd2*>(s);
        return new TGeoTrd2(x->GetDx1() * f, x->GetDx2() * f, x->GetDy1() * f, x->GetDy2() * f,
                            x->GetDz() * f);
    }
    if (t == typeid(TGeoPara)) {
        const auto* x = static_cast<const TGeoPara*>(s);
        return new TGeoPara(x->GetX() * f, x->GetY() * f, x->GetZ() * f, x->GetAlpha(),
                            x->GetTheta(), x->GetPhi());
    }
    if (t == typeid(TGeoTrap)) {
        const auto* x = static_cast<const TGeoTrap*>(s);
        return new TGeoTrap(x->GetDz() * f, x->GetTheta(), x->GetPhi(), x->GetH1() * f,
                            x->GetBl1() * f, x->GetTl1() * f, x->GetAlpha1(), x->GetH2() * f,
                            x->GetBl2() * f, x->GetTl2() * f, x->GetAlpha2());
    }
    if (t == typeid(TGeoGtra)) {
        const auto* x = static_cast<const TGeoGtra*>(s);
        return new TGeoGtra(x->GetDz() * f, x->GetTheta(), x->GetPhi(), x->GetTwistAngle(),
                            x->GetH1() * f, x->GetBl1() * f, x->GetTl1() * f, x->GetAlpha1(),
                            x->GetH2() * f, x->GetBl2() * f, x->GetTl2() * f, x->GetAlpha2());
    }
    if (t == typeid(TGeoArb8)) {
        const auto* x = static_cast<const TGeoArb8*>(s);
        // GetVertices() is non-const in TGeo but only returns a pointer.
        const Double_t* src = const_cast<TGeoArb8*>(x)->GetVertices();
        Double_t v[16];
        for (int i = 0; i < 16; ++i) v[i] = src[i] * f;
        return new TGeoArb8(x->GetDz() * f, v);
    }
    if (t == typeid(TGeoSphere)) {
        const auto* x = static_cast<const TGeoSphere*>(s);
        return new TGeoSphere(x->GetRmin() * f, x->GetRmax() * f, x->GetTheta1(), x->GetTheta2(),
                              x->GetPhi1(), x->GetPhi2());
    }
    if (t == typeid(TGeoTorus)) {
        const auto* x = static_cast<const TGeoTorus*>(s);
        return new TGeoTorus(x->GetR() * f, x->GetRmin() * f, x->GetRmax() * f, x->GetPhi1(),
                             x->GetDphi());
    }
    if (t == typeid(TGeoParaboloid)) {
        const auto* x = static_cast<const TGeoParaboloid*>(s);
        return new TGeoParaboloid(x->GetRlo() * f, x->GetRhi() * f, x->GetDz() * f);
    }
    if (t == typeid(TGeoXtru)) {
        if (TGeoShape* out = cloneXtru(static_cast<const TGeoXtru*>(s), f)) return out;
        // fall through to the generic wrapper
    }
    if (t == typeid(TGeoCompositeShape)) {
        return cloneComposite(static_cast<const TGeoCompositeShape*>(s), f);
    }
    if (t == typeid(TGeoScaledShape)) {
        // GDML ellipsoids, elliptical cones and <scaledSolid>s. Uniform scaling
        // commutes with the diagonal scale, so fold f into it and keep
        // referencing the original inner shape.
        const auto* x = static_cast<const TGeoScaledShape*>(s);
        TGeoShape* inner = x->GetShape();
        const Double_t* k = x->GetScale()->GetScale();
        if (inner && typeid(*inner) == typeid(TGeoCompositeShape)) {
            // A composite can't be meshed through a scale wrapper's buffer, so
            // rebuild it at the new size and wrap that (the rebuilt inner shape
            // is not owned by the wrapper and lives for the process lifetime).
            TGeoShape* in2 = CloneScaledShape(inner, f);
            if (!in2) return nullptr;
            return new TGeoScaledShape(in2, new TGeoScale(k[0], k[1], k[2]));
        }
        if (!inner) return nullptr;
        return new TGeoScaledShape(inner, new TGeoScale(k[0] * f, k[1] * f, k[2] * f));
    }
    if (t == typeid(TGeoHalfSpace) || t == typeid(TGeoShapeAssembly)) {
        return nullptr;  // infinite / no solid of its own: nothing to draw
    }

    // Anything else (tessellated solids, twisted shapes added in newer ROOT,
    // ...): wrap the ORIGINAL in a uniform scale. The wrapper does not own it;
    // the cached GDML world keeps it alive.
    return new TGeoScaledShape(const_cast<TGeoShape*>(s), new TGeoScale(f, f, f));
}

GdmlWorld GetCachedGdmlWorld(const std::string& gdml_path) {
    auto& cache = gdmlCache();
    if (auto it = cache.find(gdml_path); it != cache.end()) return it->second.world;
    if (!std::filesystem::is_regular_file(gdml_path)) return {};

    // See the file header: park the caller's gGeoManager so constructing ours
    // does not delete it (and every shape registered with it).
    TGeoManager* previous = gGeoManager;
    gGeoManager = nullptr;

    auto* manager = new TGeoManager("sea_cucumber_gdml", gdml_path.c_str());
    TGeoVolume* world = nullptr;
    {
        TGDMLParse parser;  // reads gGeoManager's default units in its ctor
        world = parser.GDMLReadFile(gdml_path.c_str());
    }

    GdmlWorld out;
    if (world) {
        manager->SetTopVolume(world);
        manager->CloseGeometry();  // fixes run-time shapes, finalises assemblies
        out.world = world;
        // TGDMLParse converts every length into the process-wide default unit.
        out.mm_per_unit = (TGeoManager::GetDefaultUnits() == TGeoManager::kG4Units) ? 1.0 : 10.0;
        cache.emplace(gdml_path, GdmlHolder{manager, out});
    } else {
        delete manager;
    }
    gGeoManager = previous;
    return out;
}

std::size_t WalkGdmlWorld(const GdmlWorld& world, const GeoLoadOptions& opt, const GeoEmit& emit) {
    if (!world.world) throw std::runtime_error("GdmlGeometrySource: null world volume");

    const auto incl = compilePatterns(opt.include, opt.icase);
    const auto excl = compilePatterns(opt.exclude, opt.icase);
    WalkState st{incl, excl, opt, emit, world.mm_per_unit, world.mm_per_unit * opt.length_scale};

    walk(world.world, TGeoHMatrix(), 0, false, st);

    if (opt.verbose && st.emitted == 0) {
        std::cerr << "[GdmlGeometrySource] 0 shapes matched out of " << st.visited
                  << " volumes visited.\n"
                     "[GdmlGeometrySource] include patterns: ";
        for (const auto& s : opt.include) std::cerr << "'" << s << "' ";
        std::cerr << "\n[GdmlGeometrySource] (empty include => match all; list the GDML "
                     "volume names with --inspect)\n";
    } else if (opt.verbose) {
        std::cout << "[GdmlGeometrySource] emitted " << st.emitted << " shapes (" << st.visited
                  << " volumes visited, " << st.pruned << " subtrees pruned by z)\n";
    }
    return st.emitted;
}

std::size_t LoadGdml(const std::string& gdml_path, const GeoLoadOptions& opt, const GeoEmit& emit) {
    const GdmlWorld w = GetCachedGdmlWorld(gdml_path);
    if (!w.world) {
        throw std::runtime_error("GdmlGeometrySource: cannot open/parse GDML '" + gdml_path + "'");
    }
    return WalkGdmlWorld(w, opt, emit);
}

std::size_t ScanGdml(const std::string& gdml_path, const GeoLoadOptions& opt, const GeoScan& scan) {
    const GdmlWorld w = GetCachedGdmlWorld(gdml_path);
    if (!w.world) {
        throw std::runtime_error("GdmlGeometrySource: cannot open/parse GDML '" + gdml_path + "'");
    }
    const auto incl = compilePatterns(opt.include, opt.icase);
    const auto excl = compilePatterns(opt.exclude, opt.icase);
    std::size_t visited = 0;
    scanWalk(w.world, TGeoHMatrix(), 0, opt, incl, excl, scan, w.mm_per_unit, visited);
    return visited;
}

GdmlGeometrySource::GdmlGeometrySource(std::string gdmlFile, GeoLoadOptions opt)
    : gdmlFile_(std::move(gdmlFile)),
      resolved_(ResolveGeometryDbPath(gdmlFile_)),
      opt_(std::move(opt)) {}

std::size_t GdmlGeometrySource::provide(const GeoEmit& emit) {
    return LoadGdml(resolved_, opt_, emit);
}

}  // namespace shipdisp

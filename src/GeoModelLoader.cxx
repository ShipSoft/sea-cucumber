// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  GeoModelLoader.cxx -- GeoModel .db -> TGeo bridge (see header).
//
//  Build dependency: GeoModelCore + GeoModelIO (GeoModel 6.22+), the same
//  packages ShipSoft/Geometry links.  See the CMake snippet at the bottom of
//  this file's companion notes.
//
//  NOTE ON API DRIFT: GeoModel's public API is stable but not frozen across
//  6.x point releases.  The calls used here (GMDBManager, ReadGeoModel::
//  buildGeoModel, GeoVPhysVol::getNChildVols/getChildVol/getXToChildVol,
//  and the GeoShape getters) have been stable for years, but if your local
//  GeoModel is older/newer than what the Geometry repo pins, expect at most
//  a couple of signature nudges.  Every such spot is flagged with `API:`.
// =============================================================================

#include "GeoModelLoader.h"

// --- GeoModel I/O ------------------------------------------------------------
#include "GeoModelDBManager/GMDBManager.h"
#include "GeoModelRead/ReadGeoModel.h"

// --- GeoModel kernel: tree + transforms --------------------------------------
#include "GeoModelKernel/GeoDefinitions.h"  // GeoTrf::Transform3D (Eigen)
#include "GeoModelKernel/GeoLogVol.h"
#include "GeoModelKernel/GeoShape.h"
#include "GeoModelKernel/GeoVPhysVol.h"

// --- GeoModel kernel: concrete shapes ----------------------------------------
#include "GeoModelKernel/GeoBox.h"
#include "GeoModelKernel/GeoCons.h"
#include "GeoModelKernel/GeoEllipticalTube.h"
#include "GeoModelKernel/GeoPara.h"
#include "GeoModelKernel/GeoPcon.h"
#include "GeoModelKernel/GeoPgon.h"
#include "GeoModelKernel/GeoShapeIntersection.h"
#include "GeoModelKernel/GeoShapeShift.h"
#include "GeoModelKernel/GeoShapeSubtraction.h"
#include "GeoModelKernel/GeoShapeUnion.h"
#include "GeoModelKernel/GeoTorus.h"
#include "GeoModelKernel/GeoTrap.h"
#include "GeoModelKernel/GeoTrd.h"
#include "GeoModelKernel/GeoTube.h"
#include "GeoModelKernel/GeoTubs.h"

// --- ROOT TGeo ---------------------------------------------------------------
#include <TGeoArb8.h>  // TGeoTrap derives from TGeoArb8
#include <TGeoBBox.h>
#include <TGeoBoolNode.h>
#include <TGeoCompositeShape.h>
#include <TGeoCone.h>
#include <TGeoEltu.h>
#include <TGeoMatrix.h>
#include <TGeoPara.h>
#include <TGeoPcon.h>
#include <TGeoPgon.h>
#include <TGeoShape.h>
#include <TGeoTorus.h>
#include <TGeoTrd2.h>
#include <TGeoTube.h>

#include <atomic>
#include <cmath>
#include <algorithm>
#include <any>
#include <map>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>

namespace shipdisp {

namespace {

// GeoModel lengths are in mm. We emit at `gLenScale` mm/scene-unit, set from
// GeoLoadOptions::length_scale at the start of each walk so geometry shares the
// display's single mm->scene scale with the hits (no per-element rescaling at
// render time). Default 1.0 = emit native mm. Not thread-safe; the walk is
// single-threaded.
double gLenScale = 1.0;
// GeoModel angles are in radians; TGeo shape ctors want degrees.
constexpr double kRadToDeg = 180.0 / M_PI;

// -----------------------------------------------------------------------------
//  Transform conversion:  GeoTrf::Transform3D (Eigen Affine3d, mm) -> TGeoHMatrix (cm)
// -----------------------------------------------------------------------------
TGeoHMatrix toTGeo(const GeoTrf::Transform3D& g) {
    // Rotation: Eigen accessor (i,j) is row i, col j regardless of storage
    // order.  TGeoHMatrix::SetRotation wants a row-major 3x3.
    const auto R = g.linear();
    const auto T = g.translation();
    Double_t rot[9] = {
        R(0, 0), R(0, 1), R(0, 2), R(1, 0), R(1, 1), R(1, 2), R(2, 0), R(2, 1), R(2, 2),
    };
    Double_t tr[3] = {T.x() * gLenScale, T.y() * gLenScale, T.z() * gLenScale};
    TGeoHMatrix m;
    m.SetRotation(rot);
    m.SetTranslation(tr);
    return m;
}

// -----------------------------------------------------------------------------
//  Shape conversion.  Returns a NEW TGeoShape (cm), or nullptr for an
//  unsupported GeoModel shape (a warning is printed once per type name).
//
//  Booleans are handled recursively: a GeoShapeShift folds its transform
//  into the operand's placement inside the parent boolean node; Union /
//  Subtraction / Intersection become a TGeoCompositeShape over a
//  TGeoBoolNode.  A shift with no enclosing boolean (rare) is applied by
//  returning the operand shape and letting the *_shift out-parameter carry
//  the extra matrix up to the emit site.
// -----------------------------------------------------------------------------
void warnUnsupported(const GeoShape* s) {
    static std::regex dummy;  // silence -Wunused in case of macro builds
    (void)dummy;
    std::cerr << "[GeoModelLoader] unsupported GeoModel shape '"
              << (s ? s->type() : std::string("<null>"))
              << "' -- skipped (add a case in convertShape())\n";
}


// -----------------------------------------------------------------------------
//  Cheap half-extents of a GeoModel shape, WITHOUT converting it to a
//  TGeoShape. Shape conversion is the dominant cost of a walk, so being able to
//  bound a volume (and therefore its whole subtree) from the GeoModel shape
//  alone is what makes subtree pruning worthwhile.
//
//  Returns false for shapes we can't measure; callers then decline to prune,
//  which is always the safe direction.
// -----------------------------------------------------------------------------
bool halfExtents(const GeoShape* s, double& dx, double& dy, double& dz) {
    if (!s) return false;
    if (const auto* b = dynamic_cast<const GeoBox*>(s)) {
        dx = b->getXHalfLength(); dy = b->getYHalfLength(); dz = b->getZHalfLength();
        return true;
    }
    if (const auto* t = dynamic_cast<const GeoTube*>(s)) {
        dx = dy = t->getRMax(); dz = t->getZHalfLength();
        return true;
    }
    if (const auto* t = dynamic_cast<const GeoTubs*>(s)) {
        dx = dy = t->getRMax(); dz = t->getZHalfLength();
        return true;
    }
    if (const auto* c = dynamic_cast<const GeoCons*>(s)) {
        dx = dy = std::max(c->getRMax1(), c->getRMax2()); dz = c->getDZ();
        return true;
    }
    if (const auto* t = dynamic_cast<const GeoTrd*>(s)) {
        dx = std::max(t->getXHalfLength1(), t->getXHalfLength2());
        dy = std::max(t->getYHalfLength1(), t->getYHalfLength2());
        dz = t->getZHalfLength();
        return true;
    }
    if (const auto* t = dynamic_cast<const GeoTrap*>(s)) {
        dx = std::max({t->getDxdyndzn(), t->getDxdypdzn(), t->getDxdyndzp(), t->getDxdypdzp()});
        dy = std::max(t->getDydzn(), t->getDydzp());
        dz = t->getZHalfLength();
        return true;
    }
    if (const auto* p = dynamic_cast<const GeoPara*>(s)) {
        dx = p->getXHalfLength(); dy = p->getYHalfLength(); dz = p->getZHalfLength();
        return true;
    }
    if (const auto* e = dynamic_cast<const GeoEllipticalTube*>(s)) {
        dx = e->getXHalfLength(); dy = e->getYHalfLength(); dz = e->getZHalfLength();
        return true;
    }
    if (const auto* p = dynamic_cast<const GeoPcon*>(s)) {
        double r = 0, zlo = 1e30, zhi = -1e30;
        for (unsigned i = 0; i < p->getNPlanes(); ++i) {
            r = std::max(r, p->getRMaxPlane(i));
            zlo = std::min(zlo, p->getZPlane(i));
            zhi = std::max(zhi, p->getZPlane(i));
        }
        if (zhi < zlo) return false;
        dx = dy = r; dz = 0.5 * (zhi - zlo);
        return true;
    }
    if (const auto* p = dynamic_cast<const GeoPgon*>(s)) {
        double r = 0, zlo = 1e30, zhi = -1e30;
        for (unsigned i = 0; i < p->getNPlanes(); ++i) {
            r = std::max(r, p->getRMaxPlane(i));
            zlo = std::min(zlo, p->getZPlane(i));
            zhi = std::max(zhi, p->getZPlane(i));
        }
        if (zhi < zlo) return false;
        dx = dy = r; dz = 0.5 * (zhi - zlo);
        return true;
    }
    if (const auto* t = dynamic_cast<const GeoTorus*>(s)) {
        dx = dy = t->getRTor() + t->getRMax(); dz = t->getRMax();
        return true;
    }
    // Booleans: bound by the operands (over-estimate is fine, never prunes
    // something that should have been kept).
    if (const auto* sh = dynamic_cast<const GeoShapeShift*>(s)) {
        if (!halfExtents(sh->getOp(), dx, dy, dz)) return false;
        const auto t = sh->getX().translation();
        dx += std::abs(t.x()); dy += std::abs(t.y()); dz += std::abs(t.z());
        return true;
    }
    if (const auto* u = dynamic_cast<const GeoShapeUnion*>(s)) {
        double ax, ay, az, bx, by, bz;
        if (!halfExtents(u->getOpA(), ax, ay, az)) return false;
        if (!halfExtents(u->getOpB(), bx, by, bz)) return false;
        dx = std::max(ax, bx); dy = std::max(ay, by); dz = std::max(az, bz);
        return true;
    }
    if (const auto* d = dynamic_cast<const GeoShapeSubtraction*>(s)) {
        return halfExtents(d->getOpA(), dx, dy, dz);  // bounded by the minuend
    }
    if (const auto* i = dynamic_cast<const GeoShapeIntersection*>(s)) {
        double ax, ay, az, bx, by, bz;
        if (!halfExtents(i->getOpA(), ax, ay, az)) return false;
        if (!halfExtents(i->getOpB(), bx, by, bz)) return false;
        dx = std::min(ax, bx); dy = std::min(ay, by); dz = std::min(az, bz);
        return true;
    }
    return false;
}

// World-frame z half-extent of a volume: the exact axis-aligned bound for a box
// under rotation R, and a safe over-estimate for anything bounded by that box.
// Returns -1 when the shape can't be measured (caller must not prune).
double zHalfExtentWorld(const GeoShape* s, const GeoTrf::Transform3D& toWorld) {
    double dx = 0, dy = 0, dz = 0;
    if (!halfExtents(s, dx, dy, dz)) return -1.0;
    const auto R = toWorld.linear();
    return std::abs(R(2, 0)) * dx + std::abs(R(2, 1)) * dy + std::abs(R(2, 2)) * dz;
}

// Forward decl for recursion.
TGeoShape* convertShape(const GeoShape* s, TGeoHMatrix& localShift);

// Build a boolean composite from two operands, honouring any shifts each
// operand carries.  Returns nullptr if either operand failed to convert.
TGeoShape* makeBoolean(const char* kind, const GeoShape* opA, const GeoShape* opB) {
    TGeoHMatrix sA, sB;  // shifts folded up from each operand
    TGeoShape* a = convertShape(opA, sA);
    TGeoShape* b = convertShape(opB, sB);
    if (!a || !b) {
        delete a;
        delete b;
        return nullptr;
    }

    // TGeoBoolNode takes ownership of the shapes and (registered) matrices.
    auto* mA = new TGeoHMatrix(sA);
    auto* mB = new TGeoHMatrix(sB);
    mA->RegisterYourself();
    mB->RegisterYourself();

    TGeoBoolNode* node = nullptr;
    const std::string k = kind;
    if (k == "union")
        node = new TGeoUnion(a, b, mA, mB);
    else if (k == "subtraction")
        node = new TGeoSubtraction(a, b, mA, mB);
    else /* intersection */
        node = new TGeoIntersection(a, b, mA, mB);

    return new TGeoCompositeShape("", node);
}

TGeoShape* convertShape(const GeoShape* s, TGeoHMatrix& localShift) {
    localShift = TGeoHMatrix();  // identity unless a Shift sets it

    if (const auto* box = dynamic_cast<const GeoBox*>(s)) {
        return new TGeoBBox(box->getXHalfLength() * gLenScale, box->getYHalfLength() * gLenScale,
                            box->getZHalfLength() * gLenScale);
    }
    if (const auto* tube = dynamic_cast<const GeoTube*>(s)) {
        return new TGeoTube(tube->getRMin() * gLenScale, tube->getRMax() * gLenScale,
                            tube->getZHalfLength() * gLenScale);
    }
    if (const auto* tubs = dynamic_cast<const GeoTubs*>(s)) {
        const double sphi = tubs->getSPhi() * kRadToDeg;
        const double dphi = tubs->getDPhi() * kRadToDeg;
        return new TGeoTubeSeg(tubs->getRMin() * gLenScale, tubs->getRMax() * gLenScale,
                               tubs->getZHalfLength() * gLenScale, sphi, sphi + dphi);
    }
    if (const auto* cons = dynamic_cast<const GeoCons*>(s)) {
        const double sphi = cons->getSPhi() * kRadToDeg;
        const double dphi = cons->getDPhi() * kRadToDeg;
        return new TGeoConeSeg(cons->getDZ() * gLenScale, cons->getRMin1() * gLenScale,
                               cons->getRMax1() * gLenScale, cons->getRMin2() * gLenScale,
                               cons->getRMax2() * gLenScale, sphi, sphi + dphi);
    }
    if (const auto* trd = dynamic_cast<const GeoTrd*>(s)) {
        return new TGeoTrd2(trd->getXHalfLength1() * gLenScale, trd->getXHalfLength2() * gLenScale,
                            trd->getYHalfLength1() * gLenScale, trd->getYHalfLength2() * gLenScale,
                            trd->getZHalfLength() * gLenScale);
    }
    if (const auto* para = dynamic_cast<const GeoPara*>(s)) {
        return new TGeoPara(para->getXHalfLength() * gLenScale, para->getYHalfLength() * gLenScale,
                            para->getZHalfLength() * gLenScale, para->getAlpha() * kRadToDeg,
                            para->getTheta() * kRadToDeg, para->getPhi() * kRadToDeg);
    }
    if (const auto* trap = dynamic_cast<const GeoTrap*>(s)) {
        return new TGeoTrap(trap->getZHalfLength() * gLenScale, trap->getTheta() * kRadToDeg,
                            trap->getPhi() * kRadToDeg, trap->getDydzn() * gLenScale,
                            trap->getDxdyndzn() * gLenScale, trap->getDxdypdzn() * gLenScale,
                            trap->getAngleydzn() * kRadToDeg, trap->getDydzp() * gLenScale,
                            trap->getDxdyndzp() * gLenScale, trap->getDxdypdzp() * gLenScale,
                            trap->getAngleydzp() * kRadToDeg);
    }
    if (const auto* eltu = dynamic_cast<const GeoEllipticalTube*>(s)) {
        return new TGeoEltu(eltu->getXHalfLength() * gLenScale, eltu->getYHalfLength() * gLenScale,
                            eltu->getZHalfLength() * gLenScale);
    }
    if (const auto* torus = dynamic_cast<const GeoTorus*>(s)) {
        const double sphi = torus->getSPhi() * kRadToDeg;
        const double dphi = torus->getDPhi() * kRadToDeg;
        return new TGeoTorus(torus->getRTor() * gLenScale, torus->getRMin() * gLenScale,
                             torus->getRMax() * gLenScale, sphi, dphi);
    }
    if (const auto* pcon = dynamic_cast<const GeoPcon*>(s)) {
        const double sphi = pcon->getSPhi() * kRadToDeg;
        const double dphi = pcon->getDPhi() * kRadToDeg;
        const unsigned n = pcon->getNPlanes();
        auto* p = new TGeoPcon(sphi, dphi, (Int_t)n);
        for (unsigned i = 0; i < n; ++i)
            p->DefineSection((Int_t)i, pcon->getZPlane(i) * gLenScale,
                             pcon->getRMinPlane(i) * gLenScale, pcon->getRMaxPlane(i) * gLenScale);
        return p;
    }
    if (const auto* pgon = dynamic_cast<const GeoPgon*>(s)) {
        const double sphi = pgon->getSPhi() * kRadToDeg;
        const double dphi = pgon->getDPhi() * kRadToDeg;
        const unsigned n = pgon->getNPlanes();
        const unsigned nsides = pgon->getNSides();
        auto* p = new TGeoPgon(sphi, dphi, (Int_t)nsides, (Int_t)n);
        for (unsigned i = 0; i < n; ++i)
            p->DefineSection((Int_t)i, pgon->getZPlane(i) * gLenScale,
                             pgon->getRMinPlane(i) * gLenScale, pgon->getRMaxPlane(i) * gLenScale);
        return p;
    }

    // ---- Booleans -----------------------------------------------------------
    if (const auto* shift = dynamic_cast<const GeoShapeShift*>(s)) {
        // Convert the operand, then compose this shift ON TOP of whatever
        // shift the operand already carried, and pass it up.
        TGeoHMatrix inner;
        TGeoShape* op = convertShape(shift->getOp(), inner);
        if (!op) return nullptr;
        localShift = toTGeo(shift->getX());  // API: getX() -> Transform3D
        localShift.Multiply(&inner);
        return op;
    }
    if (const auto* u = dynamic_cast<const GeoShapeUnion*>(s))
        return makeBoolean("union", u->getOpA(), u->getOpB());
    if (const auto* d = dynamic_cast<const GeoShapeSubtraction*>(s))
        return makeBoolean("subtraction", d->getOpA(), d->getOpB());
    if (const auto* i = dynamic_cast<const GeoShapeIntersection*>(s))
        return makeBoolean("intersection", i->getOpA(), i->getOpB());

    warnUnsupported(s);
    return nullptr;
}

// -----------------------------------------------------------------------------
//  Recursive walk.
// -----------------------------------------------------------------------------
struct WalkState {
    const std::vector<std::regex>& include;
    const std::vector<std::regex>& exclude;
    const GeoLoadOptions& opt;
    const GeoEmit& emit;
    std::size_t emitted = 0;
    std::size_t visited = 0;
    std::size_t pruned = 0;
    bool capped = false;
};

bool matchesAny(const std::string& name, const std::vector<std::regex>& pats) {
    for (const auto& p : pats)
        if (std::regex_search(name, p)) return true;
    return false;
}

void walk(const GeoVPhysVol* vol, const GeoTrf::Transform3D& parentToWorld, int depth,
          WalkState& st) {
    if (!vol || st.capped) return;
    if (st.opt.max_depth >= 0 && depth > st.opt.max_depth) return;

    const unsigned nChild = vol->getNChildVols();  // API: expanded child count
    for (unsigned i = 0; i < nChild; ++i) {
        if (st.emitted >= st.opt.max_shapes) {
            if (!st.capped) {
                std::cerr << "[GeoModelLoader] hit max_shapes=" << st.opt.max_shapes
                          << " -- stopping (tighten "
                             "include/exclude regexes)\n";
                st.capped = true;
            }
            return;
        }

        const GeoVPhysVol* child = &(*vol->getChildVol(i));  // PVConstLink deref
        const GeoTrf::Transform3D childToWorld =
            parentToWorld * vol->getXToChildVol(i);  // API: child transform

        const GeoLogVol* lv = child->getLogVol();
        const std::string name = lv ? lv->getName() : std::string("<noLV>");
        ++st.visited;

        // Cheap z gate FIRST: converting a GeoShape into a TGeoShape is by far
        // the most expensive thing here, so on a ~1M-volume geometry we must
        // decide before doing it.
        bool inZ = true;
        if (st.opt.use_z_window) {
            const double zc = childToWorld.translation().z();
            // Bound the volume from the GeoModel shape alone. Children are
            // contained in their parent, so a volume whose extent misses the
            // window cannot have any descendant inside it -- prune the entire
            // subtree. This is what stops us walking the whole muon shield to
            // reach the spectrometer. If the shape can't be measured we do not
            // prune (safe direction).
            const double dz =
                lv ? zHalfExtentWorld(lv->getShape(), childToWorld) : -1.0;
            if (dz >= 0.0) {
                if (zc + dz < st.opt.z_window_min || zc - dz > st.opt.z_window_max) {
                    ++st.pruned;
                    continue;  // skip this volume AND its whole subtree
                }
            }
            inZ = (zc >= st.opt.z_window_min && zc <= st.opt.z_window_max);
        }

        const bool incl = st.include.empty() || matchesAny(name, st.include);
        const bool excl = !st.exclude.empty() && matchesAny(name, st.exclude);

        if (excl) continue;  // prune: neither emit nor descend into the subtree

        const bool matched = incl && inZ;

        if (matched) {
            TGeoHMatrix shift;
            TGeoShape* shape = lv ? convertShape(lv->getShape(), shift) : nullptr;
            if (shape) {
                TGeoHMatrix global = toTGeo(childToWorld);
                global.Multiply(&shift);  // fold any top-level Shift in
                st.emit(name + "#" + std::to_string(i), shape, global, depth);
                ++st.emitted;

            }
            if (st.opt.stop_at_match) continue;  // envelope: don't descend
        }
        // Descend (unmatched always; matched only if stop_at_match == false).
        walk(child, childToWorld, depth + 1, st);
    }
}

std::vector<std::regex> compile(const std::vector<std::string>& pats, bool icase = false) {
    std::vector<std::regex> out;
    out.reserve(pats.size());
    const auto flags =
        icase ? (std::regex::ECMAScript | std::regex::icase) : std::regex::ECMAScript;
    (void)flags;
    for (const auto& s : pats) out.emplace_back(CompileNamePattern(s, icase));
    return out;
}

}  // namespace

// =============================================================================
//  Public entry points
// =============================================================================
std::size_t WalkGeoModelWorld(const GeoVPhysVol* world, const GeoLoadOptions& opt,
                              const GeoEmit& emit) {
    if (!world) throw std::runtime_error("GeoModelLoader: null world volume");

    gLenScale = opt.length_scale;  // mm -> scene for this walk

    const auto incl = compile(opt.include);
    const auto excl = compile(opt.exclude);
    WalkState st{incl, excl, opt, emit};

    walk(world, GeoTrf::Transform3D::Identity(), 0, st);

    if (opt.verbose && st.emitted == 0) {
        std::cerr << "[GeoModelLoader] 0 shapes matched out of " << st.visited
                  << " volumes visited.\n"
                     "[GeoModelLoader] include patterns: ";
        for (const auto& s : opt.include) std::cerr << "'" << s << "' ";
        std::cerr << "\n[GeoModelLoader] (empty include => match all; check "
                     "your regex against the gmex volume names)\n";
    } else if (opt.verbose) {
        std::cout << "[GeoModelLoader] emitted " << st.emitted << " shapes (" << st.visited
                  << " volumes visited, " << st.pruned << " subtrees pruned by z)\n";
    }
    return st.emitted;
}

namespace {

// Keeps a parsed GeoModel world alive for the process lifetime. The GMDBManager
// and ReadGeoModel must outlive the tree, and the world handle is stored in a
// std::any so this works whether buildGeoModel() returns a raw pointer or an
// intrusive smart pointer (the type differs across GeoModel 6.x releases).
struct WorldHolder {
    std::shared_ptr<GMDBManager> db;
    std::unique_ptr<GeoModelIO::ReadGeoModel> reader;
    std::any worldLink;
    const GeoVPhysVol* world = nullptr;
};

std::map<std::string, WorldHolder>& worldCache() {
    static std::map<std::string, WorldHolder> cache;
    return cache;
}

}  // namespace

namespace {

// Name/position-only traversal: no GeoShape -> TGeoShape conversion at all.
void scanWalk(const GeoVPhysVol* vol, const GeoTrf::Transform3D& parentToWorld, int depth,
              const GeoLoadOptions& opt, const std::vector<std::regex>& include,
              const std::vector<std::regex>& exclude, const GeoScan& scan, std::size_t& visited) {
    if (!vol) return;
    if (opt.max_depth >= 0 && depth > opt.max_depth) return;

    const unsigned nChild = vol->getNChildVols();
    for (unsigned i = 0; i < nChild; ++i) {
        const GeoVPhysVol* child = &(*vol->getChildVol(i));
        const GeoTrf::Transform3D childToWorld = parentToWorld * vol->getXToChildVol(i);
        const GeoLogVol* lv = child->getLogVol();
        const std::string name = lv ? lv->getName() : std::string("<noLV>");
        ++visited;

        if (!exclude.empty() && matchesAny(name, exclude)) continue;  // prune subtree
        const bool matched = include.empty() || matchesAny(name, include);
        if (matched) {
            const GeoLogVol* clv = child->getLogVol();
            const double dz = clv ? zHalfExtentWorld(clv->getShape(), childToWorld) : -1.0;
            scan(name, childToWorld.translation().z(), dz, depth);
            // Locating a subsystem envelope does not require walking its
            // interior, which may hold ~100k volumes.
            if (opt.stop_at_match) continue;
        }
        scanWalk(child, childToWorld, depth + 1, opt, include, exclude, scan, visited);
    }
}

}  // namespace


std::regex CompileNamePattern(const std::string& pattern, bool icase) {
    const auto flags =
        icase ? (std::regex::ECMAScript | std::regex::icase) : std::regex::ECMAScript;
    try {
        return std::regex(pattern, flags);
    } catch (const std::regex_error&) {
        // Not a regex -- treat it as a glob ("*ms*" -> ".*ms.*").
        std::string re;
        re.reserve(pattern.size() * 2);
        for (const char ch : pattern) {
            switch (ch) {
                case '*': re += ".*"; break;
                case '?': re += '.'; break;
                case '.': case '+': case '(': case ')': case '[': case ']':
                case '{': case '}': case '^': case '$': case '|': case '\\':
                    re += '\\';
                    re += ch;
                    break;
                default: re += ch;
            }
        }
        try {
            return std::regex(re, flags);
        } catch (const std::regex_error& e) {
            std::cerr << "[GeoModelLoader] unusable name pattern '" << pattern << "' (" << e.what()
                      << ") -- it will match nothing\n";
            return std::regex("(?!)");  // never matches
        }
    }
}

std::size_t ScanGeoModelDB(const std::string& db_path, const GeoLoadOptions& opt,
                           const GeoScan& scan) {
    const GeoVPhysVol* world = GetCachedGeoModelWorld(db_path);
    if (!world) {
        throw std::runtime_error("GeoModelLoader: cannot open/build .db '" + db_path + "'");
    }
    const auto incl = compile(opt.include, opt.icase);
    const auto excl = compile(opt.exclude, opt.icase);
    std::size_t visited = 0;
    scanWalk(world, GeoTrf::Transform3D::Identity(), 0, opt, incl, excl, scan, visited);
    return visited;
}

const GeoVPhysVol* GetCachedGeoModelWorld(const std::string& db_path) {
    auto& cache = worldCache();
    if (auto it = cache.find(db_path); it != cache.end()) return it->second.world;

    WorldHolder h;
    h.db = std::make_shared<GMDBManager>(db_path);
    if (!h.db->checkIsDBOpen()) return nullptr;

    h.reader = std::make_unique<GeoModelIO::ReadGeoModel>(h.db);
    auto link = h.reader->buildGeoModel();
    if (!link) return nullptr;
    h.world = &(*link);
    h.worldLink = std::move(link);  // keep the handle (and refcount) alive

    const GeoVPhysVol* w = h.world;
    cache.emplace(db_path, std::move(h));
    return w;
}

std::size_t LoadGeoModelDB(const std::string& db_path, const GeoLoadOptions& opt,
                           const GeoEmit& emit) {
    const GeoVPhysVol* world = GetCachedGeoModelWorld(db_path);
    if (!world) {
        throw std::runtime_error("GeoModelLoader: cannot open/build .db '" + db_path + "'");
    }
    return WalkGeoModelWorld(world, opt, emit);
}

}  // namespace shipdisp

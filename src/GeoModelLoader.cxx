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

        const bool incl = st.include.empty() || matchesAny(name, st.include);
        const bool excl = !st.exclude.empty() && matchesAny(name, st.exclude);

        if (excl) continue;  // prune: neither emit nor descend into the subtree

        const bool matched = incl;

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

std::vector<std::regex> compile(const std::vector<std::string>& pats) {
    std::vector<std::regex> out;
    out.reserve(pats.size());
    for (const auto& s : pats) out.emplace_back(s, std::regex::ECMAScript);
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
                  << " volumes visited)\n";
    }
    return st.emitted;
}

std::size_t LoadGeoModelDB(const std::string& db_path, const GeoLoadOptions& opt,
                           const GeoEmit& emit) {
    // --- open the SQLite geometry file ---------------------------------------
    auto db = std::make_shared<GMDBManager>(db_path);
    if (!db->checkIsDBOpen())
        throw std::runtime_error("GeoModelLoader: cannot open .db '" + db_path + "'");

    // --- rebuild the GeoModel tree -------------------------------------------
    GeoModelIO::ReadGeoModel reader(db);  // shared_ptr ctor (6.x-preferred)
    // API: buildGeoModel() returns the world physical volume.  Depending on
    // the GeoModel version this is either a raw `GeoVPhysVol*` or a
    // `PVConstLink` (a GeoIntrusivePtr).  `auto` + a null test via `if (!w)`
    // works for both (GeoIntrusivePtr has an explicit-bool conversion), so
    // we never dereference before checking.
    auto worldLink = [&]() {
        try {
            return reader.buildGeoModel();
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("GeoModelLoader: buildGeoModel failed: ") +
                                     e.what());
        }
    }();
    if (!worldLink)
        throw std::runtime_error("GeoModelLoader: null world volume from '" + db_path + "'");

    return WalkGeoModelWorld(&(*worldLink), opt, emit);
}

}  // namespace shipdisp

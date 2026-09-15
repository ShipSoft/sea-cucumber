// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
// =============================================================================
//  sea_cucumber -- the SHiP event display.
//
//  Inputs: --geometry <ship.db>  --data <events.root>  [--view <view.toml>]
//
//  Draws the detector geometry, the event HITS (pink, size ramped by energy)
//  and the TRUTH decay-vertex marker. No fabricated tracks/reco.
//
//  Layout: one large viewer with the whole detector, plus one zoomed viewer per
//  configured REGION (by default: Spectrometer, Calorimeter, SND). Each region
//  gets its own pair of scenes holding only the geometry and hits inside its z
//  window, so the camera auto-fit frames exactly that sub-detector.
//
//  A region's window is either explicit (zmin/zmax) or derived from the
//  geometry by matching volume names -- the latter keeps the display geometry
//  agnostic for detectors whose position we don't want to hardcode.
//
//  One uniform scene scale (mm * view.hit_scale) is shared by geometry + hits.
// =============================================================================

#include <ROOT/REveElement.hxx>
#include <ROOT/REveGeoShape.hxx>
#include <ROOT/REveManager.hxx>
#include <ROOT/REvePointSet.hxx>
#include <ROOT/REveScene.hxx>
#include <ROOT/REveTrans.hxx>
#include <ROOT/REveViewer.hxx>

#include <Rtypes.h>
#include <TApplication.h>
#include <TColor.h>
#include <TGeoBBox.h>
#include <TGeoMatrix.h>
#include <TGeoShape.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "EventNavigator.h"
#include "GeoModelGeometrySource.h"
#include "GeometryCache.h"
#include "IEventSource.h"
#include "IGeometrySource.h"
#include "RNTupleEventSource.h"
#include "ViewConfig.h"

namespace REX = ROOT::Experimental;

namespace shipdisp {

namespace {

Color_t hexColor(const std::string& s, Color_t fb = kGray) {
    if (s.size() == 7 && s[0] == '#') return static_cast<Color_t>(TColor::GetColor(s.c_str()));
    try {
        return static_cast<Color_t>(std::stoi(s));
    } catch (...) {
        return fb;
    }
}

// Camera orientation.
//
// ROOT's ECameraType names ("XOY", "XOZ", "ZOY") do NOT map onto the intuitive
// "which plane am I looking at" reading -- kCameraOrthoXOY shows the detector
// extended along the beam, i.e. it looks along x/y, not along z. So the config
// uses INTENT names, with the raw enum names available as an escape hatch:
//
//   "side"  / "xz"  -- detector seen lengthways        (kCameraOrthoXOZ)
//   "top"   / "xy0" -- from above, also lengthways     (kCameraOrthoXOY)
//   "front" / "xy"  -- facing the beam, cross-section  (kCameraOrthoZOY)
//   "3d"    / "persp"                                  (kCameraPerspXOZ)
//
// Raw names ("orthoxoy", "orthoxoz", "orthozoy", "perspxoz") bypass all of
// this if the intent names still don't land where you expect.
REX::REveViewer::ECameraType cameraFor(const std::string& c) {
    // --- raw enum names ------------------------------------------------------
    if (c == "orthoxoy") return REX::REveViewer::kCameraOrthoXOY;
    if (c == "orthoxoz") return REX::REveViewer::kCameraOrthoXOZ;
    if (c == "orthozoy") return REX::REveViewer::kCameraOrthoZOY;
    if (c == "perspxoz") return REX::REveViewer::kCameraPerspXOZ;
    // --- intent names --------------------------------------------------------
    if (c == "front" || c == "xy" || c == "yx") return REX::REveViewer::kCameraOrthoZOY;
    if (c == "top" || c == "xy0") return REX::REveViewer::kCameraOrthoXOY;
    if (c == "3d" || c == "persp") return REX::REveViewer::kCameraPerspXOZ;
    return REX::REveViewer::kCameraOrthoXOZ;  // "side" / "xz" (default)
}

// Human-readable description of what a camera name resolves to, so the log can
// be correlated with what actually appears on screen.
const char* cameraDesc(const std::string& c) {
    if (c == "orthoxoy" || c == "top" || c == "xy0") return "kCameraOrthoXOY (top)";
    if (c == "orthozoy" || c == "front" || c == "xy" || c == "yx")
        return "kCameraOrthoZOY (front, facing the beam)";
    if (c == "perspxoz" || c == "3d" || c == "persp") return "kCameraPerspXOZ (perspective)";
    return "kCameraOrthoXOZ (side)";
}

}  // namespace

class EventDisplay {
   public:
    explicit EventDisplay(ViewConfig view) : view_(std::move(view)), scale_(view_.hit_scale) {}

    void init() {
        eve_ = REX::REveManager::Create();
        geoScene_ = eve_->GetGlobalScene();
        eventScene_ = eve_->GetEventScene();
        geoHolder_ = new REX::REveElement("Detector");
        geoScene_->AddElement(geoHolder_);
        eventHolder_ = new REX::REveElement("Event");
        eventScene_->AddElement(eventHolder_);

        // One scene pair per region, so each zoom viewer sees only its own
        // sub-detector and its camera auto-fit frames just that.
        regions_.reserve(view_.regions.size());
        for (const auto& rv : view_.regions) {
            Region r;
            r.cfg = rv;
            r.geoScene = eve_->SpawnNewScene((rv.name + "Geo").c_str());
            r.eventScene = eve_->SpawnNewScene((rv.name + "Event").c_str());
            r.geoHolder = new REX::REveElement((rv.name + "Detector").c_str());
            r.geoScene->AddElement(r.geoHolder);
            r.eventHolder = new REX::REveElement((rv.name + "Hits").c_str());
            r.eventScene->AddElement(r.eventHolder);
            regions_.push_back(r);
        }
    }

    void setupViewers() {
        if (auto* def = eve_->GetDefaultViewer()) {
            def->SetName("Sea cucumber main panel");
            def->SetCameraType(REX::REveViewer::kCameraPerspXOZ);
            def->SetBlackBackground(true);
        }
        for (auto& r : regions_) {
            const std::string title = r.cfg.title.empty() ? r.cfg.name : r.cfg.title;
            REX::REveViewer* v = eve_->SpawnNewViewer(r.cfg.name.c_str(), title.c_str());
            if (!v) continue;
            v->RemoveElements();  // drop auto-attached global/event scenes
            v->AddScene(r.geoScene);
            v->AddScene(r.eventScene);
            v->SetCameraType(cameraFor(r.cfg.camera));
            v->SetBlackBackground(true);
            std::cout << "[sea_cucumber] viewer '" << r.cfg.name << "': camera \"" << r.cfg.camera
                      << "\" -> " << cameraDesc(r.cfg.camera) << "\n";
        }
    }

    // Whole-detector geometry (subsystem envelopes) into the large viewer.
    void loadGeometry(IGeometrySource& src) {
        std::cout << "[sea_cucumber] reading geometry database..." << std::endl;
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t n = src.provide(
            [&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& global, int) {
                auto* es = new REX::REveGeoShape(name.c_str());
                es->SetShape(shape);
                es->RefMainTrans().SetFrom(const_cast<TGeoHMatrix&>(global));
                es->SetMainColor(geoColor(name));
                es->SetMainTransparency(static_cast<Char_t>(view_.transparencyForVolume(name)));
                geoHolder_->AddElement(es);
            });
        const auto t1 = std::chrono::steady_clock::now();
        std::cout << "[sea_cucumber] geometry: " << n << " shapes in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
                  << " ms\n";
    }

    // Resolve each region's z window BEFORE any shape is built. Explicit
    // windows are taken as-is; name-derived ones come from a single cheap scan
    // pass (no shape conversion), so this costs almost nothing even on a
    // ~1M-volume geometry. Returns the union window, which the caller uses to
    // restrict what the expensive walk actually converts.
    // Assign each region's window from the per-region match accumulators
    // (lo/hi/nmatch) plus its explicit config, and return the union z-window.
    // Shared by the live-scan and cache-replay resolvers.
    std::pair<double, double> finalizeWindows(const std::vector<double>& lo,
                                              const std::vector<double>& hi,
                                              const std::vector<std::size_t>& nmatch) {
        double uLo = 1e30, uHi = -1e30;
        static const char* kAx[3] = {"x", "y", "z"};
        for (std::size_t k = 0; k < regions_.size(); ++k) {
            auto& r = regions_[k];
            std::string desc;
            for (int ax = 0; ax < 3; ++ax) {
                if (!r.cfg.has_window[ax]) continue;
                r.has[ax] = true;
                r.lo[ax] = r.cfg.wmin[ax];
                r.hi[ax] = r.cfg.wmax[ax];
                desc += std::string(desc.empty() ? "" : ", ") + kAx[ax] + " [" +
                        std::to_string(static_cast<std::int64_t>(r.lo[ax])) + ", " +
                        std::to_string(static_cast<std::int64_t>(r.hi[ax])) + "]";
            }
            if (!desc.empty()) {
                r.resolved = true;
                std::cout << "[sea_cucumber] region '" << r.cfg.name << "': window " << desc
                          << " mm (explicit)\n";
            } else if (nmatch[k] > 0 && hi[k] > lo[k]) {
                // Name-derived windows constrain z (that is what locating a
                // subsystem along the beam means).
                const double margin = r.cfg.margin_frac * std::max(1.0, hi[k] - lo[k]);
                r.has[2] = true;
                r.lo[2] = lo[k] - margin;
                r.hi[2] = hi[k] + margin;
                r.resolved = true;
                std::cout << "[sea_cucumber] region '" << r.cfg.name << "': window from "
                          << nmatch[k] << " volumes matching '" << r.cfg.match << "' -> z ["
                          << r.lo[2] << ", " << r.hi[2] << "] mm\n";
            } else {
                std::cerr << "[sea_cucumber] region '" << r.cfg.name
                          << "': no window resolved -- set xmin/xmax, ymin/ymax or zmin/zmax, "
                             "or a `match` pattern\n";
            }
            if (r.resolved && r.has[2]) {
                uLo = std::min(uLo, r.lo[2]);
                uHi = std::max(uHi, r.hi[2]);
            } else if (r.resolved) {
                // A region with no z constraint needs the whole beam line, so
                // the walk cannot be restricted in z.
                uLo = -1e30;
                uHi = 1e30;
            }
        }
        return {uLo, uHi};
    }

    // Resolve region windows by REPLAYING a geometry source (a cache), instead
    // of scanning the live .db. Used on the --geo-cache path so the database is
    // never touched. Costs one extra replay of the (already reduced) region
    // cache -- cheap compared to a GeoModel build.
    std::pair<double, double> resolveRegionWindowsFromSource(IGeometrySource& src) {
        std::vector<double> lo(regions_.size(), 1e30), hi(regions_.size(), -1e30);
        std::vector<std::size_t> nmatch(regions_.size(), 0);
        std::vector<std::regex> res(regions_.size());
        std::vector<bool> useRgx(regions_.size(), false);
        for (std::size_t k = 0; k < regions_.size(); ++k) {
            const auto& c = regions_[k].cfg;
            if (c.hasAnyWindow() || c.match.empty()) continue;
            try {
                res[k] = std::regex(c.match, std::regex::ECMAScript | std::regex::icase);
                useRgx[k] = true;
            } catch (const std::regex_error& e) {
                std::cerr << "[sea_cucumber] region '" << c.name << "': bad match regex ("
                          << e.what() << ")\n";
            }
        }
        src.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int) {
            const double z = g.GetTranslation()[2] / scale_;
            double dz = 0.0;
            if (const auto* bb = dynamic_cast<const TGeoBBox*>(shape)) dz = bb->GetDZ() / scale_;
            for (std::size_t k = 0; k < regions_.size(); ++k) {
                if (useRgx[k] && std::regex_search(name, res[k])) {
                    lo[k] = std::min(lo[k], z - dz);
                    hi[k] = std::max(hi[k], z + dz);
                    ++nmatch[k];
                }
            }
            delete shape;  // we own the emitted shape in this scan-only replay
        });
        return finalizeWindows(lo, hi, nmatch);
    }

    std::pair<double, double> resolveRegionWindows(const std::string& dbPath,
                                                   const GeoLoadOptions& baseOpt) {
        bool needScan = false;
        for (const auto& r : regions_) {
            if (!r.cfg.hasAnyWindow() && !r.cfg.match.empty()) needScan = true;
        }

        // Per-region accumulators for the name-derived windows.
        std::vector<double> lo(regions_.size(), 1e30), hi(regions_.size(), -1e30);
        std::vector<std::size_t> nmatch(regions_.size(), 0);
        std::vector<std::regex> res(regions_.size());
        std::vector<bool> useRgx(regions_.size(), false);

        if (needScan) {
            for (std::size_t k = 0; k < regions_.size(); ++k) {
                const auto& c = regions_[k].cfg;
                if (c.hasAnyWindow() || c.match.empty()) continue;
                try {
                    res[k] = std::regex(c.match, std::regex::ECMAScript | std::regex::icase);
                    useRgx[k] = true;
                } catch (const std::regex_error& e) {
                    std::cerr << "[sea_cucumber] region '" << c.name << "': bad match regex ("
                              << e.what() << ")\n";
                }
            }
            GeoLoadOptions scanOpt = baseOpt;
            // Bounded depth: the scan descends through NON-matching volumes, so
            // an unbounded one walks the entire geometry. Subsystem envelopes
            // are shallow, so a small depth finds them at a fraction of the
            // cost. Configurable via `scan_depth`.
            scanOpt.max_depth = view_.scan_depth;
            scanOpt.stop_at_match = true;  // and stop at the envelope itself
            scanOpt.icase = true;          // names vary in capitalisation
            // Only report volumes a region actually asked for: the walk then
            // descends just far enough to find each subsystem envelope instead
            // of touching every volume in the geometry.
            scanOpt.include.clear();
            for (const auto& r : regions_) {
                if (!r.cfg.hasAnyWindow() && !r.cfg.match.empty()) {
                    scanOpt.include.push_back(r.cfg.match);
                }
            }
            std::cout << "[sea_cucumber] scanning geometry for region windows (depth <= "
                      << scanOpt.max_depth << ")..." << std::endl;
            const auto t0 = std::chrono::steady_clock::now();
            const std::size_t visited = ScanGeoModelDB(
                dbPath, scanOpt, [&](const std::string& name, double z, double dz, int) {
                    // Use the measured extent when available so the window
                    // covers the whole subsystem, not just its origin.
                    const double half = (dz >= 0.0) ? dz : 0.0;
                    for (std::size_t k = 0; k < regions_.size(); ++k) {
                        if (!useRgx[k]) continue;
                        if (std::regex_search(name, res[k])) {
                            lo[k] = std::min(lo[k], z - half);
                            hi[k] = std::max(hi[k], z + half);
                            ++nmatch[k];
                        }
                    }
                });
            const auto t1 = std::chrono::steady_clock::now();
            std::cout << "[sea_cucumber] name scan: " << visited << " volumes inspected in "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
                      << " ms\n";
        }

        return finalizeWindows(lo, hi, nmatch);
    }

    // One deep walk, distributed to every region. Cheaper than walking per
    // region, and lets a region derive its window from the same pass.
    void loadRegionGeometry(IGeometrySource& src) {
        struct Rec {
            TGeoShape* shape;
            TGeoHMatrix m;
            std::string name;
            double c[3];  // centre (x, y, z), mm
            double d[3];  // half-extents, mm (0 if unknown)
            bool used;
        };
        std::vector<Rec> recs;
        double zmin = 1e30, zmax = -1e30;
        std::cout << "[sea_cucumber] building region geometry..." << std::endl;
        const auto tWalk0 = std::chrono::steady_clock::now();
        src.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int) {
            const Double_t* t = g.GetTranslation();
            Rec r{shape, g, name, {t[0] / scale_, t[1] / scale_, t[2] / scale_}, {0, 0, 0}, false};
            if (const auto* bb = dynamic_cast<const TGeoBBox*>(shape)) {
                r.d[0] = bb->GetDX() / scale_;
                r.d[1] = bb->GetDY() / scale_;
                r.d[2] = bb->GetDZ() / scale_;
            }
            recs.push_back(r);
            zmin = std::min(zmin, r.c[2]);
            zmax = std::max(zmax, r.c[2]);
        });
        const auto tWalk1 = std::chrono::steady_clock::now();
        std::cout << "[sea_cucumber] region walk: " << recs.size() << " shapes built in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(tWalk1 - tWalk0).count()
                  << " ms, z-range [" << zmin << ", " << zmax << "] mm\n";

        for (auto& r : regions_) {
            if (!r.resolved) continue;

            // --- select volumes ---------------------------------------------
            // Keep volumes whose z EXTENT fits the window, not merely their
            // centre: that rejects world/cavern shells which are centred in
            // range but span the whole detector (useless in a zoom, and very
            // expensive to render).
            // Per-region name filters. The geometry walk is shared by every
            // region, so these are applied here rather than during the walk.
            // Patterns may be regex or glob ("*ms*").
            std::vector<std::regex> rIncl, rExcl;
            for (const auto& pat : r.cfg.include) rIncl.push_back(CompileNamePattern(pat, true));
            for (const auto& pat : r.cfg.exclude) rExcl.push_back(CompileNamePattern(pat, true));
            auto anyMatch = [](const std::string& n, const std::vector<std::regex>& v) {
                for (const auto& re : v) {
                    if (std::regex_search(n, re)) return true;
                }
                return false;
            };

            std::vector<Rec*> keep;
            std::size_t rejectedBig = 0;
            std::size_t rejectedName = 0;
            for (auto& rec : recs) {
                // A volume must sit inside EVERY axis window that is set.
                bool inAll = true;
                bool oversized = false;
                for (int ax = 0; ax < 3 && inAll; ++ax) {
                    if (!r.has[ax]) continue;
                    const double win = r.hi[ax] - r.lo[ax];
                    const double tol = 0.25 * std::max(1.0, win);
                    if (rec.c[ax] < r.lo[ax] || rec.c[ax] > r.hi[ax]) {
                        inAll = false;
                        break;
                    }
                    // Keep only volumes whose EXTENT fits, not merely their
                    // centre: that rejects world/cavern shells which are
                    // centred in range but span the whole detector.
                    if (rec.c[ax] - rec.d[ax] < r.lo[ax] - tol ||
                        rec.c[ax] + rec.d[ax] > r.hi[ax] + tol) {
                        inAll = false;
                        oversized = true;
                        break;
                    }
                }
                if (!inAll) {
                    if (oversized) ++rejectedBig;
                    continue;
                }
                if (!rExcl.empty() && anyMatch(rec.name, rExcl)) {
                    ++rejectedName;
                    continue;
                }
                if (!rIncl.empty() && !anyMatch(rec.name, rIncl)) {
                    ++rejectedName;
                    continue;
                }
                keep.push_back(&rec);
            }

            // Budget: keep the visually largest so the picture stays
            // representative while the viewer stays interactive.
            std::size_t dropped = 0;
            if (keep.size() > r.cfg.max_shapes) {
                std::sort(keep.begin(), keep.end(),
                          [](const Rec* a, const Rec* b) { return a->d[2] > b->d[2]; });
                dropped = keep.size() - r.cfg.max_shapes;
                keep.resize(r.cfg.max_shapes);
            }

            // Centre the region on the origin: Eve7 auto-fits about (0,0,0),
            // so content tens of metres downstream would otherwise render far
            // off to one side. We shift the CONTENT because the web client
            // exposes no reliable way to place the camera.
            //
            // Per axis: if the region has an explicit window, centre on the
            // window MIDPOINT. This makes the window control both the framing
            // (its centre) and the zoom (its size) predictably -- narrow it to
            // zoom in, shift it to pan -- and, crucially, it frames the whole
            // window rather than just the geometry, so hits that extend past
            // the drawn volumes are no longer chopped. Axes with no explicit
            // window fall back to the geometry centroid.
            if (r.cfg.recenter && !keep.empty()) {
                double clo[3] = {1e30, 1e30, 1e30}, chi[3] = {-1e30, -1e30, -1e30};
                for (const auto* rec : keep) {
                    const Double_t* t = rec->m.GetTranslation();
                    for (int ax = 0; ax < 3; ++ax) {
                        clo[ax] = std::min(clo[ax], t[ax]);
                        chi[ax] = std::max(chi[ax], t[ax]);
                    }
                }
                double c[3];
                for (int ax = 0; ax < 3; ++ax) {
                    c[ax] = r.has[ax] ? 0.5 * (r.lo[ax] + r.hi[ax]) * scale_
                                      : 0.5 * (clo[ax] + chi[ax]);
                }
                r.ox = c[0];
                r.oy = c[1];
                r.oz = c[2];
            }
            // Manual nudge, given in mm -> scene units.
            r.ox += r.cfg.offset_x * scale_;
            r.oy += r.cfg.offset_y * scale_;
            r.oz += r.cfg.offset_z * scale_;

            for (auto* rec : keep) {
                // A shape can only be owned by one REveGeoShape; clone if a
                // previous region already took it (windows may overlap).
                TGeoShape* sh =
                    rec->used ? static_cast<TGeoShape*>(rec->shape->Clone()) : rec->shape;
                rec->used = true;
                auto* d = new REX::REveGeoShape(rec->name.c_str());
                d->SetShape(sh);
                TGeoHMatrix shifted = rec->m;
                const Double_t* t0 = shifted.GetTranslation();
                const Double_t nt[3] = {t0[0] - r.ox, t0[1] - r.oy, t0[2] - r.oz};
                shifted.SetTranslation(const_cast<Double_t*>(nt));
                d->RefMainTrans().SetFrom(shifted);
                d->SetMainColor(geoColor(rec->name));
                d->SetMainTransparency(static_cast<Char_t>(
                    std::min(view_.transparencyForVolume(rec->name), r.cfg.max_transparency)));
                r.geoHolder->AddElement(d);
            }

            // Force the ortho auto-fit to show the WHOLE window. Eve7's web
            // viewer appears to scale its orthographic camera to one screen
            // axis, so a detector that is long in z but thin transversely gets
            // fit to the narrow transverse extent and the long z overflows the
            // panel (the user then has to zoom out). To defeat that we add an
            // invisible CUBE of corner points: equal half-extent on all three
            // axes, sized to the LONGEST window axis and centred at the origin
            // (where the recentred content sits). Whichever axis the fit keys
            // on, the scale is then large enough to show the full extent.
            // Points count toward the scene bounding box regardless of marker
            // rendering. Trade-off: a long thin region will show empty space on
            // its short axes -- that is the honest shape of the thing; narrow
            // the window to zoom.
            if (r.cfg.recenter) {
                double half[3];
                for (int ax = 0; ax < 3; ++ax) {
                    if (r.has[ax]) {
                        half[ax] = 0.5 * (r.hi[ax] - r.lo[ax]) * scale_;
                    } else {
                        double lo = 1e30, hi = -1e30;
                        for (const auto* rec : keep) {
                            const double c = rec->m.GetTranslation()[ax] - r.o(ax);
                            lo = std::min(lo, c);
                            hi = std::max(hi, c);
                        }
                        half[ax] = (hi > lo) ? 0.5 * (hi - lo) : 1.0;
                    }
                }
                const double H = std::max({half[0], half[1], half[2]});
                auto* frame = new REX::REvePointSet("fit_frame");
                for (int i = 0; i < 8; ++i) {
                    frame->SetNextPoint(static_cast<float>((i & 1) ? H : -H),
                                        static_cast<float>((i & 2) ? H : -H),
                                        static_cast<float>((i & 4) ? H : -H));
                }
                frame->SetMarkerStyle(1);
                frame->SetMarkerSize(0);          // invisible: only the bbox matters
                frame->SetMainTransparency(100);  // transparent as a fallback
                r.geoHolder->AddElement(frame);
            }
            std::cout << "[sea_cucumber] region '" << r.cfg.name << "': " << keep.size()
                      << " shapes (rejected " << rejectedBig << " oversized, " << rejectedName
                      << " by name, dropped " << dropped << " over budget " << r.cfg.max_shapes
                      << "), centred by (" << r.ox / scale_ << ", " << r.oy / scale_ << ", "
                      << r.oz / scale_ << ") mm\n";
        }

        // Free anything no region took ownership of.
        for (auto& rec : recs) {
            if (!rec.used) delete rec.shape;
        }
    }

    void setSource(IEventSource* s) { source_ = s; }
    std::int64_t numEvents() const { return source_ ? source_->numEvents() : 0; }

    void gotoEvent(std::int64_t i) {
        current_ = i;
        if (!source_ || !source_->loadEvent(i)) {
            std::cerr << "[sea_cucumber] no event " << i << "\n";
            return;
        }
        eventHolder_->DestroyElements();
        for (auto& r : regions_) r.eventHolder->DestroyElements();
        drawHits(source_->hits());
        if (view_.decay.draw) drawDecayVertex(source_->mcParticles());
    }

    /// Publish logo/ over the display's web server, so the browser can fetch
    /// e.g. /sea_cucumber_logo/sc.png. Harmless if the directory is absent.
    void serveLogo(const std::string& dir) {
        if (dir.empty()) return;
        try {
            eve_->AddLocation("sea_cucumber_logo/", dir);
            std::cout << "[sea_cucumber] serving logo from '" << dir
                      << "' at /sea_cucumber_logo/\n";
        } catch (const std::exception& e) {
            std::cerr << "[sea_cucumber] could not serve logo dir '" << dir << "': " << e.what()
                      << "\n";
        }
    }

    /// Add the in-GUI event navigator to the Eve world, so it appears in the
    /// browser tree and its *MENU* methods are reachable by right-click.
    void addNavigator() {
        auto* nav = new EventNavigator("EventNavigator");
        eve_->GetWorld()->AddElement(nav);
        nav->Configure([this](std::int64_t i) { this->gotoEvent(i); }, numEvents(), current_);
        std::cout << "[sea_cucumber] event navigator ready -- right-click "
                     "'EventNavigator' in the browser tree for Next / Previous / GotoEvent\n";
    }

    void show() {
        if (auto* vl = eve_->GetViewers()) vl->RepaintAllViewers(true, false);
        eve_->Show();
    }

   private:
    struct Region {
        RegionView cfg;
        REX::REveScene* geoScene = nullptr;
        REX::REveScene* eventScene = nullptr;
        REX::REveElement* geoHolder = nullptr;
        REX::REveElement* eventHolder = nullptr;
        // Per-axis window (0=x, 1=y, 2=z); `has[ax]` false means unconstrained.
        bool has[3] = {false, false, false};
        double lo[3] = {0, 0, 0};
        double hi[3] = {0, 0, 0};
        bool resolved = false;
        // Offset (scene units) subtracted from this region's contents so the
        // region is centred on the origin, where Eve7's camera auto-fit looks.
        double ox = 0, oy = 0, oz = 0;
        // Per-axis accessor for the offset (0=x, 1=y, 2=z).
        double o(int ax) const { return ax == 0 ? ox : (ax == 1 ? oy : oz); }
    };

    float sx(double mm) const { return static_cast<float>(mm * scale_); }

    // Config style if the name matches one; otherwise vary between blue and
    // cream by a stable name hash, so the palette is used even on geometries
    // whose volume names we don't recognise.
    Color_t geoColor(const std::string& name) const {
        const std::string& c = view_.colorForVolume(name);
        if (c != view_.geometry.default_color) return hexColor(c);
        const std::size_t h = std::hash<std::string>{}(name);
        return hexColor((h % 4 == 0) ? "#F1DEBC" : view_.geometry.default_color);
    }

    void drawHits(const std::vector<SHiP::SimHit>& hits) {
        if (hits.empty()) return;
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (const auto& h : hits) {
            lo = std::min(lo, h.energyDeposit);
            hi = std::max(hi, h.energyDeposit);
        }
        const double span = (hi > lo) ? (hi - lo) : 1.0;
        const Color_t pink = hexColor(view_.hits.color_high);

        auto makeBins = [&](REX::REveElement* holder, const char* tag, float baseSize) {
            std::array<REX::REvePointSet*, kBins> bins{};
            for (int b = 0; b < kBins; ++b) {
                const float size = baseSize * (1.0f + 1.8f * (view_.hits.color_by_energy ? b : 0) /
                                                          std::max(1, kBins - 1));
                auto* ps =
                    new REX::REvePointSet((std::string("hits_") + tag + std::to_string(b)).c_str());
                ps->SetMarkerStyle(view_.hits.marker_style);
                ps->SetMarkerSize(size);
                ps->SetMarkerColor(pink);
                holder->AddElement(ps);
                bins[b] = ps;
            }
            return bins;
        };

        auto full = makeBins(eventHolder_, "f", view_.hits.marker_size);
        std::vector<std::array<REX::REvePointSet*, kBins>> regionBins;
        regionBins.reserve(regions_.size());
        for (auto& r : regions_) {
            // Per-view marker size, falling back to the global one.
            const float sz =
                (r.cfg.hit_marker_size >= 0.0f) ? r.cfg.hit_marker_size : view_.hits.marker_size;
            regionBins.push_back(makeBins(r.eventHolder, r.cfg.name.c_str(), sz));
        }

        std::vector<std::size_t> counts(regions_.size(), 0);
        for (const auto& h : hits) {
            int b = view_.hits.color_by_energy
                        ? static_cast<int>((h.energyDeposit - lo) / span * (kBins - 1) + 0.5)
                        : kBins - 1;
            b = std::clamp(b, 0, kBins - 1);
            const float x = sx(h.position[0]);
            const float y = sx(h.position[1]);
            const float z = sx(h.position[2]);
            full[b]->SetNextPoint(x, y, z);
            for (std::size_t k = 0; k < regions_.size(); ++k) {
                const auto& r = regions_[k];
                bool inAll = r.resolved;
                for (int ax = 0; ax < 3 && inAll; ++ax) {
                    if (r.has[ax] && (h.position[ax] < r.lo[ax] || h.position[ax] > r.hi[ax])) {
                        inAll = false;
                    }
                }
                if (inAll) {
                    // Same frame shift as this region's geometry.
                    regionBins[k][b]->SetNextPoint(x - static_cast<float>(r.ox),
                                                   y - static_cast<float>(r.oy),
                                                   z - static_cast<float>(r.oz));
                    ++counts[k];
                }
            }
        }
        std::cout << "[sea_cucumber] hits: " << hits.size() << " total";
        for (std::size_t k = 0; k < regions_.size(); ++k) {
            std::cout << ", " << regions_[k].cfg.name << "=" << counts[k];
        }
        std::cout << "\n";
    }

    void drawDecayVertex(const std::vector<SHiP::MCParticle>& mc) {
        // The truth decay vertex is the production point shared by the decay
        // daughters, i.e. the first MC particle that has a mother.
        const SHiP::MCParticle* v = nullptr;
        for (const auto& p : mc) {
            if (p.motherId >= 0) {
                v = &p;
                break;
            }
        }
        if (!v) return;

        auto marker = [&](REX::REveElement* holder, float size, double ox, double oy, double oz) {
            auto* ps = new REX::REvePointSet("decay_vertex");
            ps->SetMarkerStyle(view_.decay.marker_style);
            ps->SetMarkerSize(size);
            ps->SetMarkerColor(hexColor(view_.decay.color));
            ps->SetNextPoint(sx(v->vertex[0]) - static_cast<float>(ox),
                             sx(v->vertex[1]) - static_cast<float>(oy),
                             sx(v->vertex[2]) - static_cast<float>(oz));
            holder->AddElement(ps);
        };

        // Full-detector view.
        marker(eventHolder_, view_.decay.marker_size, 0, 0, 0);

        // Each zoom view, in that view's own shifted frame.
        for (auto& r : regions_) {
            if (!r.resolved || !r.cfg.draw_decay) continue;
            if (r.cfg.decay_clip) {
                bool inAll = true;
                for (int ax = 0; ax < 3 && inAll; ++ax) {
                    if (r.has[ax] && (v->vertex[ax] < r.lo[ax] || v->vertex[ax] > r.hi[ax])) {
                        inAll = false;
                    }
                }
                if (!inAll)
                    continue;  // outside this view; drawing it would
                               // stretch the camera fit and undo the zoom
            }
            const float size = (r.cfg.decay_marker_size >= 0.0f) ? r.cfg.decay_marker_size
                                                                 : view_.decay.marker_size;
            marker(r.eventHolder, size, r.ox, r.oy, r.oz);
        }
    }

    static constexpr int kBins = 6;
    ViewConfig view_;
    double scale_;
    REX::REveManager* eve_ = nullptr;
    REX::REveScene* geoScene_ = nullptr;
    REX::REveScene* eventScene_ = nullptr;
    REX::REveElement* geoHolder_ = nullptr;
    REX::REveElement* eventHolder_ = nullptr;
    std::vector<Region> regions_;
    IEventSource* source_ = nullptr;
    std::int64_t current_ = 0;
};

}  // namespace shipdisp

namespace {
void usage(const char* a0) {
    std::cerr << "Usage: " << a0
              << " --geometry <ship.db> --data <events.root> [--view <view.toml>]\n"
                 "               [--ntuple <name>] [--event <i>] [--scale <f>] [--logo <dir>]\n"
                 "               [--geo-cache <prefix>]  use/require cached geometry (see "
                 "make_geometry_cache)\n"
                 "               [--inspect [depth]] [--inspect-match <pat>] [--inspect-all]\n"
                 "                     list the geometry's volumes (name, count, z span) and "
                 "exit;\n"
                 "                     <pat> is a regex or glob, e.g. \"*snd*\"\n";
}
}  // namespace

int main(int argc, char* argv[]) {
    std::string geometry, data, viewFile, ntuple, geoCache;
    std::int64_t event = 0;
    double scaleOverride = -1.0;
    int inspectDepth = -1;         // >=0 => print a geometry inventory and exit
    std::string inspectMatch;      // optional name filter for --inspect
    std::string logoDir = "logo";  // served at /sea_cucumber_logo/
    bool inspectAll = false;       // list every instance instead of aggregating
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* n) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << n << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--geometry") {
            geometry = next("--geometry");
        } else if (a == "--data") {
            data = next("--data");
        } else if (a == "--view") {
            viewFile = next("--view");
        } else if (a == "--ntuple") {
            ntuple = next("--ntuple");
        } else if (a == "--geo-cache") {
            geoCache = next("--geo-cache");
        } else if (a == "--event") {
            event = std::atoll(next("--event").c_str());
        } else if (a == "--scale") {
            scaleOverride = std::atof(next("--scale").c_str());
        } else if (a == "--logo") {
            logoDir = next("--logo");
        } else if (a == "--inspect-match") {
            inspectMatch = next("--inspect-match");
        } else if (a == "--inspect-all") {
            inspectAll = true;
        } else if (a == "--inspect") {
            // Optional depth; defaults to 2 when the next token is another flag.
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                inspectDepth = std::atoi(next("--inspect").c_str());
            } else {
                inspectDepth = 2;
            }
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            usage(argv[0]);
            return 2;
        }
    }
    // --inspect only needs the geometry.
    if (geometry.empty() || (data.empty() && inspectDepth < 0)) {
        usage(argv[0]);
        return 2;
    }

    shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile);
    if (scaleOverride > 0) view.hit_scale = scaleOverride;
    if (!ntuple.empty()) view.ntuple = ntuple;

    // --- inventory mode ------------------------------------------------------
    // Answers "where is each subsystem in z?" so region windows can be set from
    // fact rather than assumption. Uses the shape-free scan, so it is fast.
    if (inspectDepth >= 0) {
        shipdisp::GeoLoadOptions io;
        io.max_depth = inspectDepth;
        io.stop_at_match = false;
        io.icase = true;
        io.length_scale = 1.0;  // report native millimetres
        struct Agg {
            std::size_t count = 0;
            double zlo = 1e30, zhi = -1e30;
            int minDepth = 1 << 30;
        };
        std::map<std::string, Agg> agg;
        struct Row {
            std::string name;
            double z, dz;
            int depth;
        };
        std::vector<Row> rows;

        std::regex filter;
        const bool useFilter = !inspectMatch.empty();
        if (useFilter) filter = shipdisp::CompileNamePattern(inspectMatch, true);

        double gLo = 1e30, gHi = -1e30;
        const std::string dbPath = shipdisp::ResolveGeometryDbPath(geometry);
        const std::size_t visited = shipdisp::ScanGeoModelDB(
            dbPath, io, [&](const std::string& n, double z, double dz, int d) {
                if (useFilter && !std::regex_search(n, filter)) return;
                auto& a = agg[n];
                ++a.count;
                a.minDepth = std::min(a.minDepth, d);
                const double half = (dz >= 0) ? dz : 0.0;
                a.zlo = std::min(a.zlo, z - half);
                a.zhi = std::max(a.zhi, z + half);
                if (dz >= 0) {
                    gLo = std::min(gLo, z - dz);
                    gHi = std::max(gHi, z + dz);
                }
                if (inspectAll) rows.push_back({n, z, dz, d});
            });

        std::cout << "\n=== geometry inventory: " << dbPath << " (depth <= " << inspectDepth << ", "
                  << visited << " volumes visited";
        if (useFilter) std::cout << ", filter \"" << inspectMatch << "\"";
        std::cout << ") ===\n"
                  << "  copy a name (or a wildcard over it) into a region's `exclude` to hide "
                     "it\n\n";

        if (inspectAll) {
            std::sort(rows.begin(), rows.end(),
                      [](const Row& a, const Row& b) { return a.z < b.z; });
            std::printf("%-56s %5s %12s %12s %12s\n", "volume", "depth", "z centre", "z min",
                        "z max");
            for (const auto& r : rows) {
                if (r.dz >= 0) {
                    std::printf("%-56s %5d %12.1f %12.1f %12.1f\n", r.name.substr(0, 56).c_str(),
                                r.depth, r.z, r.z - r.dz, r.z + r.dz);
                } else {
                    std::printf("%-56s %5d %12.1f %12s %12s\n", r.name.substr(0, 56).c_str(),
                                r.depth, r.z, "?", "?");
                }
            }
        } else {
            // Aggregated: one row per distinct NAME, which is what you actually
            // need in order to write an `exclude` pattern.
            std::vector<std::pair<std::string, Agg>> list(agg.begin(), agg.end());
            std::sort(list.begin(), list.end(),
                      [](const auto& a, const auto& b) { return a.second.zlo < b.second.zlo; });
            std::printf("%-56s %8s %5s %12s %12s\n", "volume name", "count", "depth", "z min",
                        "z max");
            for (const auto& [name, a] : list) {
                std::printf("%-56s %8zu %5d %12.1f %12.1f\n", name.substr(0, 56).c_str(), a.count,
                            a.minDepth, a.zlo, a.zhi);
            }
            std::cout << "\n  " << list.size() << " distinct volume names ("
                      << "--inspect-all lists every instance)\n";
        }
        if (gHi > gLo) {
            std::cout << "  overall measured z span: [" << gLo << ", " << gHi << "] mm\n";
        }
        std::cout << std::endl;
        return 0;
    }

    shipdisp::RNTupleEventSource source(data, view.ntuple);

    shipdisp::GeoLoadOptions geoOpt;
    geoOpt.include = view.geometry.include;
    geoOpt.exclude = view.geometry.exclude;
    geoOpt.max_depth = view.geometry.max_depth;
    geoOpt.stop_at_match = view.geometry.stop_at_match;
    geoOpt.length_scale = view.hit_scale;
    shipdisp::GeoModelGeometrySource geoSrc(geometry, geoOpt);

    // Deeper walk feeding the zoom regions.
    shipdisp::GeoLoadOptions regionOpt = geoOpt;
    regionOpt.stop_at_match = false;  // descend past envelopes for real detail
    regionOpt.max_depth = view.region_depth;
    // geometry.exclude may hide subsystems the zoom regions need (e.g. the
    // SND); region_exclude lets the two be configured independently.
    if (view.has_region_exclude) regionOpt.exclude = view.region_exclude;
    // Never pull world/cavern shells into a zoom scene: they span the whole
    // detector, add nothing to a close-up, and are costly to render.
    regionOpt.exclude.insert(regionOpt.exclude.end(), {"Cavern", "World", "Rock", "Hall"});
    // With the z window doing the limiting, the emission cap is only a runaway
    // guard -- keep it well above what a single sub-detector needs.
    regionOpt.max_shapes = 50000;

    TApplication app("sea_cucumber", &argc, argv);
    shipdisp::EventDisplay ed(view);
    ed.init();
    ed.setSource(&source);

    // Prefer cached geometry when a valid cache is present: it skips the
    // multi-second GeoModel read entirely (ALICE O2 event-display pattern,
    // arXiv:2503.00088). Falls back to the live .db otherwise.
    const std::string mainCachePath = geoCache.empty() ? "" : geoCache + ".main.root";
    const std::string regionCachePath = geoCache.empty() ? "" : geoCache + ".region.root";
    bool useCache = !geoCache.empty() &&
                    shipdisp::CachedGeometrySource::isValidCache(mainCachePath) &&
                    shipdisp::CachedGeometrySource::isValidCache(regionCachePath);
    if (!geoCache.empty() && !useCache) {
        std::cerr << "[sea_cucumber] geometry cache '" << geoCache
                  << ".{main,region}.root' missing or stale -- falling back to the .db "
                     "(run make_geometry_cache to build it)\n";
    }
    if (useCache) {
        // A cache stores positions already multiplied by the scale it was
        // written at; replaying it under a different --scale/--view would
        // silently mis-scale the whole display, so treat that as stale.
        const double cScale = shipdisp::CachedGeometrySource::cachedScale(mainCachePath);
        if (!(std::abs(cScale - view.hit_scale) <= 1e-9 * view.hit_scale)) {
            std::cerr << "[sea_cucumber] geometry cache '" << geoCache << "' was written at scale "
                      << cScale << " but the current view uses " << view.hit_scale
                      << " -- falling back to the .db (rebuild with make_geometry_cache)\n";
            useCache = false;
        }
    }

    try {
        if (useCache) {
            std::cout << "[sea_cucumber] using geometry cache '" << geoCache << ".*'\n";
            shipdisp::CachedGeometrySource mainCache(mainCachePath);
            ed.loadGeometry(mainCache);
            // Resolve windows from the region cache (no .db touch), then replay
            // it to build. The cache is already depth/exclude-bounded, so no z
            // restriction is needed.
            shipdisp::CachedGeometrySource regionScan(regionCachePath);
            ed.resolveRegionWindowsFromSource(regionScan);
            shipdisp::CachedGeometrySource regionCache(regionCachePath);
            ed.loadRegionGeometry(regionCache);
        } else {
            ed.loadGeometry(geoSrc);

            // Resolve the region windows with a cheap, shape-free scan, then
            // restrict the expensive walk to their union.
            const auto [uLo, uHi] = ed.resolveRegionWindows(geoSrc.resolvedPath(), regionOpt);
            if (uHi > uLo && uLo > -1e29 && uHi < 1e29) {
                const double pad = 0.1 * std::max(1.0, uHi - uLo);
                regionOpt.use_z_window = true;
                regionOpt.z_window_min = uLo - pad;
                regionOpt.z_window_max = uHi + pad;
                std::cout << "[sea_cucumber] region walk restricted to z ["
                          << regionOpt.z_window_min << ", " << regionOpt.z_window_max << "] mm\n";
            }
            shipdisp::GeoModelGeometrySource regionSrc(geometry, regionOpt);
            ed.loadRegionGeometry(regionSrc);
        }
    } catch (const std::exception& e) {
        std::cerr << "[sea_cucumber] geometry load failed: " << e.what() << "\n";
        return 1;
    }
    ed.setupViewers();
    const std::int64_t nEv = ed.numEvents();
    if (nEv > 0) {
        if (event < 0 || event >= nEv) {
            std::cerr << "[sea_cucumber] requested event " << event << " but the file has " << nEv
                      << " -- showing event 0\n";
            event = 0;
        }
        ed.gotoEvent(event);
    } else {
        std::cerr << "[sea_cucumber] no events available in '" << data << "'\n";
    }
    ed.serveLogo(logoDir);
    ed.addNavigator();
    ed.show();
    app.Run();
    return 0;
}

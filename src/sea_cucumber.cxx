// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  sea_cucumber -- the SHiP event display.
//
//  Inputs: --geometry <ship.db>  --data <events.root>  [--view <view.toml>]
//
//  Draws the detector geometry, the event HITS (pink, size ramped by energy),
//  and the TRUTH decay-vertex marker. No fabricated tracks/reco.
//
//  Viewers: a full 3D perspective (main), plus three viewers zoomed onto the
//  detector DOWNSTREAM OF THE DECAY VOLUME -- a 3D zoom, an XZ elevation and a
//  YZ elevation. The zoom is geometry-agnostic: the downstream window is found
//  from the geometry (end of the decay volume -> end of the detector), not a
//  hardcoded z. Those viewers get a dedicated scene holding only that region,
//  so their camera auto-fit frames just it.
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
#include <TGeoMatrix.h>
#include <TGeoShape.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "GeoModelGeometrySource.h"
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
}  // namespace

class EventDisplay {
   public:
    explicit EventDisplay(ViewConfig view) : view_(std::move(view)), scale_(view_.hit_scale) {
        effDownZmin_ = view_.downstream.zmin;
        effDownZmax_ = view_.downstream.zmax;
    }

    void init() {
        eve_ = REX::REveManager::Create();
        geoScene_ = eve_->GetGlobalScene();
        eventScene_ = eve_->GetEventScene();
        geoHolder_ = new REX::REveElement("Detector");
        geoScene_->AddElement(geoHolder_);
        eventHolder_ = new REX::REveElement("Event");
        eventScene_->AddElement(eventHolder_);

        downGeoScene_ = eve_->SpawnNewScene("DownstreamGeo");
        downEventScene_ = eve_->SpawnNewScene("DownstreamEvent");
        downGeoHolder_ = new REX::REveElement("DownstreamDetector");
        downGeoScene_->AddElement(downGeoHolder_);
        downEventHolder_ = new REX::REveElement("DownstreamEvent");
        downEventScene_->AddElement(downEventHolder_);
    }

    void setupViewers() {
        if (auto* def = eve_->GetDefaultViewer()) {
            def->SetCameraType(REX::REveViewer::kCameraPerspXOZ);
            def->SetBlackBackground(true);
        }
        auto makeDown = [&](const char* name, const char* title, REX::REveViewer::ECameraType cam) {
            if (REX::REveViewer* v = eve_->SpawnNewViewer(name, title)) {
                v->RemoveElements();  // drop auto-attached global/event scenes
                v->AddScene(downGeoScene_);
                v->AddScene(downEventScene_);
                v->SetCameraType(cam);
                v->SetBlackBackground(true);
            }
        };
        makeDown("Downstream 3D", "Downstream zoom (3D)", REX::REveViewer::kCameraPerspXOZ);
        makeDown("Downstream XZ", "Downstream XZ elevation", REX::REveViewer::kCameraOrthoXOZ);
        makeDown("Downstream YZ", "Downstream YZ elevation", REX::REveViewer::kCameraOrthoZOY);
    }

    // Main geometry (subsystem envelopes) into the full scene.
    void loadGeometry(IGeometrySource& src) {
        const std::size_t n = src.provide(
            [&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& global, int) {
                auto* es = new REX::REveGeoShape(name.c_str());
                es->SetShape(shape);
                es->RefMainTrans().SetFrom(const_cast<TGeoHMatrix&>(global));
                es->SetMainColor(geoColor(name));
                es->SetMainTransparency(static_cast<Char_t>(view_.transparencyForVolume(name)));
                geoHolder_->AddElement(es);
            });
        std::cout << "[sea_cucumber] geometry: " << n << " shapes\n";
    }

    void setSource(IEventSource* s) { source_ = s; }
    std::int64_t numEvents() const { return source_ ? source_->numEvents() : 0; }

    // Set the downstream zoom window from the event's hits (robust, geometry
    // agnostic: the zoom frames wherever the event activity actually is). Falls
    // back to the geometry-derived window if the event has no hits.
    void setDownstreamWindowFromEvent(std::int64_t i) {
        if (!source_ || !source_->loadEvent(i)) return;
        const auto& hits = source_->hits();
        if (hits.empty()) return;
        double zmin = 1e30, zmax = -1e30;
        for (const auto& h : hits) {
            zmin = std::min(zmin, h.position[2]);
            zmax = std::max(zmax, h.position[2]);
        }
        const double margin = std::max(1000.0, 0.15 * (zmax - zmin));
        effDownZmin_ = zmin - margin;
        effDownZmax_ = zmax + margin;
        downFromHits_ = true;
        std::cout << "[sea_cucumber] ZOOM WINDOW from " << hits.size() << " hits: z ["
                  << effDownZmin_ << ", " << effDownZmax_ << "] mm\n";
    }

    // Deeper walk -> fill the downstream scene with the real detector volumes in
    // the zoom window. If the window wasn't set from hits, derive it here from
    // the geometry (downstream ~45% of the z-span).
    void loadDownstreamGeometry(IGeometrySource& src) {
        struct Rec {
            TGeoShape* shape;
            TGeoHMatrix m;
            std::string name;
            double z;
        };
        std::vector<Rec> recs;
        double zmin = 1e30, zmax = -1e30;
        src.provide([&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& g, int) {
            const double z = g.GetTranslation()[2] / scale_;
            recs.push_back({shape, g, name, z});
            zmin = std::min(zmin, z);
            zmax = std::max(zmax, z);
        });

        if (!downFromHits_) {
            if (view_.downstream.auto_window && zmax > zmin) {
                effDownZmin_ = zmin + 0.55 * (zmax - zmin);
                effDownZmax_ = zmax + 0.03 * std::max(1.0, zmax - zmin);
            } else {
                effDownZmin_ = view_.downstream.zmin;
                effDownZmax_ = view_.downstream.zmax;
            }
        }

        std::size_t added = 0;
        for (auto& r : recs) {
            if (r.z >= effDownZmin_ && r.z <= effDownZmax_) {
                auto* d = new REX::REveGeoShape(r.name.c_str());
                d->SetShape(r.shape);
                d->RefMainTrans().SetFrom(r.m);
                d->SetMainColor(geoColor(r.name));
                d->SetMainTransparency(static_cast<Char_t>(view_.transparencyForVolume(r.name)));
                downGeoHolder_->AddElement(d);
                ++added;
            } else {
                delete r.shape;
            }
        }
        std::cout << "[sea_cucumber] downstream: full geometry z-range [" << zmin << ", " << zmax
                  << "] mm; ZOOM WINDOW [" << effDownZmin_ << ", " << effDownZmax_ << "] mm -> "
                  << added << " geometry shapes\n";
    }

    void gotoEvent(std::int64_t i) {
        if (!source_ || !source_->loadEvent(i)) {
            std::cerr << "[sea_cucumber] no event " << i << "\n";
            return;
        }
        eventHolder_->DestroyElements();
        downEventHolder_->DestroyElements();
        drawHits(source_->hits());
        if (view_.decay.draw) drawDecayVertex(source_->mcParticles());
    }

    void show() {
        if (auto* vl = eve_->GetViewers()) vl->RepaintAllViewers(true, false);
        eve_->Show();
    }

   private:
    float sx(double mm) const { return static_cast<float>(mm * scale_); }

    // Geometry colour: config style if the name matches one; otherwise vary
    // between blue and cream by a stable name hash so the palette is used even
    // on geometries whose volume names we don't recognise (agnostic).
    Color_t geoColor(const std::string& name) const {
        const std::string& c = view_.colorForVolume(name);
        if (c != view_.geometry.default_color) return hexColor(c);
        const std::size_t h = std::hash<std::string>{}(name);
        return hexColor((h % 4 == 0) ? "#F1DEBC" : view_.geometry.default_color);
    }

    void drawHits(const std::vector<SHiP::SimHit>& hits) {
        if (hits.empty()) return;
        double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
        for (const auto& h : hits) {
            lo = std::min(lo, h.energyDeposit);
            hi = std::max(hi, h.energyDeposit);
        }
        const double span = (hi > lo) ? (hi - lo) : 1.0;
        const Color_t pink = hexColor(view_.hits.color_high);  // hits are pink
        const double zlo = effDownZmin_, zhi = effDownZmax_;

        // Bins encode energy via marker SIZE (colour stays pink).
        std::array<REX::REvePointSet*, kBins> full{}, down{};
        for (int b = 0; b < kBins; ++b) {
            const float size =
                view_.hits.marker_size *
                (1.0f + 1.8f * (view_.hits.color_by_energy ? b : 0) / std::max(1, kBins - 1));
            auto mk = [&](const char* tag) {
                auto* ps =
                    new REX::REvePointSet((std::string("hits_") + tag + std::to_string(b)).c_str());
                ps->SetMarkerStyle(view_.hits.marker_style);
                ps->SetMarkerSize(size);
                ps->SetMarkerColor(pink);
                return ps;
            };
            full[b] = mk("f");
            down[b] = mk("d");
        }
        std::size_t downCount = 0;
        for (const auto& h : hits) {
            int b = view_.hits.color_by_energy
                        ? int((h.energyDeposit - lo) / span * (kBins - 1) + 0.5)
                        : kBins - 1;
            b = std::clamp(b, 0, kBins - 1);
            const float x = sx(h.position[0]), y = sx(h.position[1]), z = sx(h.position[2]);
            full[b]->SetNextPoint(x, y, z);
            if (h.position[2] >= zlo && h.position[2] <= zhi) {
                down[b]->SetNextPoint(x, y, z);
                ++downCount;
            }
        }
        for (int b = 0; b < kBins; ++b) {
            eventHolder_->AddElement(full[b]);
            downEventHolder_->AddElement(down[b]);
        }
        std::cout << "[sea_cucumber] hits: " << hits.size() << " total, " << downCount
                  << " downstream (z in [" << zlo << ", " << zhi << "] mm)\n";
    }

    void drawDecayVertex(const std::vector<SHiP::MCParticle>& mc) {
        for (const auto& p : mc) {
            if (p.motherId < 0) continue;
            auto* ps = new REX::REvePointSet("decay_vertex");
            ps->SetMarkerStyle(view_.decay.marker_style);
            ps->SetMarkerSize(view_.decay.marker_size);
            ps->SetMarkerColor(hexColor(view_.decay.color));
            ps->SetNextPoint(sx(p.vertex[0]), sx(p.vertex[1]), sx(p.vertex[2]));
            eventHolder_->AddElement(ps);
            return;
        }
    }

    static constexpr int kBins = 6;
    ViewConfig view_;
    double scale_;
    double effDownZmin_ = 0, effDownZmax_ = 0;
    bool downFromHits_ = false;
    REX::REveManager* eve_ = nullptr;
    REX::REveScene* geoScene_ = nullptr;
    REX::REveScene* eventScene_ = nullptr;
    REX::REveScene* downGeoScene_ = nullptr;
    REX::REveScene* downEventScene_ = nullptr;
    REX::REveElement* geoHolder_ = nullptr;
    REX::REveElement* eventHolder_ = nullptr;
    REX::REveElement* downGeoHolder_ = nullptr;
    REX::REveElement* downEventHolder_ = nullptr;
    IEventSource* source_ = nullptr;
};

}  // namespace shipdisp

namespace {
void usage(const char* a0) {
    std::cerr << "Usage: " << a0
              << " --geometry <ship.db> --data <events.root> [--view <view.toml>]\n"
                 "               [--ntuple <name>] [--event <i>] [--scale <f>]\n";
}
}  // namespace

int main(int argc, char* argv[]) {
    std::string geometry, data, viewFile, ntuple;
    std::int64_t event = 0;
    double scaleOverride = -1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* n) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << n << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--geometry")
            geometry = next("--geometry");
        else if (a == "--data")
            data = next("--data");
        else if (a == "--view")
            viewFile = next("--view");
        else if (a == "--ntuple")
            ntuple = next("--ntuple");
        else if (a == "--event")
            event = std::atoll(next("--event").c_str());
        else if (a == "--scale")
            scaleOverride = std::atof(next("--scale").c_str());
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            usage(argv[0]);
            return 2;
        }
    }
    if (geometry.empty() || data.empty()) {
        usage(argv[0]);
        return 2;
    }

    shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile);
    if (scaleOverride > 0) view.hit_scale = scaleOverride;
    if (!ntuple.empty()) view.ntuple = ntuple;

    shipdisp::RNTupleEventSource source(data, view.ntuple);

    shipdisp::GeoLoadOptions geoOpt;
    geoOpt.include = view.geometry.include;
    geoOpt.exclude = view.geometry.exclude;
    geoOpt.max_depth = view.geometry.max_depth;
    geoOpt.stop_at_match = view.geometry.stop_at_match;
    geoOpt.length_scale = view.hit_scale;
    shipdisp::GeoModelGeometrySource geoSrc(geometry, geoOpt);

    shipdisp::GeoLoadOptions downOpt = geoOpt;
    downOpt.stop_at_match = false;
    downOpt.max_depth = 5;
    if (downOpt.exclude.empty()) downOpt.exclude = {"Straw", "Tile"};
    shipdisp::GeoModelGeometrySource downSrc(geometry, downOpt);

    TApplication app("sea_cucumber", &argc, argv);
    shipdisp::EventDisplay ed(view);
    ed.init();
    ed.setSource(&source);
    // Set the zoom window from the event's hits first, so the downstream
    // geometry walk fills exactly that region.
    ed.setDownstreamWindowFromEvent(event);
    try {
        ed.loadGeometry(geoSrc);
        ed.loadDownstreamGeometry(downSrc);
    } catch (const std::exception& e) {
        std::cerr << "[sea_cucumber] geometry load failed: " << e.what() << "\n";
        return 1;
    }
    ed.setupViewers();
    if (ed.numEvents() > 0) ed.gotoEvent(event);
    ed.show();
    app.Run();
    return 0;
}

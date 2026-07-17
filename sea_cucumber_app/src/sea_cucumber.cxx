// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  sea_cucumber -- the SHiP event display.
//
//  A geometry- and data-model-agnostic REve web display. It takes three
//  inputs and nothing else:
//      --geometry  a GeoModel .db      (arbitrary detector)
//      --data      an RNTuple .root    (official SHiP event data model)
//      --view      a TOML view config  (optional styling; sensible defaults)
//
//  The core knows only about visual primitives + the view config; the EDM is
//  read behind IEventSource (RNTupleEventSource) and the geometry behind
//  IGeometrySource (GeoModelGeometrySource), mirroring aegir's provider split.
//  Everything is in one uniform scene scale (mm * view.hit_scale): geometry and
//  hits share it, so detector and event overlays register exactly.
// =============================================================================

#include <ROOT/REveElement.hxx>
#include <ROOT/REveGeoShape.hxx>
#include <ROOT/REveManager.hxx>
#include <ROOT/REvePointSet.hxx>
#include <ROOT/REveScene.hxx>
#include <ROOT/REveStraightLineSet.hxx>
#include <ROOT/REveViewer.hxx>

#include <TApplication.h>
#include <TGeoMatrix.h>
#include <TGeoShape.h>
#include <Rtypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "GeoModelGeometrySource.h"
#include "IEventSource.h"
#include "IGeometrySource.h"
#include "RNTupleEventSource.h"
#include "ViewConfig.h"

namespace REX = ROOT::Experimental;

namespace shipdisp {

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
    }

    // Three viewers sharing both scenes, differing only in camera -- the
    // fixed-target layout (perspective + two orthographic elevations).
    void setupViewers() {
        auto makeViewer = [&](const char* name, const char* title,
                              REX::REveViewer::ECameraType cam) {
            if (REX::REveViewer* v = eve_->SpawnNewViewer(name, title)) {
                v->AddScene(geoScene_);
                v->AddScene(eventScene_);
                v->SetCameraType(cam);
                v->SetBlackBackground(true);
            }
        };
        makeViewer("3D view", "Perspective 3D view", REX::REveViewer::kCameraPerspXOZ);
        makeViewer("Side XZ", "Orthographic side elevation", REX::REveViewer::kCameraOrthoXOZ);
        makeViewer("Beam YZ", "Orthographic view along the beam", REX::REveViewer::kCameraOrthoZOY);
        if (auto* def = eve_->GetDefaultViewer()) {
            def->SetCameraType(REX::REveViewer::kCameraPerspXOZ);
            def->SetBlackBackground(true);
        }
    }

    // Pull shapes from any geometry provider; colour by the view config.
    void loadGeometry(IGeometrySource& src) {
        const std::size_t n = src.provide(
            [&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& global, int) {
                auto* es = new REX::REveGeoShape(name.c_str());
                es->SetShape(shape);  // takes ownership; shape already in scene units
                es->RefMainTrans().SetFrom(const_cast<TGeoHMatrix&>(global));
                es->SetMainColor(static_cast<Color_t>(view_.colorForVolume(name)));
                es->SetMainTransparency(
                    static_cast<Char_t>(view_.transparencyForVolume(name)));
                geoHolder_->AddElement(es);
            });
        std::cout << "[sea_cucumber] geometry: " << n << " shapes\n";
    }

    void setSource(IEventSource* s) { source_ = s; }
    std::int64_t numEvents() const { return source_ ? source_->numEvents() : 0; }

    void gotoEvent(std::int64_t i) {
        if (!source_ || !source_->loadEvent(i)) {
            std::cerr << "[sea_cucumber] no event " << i << "\n";
            return;
        }
        eventHolder_->DestroyElements();
        drawHits(source_->hits());
        if (view_.particles.draw_mc) drawMC(source_->mcParticles());
        if (view_.particles.draw_sim) drawSim(source_->simParticles());
        if (view_.particles.draw_rec) drawRec(source_->recParticles());
        std::cout << "[sea_cucumber] event " << i << ": " << source_->hits().size()
                  << " hits, " << source_->mcParticles().size() << " mc, "
                  << source_->simParticles().size() << " sim, "
                  << source_->recParticles().size() << " rec\n";
    }

    void show() {
        if (auto* vl = eve_->GetViewers()) vl->RepaintAllViewers(true, false);
        eve_->Show();
    }

   private:
    float sx(double mm_x) const { return static_cast<float>(mm_x * scale_); }

    void drawHits(const std::vector<SHiP::SimHit>& hits) {
        if (hits.empty()) return;
        if (view_.hits.color_by_energy)
            drawHitsByEnergy(hits);
        else
            drawHitsByDetector(hits);
    }

    // Energy heat-map: bin deposits across a small red->violet palette.
    void drawHitsByEnergy(const std::vector<SHiP::SimHit>& hits) {
        static const std::array<Color_t, 5> kPal = {kViolet, kAzure, kGreen, kOrange, kRed};
        double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
        for (const auto& h : hits) {
            lo = std::min(lo, h.energyDeposit);
            hi = std::max(hi, h.energyDeposit);
        }
        const double span = (hi > lo) ? (hi - lo) : 1.0;

        std::array<REX::REvePointSet*, 5> bins{};
        for (std::size_t b = 0; b < kPal.size(); ++b) {
            bins[b] = new REX::REvePointSet(("hits_E" + std::to_string(b)).c_str());
            bins[b]->SetMarkerStyle(view_.hits.marker_style);
            bins[b]->SetMarkerSize(view_.hits.marker_size);
            bins[b]->SetMarkerColor(kPal[b]);
        }
        for (const auto& h : hits) {
            auto b = static_cast<std::size_t>((h.energyDeposit - lo) / span * (kPal.size() - 1) + 0.5);
            b = std::min(b, kPal.size() - 1);
            bins[b]->SetNextPoint(sx(h.position[0]), sx(h.position[1]), sx(h.position[2]));
        }
        for (auto* ps : bins) eventHolder_->AddElement(ps);
    }

    // Categorical: one colour per distinct detectorId.
    void drawHitsByDetector(const std::vector<SHiP::SimHit>& hits) {
        static const std::array<Color_t, 8> kPal = {kAzure,  kPink,   kOrange, kGreen,
                                                     kViolet, kCyan,   kSpring, kRed};
        std::map<std::int32_t, REX::REvePointSet*> byDet;
        int next = 0;
        for (const auto& h : hits) {
            auto it = byDet.find(h.detectorId);
            if (it == byDet.end()) {
                auto* ps = new REX::REvePointSet(("hits_det" + std::to_string(h.detectorId)).c_str());
                ps->SetMarkerStyle(view_.hits.marker_style);
                ps->SetMarkerSize(view_.hits.marker_size);
                ps->SetMarkerColor(kPal[next++ % kPal.size()]);
                eventHolder_->AddElement(ps);
                it = byDet.emplace(h.detectorId, ps).first;
            }
            it->second->SetNextPoint(sx(h.position[0]), sx(h.position[1]), sx(h.position[2]));
        }
    }

    // Momentum-direction stub from a vertex (MC / Rec).
    template <typename P>
    void drawDirections(const std::vector<P>& parts, const char* name, Color_t col) {
        if (parts.empty()) return;
        auto* ls = new REX::REveStraightLineSet(name);
        ls->SetLineColor(col);
        ls->SetLineWidth(2);
        const float L = view_.particles.direction_length_mm;
        for (const auto& p : parts) {
            const double pm =
                std::sqrt(p.momentum[0] * p.momentum[0] + p.momentum[1] * p.momentum[1] +
                          p.momentum[2] * p.momentum[2]);
            if (pm <= 0) continue;
            const double ux = p.momentum[0] / pm, uy = p.momentum[1] / pm, uz = p.momentum[2] / pm;
            ls->AddLine(sx(p.vertex[0]), sx(p.vertex[1]), sx(p.vertex[2]),
                        sx(p.vertex[0] + ux * L), sx(p.vertex[1] + uy * L),
                        sx(p.vertex[2] + uz * L));
        }
        eventHolder_->AddElement(ls);
    }

    void drawMC(const std::vector<SHiP::MCParticle>& v) { drawDirections(v, "mc", kYellow); }
    void drawRec(const std::vector<SHiP::RecParticle>& v) { drawDirections(v, "rec", kWhite); }

    // SimParticle: vertex -> endpoint (a real trajectory segment).
    void drawSim(const std::vector<SHiP::SimParticle>& v) {
        if (v.empty()) return;
        auto* ls = new REX::REveStraightLineSet("sim");
        ls->SetLineColor(kGray + 1);
        ls->SetLineWidth(1);
        for (const auto& p : v)
            ls->AddLine(sx(p.vertex[0]), sx(p.vertex[1]), sx(p.vertex[2]), sx(p.endpoint[0]),
                        sx(p.endpoint[1]), sx(p.endpoint[2]));
        eventHolder_->AddElement(ls);
    }

    ViewConfig view_;
    double scale_;
    REX::REveManager* eve_ = nullptr;
    REX::REveScene* geoScene_ = nullptr;
    REX::REveScene* eventScene_ = nullptr;
    REX::REveElement* geoHolder_ = nullptr;
    REX::REveElement* eventHolder_ = nullptr;
    IEventSource* source_ = nullptr;
};

}  // namespace shipdisp

// =============================================================================
//  CLI
// =============================================================================
namespace {
void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
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
        if (a == "--geometry") geometry = next("--geometry");
        else if (a == "--data") data = next("--data");
        else if (a == "--view") viewFile = next("--view");
        else if (a == "--ntuple") ntuple = next("--ntuple");
        else if (a == "--event") event = std::atoll(next("--event").c_str());
        else if (a == "--scale") scaleOverride = std::atof(next("--scale").c_str());
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { std::cerr << "unknown argument: " << a << "\n"; usage(argv[0]); return 2; }
    }
    if (geometry.empty() || data.empty()) {
        usage(argv[0]);
        return 2;
    }

    shipdisp::ViewConfig view = shipdisp::LoadViewConfig(viewFile);
    if (scaleOverride > 0) view.hit_scale = scaleOverride;
    if (!ntuple.empty()) view.ntuple = ntuple;

    // Event source (official RNTuple EDM).
    shipdisp::RNTupleEventSource source(data, view.ntuple);

    // Geometry provider (GeoModel .db, aegir-style resolution + service seam).
    shipdisp::GeoLoadOptions geoOpt;
    geoOpt.include = view.geometry.include;
    geoOpt.exclude = view.geometry.exclude;
    geoOpt.max_depth = view.geometry.max_depth;
    geoOpt.stop_at_match = view.geometry.stop_at_match;
    geoOpt.length_scale = view.hit_scale;  // emit geometry directly in scene units
    shipdisp::GeoModelGeometrySource geoSrc(geometry.empty() ? view.geometry.db_file : geometry,
                                            geoOpt);

    // REve needs a TApplication event loop.
    TApplication app("sea_cucumber", &argc, argv);

    shipdisp::EventDisplay ed(view);
    ed.init();
    try {
        ed.loadGeometry(geoSrc);
    } catch (const std::exception& e) {
        std::cerr << "[sea_cucumber] geometry load failed: " << e.what() << "\n";
        return 1;
    }
    ed.setupViewers();
    ed.setSource(&source);
    if (ed.numEvents() > 0) ed.gotoEvent(event);
    ed.show();
    app.Run();
    return 0;
}

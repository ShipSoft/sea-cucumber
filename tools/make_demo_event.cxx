// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  make_demo_event -- generate a demonstration event in the official SHiP data
//  model, for showing off sea_cucumber without a full simulation.
//
//  What it writes (deliberately minimal -- no fabricated reconstruction):
//    * simHits: hits in the DOWNSTREAM sub-detector section only (tracker /
//      timing / calorimeter), placed on the real detector volumes found in the
//      geometry. A neutral particle decays in the decay volume into two charged
//      daughters whose (undrawn) trajectories seed the downstream hits, with a
//      calorimeter shower cluster per daughter.
//    * mcParticles: the mother and its two daughters, so the display can mark
//      the TRUTH decay vertex. No tracks/vertices are fabricated beyond this.
//    * simResult: bundles the hits.
//
//  GEOMETRY-AWARE: with --geometry <ship.db> it walks the real geometry
//  (reusing GeoModelLoader) so hits land on the detectors you load. The
//  "downstream section" is taken as roughly the last 15 m in z; override with
//  --down-min <mm>. Falls back to an approximate SHiP layout with no geometry.
//
//  Units: mm, GeV/c, GeV, ns.
//  Usage: make_demo_event [--geometry ship.db] [--output demo_events.root]
//                         [--events N] [--seed S] [--down-min <mm>]
// =============================================================================

#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleWriter.hxx>

#include <TGeoMatrix.h>
#include <TGeoShape.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "GeoModelLoader.h"
#include "SHiP/MCParticle.hpp"
#include "SHiP/SimHit.hpp"
#include "SHiP/SimResult.hpp"

namespace {

using Vec3 = std::array<double, 3>;

enum class Sub { Target, Muon, Tagger, Decay, SBT, Tracker, Magnet, Timing, ECAL, HCAL, Other };

Sub classify(const std::string& n) {
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (has("Straw") || has("Tracker")) return Sub::Tracker;
    if (has("ECAL") || has("Ecal")) return Sub::ECAL;
    if (has("HCAL") || has("Hcal")) return Sub::HCAL;
    if (has("Calo")) return Sub::ECAL;
    if (has("SBT") || (has("Tagger") && has("Background"))) return Sub::SBT;
    if (has("Timing")) return Sub::Timing;
    if (has("UpstreamTagger") || has("UBT")) return Sub::Tagger;
    if (has("DecayVolume") || has("Vessel") || has("Decay")) return Sub::Decay;
    if (has("Magnet")) return Sub::Magnet;
    if (has("MuonShield") || has("Muon")) return Sub::Muon;
    if (has("Target")) return Sub::Target;
    return Sub::Other;
}

struct GeoInfo {
    std::map<Sub, std::vector<Vec3>> centres;
    double zmax = -1e30, zmin = 1e30;

    bool has(Sub s) const {
        auto it = centres.find(s);
        return it != centres.end() && !it->second.empty();
    }
    double meanZ(Sub s, double fb) const {
        if (!has(s)) return fb;
        double sum = 0;
        for (const auto& c : centres.at(s)) sum += c[2];
        return sum / centres.at(s).size();
    }
    std::pair<double, double> zRange(Sub s, double lo, double hi) const {
        if (!has(s)) return {lo, hi};
        double a = 1e30, b = -1e30;
        for (const auto& c : centres.at(s)) { a = std::min(a, c[2]); b = std::max(b, c[2]); }
        return {a, b};
    }
};

GeoInfo loadGeometry(const std::string& db) {
    GeoInfo g;
    if (db.empty()) return g;
    shipdisp::GeoLoadOptions opt;
    opt.stop_at_match = false;
    opt.max_depth = 6;
    opt.length_scale = 1.0;
    opt.verbose = false;
    try {
        shipdisp::LoadGeoModelDB(
            db, opt, [&](const std::string& name, TGeoShape* shape, const TGeoHMatrix& m, int) {
                const Double_t* t = m.GetTranslation();
                g.centres[classify(name)].push_back({t[0], t[1], t[2]});
                g.zmax = std::max(g.zmax, t[2]);
                g.zmin = std::min(g.zmin, t[2]);
                delete shape;
            });
    } catch (const std::exception& e) {
        std::cerr << "[make_demo_event] geometry walk failed (" << e.what()
                  << ") -- using fallback layout\n";
        g = GeoInfo{};
    }
    return g;
}

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double norm(const Vec3& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
Vec3 atZ(const Vec3& v, const Vec3& d, double z) {
    const double s = (z - v[2]) / d[2];
    return {v[0] + d[0] * s, v[1] + d[1] * s, z};
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string geometry, output = "demo_events.root";
    int nEvents = 1;
    unsigned seed = 12345;
    double downMinArg = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nxt = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--geometry") geometry = nxt();
        else if (a == "--output") output = nxt();
        else if (a == "--events") nEvents = std::atoi(nxt());
        else if (a == "--seed") seed = std::atoi(nxt());
        else if (a == "--down-min") downMinArg = std::atof(nxt());
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: make_demo_event [--geometry ship.db] [--output f.root] "
                         "[--events N] [--seed S] [--down-min mm]\n";
            return 0;
        }
    }

    const GeoInfo geo = loadGeometry(geometry);
    std::mt19937 rng(seed);
    auto uni = [&](double a, double b) { return std::uniform_real_distribution<>(a, b)(rng); };
    auto gaus = [&](double m, double s) { return std::normal_distribution<>(m, s)(rng); };

    // Layout anchors from real geometry, else approximate SHiP z's (mm).
    const auto [decLo, decHi] = geo.zRange(Sub::Decay, 30000, 60000);
    const double zTarget = geo.meanZ(Sub::Target, 0.0);
    const double zTiming = geo.meanZ(Sub::Timing, 88000);
    const double zEcal = geo.meanZ(Sub::ECAL, 90000);
    const double zHcal = geo.meanZ(Sub::HCAL, 93000);
    // Downstream cut: everything past the decay volume (geometry-agnostic), or
    // the user override, or a sensible fallback.
    const double geomMax = (geo.zmax > -1e29) ? geo.zmax : 95000.0;
    double decayEnd = -1e30;
    if (geo.has(Sub::Decay))
        for (const auto& c : geo.centres.at(Sub::Decay)) decayEnd = std::max(decayEnd, c[2]);
    const double downMin = (downMinArg > 0) ? downMinArg
                           : (decayEnd > -1e29) ? decayEnd
                                                : geomMax - 15000.0;

    // Downstream tracker-station z's (real straw stations if present).
    std::vector<double> trackerZ;
    if (geo.has(Sub::Tracker))
        for (const auto& c : geo.centres.at(Sub::Tracker))
            if (c[2] >= downMin) trackerZ.push_back(c[2]);
    std::sort(trackerZ.begin(), trackerZ.end());
    trackerZ.erase(std::unique(trackerZ.begin(), trackerZ.end(),
                               [](double x, double y) { return std::abs(x - y) < 50; }),
                   trackerZ.end());
    if (trackerZ.size() < 4) {
        trackerZ.clear();
        const double a = std::max(downMin, 82000.0);
        for (int k = 0; k < 4; ++k) trackerZ.push_back(a + (zTiming - a) * k / 4.0);
    }

    auto model = ROOT::RNTupleModel::Create();
    auto mcP = model->MakeField<std::vector<SHiP::MCParticle>>("mcParticles");
    auto simH = model->MakeField<std::vector<SHiP::SimHit>>("simHits");
    auto simR = model->MakeField<SHiP::SimResult>("simResult");
    auto writer = ROOT::RNTupleWriter::Recreate(std::move(model), "events", output);

    for (int ev = 0; ev < nEvents; ++ev) {
        mcP->clear(); simH->clear(); simR->hits.clear(); simR->particles.clear();

        // Truth decay vertex, upstream in the decay volume.
        const Vec3 vtx = {gaus(0, 120), gaus(0, 120),
                          uni(decLo + 0.4 * (decHi - decLo), decHi - 2000)};

        // Mother (neutral, from target) -> two charged daughters.
        const double Pm = uni(30, 80);
        const Vec3 pMother = {uni(-0.2, 0.2), uni(-0.2, 0.2), Pm};
        {
            SHiP::MCParticle m;
            m.pdgCode = 9900015;  // HNL-like placeholder
            m.vertex = {0, 0, zTarget};
            m.momentum = pMother;
            m.energy = std::sqrt(Pm * Pm + 1.0);
            m.motherId = -1;
            m.status = 2;
            mcP->push_back(m);
        }
        const double theta = uni(0.03, 0.08);
        const double phi = uni(0, 2 * M_PI);
        const Vec3 kick = {std::cos(phi) * theta * Pm, std::sin(phi) * theta * Pm, 0};
        std::array<Vec3, 2> pDau = {add(scale(pMother, 0.5), kick),
                                    add(scale(pMother, 0.5), scale(kick, -1))};
        const std::array<int, 2> pdg = {13, -13};

        for (int d = 0; d < 2; ++d) {
            const Vec3& p = pDau[d];
            const Vec3 dir = scale(p, 1.0 / p[2]);
            {
                SHiP::MCParticle mc;
                mc.pdgCode = pdg[d];
                mc.vertex = vtx;  // <-- the truth decay vertex the display marks
                mc.momentum = p;
                mc.energy = std::sqrt(norm(p) * norm(p) + 0.105 * 0.105);
                mc.motherId = 0;
                mc.status = 1;
                mcP->push_back(mc);
            }
            auto hit = [&](const Vec3& x, std::int32_t id, double edep) {
                SHiP::SimHit h;
                h.detectorId = id;
                h.trackId = d + 1;
                h.pdgCode = pdg[d];
                h.position = x;
                h.momentum = p;
                h.energyDeposit = edep;
                h.time = norm(add(x, scale(vtx, -1))) / 299.792458;
                h.pathLength = 10;
                simH->push_back(h);
            };
            // Downstream tracker stations + timing.
            int plane = 0;
            for (double z : trackerZ)
                if (z > vtx[2]) hit(atZ(vtx, dir, z), 1000 + 100 * d + plane++, uni(0.001, 0.004));
            if (zTiming >= downMin) hit(atZ(vtx, dir, zTiming), 2000 + d, uni(0.002, 0.005));
            // ECAL shower cluster.
            const Vec3 e0 = atZ(vtx, dir, zEcal);
            for (int s = 0; s < 25; ++s)
                hit({e0[0] + gaus(0, 120), e0[1] + gaus(0, 120), zEcal + gaus(0, 120)},
                    3000 + 100 * d + s, std::abs(gaus(0.15, 0.12)) + 0.01);
            // HCAL deposits.
            const Vec3 h0 = atZ(vtx, dir, zHcal);
            for (int s = 0; s < 8; ++s)
                hit({h0[0] + gaus(0, 200), h0[1] + gaus(0, 200), zHcal + gaus(0, 200)},
                    4000 + 100 * d + s, std::abs(gaus(0.3, 0.2)) + 0.02);
        }

        simR->hits = *simH;  // bundle
        writer->Fill();
        std::cout << "[make_demo_event] event " << ev << ": " << simH->size()
                  << " downstream hits (z>=" << downMin << " mm), decay vertex z=" << vtx[2]
                  << " mm\n";
    }
    std::cout << "[make_demo_event] wrote '" << output << "' (" << nEvents << " event(s))\n";
    return 0;
}

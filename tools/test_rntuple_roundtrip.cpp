// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  Writes a small "events" RNTuple with the official EDM fields, then reads it
//  back through RNTupleEventSource and checks the reader sees the right events,
//  fields, and the simResult fallback. Exercises the real reader end-to-end.
// =============================================================================

#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleWriter.hxx>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "RNTupleEventSource.h"
#include "SHiP/SimHit.hpp"
#include "SHiP/SimParticle.hpp"
#include "SHiP/SimResult.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
}  // namespace

int main() {
    const std::string path = "test_events_roundtrip.root";

    // --- write two events ----------------------------------------------------
    {
        auto model = ROOT::RNTupleModel::Create();
        auto hits = model->MakeField<std::vector<SHiP::SimHit>>("simHits");
        auto result = model->MakeField<SHiP::SimResult>("simResult");

        auto writer = ROOT::RNTupleWriter::Recreate(std::move(model), "events", path);

        // event 0: one flat hit
        hits->clear();
        {
            SHiP::SimHit h;
            h.detectorId = 7;
            h.position = {1.0, 2.0, 3.0};
            h.energyDeposit = 0.5;
            hits->push_back(h);
        }
        result->hits.clear();
        result->particles.clear();
        writer->Fill();

        // event 1: two flat hits, and a SimResult carrying one particle
        hits->clear();
        for (int k = 0; k < 2; ++k) {
            SHiP::SimHit h;
            h.detectorId = 100 + k;
            h.position = {double(k), 0.0, 10.0};
            h.energyDeposit = 0.1 * (k + 1);
            hits->push_back(h);
        }
        result->hits.clear();
        result->particles.clear();
        {
            SHiP::SimParticle p;
            p.pdgCode = 13;
            p.vertex = {0, 0, 0};
            p.endpoint = {0, 0, 100};
            result->particles.push_back(p);
        }
        writer->Fill();
    }  // writer flushes and closes on destruction

    // --- read back -----------------------------------------------------------
    shipdisp::RNTupleEventSource src(path, "events");
    check(src.numEvents() == 2, "numEvents == 2");

    check(src.loadEvent(0), "loadEvent(0)");
    check(src.hits().size() == 1, "event 0 has 1 hit");
    if (!src.hits().empty()) {
        check(src.hits()[0].detectorId == 7, "event 0 hit detectorId == 7");
        check(src.hits()[0].position[2] == 3.0, "event 0 hit z == 3");
    }

    check(src.loadEvent(1), "loadEvent(1)");
    check(src.hits().size() == 2, "event 1 has 2 hits");
    // simParticles field absent -> fall back to simResult.particles
    check(src.simParticles().size() == 1, "event 1 simResult fallback: 1 particle");

    check(!src.loadEvent(2), "loadEvent(2) is out of range");

    std::remove(path.c_str());

    if (failures == 0) std::cout << "test_rntuple_roundtrip: OK\n";
    return failures == 0 ? 0 : 1;
}

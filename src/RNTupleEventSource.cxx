// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  RNTupleEventSource.cxx -- see header.
//
//  The bind pattern mirrors the data-model repo's own RNTuple round-trip test
//  (tests/test_rntuple_io.cpp): open the reader, take the default entry, get a
//  shared_ptr<T> per field, then LoadEntry(i) refills those pointees. That is
//  the pattern the data-model authors validate against their ROOT version, so
//  it is guaranteed to match.
// =============================================================================

#include "RNTupleEventSource.h"

#include <ROOT/REntry.hxx>
#include <ROOT/RNTupleReader.hxx>

#include <TFile.h>
#include <TKey.h>
#include <TList.h>

#include <iostream>
#include <memory>

#include "SHiP/SimResult.hpp"

namespace shipdisp {

namespace {
// Shared empties returned when a field is absent, so accessors can hand back a
// const reference without allocating.
const std::vector<SHiP::SimHit> kNoHits;
const std::vector<SHiP::SimParticle> kNoParticles;
const std::vector<SHiP::MCParticle> kNoMC;
const std::vector<SHiP::RecParticle> kNoRec;

// Bind one top-level field if present; returns nullptr (and prints an info
// line) if the field is missing or its type doesn't match. RNTuple's default
// entry only exposes fields that exist on disk, so GetPtr throws for an absent
// or mistyped field -- we treat that as "not present" rather than fatal.
template <typename T>
std::shared_ptr<T> tryBind(const ROOT::REntry& entry, const char* name) {
    try {
        return entry.GetPtr<T>(name);
    } catch (const std::exception&) {
        std::cout << "[RNTupleEventSource] field '" << name << "' not present -- skipping\n";
        return nullptr;
    }
}
}  // namespace

struct RNTupleEventSource::Impl {
    std::unique_ptr<ROOT::RNTupleReader> reader;

    // Bound pointees (any may be null if the field is absent).
    std::shared_ptr<std::vector<SHiP::MCParticle>> mc;
    std::shared_ptr<std::vector<SHiP::SimHit>> hits;
    std::shared_ptr<std::vector<SHiP::SimParticle>> parts;
    std::shared_ptr<std::vector<SHiP::RecParticle>> rec;
    std::shared_ptr<SHiP::SimResult> result;

    // Per-event "effective" views: flat field if bound, else sim_result's
    // bundle, else the shared empty. The fallback keys on the field being
    // ABSENT from the file (bind failed), never on it being empty for this
    // event -- an event with legitimately zero flat hits must not silently
    // switch to the sim_result bundle.
    const std::vector<SHiP::SimHit>* effHits = &kNoHits;
    const std::vector<SHiP::SimParticle>* effParts = &kNoParticles;

    void refresh() {
        effHits = hits ? hits.get() : (result ? &result->hits : &kNoHits);
        effParts = parts ? parts.get() : (result ? &result->particles : &kNoParticles);
    }
};

RNTupleEventSource::RNTupleEventSource(const std::string& path, const std::string& ntupleName)
    : p_(std::make_unique<Impl>()) {
    try {
        p_->reader = ROOT::RNTupleReader::Open(ntupleName, path);
    } catch (const std::exception& e) {
        std::cerr << "[RNTupleEventSource] cannot open RNTuple '" << ntupleName << "' in '" << path
                  << "': " << e.what() << "\n";
        // List the file's objects so the user can pick the right --ntuple.
        if (std::unique_ptr<TFile> f{TFile::Open(path.c_str())}; f && !f->IsZombie()) {
            std::cerr << "[RNTupleEventSource] objects found in '" << path << "':\n";
            if (auto* keys = f->GetListOfKeys())
                for (auto* o : *keys) {
                    auto* k = static_cast<TKey*>(o);
                    std::cerr << "      " << k->GetName() << "  (" << k->GetClassName() << ")\n";
                }
            std::cerr
                << "[RNTupleEventSource] re-run with --ntuple <name> for the RNTuple you want\n";
        }
        return;
    }
    if (!p_->reader) {
        std::cerr << "[RNTupleEventSource] null reader for '" << path << "'\n";
        return;
    }

    const auto& entry = p_->reader->GetModel().GetDefaultEntry();
    p_->mc = tryBind<std::vector<SHiP::MCParticle>>(entry, "mc_particles");
    p_->hits = tryBind<std::vector<SHiP::SimHit>>(entry, "sim_hits");
    p_->parts = tryBind<std::vector<SHiP::SimParticle>>(entry, "sim_particles");
    p_->rec = tryBind<std::vector<SHiP::RecParticle>>(entry, "rec_particles");
    p_->result = tryBind<SHiP::SimResult>(entry, "sim_result");

    std::cout << "[RNTupleEventSource] '" << path << "' ntuple '" << ntupleName
              << "': " << p_->reader->GetNEntries() << " events\n";
}

RNTupleEventSource::~RNTupleEventSource() = default;

std::int64_t RNTupleEventSource::numEvents() const {
    return p_->reader ? static_cast<std::int64_t>(p_->reader->GetNEntries()) : 0;
}

bool RNTupleEventSource::loadEvent(std::int64_t i) {
    if (!p_->reader) return false;
    if (i < 0 || i >= static_cast<std::int64_t>(p_->reader->GetNEntries())) return false;
    p_->reader->LoadEntry(static_cast<std::uint64_t>(i));
    p_->refresh();
    return true;
}

const std::vector<SHiP::SimHit>& RNTupleEventSource::hits() const { return *p_->effHits; }
const std::vector<SHiP::SimParticle>& RNTupleEventSource::simParticles() const {
    return *p_->effParts;
}
const std::vector<SHiP::MCParticle>& RNTupleEventSource::mcParticles() const {
    return p_->mc ? *p_->mc : kNoMC;
}
const std::vector<SHiP::RecParticle>& RNTupleEventSource::recParticles() const {
    return p_->rec ? *p_->rec : kNoRec;
}

}  // namespace shipdisp

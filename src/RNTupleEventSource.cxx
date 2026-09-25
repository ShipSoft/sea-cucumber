// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
// =============================================================================
//  RNTupleEventSource.cxx -- see header.
//
//  Each collection the display uses is read through its own RNTupleView,
//  bound to a shared_ptr the accessors hand out. A view reconstructs only its
//  own field, so the reader never touches the rest of the RNTuple: fields the
//  display does not know about (e.g. aegir's `event_header`, or anything the
//  data model grows in future) are simply never materialised. Going through
//  GetModel()/LoadEntry() instead would rebuild *every* on-disk field and
//  abort on the first type without a dictionary in this build (issue #35).
// =============================================================================

#include "RNTupleEventSource.h"

#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleView.hxx>

#include <TFile.h>
#include <TKey.h>
#include <TList.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

#include "SHiP/SimResult.hpp"

namespace shipdisp {

namespace {
// Shared empties returned when a field is absent, so accessors can hand back a
// const reference without allocating.
const std::vector<SHiP::SimHit> kNoHits;
const std::vector<SHiP::SimParticle> kNoParticles;
const std::vector<SHiP::MCParticle> kNoMC;
const std::vector<SHiP::RecParticle> kNoRec;

/// One top-level field read through its own view. `value` is null when the
/// field is absent or has an incompatible type; load() is then a no-op.
template <typename T>
struct BoundField {
    std::shared_ptr<T> value;
    std::optional<ROOT::RNTupleView<T>> view;

    void load(ROOT::NTupleSize_t i) {
        if (view) (*view)(i);
    }
};

/// Bind field @p name as type T, or return an empty BoundField (with an info
/// line) if it is missing or mistyped -- never fatal.
template <typename T>
BoundField<T> bindField(ROOT::RNTupleReader& reader, std::string_view name) {
    if (reader.GetDescriptor().FindFieldId(name) == ROOT::kInvalidDescriptorId) {
        std::cout << "[RNTupleEventSource] field '" << name << "' not present -- skipping\n";
        return {};
    }
    try {
        auto value = std::make_shared<T>();
        auto view = reader.GetView<T>(name, value);
        return {std::move(value), std::move(view)};
    } catch (const std::exception& e) {
        std::cerr << "[RNTupleEventSource] field '" << name
                  << "' has an incompatible type -- skipping: " << e.what() << "\n";
        return {};
    }
}
}  // namespace

struct RNTupleEventSource::Impl {
    // Declared first so it is destroyed last: the views below read through it.
    std::unique_ptr<ROOT::RNTupleReader> reader;

    BoundField<std::vector<SHiP::MCParticle>> mc;
    BoundField<std::vector<SHiP::SimHit>> hits;
    BoundField<std::vector<SHiP::SimParticle>> parts;
    BoundField<std::vector<SHiP::RecParticle>> rec;
    BoundField<SHiP::SimResult> result;

    // Per-event "effective" views: flat field if available, else simResult's
    // bundle, else the shared empty.
    const std::vector<SHiP::SimHit>* effHits = &kNoHits;
    const std::vector<SHiP::SimParticle>* effParts = &kNoParticles;

    void load(ROOT::NTupleSize_t i) {
        mc.load(i);
        hits.load(i);
        parts.load(i);
        rec.load(i);
        result.load(i);

        const auto& h = hits.value;
        const auto& p = parts.value;
        const auto& r = result.value;
        effHits = (h && !h->empty()) ? h.get() : r ? &r->hits : &kNoHits;
        effParts = (p && !p->empty()) ? p.get() : r ? &r->particles : &kNoParticles;
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

    auto& reader = *p_->reader;
    p_->mc = bindField<std::vector<SHiP::MCParticle>>(reader, "mc_particles");
    p_->hits = bindField<std::vector<SHiP::SimHit>>(reader, "sim_hits");
    p_->parts = bindField<std::vector<SHiP::SimParticle>>(reader, "sim_particles");
    p_->rec = bindField<std::vector<SHiP::RecParticle>>(reader, "rec_particles");
    p_->result = bindField<SHiP::SimResult>(reader, "sim_result");

    std::cout << "[RNTupleEventSource] '" << path << "' ntuple '" << ntupleName
              << "': " << reader.GetNEntries() << " events\n";
}

RNTupleEventSource::~RNTupleEventSource() = default;

std::int64_t RNTupleEventSource::numEvents() const {
    return p_->reader ? static_cast<std::int64_t>(p_->reader->GetNEntries()) : 0;
}

bool RNTupleEventSource::loadEvent(std::int64_t i) {
    if (!p_->reader) return false;
    if (i < 0 || i >= static_cast<std::int64_t>(p_->reader->GetNEntries())) return false;
    p_->load(static_cast<ROOT::NTupleSize_t>(i));
    return true;
}

const std::vector<SHiP::SimHit>& RNTupleEventSource::hits() const { return *p_->effHits; }
const std::vector<SHiP::SimParticle>& RNTupleEventSource::simParticles() const {
    return *p_->effParts;
}
const std::vector<SHiP::MCParticle>& RNTupleEventSource::mcParticles() const {
    return p_->mc.value ? *p_->mc.value : kNoMC;
}
const std::vector<SHiP::RecParticle>& RNTupleEventSource::recParticles() const {
    return p_->rec.value ? *p_->rec.value : kNoRec;
}

}  // namespace shipdisp

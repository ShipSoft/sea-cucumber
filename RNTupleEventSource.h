#ifndef SHIPDISP_RNTUPLE_EVENTSOURCE_H
#define SHIPDISP_RNTUPLE_EVENTSOURCE_H

// =============================================================================
//  RNTupleEventSource.h
//
//  Reads the official SHiP event data model from an RNTuple -- the exact
//  format aegir's `sim_output_module` writes. Field layout (confirmed from the
//  data-model repo's RNTuple/TTree round-trip tests) is one top-level field
//  per collection in an ntuple named "events":
//
//      mcParticles   : std::vector<SHiP::MCParticle>
//      simHits       : std::vector<SHiP::SimHit>
//      simParticles  : std::vector<SHiP::SimParticle>
//      recParticles  : std::vector<SHiP::RecParticle>
//      simResult     : SHiP::SimResult   { vector<SimHit>, vector<SimParticle> }
//
//  The reader is TOLERANT: any of these fields may be missing. hits() and
//  simParticles() prefer the flat `simHits` / `simParticles` fields and fall
//  back to `simResult`'s bundled collections when the flat ones are absent,
//  so a file written with only `simResult` still displays.
//
//  Requires ROOT 6.40+ (ROOTNTuple component) and links SHiP::SHiPDataModel,
//  matching aegir's dependency set. The RNTuple headers are kept out of this
//  header via pimpl so the display core includes only IEventSource.
// =============================================================================

#include <memory>
#include <string>

#include "IEventSource.h"

namespace shipdisp {

class RNTupleEventSource : public IEventSource {
   public:
    /// Open @p path and attach to RNTuple @p ntupleName (default "events",
    /// the aegir / data-model convention). If opening fails, numEvents()
    /// returns 0 and loadEvent() returns false -- construction never throws.
    explicit RNTupleEventSource(const std::string& path, const std::string& ntupleName = "events");
    ~RNTupleEventSource() override;

    RNTupleEventSource(const RNTupleEventSource&) = delete;
    RNTupleEventSource& operator=(const RNTupleEventSource&) = delete;

    std::int64_t numEvents() const override;
    bool loadEvent(std::int64_t i) override;

    const std::vector<SHiP::SimHit>& hits() const override;
    const std::vector<SHiP::SimParticle>& simParticles() const override;
    const std::vector<SHiP::MCParticle>& mcParticles() const override;
    const std::vector<SHiP::RecParticle>& recParticles() const override;

   private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace shipdisp

#endif  // SHIPDISP_RNTUPLE_EVENTSOURCE_H

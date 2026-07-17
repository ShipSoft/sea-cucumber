// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) CERN for the benefit of the SHiP Collaboration
#ifndef SHIPDISP_IEVENTSOURCE_H
#define SHIPDISP_IEVENTSOURCE_H

// =============================================================================
//  IEventSource.h
//
//  Abstract per-event data source for the sea_cucumber display. This is the
//  seam that keeps the display CORE agnostic of *how* events are stored: the
//  core asks an IEventSource for "the collections for event i" and never
//  touches ROOT I/O directly.
//
//  The concrete source that matches the official ShipSoft stack is
//  RNTupleEventSource (see RNTupleEventSource.h), which reads the RNTuple
//  written by aegir's sim_output_module. A TTree source could implement the
//  same interface without the core noticing.
//
//  UNITS: everything here is the data model's NATIVE units, unchanged from
//  disk -- positions/lengths in millimetres, momenta in GeV/c, energy in GeV,
//  time in ns (see the data-model Units.hpp contract). The display core owns
//  the single mm -> scene-cm conversion (the same fHitScale / fOff pipeline
//  the prototype already uses), so no unit maths happens in the sources.
// =============================================================================

#include <cstdint>
#include <vector>

#include "SHiP/MCParticle.hpp"
#include "SHiP/RecParticle.hpp"
#include "SHiP/SimHit.hpp"
#include "SHiP/SimParticle.hpp"

namespace shipdisp {

class IEventSource {
   public:
    virtual ~IEventSource() = default;

    /// Number of events available (0 if the source failed to open).
    virtual std::int64_t numEvents() const = 0;

    /// Make event @p i current. Returns false if @p i is out of range or the
    /// source is not open. After a successful call, the accessors below refer
    /// to event @p i until the next loadEvent().
    virtual bool loadEvent(std::int64_t i) = 0;

    /// Collections for the current event. Each returns an empty vector if the
    /// underlying field is absent from the file (the reader is tolerant of
    /// files that carry only some of the data-model collections).
    virtual const std::vector<SHiP::SimHit>& hits() const = 0;
    virtual const std::vector<SHiP::SimParticle>& simParticles() const = 0;
    virtual const std::vector<SHiP::MCParticle>& mcParticles() const = 0;
    virtual const std::vector<SHiP::RecParticle>& recParticles() const = 0;
};

}  // namespace shipdisp

#endif  // SHIPDISP_IEVENTSOURCE_H

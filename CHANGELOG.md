<!-- SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration -->
<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Changelog

## [0.1.0] - unreleased
### Added
- Standalone REve event display reading the official SHiP data model (RNTuple)
  and GeoModel `.db` geometry.
- `IEventSource` / `RNTupleEventSource`: tolerant reader of the `events` ntuple
  (mcParticles, simHits, simParticles, recParticles, simResult).
- `IGeometrySource` / `GeoModelGeometrySource`: GeoModel `.db` provider mirroring
  aegir, with aegir-style DB resolution and a `SHiPGeometryService` seam.
- `GeoModelLoader`: GeoModel world -> TGeo/REve shape translation.
- TOML view config; pixi build/test/lint tasks.

### Fixed
- `RNTupleEventSource` no longer aborts on files containing fields it does not
  display (e.g. aegir's `event_header`): each displayed collection is read
  through its own `RNTupleView`, so other fields are never reconstructed and
  need no dictionary (#35).

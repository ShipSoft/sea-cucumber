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

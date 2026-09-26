<!-- SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration -->
<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Changelog

## [0.1.0] - unreleased
### Added
- GDML geometry input. `--geometry` now accepts a `.gdml` file as well as a
  GeoModel `.db` in `sea_cucumber` (incl. `--inspect` and name-matched regions),
  `make_web_data`, `make_geometry_cache` and `make_demo_event`.
  `GdmlGeometrySource` parses with `TGDMLParse`, emits in mm under the same
  include/exclude/depth/z-window rules as the GeoModel walk, treats matching
  `<assembly>`s as envelopes, and leaves any existing `gGeoManager` untouched.
  `MakeGeometrySource` / `ScanGeometry` pick the backend by extension, then
  by file content. New `test_gdml_geometry` test.
- Standalone REve event display reading the official SHiP data model (RNTuple)
  and GeoModel `.db` geometry.
- `IEventSource` / `RNTupleEventSource`: tolerant reader of the `events` ntuple
  (mcParticles, simHits, simParticles, recParticles, simResult).
- `IGeometrySource` / `GeoModelGeometrySource`: GeoModel `.db` provider mirroring
  aegir, with aegir-style DB resolution and a `SHiPGeometryService` seam.
- `GeoModelLoader`: GeoModel world -> TGeo/REve shape translation.
- TOML view config; pixi build/test/lint tasks.

### Fixed
- `make_web_data` no longer drops boolean volumes (`TGeoCompositeShape`) from
  `geometry.json`: `TGeoCompositeShape::MakeBuffer3D()` returns nullptr, so they
  are now meshed with ROOT's CSG library (`RCsg`), as the REve viewer does.
  Meshing lives in `ShapeMesh` with a new `test_shape_mesh` test.
- `RNTupleEventSource` no longer aborts on files containing fields it does not
  display (e.g. aegir's `event_header`): each displayed collection is read
  through its own `RNTupleView`, so other fields are never reconstructed and
  need no dictionary (#35).

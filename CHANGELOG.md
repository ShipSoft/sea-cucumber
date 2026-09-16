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
- User TOML config for appearance defaults, found along the usual paths
  (`--config`, `$SEA_CUCUMBER_CONFIG`, the CWD, `$XDG_CONFIG_HOME`,
  `$XDG_CONFIG_DIRS`, `$CONDA_PREFIX`) and layered over the view config's
  `[ui]` block; `--no-config` opts out.
- Web frontend: "Set as default" and "Revert" under Colour scheme, remembering
  the scheme, text sizes and menu width in the browser.

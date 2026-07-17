<!-- SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration -->
<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# sea_cucumber

The SHiP event display: a geometry- and data-model-agnostic [REve](https://root.cern/) web
display. It takes three inputs and nothing else — a GeoModel `.db` geometry, an RNTuple data
file in the official SHiP event data model, and an optional TOML view config — and renders the
event. It links no analysis or framework code, so it runs independently of how the data was
produced.

It consumes the geometry and data model the same way [aegir](https://github.com/ShipSoft/aegir)
does: it links [SHiPDataModel](https://github.com/ShipSoft/data-model), reads the RNTuple that
aegir's `sim_output_module` writes, and loads the `.db` through the same provider pattern and
DB-resolution convention.

## Build, test, lint (pixi)

[Install pixi](https://pixi.sh), then:

```
pixi install         # resolve dependencies (one-off)
pixi run build       # configure + build
pixi run test        # ctest (RNTuple round-trip + DB resolution)
pixi run lint        # prek: clang-format, cpplint, gersemi, reuse, codespell
```

`build` depends on `configure`; `test` depends on `build`. The SHiP-specific packages
(`shipdatamodel`, `geomodel`) resolve from the `prefix.dev/ship` channel, everything else from
conda-forge — the same setup as aegir.

## Run

```
pixi run sea_cucumber --geometry ship_geometry.db --data output.root --view views/default.toml
```

Flags: `--geometry <db>` (required), `--data <root>` (required), `--view <toml>`,
`--ntuple <name>` (default `events`), `--event <i>`, `--scale <f>`. A bare `--geometry`
filename is resolved against the CWD first, then `$SHIPGEOMETRY_ROOT/share/geometry/`, matching
aegir. The display opens in your browser via ROOT's web REve.

### Demo event

To see it working without a full simulation, generate a synthetic event and
display it in one step (point it at your geometry):

```
pixi run demo --geometry /path/to/ship_geometry.db
```

`make_demo_event` reads the geometry and places hits on the real detector
volumes: a neutral particle decays in the decay volume into two charged
daughters that cross the tracker stations, shower in the calorimeter, and set
off a few SBT cells, with MC-truth and reconstructed overlays. It writes
`demo_events.root` (the official `events` RNTuple), which `sea_cucumber` then
opens. Run the generator alone with `pixi run demo-event` (or
`make_demo_event --geometry ... --output ... --events N`).

## Architecture

```
              +------------------+
  .db  ---->  | IGeometrySource  |  GeoModelGeometrySource
              |                  |    -> (SHiPGeometryService | GeoModelIO)
              |                  |    -> GeoModelLoader (GeoModel -> TGeo/REve)
              +---------+--------+
                        |
                        v
  view.toml -->  [ EventDisplay core ]  -- REve scenes + 3 viewers
                        ^
                        |
              +---------+--------+
  .root ----> |  IEventSource    |  RNTupleEventSource (events ntuple)
              +------------------+
```

The core knows only about visual primitives and the view config. Backends sit behind
`IGeometrySource` / `IEventSource`, so swapping detector or file format never touches the core.

### Data model

Reads the `events` RNTuple fields written by aegir: `mcParticles`, `simHits`, `simParticles`,
`recParticles`, and the bundled `simResult`. The reader is tolerant — any field may be absent,
and hits/particles fall back to `simResult` when the flat collections are missing. Positions are
millimetres on disk; the display applies one `hit_scale` factor to geometry and hits alike.

### Geometry

`GeoModelGeometrySource` obtains the GeoModel world (via `SHiPGeometryService` when built with
`-DSHIP_USE_GEOMETRY_SERVICE=ON`, else directly with GeoModelIO) and `GeoModelLoader` translates
it into REve shapes, coloured per the view config. Dense replicated subsystems (straws, tiles)
should be filtered via `include`/`exclude` regexes so the web client stays responsive.

## Status / notes

- The `SHiPGeometryService` call is stubbed behind `SHIP_USE_GEOMETRY_SERVICE`; bind the one
  include + load call once the service header is available (see `GeoModelGeometrySource.cxx`).
- Live event navigation in the browser is not yet wired; `--event` selects the event to render.
- Replace `LICENSES/LGPL-3.0-or-later.txt` with the full licence text (`reuse download
  LGPL-3.0-or-later`) before publishing.

## Licence

LGPL-3.0-or-later. Copyright is held by CERN for the benefit of the SHiP Collaboration.

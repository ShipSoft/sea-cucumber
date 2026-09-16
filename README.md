<!-- SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration -->
<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->

<p align="center">
  <img src="logo/sc.png" alt="sea_cucumber logo" width="180" />
</p>

<h1 align="center">sea_cucumber</h1>

<p align="center">The SHiP event display.</p>

sea_cucumber reads a GeoModel geometry database and the official SHiP RNTuple
data model and renders geometry and events. It takes three inputs and nothing
else — a GeoModel `.db`, an RNTuple data file in the SHiP event data model, and
an optional TOML view config — and links no analysis or framework code, so it
runs independently of how the data was produced.

There are **two viewers**, sharing one core:

- a **web frontend** (three.js) — a panelised, fully controllable interface you
  run in the browser (the primary interface); and
- a **ROOT REve** display — the original in-ROOT viewer, kept as a parallel
  option.

Following the ALICE O2 event-display design ([arXiv:2503.00088](https://arxiv.org/abs/2503.00088)),
the C++ side is a *producer*: it loads the `.db`, reads the data model, resolves
colours, and (for the web path) tessellates geometry into display files; the web
frontend then just *renders* them. Nothing about the physics or geometry is
re-interpreted at view time.

It consumes geometry and data the same way [aegir](https://github.com/ShipSoft/aegir)
does: it links [SHiPDataModel](https://github.com/ShipSoft/data-model), reads the
`events` RNTuple, and loads the `.db` through the same DB-resolution convention.

## Build, test, lint (pixi)

[Install pixi](https://pixi.sh), then:

```
pixi run build       # configure + build
pixi run test        # ctest (RNTuple round-trip, DB resolution, geometry cache)
pixi run lint        # prek: clang-format, cpplint, gersemi, cmakelint, codespell, reuse
```

`build` depends on `configure`; `test` on `build`. SHiP-specific packages
(`shipdatamodel`, `geomodel`) resolve from the `prefix.dev/ship` channel, the
rest from conda-forge — the same setup as aegir.

## Run — web frontend

Two steps: produce the display files, then serve them.

```
pixi run web-data --geometry ship_geometry.db --data output.root --view views/default.toml
pixi run web        # open http://localhost:8080
```

`web-data` tessellates the geometry and writes `web/data/` (`geometry.json`, one
`event_<i>.json` per event, `manifest.json`). Useful flags: `--out <dir>`,
`--events all|<i>`, `--depth <n>`, `--max-shapes <n>`.

In the browser you get a control sidebar, a main 3D view, and any number of
floating views you create and shape interactively — draw a region on a view to
open it as a zoom, resize/stack/lock views, recolour borders, and save or load
the whole layout. See the [manual](docs/MANUAL.md) for the full interface.

## Run — ROOT REve display

```
pixi run sc --geometry ship_geometry.db --data output.root --view views/default.toml --event 0
```

Flags: `--geometry <db>` (required), `--data <root>` (required), `--view <toml>`,
`--ntuple <name>` (default `events`), `--event <i>`, `--scale <f>`, `--logo <dir>`,
`--geo-cache <prefix>`. A bare `--geometry` filename is resolved against the CWD,
then `$SHIPGEOMETRY_ROOT/share/geometry/`, matching aegir. The aliases `sc`,
`run_event_display`, and `ED` all launch it.

## Configure

The view config says what to draw and is meant to be shared. How the display
*looks* to you — colour scheme, text sizes, menu width — belongs in your own
config, which both tools find without being told, highest precedence first:

```
--config <path>                              # explicit; must exist
$SEA_CUCUMBER_CONFIG                         # explicit; warns if it is gone
./sea_cucumber.toml                          # project-local
$XDG_CONFIG_HOME/sea_cucumber/config.toml    # default ~/.config/...
$XDG_CONFIG_DIRS entries, then $CONDA_PREFIX/share/sea_cucumber/config.toml
```

Each file that exists is applied in turn, overriding only the keys it sets, on
top of the view config's `[ui]` block. `--no-config` skips the search.

```toml
[ui]
color_scheme = "ship_night"   # any key of SCHEMES in web/js/schemes.js
font_scale = 1.1
[ui.fonts]
menu = 14
```

Copy `configs/sea_cucumber.example.toml` to get started. In the browser you can
override all of it again per browser: under **Colour scheme**, "Set as default"
remembers the scheme and text sizes locally, "Revert" drops them. That layer is
browser-side on purpose — whoever runs `make_web_data` and whoever is looking at
the page need not be on the same machine.

## Inspect the geometry

To find where subsystems sit in z (no data file needed):

```
pixi run sc --geometry ship_geometry.db --inspect 4
pixi run sc --geometry ship_geometry.db --inspect 6 --inspect-match "*snd*"
```

## Geometry cache

Reading a ~1M-volume `.db` takes seconds on every launch. Convert it once so the
REve display loads quickly:

```
pixi run geo-cache --geometry ship_geometry.db --view views/default.toml
pixi run sc --geo-cache geocache --geometry ship_geometry.db --data output.root
```

## Architecture

```
  .db  ---->  IGeometrySource  (GeoModelGeometrySource / CachedGeometrySource)
                     |
                     v
  view.toml -->  [ core ]  --->  REve viewer          (pixi run sc)
                     |     \-->  make_web_data (JSON)  --->  three.js frontend
                     ^                                        (pixi run web)
                     |
  .root ---->  IEventSource     (RNTupleEventSource, events ntuple)
```

The core knows only visual primitives and the view config. Backends sit behind
`IGeometrySource` / `IEventSource`, so swapping detector or file format never
touches the core. The web frontend depends only on the JSON the producer writes
— not on REve.

### Data model

Reads the `events` RNTuple fields `mc_particles`, `sim_hits`, `sim_particles`,
`rec_particles`, and the bundled `sim_result`. The reader is tolerant — any
field may be absent, and hits/particles fall back to `sim_result` when the flat
collections are missing. Positions are millimetres on disk.

### Geometry

`GeoModelGeometrySource` obtains the GeoModel world (via `SHiPGeometryService`
when built with `-DSHIP_USE_GEOMETRY_SERVICE=ON`, else directly with GeoModelIO).
For REve it becomes REve shapes; for the web it is tessellated to triangle meshes
via `TBuffer3D`. Dense replicated subsystems (straws, tiles) should be filtered
via `include`/`exclude` regexes to keep both viewers responsive.

## Documentation

- [docs/MANUAL.md](docs/MANUAL.md) — full user and developer reference.
- [docs/RELEASING.md](docs/RELEASING.md) — versioning and changelog workflow.

## Status

Alpha. Both viewers are usable for day-to-day event inspection; interfaces may
still change. Known limitations are listed in the release notes and the manual —
notably that the web frontend loads three.js from a CDN (vendor it locally on
air-gapped networks), and that the `SHiPGeometryService` binding is still stubbed
behind `SHIP_USE_GEOMETRY_SERVICE`.

## Licence

LGPL-3.0-or-later. Copyright is held by CERN for the benefit of the SHiP
Collaboration.

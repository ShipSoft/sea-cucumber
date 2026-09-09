<!--
SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# sea_cucumber web frontend

An ALICE-style split: the C++ side stays the single source of truth (it loads
the GeoModel `.db`, reads the SHiP data model, tessellates geometry) and writes
**display files**; this static page renders them in the browser with three.js.
No build step, no framework, no Node — three.js is pulled from a CDN via an
import map. The C++ tree, CMake, and pixi model are unchanged; this is one
additive directory.

## Layout

```
web/
  index.html      toolbar + canvas + side panel (the chrome)
  css/app.css     dark instrument-panel styling, SHiP palette
  js/
    data.js       the ONLY file that knows the on-disk format
    main.js       three.js renderer + toolbar wiring
  logo/sc.png     optional; shown top-left if present
```

## Data contract

The page reads these from a data directory (default `data/`, override with
`?data=some/dir/`):

- `manifest.json` — `{ "nEvents", "geometry", "unit_mm_per_scene" }`
- `geometry.json` — `{ "meshes": [ { name, color, transparency, vertices, indices } ] }`
  (vertices are flat `x,y,z` triples in mm; indices are a triangle list)
- `event_<i>.json` — `{ event, hits[], vertex?, tracks?, clusters? }`

`tracks`/`clusters` are rendered if present. The SHiP producer does not emit
them yet; empty arrays simply draw nothing.

See `js/data.js` for the exact shapes — keep it as the single point of change
when the producer's output evolves.

## What still needs building (C++ side)

1. A **mesh emitter**: tessellate each `TGeoShape` (via `TBuffer3D`) behind the
   existing `IGeometrySource` seam and write `geometry.json`.
2. An **event writer**: dump the current event's hits (and later tracks,
   clusters) to `event_<i>.json`, plus a `manifest.json`.

Both are pure C++, testable with `pixi run`, and reuse the sources the display
already has. Until they exist, the page loads and shows a "no data" message.

## Running

Serve this directory over HTTP (a static file server is enough):

```
pixi run web        # serves web/ on http://localhost:8080
```

Then open the URL. Point `?data=` at wherever the C++ side wrote the files.

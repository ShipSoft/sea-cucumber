<!--
SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# sea_cucumber manual

The SHiP event display. It reads a GeoModel geometry database (or a GDML file)
and the official SHiP RNTuple data model, and renders geometry and events two ways: the built-in
ROOT REve viewer, and a standalone web frontend (three.js) that you fully
control.

This manual covers everything: building, the command-line tools, the view
configuration, and the web interface.

Contact: Matei Climescu, matclim@cern.ch
rev 0.1, 10.09.2026

---

## 1. Concepts and architecture

sea_cucumber follows the ALICE O2 event-display pattern (arXiv:2503.00088): the
C++ side is the single source of truth — it loads the geometry, reads the data
model, resolves colours, and tessellates geometry — and it *produces display
files*; a separate viewer *renders* them. Nothing about the physics or geometry
is re-interpreted at view time.

There are three ways to view:

- **REve** (`sea_cucumber`): the ROOT web viewer, launched from C++.
- **Web frontend** (`web/`): a static three.js page consuming produced JSON.
- **Inspection** (`--inspect`): a text dump of the geometry, no rendering.

The reusable core (`src/`) is split behind two interfaces so the pieces are
independently testable:

- `IEventSource` — yields an event's hits, MC particles, etc.
  (`RNTupleEventSource` reads the `events` RNTuple).
- `IGeometrySource` — emits geometry shapes (`GeoModelGeometrySource` walks the
  GeoModel `.db`; `GdmlGeometrySource` walks a `.gdml` file;
  `CachedGeometrySource` replays a pre-built cache). `MakeGeometrySource`
  picks the `.db` or GDML backend from the `--geometry` file.

---

## 2. Building and running

sea_cucumber is a pixi workspace. All workflows go through pixi tasks:

| Task | What it does |
|------|--------------|
| `pixi run build` | Configure + compile (CMake + Ninja). |
| `pixi run test` | Build, then run the CTest suite. |
| `pixi run lint` | Run the pre-commit hooks (formatting, cpplint, REUSE…). |
| `pixi run sea_cucumber …` (aliases `sc`, `run_event_display`, `ED`) | Launch the REve display. |
| `pixi run web-data …` | Produce the web display files into `web/data`. |
| `pixi run web` | Serve `web/` at http://localhost:8080. |
| `pixi run geo-cache …` | Build a geometry cache to speed up REve start-up. |
| `pixi run clean` | Remove the build directory. |

### The REve display

```
pixi run sc --geometry files/ship_geometry.db --data files/llp_display.root \
            --view views/default.toml --event 16
```

Key flags: `--geometry <db|gdml>`, `--data <root>`, `--view <toml>`, `--event <i>`,
`--ntuple <name>`, `--scale <f>`, `--logo <dir>`, `--geo-cache <prefix>`.

### The web display

Two steps — produce the data, then serve it:

```
pixi run web-data --geometry files/ship_geometry.db --data files/llp_display.root \
                  --view views/default.toml
pixi run web        # open http://localhost:8080
```

`web-data` flags: `--geometry` (`.db` or `.gdml`), `--data`, `--view`, `--out <dir>` (default
`web/data`), `--events all|<i>`, `--depth <n>` (walk depth, default 4),
`--max-shapes <n>` (cap, default 20000).

---

## 3. Inspecting the geometry

To find where subsystems sit in z (so you can set region windows), dump the
geometry without any data file:

```
pixi run sc --geometry files/ship_geometry.db --inspect 4
pixi run sc --geometry files/ship_geometry.db --inspect 6 --inspect-match "*snd*"
```

`--inspect [depth]` prints each volume name aggregated with its count and z
span. `--inspect-match <pat>` filters by name (regex or glob, e.g. `*snd*`).
`--inspect-all` lists every instance instead of aggregating. You can also read
names straight from the SQLite `.db`:

```
sqlite3 files/ship_geometry.db "SELECT name FROM LogVols ORDER BY name;"
```

`--inspect` works the same on a GDML file (`--geometry ship.gdml`); there the
names are the GDML `<volume>`/`<assembly>` names, which you can also grep from
the file's `<structure>` section.

---

## 4. The view configuration (`views/*.toml`)

The view config drives the REve display and seeds the web producer. Globals:

- `hit_scale` — mm → scene-unit scale.
- `ntuple` — RNTuple name (default `events`).
- `scan_depth` / `region_depth` — how deep the name-scan and region walks go.
- `region_exclude` — volume patterns dropped from region walks.

### `[geometry]`

`db_file`, `include`, `exclude` (regex or glob), `max_depth`, `stop_at_match`,
`default_color` (hex), `default_transparency`. Volumes matching a `[[style]]`
get that colour; unmatched volumes get `default_color`.

### `[hits]` and `[decay]`

`[hits]`: `marker_style`, `marker_size`, `color_by_energy`, `color_low`,
`color_high`. Energy is encoded as marker size — `marker_size` is the lowest
bin, the highest renders at 2.8×.

`[decay]`: `draw`, `color`, `marker_style`, `marker_size` — the truth decay
vertex (the first MC particle with a mother).

### `[[region]]` — zoom views

Each region is a windowed sub-view. Selection is **per axis** and independent of
the camera:

- `xmin`/`xmax`, `ymin`/`ymax`, `zmin`/`zmax` — any subset; a volume/hit is kept
  only if inside every axis window that is set.
- `match` — derive a z window by volume name instead of hardcoding it.
- `camera` — `"side"`/`"xz"` (looking along y), `"front"`/`"xy"` (along z,
  beam's-eye), `"top"`/`"xy0"`, `"3d"`. Raw ROOT enum names also accepted.
- `exclude`/`include` — drop/restrict volumes in THIS view only.
- `max_shapes`, `max_transparency`, `margin_frac`.
- `recenter` (+ `offset_x/y/z`) — centre the region on its window so the camera
  frames it (Eve7 auto-fits about the origin).
- `hit_marker_size`, `draw_decay`, `decay_clip`, `decay_marker_size` — per-view
  event styling.

Camera and window are independent: a side view is normally a z slab, a front
view an x or y slab. A mismatch is a warning, not an error — whatever windows
you supplied are used, z preferred.

---

## 5. The web frontend

Layout: a control **sidebar** (left), the **main** 3D view (centre), and any
number of **floating views** you create.

### Sidebar controls

The Show toggles (geometry / hits / decay vertex) and the hit-size slider act on
the **selected view**, or the main view if none is selected. Each view keeps its
own settings, and a new view inherits them from the view it was created from.
Camera buttons (3D / Side / Front / Top) reorient the selected view (else main).
Clicking the main view deselects.

### Creating views

- **+ New view** — a floating panel showing the full detector, inheriting the
  current view's display options. Resize by the corner, move by the title bar,
  rename by double-clicking the title, close with ×.
- **Select view location** — click it, then drag a rectangle on any view (the
  "mother"). A new child view is created showing that boxed region. Orient the
  mother first (Side/Front/Top): the two on-screen axes become the window, the
  axis into the screen stays unconstrained. Select the mother first so you know
  which panel you are drawing on; dragging on the main view uses the full
  detector.

### View context menu (right-click a view)

- **Resize numerically…** — set an exact pixel width/height (current shown).
- **Stack left / right / top / bottom** — move the view to that edge, keeping
  its other coordinate, stopping on collision with another view. Chain them
  (e.g. right then bottom) to tile into a corner.
- **Lock window** — strip the chrome for a clean display: thin seamless border,
  no shadow, frozen position, hidden close button (title stays). Right-click a
  locked view for **Unlock**.
- **Set boundary colours…** — a colour picker with a hex field; applied live,
  persists across lock/unlock, with **Restore default**.

### Saving your setup

- **Save setup** downloads `sea_cucumber_setup.json`: every view's name,
  position, size, region window, camera, display options, lock state, and border
  colour (but never the event — a setup is layout only).
- **Load setup** reads such a file back and rebuilds the arrangement.
- On start-up the frontend auto-loads `configs/sea_cucumber_default_setup.json`
  if present; edit that file to change the default layout.

### Colours

Dark-earth background, blue-dominant detector geometry (with cream and muted
purple mixed in), darker-pink hits, lighter-pink decay vertex, cream text.
Detector colour comes from the producer; hit/vertex colours are set in
`web/js/main.js`.

### Assets

- `web/logo/sc.png` — square logo (sidebar + tab icon); auto-copied from the
  repo `logo/` by the producer.
- `web/fonts/mononoki-Regular.woff2` — the wordmark font (SIL OFL, from
  github.com/madmalik/mononoki). Falls back to system monospace if absent.

---

## 6. GDML geometry

Every tool that takes `--geometry` (`sc`, `web-data`, `geo-cache`, `demo`)
accepts a GDML file as well as a GeoModel `.db`:

```
pixi run sc --geometry files/ship_geometry.gdml --data files/llp_display.root \
            --view views/default.toml
```

- **Format choice.** A `.gdml` extension selects GDML and `.db` / `.sqlite`
  selects GeoModel. Any other extension is decided by content (an SQLite header
  vs an XML/GDML document), so e.g. `detector.xml` works. Bare filenames resolve
  exactly like `.db` files: CWD first, then `$SHIPGEOMETRY_ROOT/share/geometry/`.
- **Parsing.** ROOT's `TGDMLParse` builds a TGeo tree once per run; the result
  is cached, so the display's several passes (main, name scan, regions) parse
  the file only once. An existing `gGeoManager` is left untouched.
- **Units.** GDML lengths are converted to millimetres before anything is
  emitted, so hits (mm on disk) and geometry line up exactly as with a `.db`.
  Give lengths explicit `lunit`/`unit` attributes (Geant4 exports always do):
  ROOT warns about unitless lengths and may not read them as mm.
- **Names and filters.** `include`/`exclude`/`[[style]]`/region patterns match
  GDML *logical volume* names (the `<volume name>`; a Geant4 `0x…` pointer
  suffix is stripped by ROOT). Names generally differ from the GeoModel ones,
  so run `--inspect` and adapt the view config to the file you load.
- **Assemblies.** An `<assembly>` has no solid of its own, so it is never drawn;
  the walk passes through it. If an assembly matches `include` (or a region's
  `match`), its children count as matched — the assembly acts as a subsystem
  envelope. `--inspect` and region `match` report assemblies with the z span of
  their contents.
- **Shapes.** All TGeo solids GDML produces are supported, including booleans,
  polycones/polyhedra, extruded solids, ellipsoids and elliptical cones. Rarer
  ones (tessellated, twisted) are drawn through a scaled wrapper; half-spaces
  are skipped with a one-line warning.
- **Cache.** `geo-cache` works on GDML too; replaying a cache never reopens the
  GDML file.

---

## 7. The geometry cache

Reading a ~1M-volume `.db` (or a large GDML file) takes seconds on every launch. The cache converts it
once to a ROOT file the display loads quickly:

```
pixi run geo-cache --geometry files/ship_geometry.db --view views/default.toml
pixi run sc --geo-cache geocache --geometry files/ship_geometry.db \
            --data files/llp_display.root --view views/default.toml
```

`geo-cache` writes `geocache.main.root` (envelopes) and `geocache.region.root`
(deep walk). With `--geo-cache`, the display skips the GeoModel/GDML read
entirely.

---

## 8. Versioning

The version lives in the `VERSION` text file at the repo root, mirrored in
`pixi.toml` (`[workspace] version`, per the ShipSoft pixi convention, readable
with `pixi workspace version get`) and in `CITATION.cff`. The web producer copies
`VERSION` next to the served page, and the frontend shows it under the wordmark.
To release, bump all three in step (and the conda recipe in `ship-conda-recipes`
when publishing).

---

## 9. Troubleshooting

- **Web page shows chrome but no 3D** — check the browser console (F12). A
  blocked three.js CDN import (locked-down network) is the usual cause; vendor
  three.js under `web/js/vendor/` and point the import map at it.
- **A region view is empty** — its window may not intersect this geometry;
  verify with `--inspect` and adjust `zmin/zmax`.
- **Hits float away from the geometry** — the data and the loaded `.db`/`.gdml`
  may be from different detector layouts; check their z ranges match. For GDML,
  also check the file gives explicit length units.
- **GDML loads but nothing is drawn** — the view config's `include` patterns
  are probably GeoModel names. Run `--inspect` on the GDML file and adjust.
- **REve right-click menu empty** — the navigator dictionary must be compiled
  into the executable, not the static library (see `CMakeLists.txt`).

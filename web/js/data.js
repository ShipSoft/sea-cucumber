// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// data.js -- load the display files the C++ side produces. This is the ONLY
// place that knows the on-disk format, so when the C++ producer's output
// changes, only this file follows.
//
// Expected files (relative to `base`, default "data/"):
//
//   manifest.json
//     { "nEvents": 20, "geometry": "geometry.json", "unit_mm_per_scene": 1000 }
//       nEvents           number of event_<i>.json files available
//       geometry          filename of the geometry mesh file
//       unit_mm_per_scene optional; mm per scene unit (default 1000, i.e. we
//                         render in metres so coordinates stay GPU-friendly)
//
//   geometry.json
//     { "meshes": [
//         { "name": "…", "color": "#20428a", "transparency": 45,
//           "vertices": [x0,y0,z0, x1,y1,z1, …],   // mm, flat triples
//           "indices":  [i0,i1,i2, …] }            // triangle list
//     ] }
//
//   event_<i>.json
//     { "event": 3,
//       "hits":   [ { "x":…, "y":…, "z":…, "e":…, "pdg":… }, … ],  // mm, GeV
//       "vertex": { "x":…, "y":…, "z":… },                        // optional
//       "tracks": [ { "points":[x0,y0,z0, …], "pdg":… }, … ],     // optional, mm
//       "clusters":[ { "x":…, "y":…, "z":…, "e":… }, … ] }        // optional
//
// tracks/clusters are read if present but the SHiP producer does not emit them
// yet; the renderer simply draws nothing for empty arrays.

async function getJSON(url) {
  const res = await fetch(url, { cache: "no-store" });
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return res.json();
}

export class DataSource {
  constructor(base = "data/") {
    this.base = base.endsWith("/") ? base : base + "/";
    this.manifest = null;
  }

  async loadManifest() {
    this.manifest = await getJSON(this.base + "manifest.json");
    return this.manifest;
  }

  get nEvents() {
    return this.manifest ? this.manifest.nEvents | 0 : 0;
  }

  // mm per scene unit; we render in metres by default so a ~100 m detector maps
  // to ~100 scene units, well within float precision and easy for the camera.
  get mmPerScene() {
    return (this.manifest && this.manifest.unit_mm_per_scene) || 1000;
  }

  // Optional UI settings ([ui] in the view TOML): font_scale, sidebar_width,
  // color_scheme, fonts.
  get ui() {
    return (this.manifest && this.manifest.ui) || {};
  }

  async loadGeometry() {
    const name = (this.manifest && this.manifest.geometry) || "geometry.json";
    const g = await getJSON(this.base + name);
    return g.meshes || [];
  }

  async loadEvent(i) {
    return getJSON(this.base + `event_${i}.json`);
  }
}

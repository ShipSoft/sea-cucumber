// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// main.js -- four-panel sea_cucumber renderer: a main view plus three region
// views (Spectrometer / Calorimeter / SND), mirroring the REve display. Each
// panel is a self-contained three.js context (its own scene + camera), so the
// region panels can show a filtered, recentred subset without touching the
// others. The C++ side stays the source of truth; this file only renders.

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { DataSource } from "./data.js";

const COL = {
  earth: 0x34240f, // panel background
  yellow: 0xe3a93c, // detector geometry (fallback)
  pink: 0xc64284, // hits
  pinkLt: 0xeda9c8, // decay vertex
};

const $ = (id) => document.getElementById(id);
const setStatus = (html) => { $("status").innerHTML = html || ""; };

// A single 3D panel: canvas + renderer + scene + camera + toggle groups.
class Panel {
  constructor(canvasOrId) {
    this.canvas = typeof canvasOrId === "string" ? $(canvasOrId) : canvasOrId;
    this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(COL.earth, 1);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(45, 1, 0.1, 1e6);
    this.controls = new OrbitControls(this.camera, this.canvas);
    this.controls.enableDamping = true;
    // Render on demand, not every frame. A flat-out requestAnimationFrame loop
    // over four WebGL contexts pegs the CPU/GPU even when nothing moves (which
    // is what made the page sluggish). Instead we redraw only when the user
    // interacts with THIS panel, or when its content changes.
    this._needsRender = true;
    this.controls.addEventListener("change", () => this.invalidate());

    this.scene.add(new THREE.AmbientLight(0xffffff, 0.8));
    const key = new THREE.DirectionalLight(0xffffff, 0.55);
    key.position.set(1, 1, 1);
    this.scene.add(key);

    this.gGeo = new THREE.Group();
    this.gHits = new THREE.Group();
    this.gVertex = new THREE.Group();
    this.scene.add(this.gGeo, this.gHits, this.gVertex);

    this.box = new THREE.Box3(); // geometry bounds, for framing
    this.offset = new THREE.Vector3(0, 0, 0); // recentre shift (scene units)
  }

  clear(g) {
    for (let i = g.children.length - 1; i >= 0; i--) {
      const c = g.children[i];
      c.geometry?.dispose();
      c.material?.dispose();
      g.remove(c);
    }
  }

  // Build geometry. `meshes` are raw producer meshes (mm). We MERGE meshes that
  // share a colour+transparency into a single BufferGeometry each: ~20k separate
  // THREE.Mesh objects means ~20k draw calls per frame, which alone stalls the
  // browser. Merging cuts that to a handful (one per distinct style).
  setGeometry(meshes, scale, win) {
    this.clear(this.gGeo);

    this.offset.set(0, 0, 0);
    if (win) {
      for (const ax of [0, 1, 2]) {
        if (win[ax]) this.offset.setComponent(ax, 0.5 * (win[ax][0] + win[ax][1]) * scale);
      }
    }

    const buckets = new Map(); // styleKey -> { color, transparency, pos:[], idx:[] }
    const box = new THREE.Box3();
    for (const m of meshes) {
      if (!m.vertices || !m.indices) continue;
      if (win && !meshInWindow(m, win)) continue;
      const t = m.transparency || 0;
      const color = m.color || "#e3a93c";
      const key = color + "|" + t;
      let bk = buckets.get(key);
      if (!bk) { bk = { color, transparency: t, pos: [], idx: [] }; buckets.set(key, bk); }
      const base = bk.pos.length / 3;
      for (let i = 0; i < m.vertices.length; i += 3) {
        const x = m.vertices[i] * scale - this.offset.x;
        const y = m.vertices[i + 1] * scale - this.offset.y;
        const z = m.vertices[i + 2] * scale - this.offset.z;
        bk.pos.push(x, y, z);
        if (x < box.min.x) box.min.x = x; if (x > box.max.x) box.max.x = x;
        if (y < box.min.y) box.min.y = y; if (y > box.max.y) box.max.y = y;
        if (z < box.min.z) box.min.z = z; if (z > box.max.z) box.max.z = z;
      }
      for (let i = 0; i < m.indices.length; i++) bk.idx.push(m.indices[i] + base);
    }

    for (const bk of buckets.values()) {
      if (!bk.pos.length) continue;
      const geom = new THREE.BufferGeometry();
      geom.setAttribute("position", new THREE.BufferAttribute(new Float32Array(bk.pos), 3));
      geom.setIndex(bk.idx);  // three.js chooses Uint16/Uint32 as needed
      geom.computeVertexNormals();
      const mat = new THREE.MeshStandardMaterial({
        color: new THREE.Color(bk.color),
        transparent: bk.transparency > 0,
        opacity: 1 - bk.transparency / 100,
        metalness: 0.0,
        roughness: 0.85,
        side: THREE.DoubleSide,
        depthWrite: bk.transparency < 50,
      });
      this.gGeo.add(new THREE.Mesh(geom, mat));
    }

    this.box = box.isEmpty()
      ? new THREE.Box3(new THREE.Vector3(-1, -1, -1), new THREE.Vector3(1, 1, 1))
      : box;
    this.invalidate();
  }

  // Build hits + vertex for one event, filtered to the same window and shifted
  // by the same offset so they stay registered with the geometry.
  setEvent(ev, scale, win) {
    this.clear(this.gHits);
    this.clear(this.gVertex);

    const hits = (ev.hits || []).filter((h) => !win || hitInWindow(h, win));
    if (hits.length) {
      const pos = new Float32Array(hits.length * 3);
      for (let i = 0; i < hits.length; i++) {
        pos[3 * i] = hits[i].x * scale - this.offset.x;
        pos[3 * i + 1] = hits[i].y * scale - this.offset.y;
        pos[3 * i + 2] = hits[i].z * scale - this.offset.z;
      }
      const g = new THREE.BufferGeometry();
      g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
      this.gHits.add(new THREE.Points(g, new THREE.PointsMaterial(
        { color: COL.pink, size: 4, sizeAttenuation: false })));
    }

    if (ev.vertex && (!win || hitInWindow(ev.vertex, win))) {
      const v = ev.vertex;
      const g = new THREE.BufferGeometry();
      g.setAttribute("position", new THREE.BufferAttribute(new Float32Array(
        [v.x * scale - this.offset.x, v.y * scale - this.offset.y, v.z * scale - this.offset.z]), 3));
      this.gVertex.add(new THREE.Points(g, new THREE.PointsMaterial(
        { color: COL.pinkLt, size: 11, sizeAttenuation: false })));
    }
    this.invalidate();
  }

  // Frame the current geometry box from a named direction.
  frame(view) {
    const c = this.box.getCenter(new THREE.Vector3());
    const size = this.box.getSize(new THREE.Vector3());
    const r = Math.max(size.x, size.y, size.z, 1) * 0.5;
    const d = r * 2.4;
    const dir = ({
      "3d": new THREE.Vector3(1, 0.7, 1),
      side: new THREE.Vector3(0, 1, 0.0001),   // look along y (xz plane)
      front: new THREE.Vector3(0.0001, 0, 1),  // look along z (xy, beam's-eye)
      top: new THREE.Vector3(0, 1, 0.0001),
    }[view] || new THREE.Vector3(1, 0.7, 1)).normalize();
    this.camera.position.copy(c).addScaledVector(dir, d);
    this.camera.up.set(0, view === "top" ? 0 : 1, view === "top" ? -1 : 0);
    this.camera.near = Math.max(d / 1000, 0.01);
    this.camera.far = d * 100;
    this.camera.updateProjectionMatrix();
    this.controls.target.copy(c);
    this.controls.update();
    this.invalidate();
  }

  invalidate() { this._needsRender = true; }

  resize() {
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    if (w && h && (this.canvas.width !== w || this.canvas.height !== h)) {
      this.renderer.setSize(w, h, false);
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
      this.invalidate();
    }
  }

  render() {
    this.resize();
    // OrbitControls damping keeps moving for a few frames after a drag; while it
    // is settling we must keep drawing, so ask it whether it still changed.
    const moved = this.controls.enableDamping ? this.controls.update() : false;
    if (moved) this.invalidate();
    if (!this._needsRender) return;
    this._needsRender = false;
    this.renderer.render(this.scene, this.camera);
  }

  // Free GPU resources and the WebGL context. Called when a floating panel is
  // closed, so contexts don't leak (browsers cap them at ~16).
  dispose() {
    this.clear(this.gGeo);
    this.clear(this.gHits);
    this.clear(this.gVertex);
    this.controls.dispose();
    this.renderer.dispose();
    this.renderer.forceContextLoss?.();
  }
}

// --- window helpers (mm) ---------------------------------------------------
// win is [ [lo,hi]|null, [lo,hi]|null, [lo,hi]|null ] for x,y,z.
function inWin1(v, w) { return !w || (v >= w[0] && v <= w[1]); }
function hitInWindow(h, win) {
  return inWin1(h.x, win[0]) && inWin1(h.y, win[1]) && inWin1(h.z, win[2]);
}
// keep a mesh if its centroid falls in every set axis window.
function meshInWindow(m, win) {
  let cx = 0, cy = 0, cz = 0;
  const n = m.vertices.length / 3;
  for (let i = 0; i < m.vertices.length; i += 3) {
    cx += m.vertices[i]; cy += m.vertices[i + 1]; cz += m.vertices[i + 2];
  }
  cx /= n; cy /= n; cz /= n;
  return inWin1(cx, win[0]) && inWin1(cy, win[1]) && inWin1(cz, win[2]);
}
// convert a manifest region's window object to the [x,y,z] array form.
function winFromRegion(rgn) {
  const w = rgn.window || {};
  const ax = (k) => (Array.isArray(w[k]) && w[k].length === 2 ? w[k] : null);
  return [ax("x"), ax("y"), ax("z")];
}

// --- app -------------------------------------------------------------------
const data = new DataSource(new URLSearchParams(location.search).get("data") || "data/");
let scale = 1 / 1000;
let current = 0;
let meshes = [];
let regions = [];

const main = new Panel("view-main");
const rpanels = [new Panel("view-r0"), new Panel("view-r1"), new Panel("view-r2")];

async function gotoEvent(i) {
  const n = data.nEvents;
  if (n <= 0) return;
  current = ((i % n) + n) % n;
  let ev;
  try {
    ev = await data.loadEvent(current);
  } catch (e) {
    setStatus(`Event ${current} failed:<br /><code>${e.message}</code>`);
    return;
  }
  main.setEvent(ev, scale, null);
  regions.forEach((rgn, k) => rpanels[k].setEvent(ev, scale, winFromRegion(rgn)));
  lastEvent = ev;
  floats.forEach((f) => f.panel.setEvent(ev, scale, null));

  $("evNum").textContent = String(ev.event ?? current);
  $("nHits").textContent = String((ev.hits || []).length);
  if (ev.hits && ev.hits.length) {
    let lo = Infinity, hi = -Infinity;
    for (const h of ev.hits) { if (h.z < lo) lo = h.z; if (h.z > hi) hi = h.z; }
    $("zRange").textContent = `${lo.toFixed(0)}…${hi.toFixed(0)}`;
  } else {
    $("zRange").textContent = "–";
  }
}

// toolbar
$("prev").addEventListener("click", () => gotoEvent(current - 1));
$("next").addEventListener("click", () => gotoEvent(current + 1));
window.addEventListener("keydown", (e) => {
  if (e.key === "ArrowLeft") gotoEvent(current - 1);
  if (e.key === "ArrowRight") gotoEvent(current + 1);
});
for (const b of document.querySelectorAll("[data-cam]")) {
  b.addEventListener("click", () => {
    main.frame(b.dataset.cam);
    document.querySelectorAll("[data-cam]").forEach((o) => o.classList.toggle("is-active", o === b));
  });
}
const setVis = (grp, on) => {
  main[grp].visible = on; main.invalidate();
  rpanels.forEach((p) => { p[grp].visible = on; p.invalidate(); });
};
$("tGeo").addEventListener("change", (e) => setVis("gGeo", e.target.checked));
$("tHits").addEventListener("change", (e) => setVis("gHits", e.target.checked));
$("tVertex").addEventListener("change", (e) => setVis("gVertex", e.target.checked));

// --- floating user-created views (stage 1: create / move / resize / rename /
//     close). Each is a full-detector view for now; restricting it to a drawn
//     region comes in a later stage. ------------------------------------------
const floats = [];       // { panel, el, name } for each floating view
let floatSeq = 0;        // names view_0, view_1, ...
let lastEvent = null;    // remember the current event to seed new panels

function bringToFront(el) {
  let z = 10;
  for (const f of floats) z = Math.max(z, parseInt(f.el.style.zIndex || "10", 10));
  el.style.zIndex = String(z + 1);
}

function createFloatingView() {
  const layer = $("float-layer");
  const el = document.createElement("section");
  el.className = "fpanel";
  const off = 40 + (floats.length % 6) * 26;  // cascade so they don't overlap exactly
  el.style.left = off + "px";
  el.style.top = off + "px";
  el.style.width = "360px";
  el.style.height = "260px";

  const name = `view_${floatSeq++}`;
  const bar = document.createElement("div");
  bar.className = "fpanel__bar";
  const title = document.createElement("span");
  title.className = "fpanel__title";
  title.title = "double-click to rename";
  title.textContent = name;
  const close = document.createElement("button");
  close.className = "fpanel__close";
  close.title = "Close view";
  close.innerHTML = "&#215;";
  bar.append(title, close);
  const canvas = document.createElement("canvas");
  canvas.className = "fpanel__canvas";
  el.append(bar, canvas);
  layer.appendChild(el);

  const panel = new Panel(canvas);
  panel.setGeometry(meshes, scale, null);   // full detector for now
  panel.frame("3d");
  if (lastEvent) panel.setEvent(lastEvent, scale, null);
  const entry = { panel, el, name };
  floats.push(entry);

  // Move by dragging the title bar.
  bar.addEventListener("pointerdown", (e) => {
    if (e.target === close || e.target.tagName === "INPUT") return;
    bringToFront(el);
    const sx = e.clientX, sy = e.clientY, ox = el.offsetLeft, oy = el.offsetTop;
    const move = (ev) => {
      el.style.left = Math.max(0, ox + (ev.clientX - sx)) + "px";
      el.style.top = Math.max(0, oy + (ev.clientY - sy)) + "px";
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  });

  // Double-click title to rename.
  title.addEventListener("dblclick", () => {
    const input = document.createElement("input");
    input.className = "fpanel__rename";
    input.value = entry.name;
    title.replaceWith(input);
    input.focus();
    input.select();
    input.addEventListener("keydown", (ev) => { if (ev.key === "Enter") input.blur(); });
    input.addEventListener("blur", () => {
      entry.name = input.value.trim() || entry.name;
      title.textContent = entry.name;
      input.replaceWith(title);
    });
  });

  // Close: dispose the GL context so we don't leak (browsers cap contexts).
  close.addEventListener("click", () => {
    const i = floats.indexOf(entry);
    if (i >= 0) floats.splice(i, 1);
    panel.dispose();
    el.remove();
  });

  bringToFront(el);
  return entry;
}

const newViewBtn = $("newView");
if (newViewBtn) newViewBtn.addEventListener("click", createFloatingView);

// render loop
function tick() {
  main.render();
  rpanels.forEach((p) => p.render());
  floats.forEach((f) => f.panel.render());
  requestAnimationFrame(tick);
}

(async function boot() {
  try {
    setStatus("Loading…");
    await data.loadManifest();
    scale = 1 / data.mmPerScene;
    regions = data.regions.slice(0, 3);
    $("evMax").textContent = String(data.nEvents - 1);

    // region panel labels from the config
    regions.forEach((rgn, k) => { const el = $(`lbl-r${k}`); if (el && rgn.name) el.textContent = rgn.name; });

    meshes = await data.loadGeometry();
    main.setGeometry(meshes, scale, null);
    main.frame("3d");
    document.querySelector('[data-cam="3d"]').classList.add("is-active");

    // region geometry + a camera framing per region
    const camFor = (c) => (c === "front" || c === "xy" ? "front" : c === "top" || c === "xy0" ? "top" : "side");
    regions.forEach((rgn, k) => {
      const win = winFromRegion(rgn);
      rpanels[k].setGeometry(meshes, scale, win);
      rpanels[k].frame(camFor(rgn.camera));
    });

    await gotoEvent(0);
    setStatus("");
    tick();
  } catch (e) {
    setStatus(`No display data under <code>${data.base}</code>. Produce it, then reload.<br /><code>${e.message}</code>`);
    tick();
  }
})();

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
    // Per-panel display options (independent of other panels). A new view
    // inherits these from the view it was created from.
    this.opts = { geo: true, hits: true, vertex: true, hitSize: 4 };

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

  // Build geometry from precomputed style buckets (see computeGeometry). Merging
  // ~20k meshes into a few BufferGeometries is what keeps draw calls low; doing
  // that merge (and the normal computation) ONCE and sharing the CPU arrays
  // across panels is what makes opening a new full-detector view fast.
  setGeometry(precomp) {
    this.clear(this.gGeo);
    this.offset.set(precomp.offset[0], precomp.offset[1], precomp.offset[2]);
    for (const bk of precomp.buckets) {
      const geom = new THREE.BufferGeometry();
      geom.setAttribute("position", new THREE.BufferAttribute(bk.positions, 3));
      geom.setAttribute("normal", new THREE.BufferAttribute(bk.normals, 3));
      geom.setIndex(new THREE.BufferAttribute(bk.indices, 1));
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
    this.box = precomp.box;
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
        { color: COL.pink, size: this.opts.hitSize, sizeAttenuation: false })));
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

  // Apply this panel's display options (visibility + hit size).
  applyOpts() {
    this.gGeo.visible = this.opts.geo;
    this.gHits.visible = this.opts.hits;
    this.gVertex.visible = this.opts.vertex;
    this.setHitSize(this.opts.hitSize);
    this.invalidate();
  }

  // Update hit marker size live, without rebuilding the event.
  setHitSize(px) {
    this.opts.hitSize = px;
    for (const pts of this.gHits.children) { pts.material.size = px; }
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
    // Compare against the CSS size we last applied, NOT canvas.width: with
    // setPixelRatio(pr) the backing store is pr*w, so `canvas.width !== w` is
    // always true and would resize+render every frame, pegging the CPU/GPU.
    if (w && h && (w !== this._cssW || h !== this._cssH)) {
      this._cssW = w;
      this._cssH = h;
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
  // Capture / restore camera + orbit target, so a saved setup reproduces the
  // exact framing of each view (including manual orbits, not just presets).
  getCameraState() {
    return {
      pos: this.camera.position.toArray(),
      target: this.controls.target.toArray(),
      up: this.camera.up.toArray(),
    };
  }
  setCameraState(s) {
    if (s.up) this.camera.up.fromArray(s.up);
    if (s.pos) this.camera.position.fromArray(s.pos);
    if (s.target) this.controls.target.fromArray(s.target);
    this.camera.updateProjectionMatrix();
    this.controls.update();
    this.invalidate();
  }

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
// Keep a mesh only if its centroid is inside every set axis window AND its
// extent along each windowed axis doesn't vastly exceed that window. The extent
// test is what stops a big mother envelope (e.g. the decay vessel), whose
// centroid sits at the vessel centre, from being pulled in whole when you box a
// small region there -- the same "oversized" rejection the REve path uses.
function meshInWindow(m, win) {
  let cx = 0, cy = 0, cz = 0;
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  const n = m.vertices.length / 3;
  for (let i = 0; i < m.vertices.length; i += 3) {
    const x = m.vertices[i], y = m.vertices[i + 1], z = m.vertices[i + 2];
    cx += x; cy += y; cz += z;
    if (x < lo[0]) lo[0] = x; if (x > hi[0]) hi[0] = x;
    if (y < lo[1]) lo[1] = y; if (y > hi[1]) hi[1] = y;
    if (z < lo[2]) lo[2] = z; if (z > hi[2]) hi[2] = z;
  }
  const c = [cx / n, cy / n, cz / n];
  for (let a = 0; a < 3; a++) {
    const w = win[a];
    if (!w) continue;                              // axis unconstrained
    if (c[a] < w[0] || c[a] > w[1]) return false;  // centroid outside
    const winLen = w[1] - w[0];
    const meshLen = hi[a] - lo[a];
    // Reject volumes much larger than the window on a constrained axis.
    if (meshLen > 1.5 * winLen + 1e-6) return false;
  }
  return true;
}
// convert a manifest region's window object to the [x,y,z] array form.
function winFromRegion(rgn) {
  const w = rgn.window || {};
  const ax = (k) => (Array.isArray(w[k]) && w[k].length === 2 ? w[k] : null);
  return [ax("x"), ax("y"), ax("z")];
}

// Merge raw producer meshes (mm) into a few style buckets, compute normals once,
// and return {buckets, box, offset} with SHARED typed arrays. The result is
// reusable across panels (each still uploads to its own GL context, but the CPU
// merge and normal computation -- the slow part of opening a view -- happen just
// once). `win` filters + recentres; null => full detector.
const _fullGeomCache = { key: null, value: null };
function computeGeometry(meshes, scale, win) {
  // Full-detector (null window) is by far the common, expensive case: cache it.
  if (!win && _fullGeomCache.value) return _fullGeomCache.value;

  const offset = [0, 0, 0];
  if (win) for (const ax of [0, 1, 2]) if (win[ax]) offset[ax] = 0.5 * (win[ax][0] + win[ax][1]) * scale;

  const buckets = new Map();
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
      const x = m.vertices[i] * scale - offset[0];
      const y = m.vertices[i + 1] * scale - offset[1];
      const z = m.vertices[i + 2] * scale - offset[2];
      bk.pos.push(x, y, z);
      if (x < box.min.x) box.min.x = x; if (x > box.max.x) box.max.x = x;
      if (y < box.min.y) box.min.y = y; if (y > box.max.y) box.max.y = y;
      if (z < box.min.z) box.min.z = z; if (z > box.max.z) box.max.z = z;
    }
    for (let i = 0; i < m.indices.length; i++) bk.idx.push(m.indices[i] + base);
  }

  // Finalise each bucket: typed arrays + normals (computed once via a temp geom).
  const out = [];
  for (const bk of buckets.values()) {
    if (!bk.pos.length) continue;
    const positions = new Float32Array(bk.pos);
    const indices = positions.length / 3 > 65535 ? new Uint32Array(bk.idx) : new Uint16Array(bk.idx);
    const tmp = new THREE.BufferGeometry();
    tmp.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    tmp.setIndex(new THREE.BufferAttribute(indices, 1));
    tmp.computeVertexNormals();
    const normals = tmp.getAttribute("normal").array;
    tmp.dispose();
    out.push({ color: bk.color, transparency: bk.transparency, positions, normals, indices });
  }

  const result = {
    buckets: out,
    offset,
    box: box.isEmpty()
      ? new THREE.Box3(new THREE.Vector3(-1, -1, -1), new THREE.Vector3(1, 1, 1))
      : box,
  };
  if (!win) _fullGeomCache.value = result;
  return result;
}

// --- app -------------------------------------------------------------------
const data = new DataSource(new URLSearchParams(location.search).get("data") || "data/");
let scale = 1 / 1000;
let current = 0;
let meshes = [];
let hitSize = 4;   // hit marker size (px), set from the sidebar

const main = new Panel("view-main");

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
  lastEvent = ev;
  floats.forEach((f) => f.panel.setEvent(ev, scale, f.win || null));

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
// --- font scale + categories + key bindings --------------------------------
// Global multiplier on every category (set from [ui] font_scale / +/-).
let fontScale = 1;
function setFontScale(s) {
  fontScale = Math.min(3, Math.max(0.4, s));
  document.documentElement.style.setProperty("--font-scale", String(fontScale));
  fitPanels();
}

// Text categories: each maps a set of elements to a CSS size variable, so one
// value resizes the whole category. Keys match [ui.fonts] in the view TOML.
const FONT_CATEGORIES = [
  { key: "window_title", label: "window titles",
    sel: ".panel__label,.fpanel__title,.fpanel__rename" },
  { key: "menu", label: "menu text",
    sel: ".brand__ver,.counter,.readout dt,.readout dd,.toggle,.side__foot,.slider,.btn,.hint" },
  { key: "heading", label: "headings", sel: ".ctl__h,.popout__title,.help h3" },
  { key: "dialog", label: "dialogs & menus", sel: ".ctxmenu__item,.field,.help table,.help p" },
  { key: "brand", label: "logo text", sel: ".brand__name" },
];
function categoryOf(el) {
  for (const c of FONT_CATEGORIES) if (el.matches && el.matches(c.sel)) return c;
  return null;
}
function categorySize(key) {
  const v = getComputedStyle(document.documentElement).getPropertyValue("--fs-" + key);
  return Math.round(parseFloat(v)) || 13;
}
function setCategorySize(key, px) {
  document.documentElement.style.setProperty("--fs-" + key, Math.max(6, px) + "px");
  fitPanels();
}

// A numeric font-size dialog: set just this element, or all of its category.
// rgb(...) -> #rrggbb for the native colour input.
function rgbToHex(rgb) {
  const m = /rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(rgb || "");
  if (!m) return "#f1debc";
  const h = (n) => Number(n).toString(16).padStart(2, "0");
  return "#" + h(m[1]) + h(m[2]) + h(m[3]);
}
// Set the text colour of every element in a category (live). New elements added
// later won't inherit it -- colour tweaks are a live, throwaway convenience.
function setCategoryColor(cat, hex) {
  document.querySelectorAll(cat.sel).forEach((e) => { e.style.color = hex; });
}

// Right-click a text label -> set its font size AND colour, for this element
// alone or for its whole category.
function showTextDialog(el) {
  const cat = categoryOf(el);
  const curSize = Math.round(parseFloat(getComputedStyle(el).fontSize)) || 13;
  const curHex = rgbToHex(getComputedStyle(el).color);
  showPopout("Text style", (body, close) => {
    const catSizeRow = cat
      ? `<label class="field">all ${cat.label} <input id="tdCatSize" type="number" min="6" max="72" value="${categorySize(cat.key)}"></label>` +
        `<div class="popout__actions"><button id="tdCatSizeSet" class="btn">Set all ${cat.label}</button></div>`
      : "";
    const catColRow = cat
      ? `<label class="field">all ${cat.label} <input id="tdCatCol" type="text" value="${curHex}" spellcheck="false"></label>` +
        `<div class="popout__actions"><button id="tdCatColSet" class="btn">Set all ${cat.label}</button></div>`
      : "";
    body.innerHTML =
      `<h4 class="popout__sub">Size (px)</h4>` +
      `<label class="field">this element <input id="tdSize" type="number" min="6" max="72" value="${curSize}"></label>` +
      `<div class="popout__actions"><button id="tdSizeSet" class="btn">Set element</button></div>` +
      catSizeRow +
      `<h4 class="popout__sub">Colour</h4>` +
      `<input id="tdPick" type="color" value="${curHex}" class="cpick" />` +
      `<label class="field">this element <input id="tdCol" type="text" value="${curHex}" spellcheck="false"></label>` +
      `<div class="popout__actions"><button id="tdColSet" class="btn">Set element</button></div>` +
      catColRow +
      `<div class="popout__actions"><button id="tdDone" class="btn btn--ghost">Done</button></div>`;

    // size
    body.querySelector("#tdSizeSet").addEventListener("click", () => {
      el.style.fontSize = Math.max(6, Number(body.querySelector("#tdSize").value) || curSize) + "px";
      fitPanels();
    });
    if (cat) body.querySelector("#tdCatSizeSet").addEventListener("click", () => {
      setCategorySize(cat.key, Math.max(6, Number(body.querySelector("#tdCatSize").value) || categorySize(cat.key)));
    });
    // colour: keep the picker and the hex field in sync
    const pick = body.querySelector("#tdPick");
    const col = body.querySelector("#tdCol");
    pick.addEventListener("input", () => { col.value = pick.value; });
    body.querySelector("#tdColSet").addEventListener("click", () => {
      const v = col.value.trim() || curHex;
      el.style.color = v;
    });
    if (cat) body.querySelector("#tdCatColSet").addEventListener("click", () => {
      const v = body.querySelector("#tdCatCol").value.trim() || curHex;
      setCategoryColor(cat, v);
    });
    body.querySelector("#tdDone").addEventListener("click", close);
  });
}

// Grow floating panels so enlarged text isn't clipped: if content is taller
// than the panel, bump the panel height to fit.
function fitPanels() {
  for (const f of floats) {
    const need = f.el.scrollHeight;
    if (need > f.el.clientHeight) f.el.style.height = need + "px";
  }
}

function typing(e) {
  const t = e.target;
  return t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
}

window.addEventListener("keydown", (e) => {
  // Let the browser keep its native shortcuts (Ctrl/Cmd +/- zoom, etc.) and
  // ignore keys while typing.
  if (e.ctrlKey || e.metaKey || e.altKey || typing(e)) return;
  const t = () => (selectedView ? selectedView.panel : main);
  switch (e.key) {
    case "ArrowLeft":  gotoEvent(current - 1); break;
    case "ArrowRight": gotoEvent(current + 1); break;
    case "n": createFloatingView(); break;
    case "3": t().frame("3d"); break;
    case "s": t().frame("side"); break;
    case "f": t().frame("front"); break;
    case "t": t().frame("top"); break;
    case "g": { const p = targetPanel(); p.opts.geo = !p.opts.geo; p.applyOpts(); syncControls(p); break; }
    case "h": { const p = targetPanel(); p.opts.hits = !p.opts.hits; p.applyOpts(); syncControls(p); break; }
    case "v": { const p = targetPanel(); p.opts.vertex = !p.opts.vertex; p.applyOpts(); syncControls(p); break; }
    case "+": case "=": setFontScale(fontScale + 0.1); break;
    case "-": case "_": setFontScale(fontScale - 0.1); break;
    case "?": toggleHelp(); break;
    default: return;
  }
  e.preventDefault();
});

// Right-click a text label -> the numeric font-size dialog for that element /
// its category. Capture phase so it takes precedence over the panel menu, but
// only for recognised text; everything else falls through.
const TEXT_SELECTOR =
  ".ctl__h, .toggle, .counter, .readout dt, .readout dd, .brand__name, .brand__ver, " +
  ".panel__label, .fpanel__title, .slider, .side__foot, .btn";
document.addEventListener("contextmenu", (e) => {
  const el = e.target.closest(TEXT_SELECTOR);
  if (!el) return;
  e.preventDefault();
  e.stopPropagation();
  showTextDialog(el);
}, true);

// Keyboard-shortcut help overlay, toggled with "?".
function toggleHelp() {
  const existing = $("help");
  if (existing) { existing.remove(); return; }
  const box = document.createElement("div");
  box.id = "help";
  box.className = "help";
  box.innerHTML =
    "<h3>Keyboard shortcuts</h3><table>" +
    "<tr><td>&#8592; / &#8594;</td><td>previous / next event</td></tr>" +
    "<tr><td>n</td><td>new view</td></tr>" +
    "<tr><td>3 / s / f / t</td><td>camera: 3D / side / front / top</td></tr>" +
    "<tr><td>g / h / v</td><td>toggle geometry / hits / vertex</td></tr>" +
    "<tr><td>+ / -</td><td>all text larger / smaller</td></tr>" +
    "<tr><td>Ctrl +/-</td><td>browser zoom (native)</td></tr>" +
    "<tr><td>?</td><td>this help</td></tr></table>" +
    "<p>Right-click a label to set its font size (or its whole category).</p>";
  box.addEventListener("click", () => box.remove());
  document.body.appendChild(box);
}
for (const b of document.querySelectorAll("[data-cam]")) {
  b.addEventListener("click", () => {
    // Reorient the selected view if there is one, otherwise the main view.
    const target = selectedView ? selectedView.panel : main;
    target.frame(b.dataset.cam);
    document.querySelectorAll("[data-cam]").forEach((o) => o.classList.toggle("is-active", o === b));
  });
}
// toggle wiring is set up below, after the selection helpers are defined.

// --- selection: one floating view can be "selected"; the Main-camera buttons
//     then reorient it, and "Select view location" assigns it a region. -------
let selectedView = null; // a floats[] entry, or null (=> operate on main)

// The panel the sidebar controls act on: the selected view, else the main view.
function targetPanel() { return selectedView ? selectedView.panel : main; }

// Reflect a panel's options in the sidebar controls.
function syncControls(panel) {
  $("tGeo").checked = panel.opts.geo;
  $("tHits").checked = panel.opts.hits;
  $("tVertex").checked = panel.opts.vertex;
  $("hitSize").value = String(panel.opts.hitSize);
  $("hitSizeVal").textContent = String(panel.opts.hitSize);
}

function selectView(entry) {
  selectedView = entry;
  for (const f of floats) f.el.classList.toggle("is-selected", f === entry);
  syncControls(entry ? entry.panel : main);
}
function clearSelectionIfGone() {
  if (selectedView && !floats.includes(selectedView)) { selectedView = null; syncControls(main); }
}

// Show/hide toggles act on the current target panel and are stored per-panel.
function wireToggle(id, key) {
  $(id).addEventListener("change", (e) => {
    const p = targetPanel();
    p.opts[key] = e.target.checked;
    p.applyOpts();
  });
}
wireToggle("tGeo", "geo");
wireToggle("tHits", "hits");
wireToggle("tVertex", "vertex");

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

function createFloatingView(opts = {}) {
  const win = opts.win || null;
  // Inherit display options from the source panel (the one this view was
  // created from), else the current target. This is what carries geometry/hits/
  // vertex visibility and hit size into the new view.
  const source = opts.source || targetPanel();

  const layer = $("float-layer");
  const el = document.createElement("section");
  el.className = "fpanel";
  const off = 4 + (floats.length % 6) * 3;  // % cascade so they don't overlap exactly
  el.style.left = off + "%";
  el.style.top = off + "%";
  el.style.width = "28%";
  el.style.height = "34%";

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
  panel.opts = { ...source.opts };                          // inherit options
  panel.setGeometry(computeGeometry(meshes, scale, win));   // full detector, or a window
  panel.frame("3d");
  if (lastEvent) panel.setEvent(lastEvent, scale, win);
  panel.applyOpts();                                        // apply inherited visibility/size
  const entry = { panel, el, name, win, locked: false, borderColor: null };
  floats.push(entry);
  attachPick(panel);           // allow drawing a sub-region on this view
  selectView(entry);           // newly created view becomes the selected one

  // Selecting: pressing the bar (not the close button / rename field) selects
  // this view so the camera buttons and "Select view location" target it.
  el.addEventListener("pointerdown", () => selectView(entry), true);

  // Move by dragging the title bar (disabled when the view is locked).
  bar.addEventListener("pointerdown", (e) => {
    if (entry.locked || e.target === close || e.target.tagName === "INPUT") return;
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
    clearSelectionIfGone();
  });

  bringToFront(el);
  // Right-click: context menu. Unlocked -> full menu; locked -> just Unlock
  // (and colours), since a locked view is meant to sit still in a display.
  el.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    selectView(entry);
    if (entry.locked) {
      showMenu(e.clientX, e.clientY, [
        { label: "Unlock window", onClick: () => setLocked(entry, false) },
        { label: "Set boundary colours…", onClick: () => showColourDialog(entry) },
      ]);
    } else {
      showMenu(e.clientX, e.clientY, [
        { label: "Resize numerically…", onClick: () => showResizeDialog(entry) },
        { separator: true },
        { label: "Stack left", onClick: () => stackWindow(entry, "left") },
        { label: "Stack right", onClick: () => stackWindow(entry, "right") },
        { label: "Stack top", onClick: () => stackWindow(entry, "top") },
        { label: "Stack bottom", onClick: () => stackWindow(entry, "bottom") },
        { separator: true },
        { label: "Lock window", onClick: () => setLocked(entry, true) },
        { label: "Set boundary colours…", onClick: () => showColourDialog(entry) },
      ]);
    }
  });

  return entry;
}

// --- view context-menu actions --------------------------------------------

// A lightweight context menu. `items` is a list of {label, onClick} and
// {separator:true}. Closes on the next click anywhere or on Escape.
function showMenu(x, y, items) {
  closeMenu();
  const menu = document.createElement("div");
  menu.className = "ctxmenu";
  menu.id = "ctxmenu";
  for (const it of items) {
    if (it.separator) {
      const hr = document.createElement("div");
      hr.className = "ctxmenu__sep";
      menu.appendChild(hr);
      continue;
    }
    const b = document.createElement("button");
    b.className = "ctxmenu__item";
    b.textContent = it.label;
    b.addEventListener("click", () => { closeMenu(); it.onClick(); });
    menu.appendChild(b);
  }
  document.body.appendChild(menu);
  // Keep it on-screen.
  const r = menu.getBoundingClientRect();
  menu.style.left = Math.min(x, window.innerWidth - r.width - 4) + "px";
  menu.style.top = Math.min(y, window.innerHeight - r.height - 4) + "px";
  // Close on a pointerdown OUTSIDE the menu, or on Escape. A pointerdown inside
  // the menu is left alone so the item's click can fire.
  setTimeout(() => {
    window.addEventListener("pointerdown", outsideClose, true);
    window.addEventListener("keydown", escClose);
  }, 0);
}
function outsideClose(e) {
  const m = $("ctxmenu");
  if (m && m.contains(e.target)) return;
  closeMenu();
}
function closeMenu() {
  const m = $("ctxmenu");
  if (m) m.remove();
  window.removeEventListener("pointerdown", outsideClose, true);
  window.removeEventListener("keydown", escClose);
}
function escClose(e) { if (e.key === "Escape") closeMenu(); }

// A small centred popout dialog. `build(body, close)` fills the body; call
// close() to dismiss. Returns nothing.
function showPopout(title, build) {
  const overlay = document.createElement("div");
  overlay.className = "popout__overlay";
  const box = document.createElement("div");
  box.className = "popout";
  const h = document.createElement("h3");
  h.className = "popout__title";
  h.textContent = title;
  const body = document.createElement("div");
  box.append(h, body);
  overlay.appendChild(box);
  document.body.appendChild(overlay);
  const close = () => overlay.remove();
  overlay.addEventListener("pointerdown", (e) => { if (e.target === overlay) close(); });
  window.addEventListener("keydown", function esc(e) {
    if (e.key === "Escape") { close(); window.removeEventListener("keydown", esc); }
  });
  build(body, close);
}

// 1) Resize numerically.
function showResizeDialog(entry) {
  showPopout("Resize view (pixels)", (body, close) => {
    const w = Math.round(entry.el.offsetWidth);
    const h = Math.round(entry.el.offsetHeight);
    body.innerHTML =
      `<label class="field">width <input id="rw" type="number" min="120" value="${w}"></label>` +
      `<label class="field">height <input id="rh" type="number" min="100" value="${h}"></label>` +
      `<div class="popout__actions"><button id="rok" class="btn">Apply</button>` +
      `<button id="rcancel" class="btn btn--ghost">Cancel</button></div>`;
    body.querySelector("#rcancel").addEventListener("click", close);
    body.querySelector("#rok").addEventListener("click", () => {
      const nw = Math.max(120, Number(body.querySelector("#rw").value) || w);
      const nh = Math.max(100, Number(body.querySelector("#rh").value) || h);
      entry.el.style.width = nw + "px";
      entry.el.style.height = nh + "px";
      close();
    });
  });
}

// 2) Stack to an edge, stopping on collision with another view.
function rectOf(el) {
  return { l: el.offsetLeft, t: el.offsetTop, w: el.offsetWidth, h: el.offsetHeight };
}
function stackWindow(entry, dir) {
  const me = rectOf(entry.el);
  const others = floats.filter((f) => f !== entry).map((f) => rectOf(f.el));
  const W = window.innerWidth, H = window.innerHeight;
  const vOverlap = (o) => me.t < o.t + o.h && o.t < me.t + me.h;   // share y-range
  const hOverlap = (o) => me.l < o.l + o.w && o.l < me.l + me.w;   // share x-range

  if (dir === "left") {
    let x = 0;
    for (const o of others) if (vOverlap(o) && o.l + o.w <= me.l) x = Math.max(x, o.l + o.w);
    entry.el.style.left = x + "px";
  } else if (dir === "right") {
    let x = W - me.w;
    for (const o of others) if (vOverlap(o) && o.l >= me.l + me.w) x = Math.min(x, o.l - me.w);
    entry.el.style.left = Math.max(0, x) + "px";
  } else if (dir === "top") {
    let y = 0;
    for (const o of others) if (hOverlap(o) && o.t + o.h <= me.t) y = Math.max(y, o.t + o.h);
    entry.el.style.top = y + "px";
  } else if (dir === "bottom") {
    let y = H - me.h;
    for (const o of others) if (hOverlap(o) && o.t >= me.t + me.h) y = Math.min(y, o.t - me.h);
    entry.el.style.top = Math.max(0, y) + "px";
  }
}

// 3) Lock / unlock: hide the chrome for a clean display, freeze position.
function setLocked(entry, locked) {
  entry.locked = locked;
  entry.el.classList.toggle("is-locked", locked);
  applyBorder(entry);
}

// 4) Boundary colours.
function applyBorder(entry) {
  // The border colour persists across lock/unlock; null => stylesheet default.
  entry.el.style.borderColor = entry.borderColor || "";
}
function showColourDialog(entry) {
  showPopout("Boundary colour", (body, close) => {
    const cur = entry.borderColor || "#e3a93c";
    body.innerHTML =
      `<input id="cpick" type="color" value="${toHex6(cur)}" class="cpick" />` +
      `<label class="field">hex <input id="chex" type="text" value="${cur}" spellcheck="false"></label>` +
      `<div class="popout__actions">` +
      `<button id="cdefault" class="btn btn--ghost">Restore default</button>` +
      `<button id="cok" class="btn">Done</button></div>`;
    const pick = body.querySelector("#cpick");
    const hex = body.querySelector("#chex");
    const apply = (v) => { entry.borderColor = v; applyBorder(entry); };
    pick.addEventListener("input", () => { hex.value = pick.value; apply(pick.value); });
    hex.addEventListener("input", () => {
      const v = hex.value.trim();
      if (/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(v)) { pick.value = toHex6(v); apply(v); }
    });
    body.querySelector("#cdefault").addEventListener("click", () => {
      entry.borderColor = null; applyBorder(entry); close();
    });
    body.querySelector("#cok").addEventListener("click", close);
  });
}
// Normalise #rgb / #rrggbb to #rrggbb for the native colour input.
function toHex6(v) {
  const m = /^#([0-9a-fA-F]{3})$/.exec(v);
  if (m) return "#" + m[1].split("").map((c) => c + c).join("");
  return /^#[0-9a-fA-F]{6}$/.test(v) ? v : "#e3a93c";
}

const newViewBtn = $("newView");
if (newViewBtn) newViewBtn.addEventListener("click", () => createFloatingView());

// --- persistence: save / load the whole setup (views, windows, cameras,
//     options, current event) to and from a JSON file on disk. -------------
const SETUP_VERSION = 1;

function currentSetup() {
  return {
    version: SETUP_VERSION,
    // The event is intentionally NOT saved: a setup describes the view layout,
    // which should apply to whatever event/data is currently loaded. Baking in
    // an event index makes setups brittle across data files.
    main: { opts: { ...main.opts }, camera: main.getCameraState() },
    views: floats.map((f) => ({
      name: f.name,
      // Use the rendered pixel box, not el.style.*: the native corner-resize
      // handle doesn't reliably write back to inline style, so style.width/
      // height can be empty/stale. offset* is always the true current geometry,
      // so this captures both dragged position and resized size.
      rect: {
        left: f.el.offsetLeft + "px",
        top: f.el.offsetTop + "px",
        width: f.el.offsetWidth + "px",
        height: f.el.offsetHeight + "px",
      },
      win: f.win,
      opts: { ...f.panel.opts },
      camera: f.panel.getCameraState(),
      locked: f.locked,
      borderColor: f.borderColor,
    })),
  };
}

// Apply a camera spec that is either a preset name ("3d"/"side"/"front"/"top")
// or a saved {pos,target,up} state.
function applyCamera(panel, cam) {
  if (!cam) return;
  if (typeof cam === "string") panel.frame(cam);
  else panel.setCameraState(cam);
}

async function saveSetup() {
  const text = JSON.stringify(currentSetup(), null, 2);
  // Persist to the authoritative config via the dev server, so a reload shows
  // this layout. If the server can't accept the write (e.g. a plain static
  // server), fall back to downloading the file for manual placement.
  try {
    const r = await fetch("/api/save-setup", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: text,
    });
    if (r.ok) {
      setStatus("Saved as default. Reload keeps this layout.");
      setTimeout(() => setStatus(""), 2500);
      return;
    }
    throw new Error("HTTP " + r.status);
  } catch (_) {
    const blob = new Blob([text], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "sea_cucumber_setup.json";
    a.click();
    URL.revokeObjectURL(url);
    setStatus("Server read-only — downloaded file; place it in configs/ to make it the default.");
  }
}

function loadSetup(setup) {
  if (!setup || !Array.isArray(setup.views)) { setStatus("Not a valid setup file."); return; }
  // Clear existing floating views.
  for (const f of [...floats]) { f.panel.dispose(); f.el.remove(); }
  floats.length = 0;
  selectedView = null;

  // Main view (layout only -- never the event).
  if (setup.main) {
    if (setup.main.opts) { main.opts = { ...main.opts, ...setup.main.opts }; main.applyOpts(); }
    applyCamera(main, setup.main.camera);
  }

  // Recreate each floating view with its window, then restore geometry,
  // options, position/size, and camera. The current event is kept as-is:
  // createFloatingView seeds each new view with the event already loaded.
  for (const v of setup.views) {
    const entry = createFloatingView({ win: v.win || null });
    if (v.name) {
      entry.name = v.name;
      const t = entry.el.querySelector(".fpanel__title");
      if (t) t.textContent = v.name;
    }
    if (v.rect) {
      if (v.rect.left) entry.el.style.left = v.rect.left;
      if (v.rect.top) entry.el.style.top = v.rect.top;
      if (v.rect.width) entry.el.style.width = v.rect.width;
      if (v.rect.height) entry.el.style.height = v.rect.height;
    }
    if (v.opts) { entry.panel.opts = { ...entry.panel.opts, ...v.opts }; entry.panel.applyOpts(); }
    applyCamera(entry.panel, v.camera);
    if (v.borderColor) { entry.borderColor = v.borderColor; applyBorder(entry); }
    if (v.locked) setLocked(entry, true);
  }

  selectView(null);   // deselect; sidebar targets main
  setStatus("");
}

const saveBtn = $("saveSetup");
if (saveBtn) saveBtn.addEventListener("click", saveSetup);
const loadBtn = $("loadSetup");
const loadFile = $("loadFile");
if (loadBtn && loadFile) {
  loadBtn.addEventListener("click", () => loadFile.click());
  loadFile.addEventListener("change", async (e) => {
    const file = e.target.files && e.target.files[0];
    if (!file) return;
    try {
      const text = await file.text();
      loadSetup(JSON.parse(text));
    } catch (err) {
      setStatus(`Could not load setup:<br /><code>${err.message}</code>`);
    }
    loadFile.value = "";  // allow re-loading the same file
  });
}

// Hit-size slider: update all panels' hit markers live (no event rebuild).
// Hit-size slider: acts on the current target panel (selected view, else main).
const hitSizeInput = $("hitSize");
if (hitSizeInput) {
  hitSizeInput.addEventListener("input", (e) => {
    const px = Number(e.target.value);
    const lbl = $("hitSizeVal");
    if (lbl) lbl.textContent = String(px);
    targetPanel().setHitSize(px);
  });
}

// --- "Select view location": in pick mode, drag a rectangle on ANY view (the
//     mother). A new child view is created showing that boxed region. The
//     source is whichever panel you drag on -- select the mother first so you
//     know which; dragging on the main view uses the full detector as source.
//     We unproject the rectangle onto a plane through that panel's scene centre,
//     take the two on-screen axes as the window, and leave the axis into the
//     screen unconstrained -- so orient the source view (Side/Front/Top) first.
let pickMode = false;
const pickBtn = $("selectLoc");
const rubber = $("rubber");

function setPickMode(on) {
  pickMode = on;
  if (pickBtn) pickBtn.classList.toggle("is-active", on);
  document.body.style.cursor = on ? "crosshair" : "";
}
if (pickBtn) pickBtn.addEventListener("click", () => setPickMode(!pickMode));

// Screen rectangle on `panel`'s canvas -> axis-aligned window (mm). Accounts for
// the panel's recentre offset, so drawing on an already-windowed view works.
function rectToWindow(panel, x0, y0, x1, y1) {
  const rect = panel.canvas.getBoundingClientRect();
  const cam = panel.camera;
  const ndc = (px, py) => new THREE.Vector2(
    ((px - rect.left) / rect.width) * 2 - 1,
    -(((py - rect.top) / rect.height) * 2 - 1)
  );
  const centre = panel.box.getCenter(new THREE.Vector3());
  const fwd = cam.getWorldDirection(new THREE.Vector3());
  const plane = new THREE.Plane().setFromNormalAndCoplanarPoint(fwd, centre);
  const ray = new THREE.Raycaster();
  const corners = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
  const pts = [];
  for (const [px, py] of corners) {
    ray.setFromCamera(ndc(px, py), cam);
    const hit = new THREE.Vector3();
    if (ray.ray.intersectPlane(plane, hit)) pts.push(hit);
  }
  if (pts.length < 4) return null;
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (const p of pts) for (let a = 0; a < 3; a++) {
    lo[a] = Math.min(lo[a], p.getComponent(a));
    hi[a] = Math.max(hi[a], p.getComponent(a));
  }
  const fabs = [Math.abs(fwd.x), Math.abs(fwd.y), Math.abs(fwd.z)];
  const depthAxis = fabs.indexOf(Math.max(...fabs));
  const off = [panel.offset.x, panel.offset.y, panel.offset.z];
  const win = [null, null, null];
  for (let a = 0; a < 3; a++) {
    if (a === depthAxis) continue;
    // panel world = mm*scale - offset  =>  mm = (world + offset)/scale
    win[a] = [(lo[a] + off[a]) / scale, (hi[a] + off[a]) / scale];
  }
  return win;
}

// Attach the region-drawing behaviour to a panel's canvas. Active only in pick
// mode; on release it creates a child view of the drawn region from THIS panel.
function attachPick(panel) {
  panel.canvas.addEventListener("pointerdown", (e) => {
    if (!pickMode) return;
    e.preventDefault();
    e.stopPropagation();
    const x0 = e.clientX, y0 = e.clientY;
    rubber.hidden = false;
    const draw = (x, y) => {
      rubber.style.left = Math.min(x0, x) + "px";
      rubber.style.top = Math.min(y0, y) + "px";
      rubber.style.width = Math.abs(x - x0) + "px";
      rubber.style.height = Math.abs(y - y0) + "px";
    };
    draw(x0, y0);
    const move = (ev) => draw(ev.clientX, ev.clientY);
    const up = (ev) => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      rubber.hidden = true;
      setPickMode(false);
      if (Math.abs(ev.clientX - x0) < 6 || Math.abs(ev.clientY - y0) < 6) return; // ignore clicks
      const win = rectToWindow(panel, x0, y0, ev.clientX, ev.clientY);
      if (!win) return;
      createFloatingView({ win, source: panel });   // child view of this region
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  }, true);
}

// The main view is a valid pick source too.
attachPick(main);
// Clicking the main view (outside pick mode) deselects any view, so the sidebar
// controls target the main view again.
main.canvas.addEventListener("pointerdown", () => { if (!pickMode) selectView(null); });

// render loop
function tick() {
  main.render();
  floats.forEach((f) => f.panel.render());
  requestAnimationFrame(tick);
}

(async function boot() {
  // Version: read the VERSION text file (served alongside the page). Shown under
  // the wordmark; silently omitted if the file is absent.
  try {
    const r = await fetch("VERSION", { cache: "no-store" });
    if (r.ok) { const v = (await r.text()).trim(); const el = $("version"); if (el && v) el.textContent = "v" + v; }
  } catch (_) { /* no version file */ }
  try {
    setStatus("Loading…");
    await data.loadManifest();
    scale = 1 / data.mmPerScene;
    if (data.ui && data.ui.font_scale) setFontScale(data.ui.font_scale);  // TOML [ui] font_scale
    if (data.ui && data.ui.sidebar_width) document.documentElement.style.setProperty("--side-w", data.ui.sidebar_width + "px");
    if (data.ui && data.ui.fonts) {
      for (const [k, v] of Object.entries(data.ui.fonts)) setCategorySize(k, v);  // [ui.fonts]
    }
    $("evMax").textContent = String(data.nEvents - 1);

    meshes = await data.loadGeometry();
    main.setGeometry(computeGeometry(meshes, scale, null));
    main.frame("3d");
    document.querySelector('[data-cam="3d"]').classList.add("is-active");

    await gotoEvent(0);

    // Build the region views from the manifest, which the producer fills from
    // views/default.toml (model A: the TOML is the single source of the web
    // layout -- windows, cameras, and panel position/size). No JSON setup, no
    // localStorage. Edit the [[region]] blocks in views/default.toml to change
    // the default arrangement.
    let cascade = 0;
    for (const rgn of data.regions) {
      const win = winFromRegion(rgn);
      const entry = createFloatingView({ win });
      if (rgn.name) {
        entry.name = rgn.name;
        const t = entry.el.querySelector(".fpanel__title");
        if (t) t.textContent = rgn.name;
      }
      // Panel geometry from the TOML (panel_x/y/w/h -> manifest .panel),
      // interpreted as VIEWPORT PERCENTAGES so a layout is resolution-
      // independent. Falls back to a percentage cascade when unset.
      const p = rgn.panel || {};
      const off = 4 + (cascade++ % 6) * 3;   // % cascade
      entry.el.style.left = (p.x != null ? p.x : off) + "%";
      entry.el.style.top = (p.y != null ? p.y : off) + "%";
      entry.el.style.width = (p.w != null ? p.w : 26) + "%";
      entry.el.style.height = (p.h != null ? p.h : 34) + "%";
      applyCamera(entry.panel, rgn.camera || "side");
    }
    selectView(null);

    setStatus("");
    tick();
  } catch (e) {
    setStatus(`No display data under <code>${data.base}</code>. Produce it, then reload.<br /><code>${e.message}</code>`);
    tick();
  }
})();

// --- draggable sidebar width ------------------------------------------------
(function () {
  const handle = $("sideResize");
  if (!handle) return;
  const MIN = 0, MAX = () => window.innerWidth;  // menu can be any width
  handle.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    handle.classList.add("is-dragging");
    const move = (ev) => {
      const w = Math.min(MAX(), Math.max(MIN, ev.clientX));
      document.documentElement.style.setProperty("--side-w", w + "px");
    };
    const up = () => {
      handle.classList.remove("is-dragging");
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  });
})();

// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// prefs.js -- appearance settings remembered in THIS browser.
//
// The producer bakes the config file's [ui] block into manifest.json (see
// UserConfig.h for how that file is found), but the browser and the machine
// that ran make_web_data need not be the same host, so a viewer cannot write
// to it. What a viewer chooses here therefore lives in localStorage, and is
// applied on top of the manifest at boot:
//
//   built-in defaults  <  view TOML [ui]  <  user config [ui]  <  this
//
// Layout is NOT stored here -- views, windows and cameras still come from the
// view config alone (see the note in main.js).
//
// Shape: { version: 1, scheme, font_scale, fonts: {category: px}, sidebar_width }

export const PREFS_KEY = "sea_cucumber.appearance.v1";
const PREFS_VERSION = 1;

// localStorage throws when storage is disabled or the page is in a private
// context, and a preference cache must never take the display down with it.
function storage() {
  try {
    return window.localStorage;
  } catch (_) {
    return null;
  }
}

export function loadPrefs() {
  const s = storage();
  if (!s) return {};
  try {
    const raw = s.getItem(PREFS_KEY);
    if (!raw) return {};
    const p = JSON.parse(raw);
    // A cache, not a document: on a version we don't know, start over rather
    // than carry migration code around.
    if (!p || typeof p !== "object" || p.version !== PREFS_VERSION) return {};
    // Anyone can edit localStorage by hand, so hand back only fields of the
    // right shape and drop the rest. This checks TYPES; the ranges belong to
    // the code that applies them (setFontScale clamps the scale), so the
    // bounds live in one place and cannot drift.
    const out = { version: PREFS_VERSION };
    if (typeof p.scheme === "string") out.scheme = p.scheme;   // main.js checks it against SCHEMES
    if (Number.isFinite(p.font_scale) && p.font_scale > 0) out.font_scale = p.font_scale;
    if (Number.isFinite(p.sidebar_width) && p.sidebar_width >= 0) out.sidebar_width = p.sidebar_width;
    if (p.fonts && typeof p.fonts === "object" && !Array.isArray(p.fonts)) {
      const fonts = {};
      for (const [k, v] of Object.entries(p.fonts)) if (Number.isFinite(v)) fonts[k] = v;
      if (Object.keys(fonts).length) out.fonts = fonts;
    }
    return out;
  } catch (_) {
    return {};
  }
}

export function hasPrefs() {
  const s = storage();
  try {
    return !!(s && s.getItem(PREFS_KEY));
  } catch (_) {
    return false;
  }
}

// Merge `patch` into what is already stored. `fonts` merges one level deeper so
// setting one category doesn't drop the others.
export function patchPrefs(patch) {
  const s = storage();
  if (!s) return false;
  const cur = loadPrefs();
  const next = { ...cur, ...patch, version: PREFS_VERSION };
  if (patch.fonts) next.fonts = { ...(cur.fonts || {}), ...patch.fonts };
  try {
    s.setItem(PREFS_KEY, JSON.stringify(next));
    return true;
  } catch (_) {
    return false;   // quota exceeded, or storage disabled mid-session
  }
}

// True when the browser no longer remembers anything; false when it still does,
// so a caller can say so rather than claim a revert that did not happen.
export function clearPrefs() {
  const s = storage();
  if (!s) return false;
  try {
    s.removeItem(PREFS_KEY);
    return true;
  } catch (_) {
    return false;   // storage disabled mid-session
  }
}

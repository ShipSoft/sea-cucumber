// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// schemes.js -- named colour schemes for the web display. Each scheme maps a
// small set of ROLES onto colours; applyScheme() (in main.js) writes those into
// the CSS variables, the 3D clear colour, the hit/vertex marker colours, and
// the detector geometry palette. Roles:
//
//   bg       main background (page, panels, 3D clear)
//   bg2      deeper wells (gaps, html background)
//   surface  raised surfaces (buttons, title bars)
//   text     foreground text
//   accent   headings / active state (the old "yellow" role)
//   hit      hit marker colour
//   vertex   decay-vertex marker colour
//   geometry array of detector shades, chosen by volume-name hash
//
// `line` (borders) and `dim` (muted text) are derived from `text` at low alpha.

export const SCHEMES = {
  // --- SHiP palette variants -------------------------------------------------
  // The detector palette uses EVERY palette colour except pink (reserved for
  // hits): navy, the blue range, the gold range, and cream. ship_original and
  // ship_db share it and differ only in background (brown vs dark blue). Pink
  // is the hit colour; light pink the vertex.
  ship_original: {
    label: "SHiP original",
    bg: "#34240f", bg2: "#241809", surface: "#45301a",
    text: "#f1debc", accent: "#e3a93c", hit: "#c64284", vertex: "#eda9c8",
    geometry: null,
  },
  ship_db: {
    label: "SHiP dark blue",
    bg: "#081b3c", bg2: "#050f22", surface: "#12305f",
    text: "#f1debc", accent: "#e3a93c", hit: "#c64284", vertex: "#eda9c8",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a93c", "#e8cfa0", "#f1debc"],
  },
  ship_db2: {
    label: "SHiP dark blue + og colors",
    bg: "#081b3c", bg2: "#050f22", surface: "#12305f",
    text: "#f1debc", accent: "#e3a93c", hit: "#c64284", vertex: "#eda9c8",
    geometry: null
  },
  ship_db3: {
    label: "SHiP dark blue + pink",
    bg: "#081b3c", bg2: "#050f22", surface: "#12305f",
    text: "#d3dae8", accent: "#e3a0c1", hit: "#f1debc", vertex: "#e3a93c",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a0c1", "#eec7db", "#f4d9e6"],
  },
  ship_db4: {
    label: "SHiP dark blue + yellow",
    bg: "#081b3c", bg2: "#050f22", surface: "#12305f",
    text: "#d3dae8", accent: "#e3a93c", hit: "#e3a0c1", vertex: "#c64284",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#ebcd94", "#f0d7aa", "#f1debc"],
  },
  ship_db5: {
    label: "SHiP dark dark blue + pink",
    bg: "#0a1528", bg2: "#081b3c", surface: "#12305f",
    text: "#d3dae8", accent: "#e3a0c1", hit: "#f1debc", vertex: "#e3a93c",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a0c1", "#eec7db", "#f4d9e6"],
  },
  ship_db6: {
    label: "SHiP dark dark blue + yellow",
    bg: "#0a1528", bg2: "#081b3c", surface: "#12305f",
    text: "#d3dae8", accent: "#e3a93c", hit: "#e3a0c1", vertex: "#c64284",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#ebcd94", "#f0d7aa", "#f1debc"],
  },
    ship_db7: {
    label: "SHiP dark dark blue monochrome",
    bg: "#0a1528", bg2: "#081b3c", surface: "#12305f",
    text: "#d3dae8", accent: "#8fa1c5", hit: "#e8ecf3", vertex: "#79eba",
    geometry: ["#20428a", "#365697", "#4b67a2", "#617aad",
               "#798eba", "#8fa1c5"],
  },
  ship_light: {
    label: "SHiP light",
    bg: "#ffffff", bg2: "#c0c0c0", surface: "#bbbbbb",
    text: "#232323", accent: "#e3a93c", hit: "#c64284", vertex: "#e3a0c1",
    geometry: ["#eeeeee", "#dddddd", "#cccccc", "#bbbbbb",
               "#aaaaaa", "#999999", "#424242"],
  },
  ship_ht: {
    // "Everything pink", using the palette's pink range: muted/dark pinks for
    // the detector, the brightest palette pink for hits (distinct), palest for
    // the vertex.
    label: "SHiP pink",
    bg: "#2b0f1e", bg2: "#1c0a14", surface: "#4a1730",
    text: "#f5d9e6", accent: "#e0359a", hit: "#e0359a", vertex: "#f5cfe0",
    geometry: ["#9e3569", "#b8497e", "#c64284", "#d96ba0", "#e79ac0", "#f0b6d2"],
  },

  // --- terminal-style schemes (bg/fg + a picked subset of the 16 colours) ----
  cobalt2: {
    label: "Cobalt2",
    bg: "#132738", bg2: "#0d1c29", surface: "#1c3a52",
    text: "#ffffff", accent: "#ffe50a", hit: "#ff005d", vertex: "#6ae3fa",
    geometry: ["#1460d2", "#5555ff", "#00bbbb", "#38de21", "#3bd01d", "#6ae3fa"],
  },
  apprentice: {
    label: "Apprentice",
    bg: "#262626", bg2: "#1c1c1c", surface: "#3a3a3a",
    text: "#bcbcbc", accent: "#ffffaf", hit: "#ff8700", vertex: "#5fafaf",
    geometry: ["#5f87af", "#87afd7", "#5f8787", "#5f875f", "#87af87", "#8787af"],
  },
  ayu_dark: {
    label: "Ayu Dark",
    bg: "#0b0e14", bg2: "#070910", surface: "#11151c",
    text: "#bfbdb6", accent: "#ffb454", hit: "#f07178", vertex: "#95e6cb",
    geometry: ["#53bdfa", "#59c2ff", "#90e1c6", "#7fd962", "#aad94c", "#cda1fa"],
  },
  dracula: {
    label: "Dracula",
    bg: "#282a36", bg2: "#21222c", surface: "#44475a",
    text: "#f8f8f2", accent: "#f1fa8c", hit: "#ff79c6", vertex: "#8be9fd",
    geometry: ["#bd93f9", "#d6acff", "#8be9fd", "#50fa7b", "#69ff94", "#ff92df"],
  },
  noctis: {
    label: "Noctis",
    bg: "#052529", bg2: "#03181b", surface: "#0d3a40",
    text: "#b2cacd", accent: "#e4b781", hit: "#e66533", vertex: "#49d6e9",
    geometry: ["#49ace9", "#60b6eb", "#49d6e9", "#49e9a6", "#60ebb1", "#df769b"],
  },
  shades_of_purple: {
    label: "Shades of Purple",
    bg: "#1e1d40", bg2: "#151430", surface: "#2c2a5c",
    text: "#ffffff", accent: "#ffe700", hit: "#ff2c70", vertex: "#79e8fb",
    geometry: ["#6943ff", "#6871ff", "#00c5c7", "#3ad900", "#43d426", "#ff77ff"],
  },
};

export const DEFAULT_SCHEME = "ship_original";

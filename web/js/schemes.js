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
  //
  // For additional ship schemes (everything named ship_<something> except
  // ship_original and ship_db) it works a little differently as the pink hits
  // are unfortunately hard to see. To make the hits more visible for the dark
  // schemes they are always the lightest or one of the lightest colours from
  // the range used in the specific plot; for the light schemes it's the darkest.
  // The vertex color is something that shows up, but often a little less
  // attention seeking.
  // The "mood" of each scheme is determined first by the background colour,
  // followed by the geometry colors and the interplay with hit / vertex colours.
  // Everything else (accent, surface, etc.) is then set following the mood.
  // More info at the specific schemes.
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
  ship_night: {
    // Scheme closely following the SHiP palette. Background is the darkest main
    // color (navy), for contrast the geometry colours are from the blue and
    // light pink range, hits are the lightest main color (cream),
    // accent is the light pink fromm the main colors
    // This combination makes the hits shine like lights during the night
    label: "SHiP night",
    bg: "#081b3c", bg2: "#0a1528", surface: "#1a3263",
    text: "#d3dae8", accent: "#e3a0c1", hit: "#f1debc", vertex: "#d89b29",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a0c1", "#eec7db", "#f4d9e6"],
  },
  ship_night2: {
    // similar to SHiP night, but background colour is darker
    // If we were already sailing through the night, now it's midnight.
    label: "SHiP midnight",
    bg: "#0a1528", bg2: "#081b3c", surface: "#081b3c",
    text: "#d3dae8", accent: "#e3a0c1", hit: "#f1debc", vertex: "#d89b29",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a0c1", "#eec7db", "#f4d9e6"],
  },
  ship_night3: {
    // somewhat similar to SHiP night, but adjusted for the pink haters
    // Pinks are exchanged for cream / gold range. Consequently cream is exchanged
    // for a very pale pink
    label: "SHiP night cream",
    bg: "#081b3c", bg2: "#0a1528", surface: "#1a3263",
    text: "#d3dae8", accent: "#e3a93c", hit: "#faedf3", vertex: "#e3a0c1",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#f0d7aa", "#f1debc", "f3e5c9"],
  },
  ship_night4: {
    // as SHiP midnight belongs to SHiP night and SHiP night cream corresponds to
    // SHiP night, SHiP midnight cream completes the little quartett
    label: "SHiP midnight cream",
    bg: "#0a1528", bg2: "#081b3c", surface: "#081b3c",
    text: "#d3dae8", accent: "#e3a93c", hit: "#faedf3", vertex: "#e3a0c1",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#f0d7aa", "#f1debc", "f3e5c9"],
  },
  ship_day: {
    // Light scheme still closely following the SHiP palette. Background is a
    // neutral pale blue (from the palette), geometry and accent use the two most
    // characteristic main colors from the palette: the blue and the (bright) pink
    // and some shades around them. To improve contrast the cream is switched for an
    // even lighter version of itself.
    // The scheme is still closely related to the language of SHiP night and in a way
    // its daytime version.
    label: "SHiP day",
    bg: "#bbc6dc", bg2: "#a5b3d0", surface: "#d3dae8",
    text: "#081b3c", accent: "#20428a", hit: "#f3e5c9", vertex: "#e0af54",
    geometry: ["#365697", "#20428a", "#617aad", "#798eba",
               "#d87aa8", "#c64284", "#e3a0c1"],
  },
  ship_day2: {
    // Following SHiP day and SHiP night cream, this is a day version for the cream
    // theme. Creams shift into the golden part of the range and for visibility the
    // distribution of hues within the geometry got a bit reshuffled.
    label: "SHiP day cream",
    bg: "#bbc6dc", bg2: "#a5b3d0", surface: "#d3dae8",
    text: "#081b3c", accent: "#20428a", hit: "#faedf3", vertex: "#e3a0c1",
    geometry: ["#d89b29", "#e4b969", "#617aad", "#798eba",
               "#20428a", "#365697", "#ebcd94"],
  },
  ship_dark: {
    // Basically SHiP (mid)night, but with a neutral dark grey background for a
    // cleaner and less playful look, still following the same colour philosophy.
    // It reminds of professional design software.
    label: "SHiP dark",
    bg: "#424242", bg2: "#606060", surface: "#686868",
    text: "#faedf3", accent: "#e3a0c1", hit: "#f3e5c9", vertex: "#e3a93c",
    geometry: ["#5a86d0", "#6e97d6", "#8fb3e0", "#a9c4e8",
               "#e3a0c1", "#eec7db", "#f4d9e6"],
  },
  ship_dark2: {
    // Following the idea of SHiP dark, but adjusted in a monochrome blue range -
    // colours of the sea. The hits are in one of the lightest shades of blue
    // from the palette, making them shine like a lighthouse in the dark
    label: "SHiP dark sea",
    bg: "#424242", bg2: "#606060", surface: "#686868",
    text: "#f3f5f9", accent: "#e8ecf3", hit: "#e8ecf3", vertex: "#a5b3d0",
    geometry: ["#617aad", "#798eba", "#365697", "#4b67a2",
               "#20428a", "#a5b3d0"],
  },
  ship_light: {
    // Basically SHiP day, but with a neutral grey background instead for an
    // understated look
    label: "SHiP light",
    bg: "#bbbbbb", bg2: "#999999", surface: "#aaaaaa",
    text: "#111111", accent: "#20428a", hit: "#f3e5c9", vertex: "#e0af54",
    geometry: ["#365697", "#20428a", "#617aad", "#798eba",
               "#d87aa8", "#c64284", "#e3a0c1"],
  },
  ship_white: {
    // A very clean theme using a white background was requested. Apart from the
    // white most colours are greys or close to greys from the palette. Hits use
    // the navy from the main colours, the vertex uses the (bright) pink. Making
    // anything look good and work as event display over the white background is
    // hard, but those work out. As the pink adds a tiny touch of playfulness,
    // the accent colour follows.
    label: "SHiP white",
    bg: "#ffffff", bg2: "#c0c0c0", surface: "#dee3ee",
    text: "#232323", accent: "#c64284", hit: "#081b3c", vertex: "#c64284",
    geometry: ["#e8ecf3", "#f3f5f9"],
  },
  ship_white2: {
    // Very close to SHiP white. It started out as alternative scheme just
    // changing the accent colour into the golden range for the pink haters.
    // Eventually the geometry colours got changed into the range as well, now
    // appearing as warm grey. Hits using the main blue, thus being slightly
    // lighter than in the original SHiP white, reminding more of the sea.
    // In total the schemes subtly reminds of a day at the beach. Spending
    // time in the sand, playing with a pink ball, all accompanied by the blue
    // waves of the hits.
    label: "SHiP white beach",
    bg: "#ffffff", bg2: "#c0c0c0", surface: "#f1debc",
    text: "#232323", accent: "#e3a93c", hit: "#20428a", vertex: "#c64284",
    geometry: ["#f9f2e4", "#fbf7ee"],
  },
  ship_blue: {
    // All blue everything. Monochrome blue scheme, making use of the full range
    // of blues from the SHiP palette. Hits again light up as in SHiP dark sea.
    label: "SHiP blue",
    bg: "#081b3c", bg2: "#0a1528", surface: "#12305f",
    text: "#d3dae8", accent: "#8fa1c5", hit: "#e8ecf3", vertex: "#a5b3d0",
    geometry: ["#617aad", "#20428a", "#365697", "#4b67a2",
               "#798eba", "#a5b3d0"],
  },
  ship_ht: {
    // "Everything pink", using the palette's pink range: muted/dark pinks for
    // the detector, the lightest palette pink for hits (to make them show), darker
    // main pink for the vertex.
    label: "SHiP pink",
    bg: "#2b0f1e", bg2: "#1c0a14", surface: "#4a1730",
    text: "#f5d9e6", accent: "#c64284", hit: "#e3a0c1", vertex: "#c64284",
    geometry: ["#9e3569", "#b8497e", "#c64284", "#d96ba0", "#e79ac0", "#f0b6d2"],
  },
  kcool: {
    // Something not SHiP and not terminal. Scheme derived from ROOTs kCool, used
    // on a dark grey background. Hits in a bright cyan really light up.
    // Matei might like it and it indead looks pretty cool.
    label: "kCool",
    bg: "#424242", bg2: "#606060", surface: "#4c6364",
    text: "#c5feff", accent: "#eb40ff", hit: "#7af2f4", vertex: "#cf2be2",
    geometry: ["#4f94c3", "#4a86c6", "#4a70c5", "#5a5bc4",
               "#961dc1", "#b321cd", "#7b28c2"],
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

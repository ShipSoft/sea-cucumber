// SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Flat config for the web frontend. Deliberately dependency-free: conda-forge
// ships no shareable config, and the `@eslint/js` / `globals` packages bundled
// with eslint live under its own node_modules, where Node's ESM resolver will
// not find them from here. So rules and browser globals are both spelled out.
//
// Globals list mirrors what web/js/ actually touches -- extend it when the
// frontend starts using a new browser API, otherwise no-undef will flag it.

export default [
  {
    files: ["**/*.js"],
    languageOptions: {
      ecmaVersion: 2023,
      sourceType: "module",
      globals: {
        Blob: "readonly",
        clearTimeout: "readonly",
        console: "readonly",
        devicePixelRatio: "readonly",
        document: "readonly",
        Event: "readonly",
        fetch: "readonly",
        getComputedStyle: "readonly",
        localStorage: "readonly",
        location: "readonly",
        requestAnimationFrame: "readonly",
        screen: "readonly",
        setTimeout: "readonly",
        URL: "readonly",
        URLSearchParams: "readonly",
        window: "readonly",
      },
    },
    linterOptions: { reportUnusedDisableDirectives: "error" },
    rules: {
      // Correctness.
      "no-undef": "error",
      "no-unused-vars": [
        "error",
        {
          args: "after-used",
          varsIgnorePattern: "^_",
          argsIgnorePattern: "^_",
          caughtErrorsIgnorePattern: "^_",
        },
      ],
      "no-constant-binary-expression": "error",
      "no-self-compare": "error",
      "no-unused-private-class-members": "error",
      // House style.
      eqeqeq: ["error", "smart"],
      "no-var": "error",
      "prefer-const": "error",
      "no-implicit-globals": "error",
    },
  },
];

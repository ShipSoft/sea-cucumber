#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) CERN for the benefit of the SHiP Collaboration
#
# Generate a demo event and display it, using the SAME geometry for both.
# Invoked by `pixi run demo`; pass --geometry <db> (and any extra sea_cucumber
# flags, e.g. --event N) after it:
#     pixi run demo --geometry /path/to/ship_geometry.db   (or a .gdml)
set -euo pipefail
cd "$(dirname "$0")/.."

GEO="ship_geometry.db"
VIEW="views/default.toml"
DATA="demo_events.root"
extra=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --geometry) GEO="$2"; shift 2 ;;
    --view)     VIEW="$2"; shift 2 ;;
    --output)   DATA="$2"; shift 2 ;;
    *)          extra+=("$1"); shift ;;
  esac
done

echo "[run_demo] geometry: $GEO"
./build/make_demo_event --geometry "$GEO" --output "$DATA"
exec ./build/sea_cucumber --geometry "$GEO" --data "$DATA" --view "$VIEW" "${extra[@]}"

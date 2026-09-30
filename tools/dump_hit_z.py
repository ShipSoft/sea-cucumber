#!/usr/bin/env python3
# SPDX-FileCopyrightText: CERN for the benefit of the SHiP Collaboration
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Independent cross-check of hit z-positions in an RNTuple file.

Shares no code with the display. If these numbers disagree with what's on
screen, the display is at fault; if they already look wrong here, the data (or
our assumption about its units/frame) is.

Usage:
    python tools/dump_hit_z.py files/llp_display.root 16
    python tools/dump_hit_z.py files/llp_display.root 16 --ntuple events

Needs uproot >= 5 (reads RNTuple directly, no ROOT/dictionary needed):
    pip install "uproot>=5" awkward
"""

import argparse
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("event", type=int, nargs="?", default=0)
    ap.add_argument("--ntuple", default="events")
    args = ap.parse_args()

    try:
        import uproot
    except ImportError:
        print("need uproot>=5:  pip install 'uproot>=5' awkward", file=sys.stderr)
        return 2

    f = uproot.open(args.file)
    if args.ntuple not in f:
        print(f"'{args.ntuple}' not in file. Keys: {f.keys()}", file=sys.stderr)
        return 1
    obj = f[args.ntuple]

    # uproot exposes RNTuple fields by name; list them so the exact spelling of
    # the hit position sub-fields is visible.
    try:
        fields = obj.keys()
    except Exception:  # noqa: BLE001
        fields = []
    print(f"fields: {fields}\n")

    arrays = obj.arrays()  # awkward record array, one row per event
    n = len(arrays)
    print(f"{args.file}: ntuple '{args.ntuple}' has {n} events")
    if args.event >= n:
        print(f"event {args.event} out of range [0,{n - 1}]", file=sys.stderr)
        return 1
    row = arrays[args.event]

    # Find the hits collection and its position member, trying known spellings.
    def get(path):
        cur = row
        for part in path.split("."):
            cur = cur[part]
        return cur

    pos = None
    for base in ("sim_hits", "sim_result.hits"):
        for member in ("position",):
            try:
                p = get(f"{base}.{member}")
                pos = p
                print(f"using {base}.{member}\n")
                break
            except Exception:  # noqa: BLE001
                continue
        if pos is not None:
            break

    if pos is None:
        print("could not locate hit positions; inspect 'fields' above and adjust.", file=sys.stderr)
        return 1

    # pos is an array of length-3 arrays (mm).
    xs = [float(v[0]) for v in pos]
    ys = [float(v[1]) for v in pos]
    zs = [float(v[2]) for v in pos]

    print(f"event {args.event}: {len(zs)} hits (mm)\n")
    print(f"{'hit':>6} {'x':>14} {'y':>14} {'z':>14}")
    for i, (x, y, z) in enumerate(zip(xs, ys, zs, strict=True)):
        print(f"{i:>6} {x:>14.2f} {y:>14.2f} {z:>14.2f}")

    if zs:
        print("\nsummary (mm):")
        print(f"  x range [{min(xs):.1f}, {max(xs):.1f}]")
        print(f"  y range [{min(ys):.1f}, {max(ys):.1f}]")
        print(f"  z range [{min(zs):.1f}, {max(zs):.1f}]  mean z {sum(zs) / len(zs):.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

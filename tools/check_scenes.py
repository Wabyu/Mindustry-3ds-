#!/usr/bin/env python3
"""Automatic checks on the control scenes (tests/control_scenes.cpp).

1. quarter-turn sprites placed at sub-pixel positions (NEAREST sampling hits texel edges)
2. texels sampled outside their own sprite rectangle (atlas bleeding)
3. slow pans: frame k must equal frame 0 shifted by the camera's whole-pixel offset
Usage: check_scenes.py <scenes_dir> <gfx_dir> <gen_dir>
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_shots as rs  # noqa: E402


def load(path):
    return [json.loads(l) for l in open(path) if l.strip()]


def main():
    scenes, gfx, gen = sys.argv[1:4]
    atlas = rs.Atlas(gfx, rs.load_refs(gen))
    names = sorted(f[:-6] for f in os.listdir(scenes) if f.endswith(".jsonl"))
    frac = quarter = mirrored = 0
    rs.STATS.update(bleed_samples=0, samples=0)
    frames = {}
    for n in names:
        cmds = load(os.path.join(scenes, n + ".jsonl"))
        for c in cmds:
            if c["op"] != "sprite":
                continue
            if c["w"] < 0 or c["h"] < 0:
                mirrored += 1
            k = c["angle"] / (math.pi / 2)
            if abs(k - round(k)) < 1e-4:
                quarter += 1
                swap = round(k) % 2 != 0
                ex, ey = (abs(c["h"]), abs(c["w"])) if swap else (abs(c["w"]), abs(c["h"]))
                x0, y0 = c["x"] - ex / 2, c["y"] - ey / 2
                if abs(x0 - round(x0)) > 1e-3 or abs(y0 - round(y0)) > 1e-3:
                    frac += 1
        if n.startswith("pan_"):
            frames[n] = np.asarray(rs.render(cmds, atlas)[0]).astype(int)
    print("mirrored draws (negative size): %d" % mirrored)
    print("quarter-turn sprites: %d, at sub-pixel positions: %d" % (quarter, frac))
    print("texels sampled outside their sprite: %d of %d (%.4f%%)" % (
        rs.STATS["bleed_samples"], rs.STATS["samples"], 100.0 * rs.STATS["bleed_samples"] / max(1, rs.STATS["samples"])))
    for prefix, (vx, vy) in (("pan_x", (0.25, 0.0)), ("pan_diag", (0.37, 0.21))):
        keys = sorted(k for k in frames if k.startswith(prefix + "_"))
        if not keys:
            continue
        base = frames[keys[0]]
        worst = 0
        for i, k in enumerate(keys):
            # whole-pixel camera shift of frame i (world units * 2 px), y axis points down on screen
            # camera +x -> content moves left; camera +y (world up) -> content moves down
            dx = math.floor(vx * i * 2 + 0.5)
            dy = math.floor(vy * i * 2 + 0.5)
            hh, ww = 192 - abs(dy), 352 - abs(dx)
            a2 = base[24:24 + hh, 24:24 + ww]
            b2 = frames[k][24 + dy:24 + dy + hh, 24 - dx:24 - dx + ww]
            diff = int((np.abs(a2 - b2).max(axis=-1) > 0).sum())
            worst = max(worst, diff)
        print("%s: %d frames, worst mismatching pixels vs. shifted frame 0: %d" % (prefix, len(keys), worst))


if __name__ == "__main__":
    main()

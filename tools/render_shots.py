#!/usr/bin/env python3
"""Compose host screenshots from draw-command recordings (tests/gfx_host.cpp).

The commands are the exact calls the 3DS build makes to citro2d; sprites are the
generated sheet images. Text uses a desktop font, so glyph shapes differ from the
3DS system font.
"""
import json
import math
import os
import sys

import re
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont


STATS = {"bleed_samples": 0, "samples": 0}
LEGACY_CENTER = os.environ.get("MDT_LEGACY_CENTER") == "1"


class Atlas:
    """Raw tex3ds output (sheetN.raw.t3x + sheetN.preview.png): same UVs as the console."""

    def __init__(self, gfx_dir, refs):
        self.sheets = {}
        self.refs = refs  # sprite id -> (sheet, local index)
        s = 0
        while os.path.exists(os.path.join(gfx_dir, "sheet%d.raw.t3x" % s)):
            d = open(os.path.join(gfx_dir, "sheet%d.raw.t3x" % s), "rb").read()
            n, bits = struct.unpack_from("<HB", d, 0)
            w, h = 1 << ((bits & 7) + 3), 1 << (((bits >> 3) & 7) + 3)
            subs = [struct.unpack_from("<6H", d, 5 + 12 * i) for i in range(n)]
            img = np.asarray(Image.open(os.path.join(gfx_dir, "sheet%d.preview.png" % s)).convert("RGBA")).astype(np.float32) / 255
            self.sheets[s] = (w, h, subs, img)
            s += 1

    def uvs(self, sid):
        """Tex3DS_SubTexture{TopLeft,TopRight,BottomLeft,BottomRight} (tex3ds.h)."""
        sheet, local = self.refs[sid]
        w, h, subs, img = self.sheets[sheet]
        _, _, l, t, r, b = subs[local]
        l, t, r, b = l / 1024, t / 1024, r / 1024, b / 1024
        if not t < b:
            return sheet, (l, t), (r, t), (l, b), (r, b)
        return sheet, (t, l), (t, r), (b, l), (b, r)


def raster_sprite(cur, atlas, c):
    """Emulates C2D_DrawImage: C2Di_CalcQuad + UV swap on negative size + GPU_NEAREST."""
    sheet, tl, tr, bl, br = atlas.uvs(c["id"])
    if c["w"] < 0:
        tl, tr, bl, br = tr, tl, br, bl
    if c["h"] < 0:
        tl, tr, bl, br = bl, br, tl, tr
    aw, ah = abs(c["w"]), abs(c["h"])
    if LEGACY_CENTER:  # previous backend: center = w/2, h/2 with signed sizes
        c = dict(c, cx=c["w"] / 2, cy=c["h"] / 2)
    pts = [(-c["cx"], -c["cy"]), (-c["cx"] + aw, -c["cy"]), (-c["cx"], -c["cy"] + ah)]
    a = c["angle"]
    if a != 0:
        sn, cs = math.sin(a), math.cos(a)
        pts = [(x * cs - y * sn, y * cs + x * sn) for x, y in pts]
    (x0, y0), (x1, y1), (x2, y2) = [(x + c["x"], y + c["y"]) for x, y in pts]
    ex, ey = (x1 - x0, y1 - y0), (x2 - x0, y2 - y0)
    xs = [x0, x1, x2, x1 + ex[0] * 0 + (x2 - x0)]
    ys = [y0, y1, y2, y1 + (y2 - y0)]
    px0, px1 = max(0, int(math.floor(min(xs)))), min(cur.shape[1], int(math.ceil(max(xs))))
    py0, py1 = max(0, int(math.floor(min(ys)))), min(cur.shape[0], int(math.ceil(max(ys))))
    if px0 >= px1 or py0 >= py1:
        return
    det = ex[0] * ey[1] - ex[1] * ey[0]
    if abs(det) < 1e-9:
        return
    gx, gy = np.meshgrid(np.arange(px0, px1) + 0.5, np.arange(py0, py1) + 0.5)
    dx, dy = gx - x0, gy - y0
    s_ = (dx * ey[1] - dy * ey[0]) / det
    t_ = (ex[0] * dy - ex[1] * dx) / det
    inside = (s_ >= 0) & (s_ < 1) & (t_ >= 0) & (t_ < 1)
    if not inside.any():
        return
    w, h, subs, img = atlas.sheets[sheet]
    u = tl[0] + s_ * (tr[0] - tl[0]) + t_ * (bl[0] - tl[0])
    v = tl[1] + s_ * (tr[1] - tl[1]) + t_ * (bl[1] - tl[1])
    tx = np.clip(np.floor(u * w).astype(int), 0, w - 1)
    ty = np.clip(np.floor((1 - v) * h).astype(int), 0, h - 1)
    # texels sampled outside the sprite's own rectangle = bleeding from the atlas neighbours
    _, _, l, t, r, b = subs[atlas.refs[c["id"]][1]]
    if t < b:  # rotated subtexture
        l, t, r, b = t, l, b, r
    rx0, rx1 = sorted((round(l / 1024 * w), round(r / 1024 * w)))
    ry0, ry1 = sorted((round((1 - t / 1024) * h), round((1 - b / 1024) * h)))
    out = inside & ((tx < rx0) | (tx >= rx1) | (ty < ry0) | (ty >= ry1))
    STATS["bleed_samples"] += int(out.sum())
    STATS["samples"] += int(inside.sum())
    tex = img[ty, tx]
    col = np.array(rgba(c["color"]), dtype=np.float32) / 255
    rgb = tex[..., :3] * (1 - c["blend"]) + col[:3] * c["blend"]
    alpha = tex[..., 3] * col[3] * inside
    dst = cur[py0:py1, px0:px1]
    dst[..., :3] = dst[..., :3] * (1 - alpha[..., None]) + rgb * alpha[..., None]
    dst[..., 3] = np.maximum(dst[..., 3], alpha)


def rgba(c):
    return ((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255)


def load_sprites(gfx_dir):
    names = {}
    for f in os.listdir(os.path.join(gfx_dir, "sprites")):
        if f.endswith(".png"):
            names[int(f.split("_", 1)[0])] = os.path.join(gfx_dir, "sprites", f)
    return names


_font_cache = {}


def font(size):
    size = max(6, int(round(size)))
    if size not in _font_cache:
        for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                     "/usr/share/fonts/TTF/DejaVuSans.ttf"):
            if os.path.exists(path):
                _font_cache[size] = ImageFont.truetype(path, size)
                break
        else:
            _font_cache[size] = ImageFont.load_default()
    return _font_cache[size]


def overlay_pil(cur, draw_fn):
    layer = Image.new("RGBA", (cur.shape[1], cur.shape[0]))
    draw_fn(ImageDraw.Draw(layer))
    src = np.asarray(layer).astype(np.float32) / 255
    a = src[..., 3:4]
    cur[..., :3] = cur[..., :3] * (1 - a) + src[..., :3] * a
    cur[..., 3:4] = np.maximum(cur[..., 3:4], a)


def render(cmds, atlas):
    screens = {0: np.zeros((240, 400, 4), np.float32), 1: np.zeros((240, 320, 4), np.float32)}
    cur = None
    for c in cmds:
        op = c["op"]
        if op == "begin":
            cur = screens[c["screen"]]
            cur[...] = np.array(rgba(c["color"]), np.float32) / 255
            continue
        if cur is None:
            continue
        if op == "sprite":
            raster_sprite(cur, atlas, c)
        elif op == "rect":
            x0, y0 = max(0, int(c["x"])), max(0, int(c["y"]))
            x1, y1 = min(cur.shape[1], int(c["x"] + c["w"])), min(cur.shape[0], int(c["y"] + c["h"]))
            if x0 < x1 and y0 < y1:
                col = np.array(rgba(c["color"]), np.float32) / 255
                d = cur[y0:y1, x0:x1]
                d[..., :3] = d[..., :3] * (1 - col[3]) + col[:3] * col[3]
                d[..., 3] = np.maximum(d[..., 3], col[3])
        elif op == "line":
            overlay_pil(cur, lambda d: d.line([c["x0"], c["y0"], c["x1"], c["y1"]], fill=rgba(c["color"]),
                                              width=max(1, int(round(c["w"])))))
        elif op == "text":
            f = font(30 * c["scale"] * 0.85)

            def txt(d):
                y = c["y"]
                for ln in c["text"].split("\n"):
                    w = d.textlength(ln, font=f)
                    x = c["x"] - (w / 2 if c["align"] == 1 else w if c["align"] == 2 else 0)
                    d.text((x, y), ln, font=f, fill=rgba(c["color"]))
                    y += 30 * c["scale"]
            overlay_pil(cur, txt)
    return {k: Image.fromarray((np.clip(v, 0, 1) * 255).astype(np.uint8)) for k, v in screens.items()}


def load_refs(gen_dir):
    refs = {}
    for i, m in enumerate(re.finditer(r"^  \{(\d+), (\d+), (\d+), (\d+)\}", open(os.path.join(gen_dir, "sprites_gen.hpp")).read(), re.M)):
        refs[i] = (int(m.group(1)), int(m.group(2)))
    return refs


def main():
    shots, gfx_dir = sys.argv[1], sys.argv[2]
    gen_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(gfx_dir.rstrip("/")), "gen")
    atlas = Atlas(gfx_dir, load_refs(gen_dir))
    if not atlas.sheets:
        sys.exit("missing %s/sheet0.raw.t3x: run tools/atlas_debug.sh" % gfx_dir)
    only = sys.argv[4:] if len(sys.argv) > 4 else None
    for fn in sorted(os.listdir(shots)):
        if not fn.endswith(".jsonl") or (only and fn[:-6] not in only):
            continue
        cmds = [json.loads(l) for l in open(os.path.join(shots, fn)) if l.strip()]
        sc = render(cmds, atlas)
        out = Image.new("RGBA", (400, 488), (20, 20, 20, 255))
        out.paste(sc[0], (0, 0))
        out.paste(sc[1], (40, 248))
        out = out.resize((800, 976), Image.NEAREST)
        name = os.path.join(shots, fn[:-6] + ".png")
        out.convert("RGB").save(name)
        sc[0].convert("RGB").save(os.path.join(shots, fn[:-6] + "_top1x.png"))
        print("wrote", name)


if __name__ == "__main__":
    main()

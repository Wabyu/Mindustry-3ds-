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

from PIL import Image, ImageDraw, ImageFont


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


def render(cmds, sprite_paths):
    screens = {0: Image.new("RGBA", (400, 240)), 1: Image.new("RGBA", (320, 240))}
    cache = {}
    cur = None
    for c in cmds:
        op = c["op"]
        if op == "begin":
            cur = screens[c["screen"]]
            cur.paste(rgba(c["color"]), (0, 0, cur.width, cur.height))
            continue
        if cur is None:
            continue
        if op == "sprite":
            sid = c["id"]
            if sid not in sprite_paths:
                continue
            if sid not in cache:
                cache[sid] = Image.open(sprite_paths[sid]).convert("RGBA")
            img = cache[sid]
            w = max(1, int(round(abs(img.width * c["sx"])))); h = max(1, int(round(abs(img.height * c["sy"]))))
            im = img.resize((w, h), Image.BILINEAR)
            if c["sx"] < 0:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            if c["sy"] < 0:
                im = im.transpose(Image.FLIP_TOP_BOTTOM)
            col = rgba(c["color"]); blend = c["blend"]
            if col != (255, 255, 255, 255) or blend:
                r, g, b, a = im.split()
                if blend:
                    solid = Image.new("RGB", im.size, col[:3])
                    rgb = Image.blend(Image.merge("RGB", (r, g, b)), solid, blend)
                    r, g, b = rgb.split()
                a = a.point(lambda v: v * col[3] // 255)
                im = Image.merge("RGBA", (r, g, b, a))
            if c["rot"]:
                im = im.rotate(c["rot"], resample=Image.BILINEAR, expand=True)
            x = int(round(c["x"] - im.width / 2)); y = int(round(c["y"] - im.height / 2))
            layer = Image.new("RGBA", cur.size)
            layer.paste(im, (x, y))
            cur.alpha_composite(layer)
        elif op == "rect":
            layer = Image.new("RGBA", cur.size)
            ImageDraw.Draw(layer).rectangle([c["x"], c["y"], c["x"] + c["w"] - 1, c["y"] + c["h"] - 1], fill=rgba(c["color"]))
            cur.alpha_composite(layer)
        elif op == "line":
            layer = Image.new("RGBA", cur.size)
            ImageDraw.Draw(layer).line([c["x0"], c["y0"], c["x1"], c["y1"]], fill=rgba(c["color"]), width=max(1, int(round(c["w"]))))
            cur.alpha_composite(layer)
        elif op == "text":
            f = font(30 * c["scale"] * 0.85)
            layer = Image.new("RGBA", cur.size)
            d = ImageDraw.Draw(layer)
            lines = c["text"].split("\n")
            y = c["y"]
            for ln in lines:
                w = d.textlength(ln, font=f)
                x = c["x"] - (w / 2 if c["align"] == 1 else w if c["align"] == 2 else 0)
                d.text((x, y), ln, font=f, fill=rgba(c["color"]))
                y += 30 * c["scale"]
            cur.alpha_composite(layer)
    return screens


def main():
    shots, gfx_dir = sys.argv[1], sys.argv[2]
    sprites = load_sprites(gfx_dir)
    for fn in sorted(os.listdir(shots)):
        if not fn.endswith(".jsonl"):
            continue
        cmds = [json.loads(l) for l in open(os.path.join(shots, fn)) if l.strip()]
        sc = render(cmds, sprites)
        # 3DS layout: top screen above the centred bottom screen
        out = Image.new("RGBA", (400, 488), (20, 20, 20, 255))
        out.paste(sc[0], (0, 0))
        out.paste(sc[1], (40, 248))
        out = out.resize((800, 976), Image.NEAREST)
        name = os.path.join(shots, fn[:-6] + ".png")
        out.convert("RGB").save(name)
        print("wrote", name)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build every 3DS asset from the pinned upstream Mindustry checkout.

Outputs
  <gen>/content_gen.hpp / content_gen.cpp  content tables (blocks, items, units)
  <gfx>/*.t3s + png                        sprite sheets for tex3ds (16 px / tile)
  <romfs>/maps/*.m3d                       sector maps converted from .msav
  <romfs>/lang/<code>.txt                  strings taken from Mindustry bundles

Nothing here is hand-tuned gameplay data: values come from the Java sources
(content definitions + class field defaults) of upstream/Mindustry.
"""
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import javacontent as jc  # noqa: E402
import msav  # noqa: E402

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: pip install pillow (or apt install python3-pil)")

SCALE = 0.5          # upstream sprites are 32 px per tile, the 3DS port uses 16 px
SHEET_MAX = 1024     # PICA200 texture size limit
TEAM_DERELICT, TEAM_SHARDED, TEAM_CRUX = 0, 1, 2

# Sectors converted from upstream maps, in campaign order (SectorPresets.java).
SECTORS = ["groundZero", "frozenForest", "craters", "ruinousShores", "stainedMountains"]

# Buildable blocks the native engine implements, keyed by Blocks.java field.
# value: engine behaviour type
BUILDABLE = {
    "conveyor": "Conveyor", "titaniumConveyor": "Conveyor",
    "junction": "Junction", "router": "Router", "distributor": "Router",
    "sorter": "Sorter", "invertedSorter": "Sorter",
    "overflowGate": "OverflowGate", "underflowGate": "OverflowGate",
    "itemBridge": "Bridge",
    "mechanicalDrill": "Drill", "pneumaticDrill": "Drill",
    "graphitePress": "Crafter", "siliconSmelter": "Crafter", "kiln": "Crafter",
    "pulverizer": "Crafter", "pyratiteMixer": "Crafter", "blastMixer": "Crafter",
    "combustionGenerator": "ConsumeGen",
    "powerNode": "PowerNode", "battery": "Battery",
    "solarPanel": "Solar", "largeSolarPanel": "Solar",
    "duo": "ItemTurret", "scatter": "ItemTurret", "hail": "ItemTurret",
    "copperWall": "Wall", "copperWallLarge": "Wall",
    "titaniumWall": "Wall", "titaniumWallLarge": "Wall",
    "scrapWall": "Wall", "scrapWallLarge": "Wall", "scrapWallHuge": "Wall",
    "coreShard": "Core", "coreFoundation": "Core",
    "container": "Storage", "vault": "Storage",
}
UNITS = ["dagger", "flare", "alpha", "beta"]
ENV_FLOOR = {"Floor", "ShallowLiquid", "EmptyFloor", "SteamVent", "AirBlock"}
ENV_OVERLAY = {"OreBlock", "OverlayFloor", "SpawnBlock"}
ENV_WALL = {"StaticWall", "StaticTree", "TreeBlock", "TallBlock", "StaticClusterWall"}
ENV_PROP = {"Prop", "SeaBush"}
CATEGORIES = ["turret", "production", "distribution", "liquid", "power", "defense",
              "crafting", "units", "effect", "logic"]


def ident(name):
    s = re.sub(r"[^0-9a-zA-Z]+", "_", name).strip("_")
    return ("_" + s) if s[:1].isdigit() else s


def cfloat(v):
    v = float(v or 0)
    r = repr(v)
    return (r if ("." in r or "e" in r) else r + ".0") + "f"


# --------------------------------------------------------------------------
class Upstream:
    def __init__(self, root):
        self.root = root
        self.src = os.path.join(root, "core", "src", "mindustry")
        self.sprites = os.path.join(root, "core", "assets-raw", "sprites")
        self.classes = jc.ClassIndex(self.src)
        self.sprite_index = {}
        for d, _, fs in os.walk(self.sprites):
            for f in fs:
                if f.endswith(".png"):
                    self.sprite_index.setdefault(f[:-4], os.path.join(d, f))
        self.block_defs = {d.field: d for d in jc.parse_defs(os.path.join(self.src, "content", "Blocks.java"))}
        self.block_by_name = {d.name: d for d in self.block_defs.values() if d.name}
        self.item_defs = [d for d in jc.parse_defs(os.path.join(self.src, "content", "Items.java"))]
        self.unit_defs = {d.field: d for d in jc.parse_defs(os.path.join(self.src, "content", "UnitTypes.java"))}
        self.sector_defs = {d.field: d for d in jc.parse_defs(os.path.join(self.src, "content", "SectorPresets.java"))}
        self.consts = {"wallHealthMultiplier": 4}
        src = jc.strip_comments(open(os.path.join(self.src, "io", "SaveFileReader.java")).read())
        fb = src[src.index("fallback"):]
        fb = fb[:fb.index(");")]
        pairs = re.findall(r'"([^"]+)"', fb)
        self.fallback = dict(zip(pairs[0::2], pairs[1::2]))

    def sprite(self, name):
        return self.sprite_index.get(name)

    def variants(self, name):
        n = 0
        while self.sprite(name + str(n + 1)):
            n += 1
        return n


# --------------------------------------------------------------------------
class Sprites:
    """Collects sprites, downsamples them and writes tex3ds sheets."""

    def __init__(self, up):
        self.up = up
        self.names = []
        self.index = {}
        self.images = []

    def add(self, name, outline=False, required=True, alias=None):
        key = alias or name
        if key in self.index:
            return self.index[key]
        path = self.up.sprite(name)
        if not path:
            if required:
                raise SystemExit("missing upstream sprite: " + name)
            return -1
        img = Image.open(path).convert("RGBA")
        if outline:
            img = add_outline(img, (0x40, 0x40, 0x49, 255), 3)
        return self.add_image(key, downscale(img, 2))

    def add_derived(self, key, name, fn, outline=False):
        """Full-resolution upstream sprite -> fn(img) -> downscaled sprite."""
        if key in self.index:
            return self.index[key]
        img = Image.open(self.up.sprite(name)).convert("RGBA")
        if outline:
            img = add_outline(img, (0x40, 0x40, 0x49, 255), 3)
        return self.add_image(key, downscale(fn(img), 2))

    def add_image(self, key, img):
        if key in self.index:
            return self.index[key]
        self.index[key] = len(self.names)
        self.names.append(key)
        self.images.append(img)
        return self.index[key]

    def add_icon(self, key, layers, size):
        """Composite icon like tools/Generators.java (block-<name>-full) from Block.icons()
        layers, reduced to the 32x32 UI size by an integer box filter."""
        canvas = compose_icon(self.up, layers, size)
        big = self.add_image("icon-" + key, downscale(canvas, size))
        self.add_image("icon-small-" + key, downscale(canvas, size * 2))  # always big + 1
        return big

    def get(self, name):
        return self.index.get(name, -1)

    def write(self, gfx_dir):
        sdir = os.path.join(gfx_dir, "sprites")
        os.makedirs(sdir, exist_ok=True)
        for old in os.listdir(sdir):  # stale sprites from a previous content set
            if old.endswith(".png"):
                os.remove(os.path.join(sdir, old))
        for old in os.listdir(gfx_dir):
            if old.startswith("sheet") and old.endswith(".t3s"):
                os.remove(os.path.join(gfx_dir, old))
        # greedy split into sheets whose area fits comfortably inside 1024x1024
        sheets, cur, area = [], [], 0
        budget = SHEET_MAX * SHEET_MAX * 0.70
        for i, img in enumerate(self.images):
            a = (img.width + 2) * (img.height + 2)
            if cur and area + a > budget:
                sheets.append(cur); cur, area = [], 0
            cur.append(i); area += a
        if cur:
            sheets.append(cur)
        refs = [None] * len(self.images)
        for s, members in enumerate(sheets):
            lines = ["--atlas -f rgba8888 -z auto --border edge"]  # 1 px colour-matched border: no bleeding between packed sprites
            for local, i in enumerate(members):
                fn = "sprites/%04d_%s.png" % (i, ident(self.names[i]))
                self.images[i].save(os.path.join(gfx_dir, fn))
                lines.append(fn)
                refs[i] = (s, local)
            with open(os.path.join(gfx_dir, "sheet%d.t3s" % s), "w") as f:
                f.write("\n".join(lines) + "\n")
        self.refs = refs
        self.sheet_count = len(sheets)


def downscale(img, factor):
    """Exact box filter in premultiplied alpha. Unlike LANCZOS it cannot create colours
    outside the range of the source pixels (no halos/ringing on pixel art)."""
    if factor == 1:
        return img
    import numpy as np
    w = (img.width + factor - 1) // factor * factor
    h = (img.height + factor - 1) // factor * factor
    if (w, h) != img.size:  # pad symmetrically to a multiple of the factor
        pad = Image.new("RGBA", (w, h))
        pad.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
        img = pad
    a = np.asarray(img).astype(np.float64) / 255
    rgb, al = a[..., :3] * a[..., 3:4], a[..., 3:4]
    def pool(x):
        return x.reshape(h // factor, factor, w // factor, factor, x.shape[-1]).mean(axis=(1, 3))
    prgb, pal = pool(rgb), pool(al)
    out = np.where(pal > 0, prgb / np.maximum(pal, 1e-9), 0)
    res = np.concatenate([out, pal], axis=-1)
    return Image.fromarray(np.clip(np.round(res * 255), 0, 255).astype(np.uint8), "RGBA")


def tint_image(img, rgb):
    """Draw.color multiply (team regions are white/grey masks)."""
    r, g, b, a = img.split()
    mul = [(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255]
    r, g, b = [ch.point(lambda v, m=m: v * m // 255) for ch, m in zip((r, g, b), mul)]
    return Image.merge("RGBA", (r, g, b, a))


def add_outline(img, color, radius):
    """Mindustry's packer outlines units/turrets; reproduce it before scaling."""
    pad = radius
    w, h = img.width + pad * 2, img.height + pad * 2
    src = Image.new("RGBA", (w, h)); src.paste(img, (pad, pad))
    a = src.getchannel("A").load()
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    r2 = radius * radius
    for y in range(h):
        for x in range(w):
            if a[x, y] > 0:
                continue
            hit = False
            for dy in range(-radius, radius + 1):
                yy = y + dy
                if yy < 0 or yy >= h:
                    continue
                for dx in range(-radius, radius + 1):
                    xx = x + dx
                    if 0 <= xx < w and dx * dx + dy * dy <= r2 and a[xx, yy] > 0:
                        hit = True; break
                if hit:
                    break
            if hit:
                o[x, y] = color
    out.alpha_composite(src)
    # crop back to the original size so sprite centres stay where upstream expects them
    return out.crop((pad, pad, pad + img.width, pad + img.height))


# --------------------------------------------------------------------------
def eval_def(up, d, extra_env=None):
    """Field values of a content definition: class defaults + body assignments."""
    vals, chain = up.classes.defaults(d.cls)
    vals = dict(vals)
    env = dict(up.consts); env.update(vals)
    if extra_env:
        env.update(extra_env)
    for k, v in jc.assignments(d.statements, env):
        vals[k] = v
        env[k] = v
    return vals, chain


def item_field_to_name(up):
    return {d.field: d.name for d in up.item_defs}


def parse_with(up, expr, env, items):
    """Parse `with(Items.copper, 12, ...)` or ItemStack.mult(x.requirements, n)."""
    expr = expr.strip()
    m = re.match(r"ItemStack\.mult\((\w+)\.requirements\s*,\s*([\d.]+)f?\)", expr)
    if m:
        base = env["__reqs"][m.group(1)]
        k = float(m.group(2))
        return [(it, int(round(a * k))) for it, a in base]
    val = jc.evaluate(expr, env)
    return [(items[r.field], int(a)) for r, a in val]


def parse_bullet(up, expr):
    inner = jc.parse_inner_new(expr)
    if not inner:
        return None
    cls, args, body = inner
    vals, _ = up.classes.defaults(cls)
    vals = dict(vals)
    if len(args) >= 1 and args[0]:
        vals["speed"] = jc.evaluate(args[0], {})
    if len(args) >= 2:
        vals["damage"] = jc.evaluate(args[1], {})
    for k, v in jc.assignments(jc.split_top(body, ";"), dict(vals)):
        vals[k] = v
    vals["__class"] = cls
    return vals


def collect_blocks(up, used_env_names):
    items = item_field_to_name(up)
    blocks = []
    reqs_by_field = {}

    def base(d, btype):
        vals, chain = eval_def(up, d)
        b = {"field": d.field, "name": d.name, "type": btype, "class": d.cls, "vals": vals,
             "chain": chain, "reqs": [], "category": None, "visibility": None,
             "consumes": [], "power_use": 0.0, "output": None, "ammo": [], "boost": None,
             # DrawMulti(..., new DrawRegion("-rotator"), new DrawRegion("-top")) layers, in order
             "draw_regions": re.findall(r'new DrawRegion\("([^"]+)"\)', d.body or "")}
        for st in d.statements:
            m = re.match(r"requirements\((.*)\)$", st, re.S)
            if m:
                args = jc.split_top(m.group(1), ",")
                b["category"] = jc.evaluate(args[0], {})
                if len(args) == 3:
                    b["visibility"] = jc.evaluate(args[1], {})
                env = {"__reqs": reqs_by_field}
                b["reqs"] = parse_with(up, args[-1], env, items)
            m = re.match(r"consumeItem\(Items\.(\w+)(?:\s*,\s*(\d+))?\)", st)
            if m:
                b["consumes"].append((items[m.group(1)], int(m.group(2) or 1)))
            m = re.match(r"consumeItems\((.*)\)$", st, re.S)
            if m:
                b["consumes"] += parse_with(up, m.group(1), {}, items)
            m = re.match(r"consumePower\(([^)]*)\)", st)
            if m:
                b["power_use"] = jc.evaluate(m.group(1), {})
            m = re.match(r"consumePowerBuffered\(([^)]*)\)", st)
            if m:
                b["power_buffer"] = jc.evaluate(m.group(1), {})
            m = re.match(r"consumeLiquid\(Liquids\.(\w+),\s*([^)]*)\)\.boost\(\)", st)
            if m:
                b["boost"] = (m.group(1), jc.evaluate(m.group(2), {}))
            if st.startswith("consume(new ConsumeItemFlammable"):
                b["flammable"] = True
            m = re.match(r"outputItem\s*=\s*new ItemStack\(Items\.(\w+),\s*(\d+)\)", st)
            if m:
                b["output"] = (items[m.group(1)], int(m.group(2)))
            m = re.match(r"ammo\((.*)\)$", st, re.S)
            if m:
                args = jc.split_top(m.group(1), ",")
                for i in range(0, len(args), 2):
                    it = jc.evaluate(args[i], {})
                    bl = parse_bullet(up, args[i + 1])
                    b["ammo"].append((items[it.field], bl))
            m = re.match(r"limitRange\(([^)]*)\)$", st)
            if m:  # Turret.limitRange(margin = 9): bullet.lifetime = (range + margin) / speed
                b["limit_range"] = jc.evaluate(m.group(1), {}) if m.group(1).strip() else 9.0
            m = re.match(r"shoot\.(\w+)\s*=\s*(.+)$", st)
            if m:
                b.setdefault("shoot", {})[m.group(1)] = jc.evaluate(m.group(2), {})
        if "limit_range" in b:
            rng = vals.get("range", 80)
            for _, bl in b["ammo"]:
                bl["lifetime"] = (rng + bl.get("rangeChange", 0) + b["limit_range"]) / bl["speed"]
        reqs_by_field[d.field] = b["reqs"]
        return b

    for field, d in up.block_defs.items():
        if field in BUILDABLE:
            blocks.append(base(d, BUILDABLE[field]))
        elif d.name in used_env_names:
            if d.cls in ENV_FLOOR:
                t = "Floor"
            elif d.cls in ENV_OVERLAY:
                t = "Ore" if d.cls == "OreBlock" else "Overlay"
            elif d.cls in ENV_WALL:
                t = "StaticWall"
            elif d.cls in ENV_PROP:
                t = "Prop"
            else:
                continue
            b = base(d, t)
            if d.cls == "OreBlock":
                m = re.match(r"\s*Items\.(\w+)", d.args)
                b["item_drop"] = items[m.group(1)] if m else None
                if not b["name"]:
                    b["name"] = "ore-" + b["item_drop"]
            drop = b["vals"].get("itemDrop")
            if isinstance(drop, jc.Ref):
                b["item_drop"] = items[drop.field]
            blocks.append(b)
        else:
            # requirements of non-ported blocks may still be referenced (ItemStack.mult)
            for st in d.statements:
                m = re.match(r"requirements\((.*)\)$", st, re.S)
                if m:
                    try:
                        reqs_by_field[field] = parse_with(up, jc.split_top(m.group(1), ",")[-1],
                                                          {"__reqs": reqs_by_field}, items)
                    except Exception:
                        pass
    for b in blocks:
        if b["name"] is None:
            raise SystemExit("block without name: " + b["field"])
    return blocks


def collect_items(up):
    out = []
    for d in up.item_defs:
        if d.cls != "Item":
            continue
        vals, _ = eval_def(up, d)
        m = re.search(r'Color\.valueOf\("([0-9a-fA-F]+)"\)', d.args)
        vals["color"] = int(m.group(1)[:6], 16) if m else 0xffffff
        vals["name"] = d.name
        vals["field"] = d.field
        out.append(vals)
    serpulo = ["scrap", "copper", "lead", "graphite", "coal", "titanium", "thorium", "silicon",
               "plastanium", "phaseFabric", "surgeAlloy", "sporePod", "sand", "blastCompound",
               "pyratite", "metaglass"]
    order = {f: i for i, f in enumerate(serpulo)}
    out = [i for i in out if i["field"] in order]
    out.sort(key=lambda i: order[i["field"]])
    return out


def collect_units(up):
    out = []
    for f in UNITS:
        d = up.unit_defs[f]
        vals, _ = eval_def(up, d)
        vals["name"] = d.name
        weapon = None
        for st in d.statements:
            m = re.match(r"weapons\.add\((.*)\)$", st, re.S)
            if m and weapon is None:
                inner = jc.parse_inner_new(m.group(1))
                wcls, wargs, wbody = inner
                wv, _ = up.classes.defaults(wcls)
                wv = dict(wv)
                wv["sprite"] = jc.evaluate(wargs[0], {}) if wargs and wargs[0] else None
                for wst in jc.split_top(wbody, ";"):
                    mm = re.match(r"bullet\s*=\s*(new .*)$", wst, re.S)
                    if mm:
                        wv["bullet"] = parse_bullet(up, mm.group(1))
                for k, v in jc.assignments(jc.split_top(wbody, ";"), dict(wv)):
                    if k != "bullet":
                        wv[k] = v
                weapon = wv
        vals["weapon"] = weapon
        out.append(vals)
    return out


def collect_sectors(up):
    out = []
    for name in SECTORS:
        d = up.sector_defs[name]
        vals, _ = eval_def(up, d)
        vals["name"] = name
        out.append(vals)
    return out


# --------------------------------------------------------------------------
# Arc "minimal" JSON (unquoted keys/strings) used by map rules

def parse_minjson(s):
    pos = [0]

    def ws():
        while pos[0] < len(s) and s[pos[0]] in " \t\r\n":
            pos[0] += 1

    def value():
        ws()
        c = s[pos[0]]
        if c == "{":
            pos[0] += 1; obj = {}
            while True:
                ws()
                if s[pos[0]] == "}":
                    pos[0] += 1; return obj
                k = token(":")
                ws(); pos[0] += 1  # ':'
                obj[k] = value(); ws()
                if s[pos[0]] == ",":
                    pos[0] += 1
        if c == "[":
            pos[0] += 1; arr = []
            while True:
                ws()
                if s[pos[0]] == "]":
                    pos[0] += 1; return arr
                arr.append(value()); ws()
                if s[pos[0]] == ",":
                    pos[0] += 1
        t = token(",}]")
        if t in ("true", "false"):
            return t == "true"
        if t == "null":
            return None
        try:
            return int(t)
        except ValueError:
            try:
                return float(t)
            except ValueError:
                return t

    def token(stops):
        ws()
        if s[pos[0]] == '"':
            j = pos[0] + 1; out = []
            while s[j] != '"':
                if s[j] == "\\":
                    j += 1
                out.append(s[j]); j += 1
            pos[0] = j + 1
            return "".join(out)
        j = pos[0]
        while j < len(s) and s[j] not in stops:
            j += 1
        t = s[pos[0]:j].strip(); pos[0] = j
        return t

    return value()


def default_waves(up, unit_ids):
    """DefaultWaves.java spawn groups, used when a map defines none."""
    path = os.path.join(up.src, "game", "Waves.java")
    src = jc.strip_comments(open(path).read())
    groups = []
    for m in re.finditer(r"new SpawnGroup\((\w+)\)\s*\{\{(.*?)\}\}", src, re.S):
        g = {"type": m.group(1)}
        for k, v in jc.assignments(jc.split_top(m.group(2), ";"), {"never": 2147483647}):
            g[k] = v
        groups.append(g)
        if len(groups) > 40:
            break
    out = []
    for g in groups:
        t = g["type"]
        if t not in unit_ids:
            continue
        out.append({"type": t, "begin": g.get("begin", 0), "end": g.get("end", 2147483647),
                    "spacing": g.get("spacing", 1), "scaling": g.get("unitScaling", 2147483647),
                    "amount": g.get("unitAmount", 1), "max": g.get("max", 40),
                    "shields": g.get("shields", 0), "shieldScaling": g.get("shieldScaling", 0)})
    return out


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", default="upstream/Mindustry")
    ap.add_argument("--gen", default="build/gen")
    ap.add_argument("--gfx", default="build/gfx")
    ap.add_argument("--romfs", default="romfs")
    ap.add_argument("--skip-sprites", action="store_true", help="tables + maps only (host tests)")
    args = ap.parse_args()

    up = Upstream(args.upstream)
    names = item_field_to_name(up)
    for d in up.block_defs.values():
        if d.name is None and getattr(d, "ore_item_field", None):
            d.name = "ore-" + names[d.ore_item_field]
    up.block_by_name = {d.name: d for d in up.block_defs.values() if d.name}
    maps = {}
    used_env = {"air", "stone", "spawn"}
    for s in SECTORS:
        m = msav.read_msav(os.path.join(args.upstream, "core", "assets", "maps", s + ".msav"))
        for arr in (m.floor, m.overlay, m.block):
            for i, n in enumerate(arr):
                arr[i] = up.fallback.get(n, n)
        maps[s] = m
        used_env |= set(m.floor) | set(m.overlay) | set(m.block)

    blocks = collect_blocks(up, used_env)
    for b in blocks:  # Block.squareSprite, needed by the simulation tables even without sprites
        if b["type"] not in ("Floor", "Ore", "Overlay", "StaticWall", "Prop") and b.get("vals") is not None:
            layers = icon_layers(up, b)
            canvas = compose_icon(up, layers, int(b["vals"].get("size", 1) or 1))
            b["square"] = canvas.getchannel("A").getextrema()[0] >= int(0.9 * 255)
    items = collect_items(up)
    units = collect_units(up)
    sectors = collect_sectors(up)
    item_ids = {it["name"]: i for i, it in enumerate(items)}
    unit_ids = {u["name"]: i for i, u in enumerate(units)}
    # air must be id 0
    blocks.sort(key=lambda b: (b["type"] != "Air",))
    blocks.insert(0, {"field": "air", "name": "air", "type": "Air", "vals": {}, "reqs": [],
                      "category": None, "consumes": [], "ammo": []})
    blocks = [b for i, b in enumerate(blocks) if not (i > 0 and b["name"] == "air")]
    block_ids = {b["name"]: i for i, b in enumerate(blocks)}

    os.makedirs(args.gen, exist_ok=True)
    sprites = Sprites(up)
    if not args.skip_sprites:
        assign_sprites(up, sprites, blocks, items, units)
    write_content(args.gen, blocks, items, units, sectors, sprites, block_ids, item_ids, unit_ids)

    os.makedirs(os.path.join(args.romfs, "maps"), exist_ok=True)
    report = {}
    for s in SECTORS:
        report[s] = write_map(os.path.join(args.romfs, "maps", s + ".m3d"), maps[s], block_ids,
                              item_ids, unit_ids, blocks, default_waves(up, unit_ids))
    write_lang(up, args.romfs, blocks, items, units)
    if not args.skip_sprites:
        sprites.write(args.gfx)
        with open(os.path.join(args.gen, "sprites_gen.hpp"), "w") as f:
            f.write("// generated by tools/build_assets.py\n#pragma once\n")
            f.write("#define MDT_SHEET_COUNT %d\n" % sprites.sheet_count)
            f.write("static const unsigned short kSpriteRefs[][4] = {\n")
            for i, (s, local) in enumerate(sprites.refs):
                img = sprites.images[i]
                f.write("  {%d, %d, %d, %d}, // %s\n" % (s, local, img.width, img.height, sprites.names[i]))
            f.write("};\n")
            f.write("#define MDT_SPRITE_COUNT %d\n" % len(sprites.refs))
            f.write("enum SpriteName : short {\n")
            for i, nm in enumerate(sprites.names):
                f.write("  SPR_%s = %d,\n" % (ident(nm), i))
            f.write("};\n")
    with open(os.path.join(args.gen, "assets_report.json"), "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    print("blocks=%d items=%d units=%d sprites=%d sectors=%d" %
          (len(blocks), len(items), len(units), len(sprites.names), len(SECTORS)))


# --------------------------------------------------------------------------
def assign_sprites(up, sp, blocks, items, units):
    sp.add("error", required=False)
    for it in items:
        it["spr"] = sp.add("item-" + it["name"])
    for b in blocks:
        n, t = b["name"], b["type"]
        b["spr"] = b["spr_top"] = b["spr_rot"] = b["spr_team"] = b["spr_var"] = b["spr_extra"] = -1
        b["variants"] = 0
        if t == "Air":
            continue
        if t in ("Floor", "Ore", "Overlay", "StaticWall", "Prop"):
            if n == "spawn":
                continue
            v = up.variants(n)
            if v:
                b["variants"] = v
                b["spr_var"] = sp.add(n + "1")
                for i in range(2, v + 1):
                    sp.add(n + str(i))
            else:
                b["spr"] = sp.add(n, required=False)
            if t == "StaticWall" and up.sprite(n + "-shadow"):
                b["spr_extra"] = sp.add(n + "-shadow")
            continue
        if t == "Conveyor":
            b["spr_var"] = sp.add(n + "-0-0")
            for blend in range(5):
                for frame in range(4):
                    if blend or frame:
                        sp.add("%s-%d-%d" % (n, blend, frame))
            # Autotiler.sliced(): topHalf = left half of the region, botHalf = right half.
            # spr_extra + frame*2 = top half, + frame*2 + 1 = bottom half (Conveyor.draw joints)
            for frame in range(4):
                src = "%s-0-%d" % (n, frame)
                k = sp.add_derived(src + "-tophalf", src, lambda im: im.crop((0, 0, im.width // 2, im.height)))
                if frame == 0:
                    b["spr_extra"] = k
                sp.add_derived(src + "-bothalf", src, lambda im: im.crop((im.width // 2, 0, im.width, im.height)))
            continue
        if t == "ItemTurret":
            size = b["vals"].get("size", 1)
            b["spr"] = sp.add("block-%d" % size)
            # DrawTurret: region + RegionParts; the official "-preview" sprite is that composition
            top = n + "-preview" if up.sprite(n + "-preview") else n
            b["spr_top"] = sp.add(top, outline=True, alias=n + "-turret")
            continue
        if t == "Wall" and up.variants(n):
            b["variants"] = up.variants(n)
            b["spr_var"] = sp.add(n + "1")
            for i in range(2, b["variants"] + 1):
                sp.add(n + str(i))
            continue
        b["spr"] = sp.add(n)
        if t == "Drill":
            b["spr_rot"] = sp.add(n + "-rotator")
            b["spr_top"] = sp.add(n + "-top")
        elif t == "Crafter" and b.get("draw_regions"):
            # DrawRegion layers: "-rotator" spins (spinSprite), the others are always drawn
            for suf in b["draw_regions"]:
                if suf == "-rotator":
                    b["spr_rot"] = sp.add(n + suf)
                else:
                    b["spr_extra"] = sp.add(n + suf)
        elif t in ("Crafter", "ConsumeGen", "Battery") and up.sprite(n + "-top"):
            b["spr_top"] = sp.add(n + "-top")  # DrawFlame / DrawWarmupRegion / battery charge
        elif t == "Core":
            # team regions are masks multiplied by the team colour (Draw.color): derelict, sharded, crux
            b["spr_team"] = sp.add_derived(n + "-team-derelict", n + "-team", lambda im: tint_image(im, 0x4d4e58))
            sp.add_derived(n + "-team-sharded", n + "-team", lambda im: tint_image(im, 0xffd37f))
            sp.add_derived(n + "-team-crux", n + "-team", lambda im: tint_image(im, 0xf25555))
        elif t == "Bridge":
            b["spr_top"] = sp.add(n + "-end")
            b["spr_rot"] = sp.add(n + "-bridge")
            b["spr_extra"] = sp.add(n + "-arrow")
    for b in blocks:
        b["spr_icon"] = -1
        if b["type"] in ("Air", "Floor", "Ore", "Overlay", "StaticWall", "Prop"):
            continue
        layers = icon_layers(up, b)
        if layers:
            b["spr_icon"] = sp.add_icon(b["name"], layers, int(b["vals"].get("size", 1) or 1))
    for u in units:
        n = u["name"]
        u["spr"] = sp.add(n, outline=True)
        u["spr_cell"] = sp.add(n + "-cell", required=False)
        u["spr_base"] = sp.add(n + "-base", required=False)
        u["spr_leg"] = sp.add(n + "-leg", outline=True, required=False)
        w = u["weapon"]
        u["spr_weapon"] = sp.add(w["sprite"], outline=True, required=False) if w and w.get("sprite") else -1
    for extra in ("bullet", "bullet-back", "shell", "shell-back", "circle-shadow", "laser", "laser-end",
                  "minelaser", "minelaser-end", "drill-item-2", "power-node-laser", "laser-end",
                  "white", "shape-3"):
        sp.add(extra, required=False)
    sp.add("logo")


SHARDED = 0xffd37f  # Team.sharded.color


def compose_icon(up, layers, size):
    """Full-resolution composite of Block.icons() layers (tools/Generators.java)."""
    canvas = Image.new("RGBA", (32 * size, 32 * size))
    for name, outline, tint in layers:
        path = up.sprite(name)
        if not path:
            continue
        im = Image.open(path).convert("RGBA")
        if outline:
            im = add_outline(im, (0x40, 0x40, 0x49, 255), 3)
        if tint is not None:
            im = tint_image(im, tint)
        canvas.alpha_composite(im, ((canvas.width - im.width) // 2, (canvas.height - im.height) // 2))
    return canvas


def icon_layers(up, b):
    """Block.icons() of the upstream classes, as (sprite, outline, tint) layers."""
    n, t = b["name"], b["type"]
    size = int(b["vals"].get("size", 1) or 1)
    if t == "Drill":                        # Drill.icons(): region, rotator, top
        return [(n, False, None), (n + "-rotator", False, None), (n + "-top", False, None)]
    if t == "ItemTurret":                   # DrawTurret.icons(): base, preview, (top)
        top = n + "-preview" if up.sprite(n + "-preview") else n
        layers = [("block-%d" % size, False, None), (top, True, None)]
        if up.sprite(n + "-top"):
            layers.append((n + "-top", False, None))
        return layers
    if t == "Conveyor":                     # Conveyor.icons(): regions[0][0]
        return [(n + "-0-0", False, None)]
    if t == "Wall":                         # Wall.icons(): name or name + "1"
        return [(n if up.sprite(n) else n + "1", False, None)]
    if t == "Core":                         # Generators: shard team top drawn over the region
        return [(n, False, None), (n + "-team", False, SHARDED)]
    if t == "Battery":                      # DrawMulti(DrawDefault, DrawPower, DrawRegion("-top"))
        return [(n, False, None), (n + "-top", False, None)]
    if b.get("draw_regions"):               # DrawMulti icons: DrawDefault + every DrawRegion
        return [(n, False, None)] + [(n + suf, False, None) for suf in b["draw_regions"]]
    return [(n, False, None)]               # DrawDefault / Block.icons(): region


# --------------------------------------------------------------------------
def write_content(gen, blocks, items, units, sectors, sp, block_ids, item_ids, unit_ids):
    h = ["// generated by tools/build_assets.py from upstream/Mindustry - do not edit",
         "#pragma once", "#include \"content.hpp\"", "namespace mdt {"]
    h.append("enum ItemId : short { %s, ITEM_COUNT };" % ", ".join("I_" + ident(i["name"]) for i in items))
    h.append("enum BlockId : short { %s, BLOCK_COUNT };" % ", ".join("B_" + ident(b["name"]) for b in blocks))
    h.append("enum UnitId : short { %s, UNIT_COUNT };" % ", ".join("U_" + ident(u["name"]) for u in units))
    h.append("enum SectorId : short { %s, SECTOR_COUNT };" % ", ".join("S_" + ident(s["name"]) for s in sectors))
    h.append("}")
    open(os.path.join(gen, "content_gen.hpp"), "w").write("\n".join(h) + "\n")

    c = ["// generated by tools/build_assets.py from upstream/Mindustry - do not edit",
         "#include \"content_gen.hpp\"", "namespace mdt {"]
    c.append("const ItemDef kItems[ITEM_COUNT] = {")
    for it in items:
        c.append('  {"%s", 0x%06x, %s, %d, %s, %s, %s, %d, %d}, ' % (
            it["name"], it["color"], cfloat(it.get("cost", 1)), int(it.get("hardness", 0)),
            cfloat(it.get("flammability", 0)), cfloat(it.get("explosiveness", 0)),
            cfloat(it.get("healthScaling", 0)), 1 if it.get("buildable", True) else 0, it.get("spr", -1)))
    c.append("};")

    def bullet(bl):
        if not bl:
            return "{}"
        return "{%s, %s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %d}" % (
            cfloat(bl.get("speed")), cfloat(bl.get("damage")), cfloat(bl.get("lifetime")),
            cfloat(bl.get("ammoMultiplier", 2)), cfloat(bl.get("reloadMultiplier", 1)),
            cfloat(bl.get("splashDamage", 0)), cfloat(bl.get("splashDamageRadius", 0)),
            cfloat(bl.get("buildingDamageMultiplier", 1)), cfloat(bl.get("homingPower", 0)),
            1 if bl.get("collidesAir", True) else 0, 1 if bl.get("collidesGround", True) else 0,
            {"BasicBulletType": 0, "FlakBulletType": 1, "ArtilleryBulletType": 2}.get(bl.get("__class"), 0))

    c.append("const BlockDef kBlocks[BLOCK_COUNT] = {")
    for b in blocks:
        v = b["vals"]
        size = int(v.get("size", 1) or 1)
        reqs = b["reqs"]
        health = v.get("health", -1)
        if isinstance(health, str) or health is None:
            health = -1
        if health == -1:  # Block.init(): scaledHealth default 40 * (1 + sum(healthScaling))
            scaled = v.get("scaledHealth", -1)
            if not isinstance(scaled, (int, float)) or scaled < 0:
                scaling = 1.0 + sum(items[item_ids[it]].get("healthScaling", 0) for it, _ in reqs)
                health = round(size * size * 40 * scaling / 5) * 5
            else:
                health = int(size * size * scaled)
        build_cost = 20.0
        if reqs:  # Block.init(): buildCost = sum(amount * item.cost) * buildCostMultiplier
            build_cost = sum(a * items[item_ids[it]].get("cost", 1) for it, a in reqs)
        bcm = v.get("buildCostMultiplier", 1)
        build_cost *= bcm if isinstance(bcm, (int, float)) else 1
        cat = b.get("category")
        cat_i = CATEGORIES.index(cat) if cat in CATEGORIES else -1
        buildable = 1 if (cat is not None and b.get("visibility") not in ("editorOnly", "sandboxOnly", "hidden")) else 0
        req_s = ", ".join("{%d, %d}" % (item_ids[it], a) for it, a in reqs[:4])
        cons_s = ", ".join("{%d, %d}" % (item_ids[it], a) for it, a in b["consumes"][:3])
        out = b.get("output")
        shoot = b.get("shoot", {})
        ammo_s = ", ".join("{%d, %s}" % (item_ids[it], bullet(bl)) for it, bl in b["ammo"][:3])
        drop = b.get("item_drop")
        def num(k, d=0):
            x = v.get(k, d)
            return x if isinstance(x, (int, float)) and not isinstance(x, bool) else d
        def flag(k, d=False):
            x = v.get(k, d)
            return 1 if x is True else 0
        is_deep = 1 if num("drownTime") > 0 else 0
        c.append(("  {\"%s\", BType::%s, %d, %d, %d, %d, %d, %s, %s, %d, "
                  "{%s}, %d, {%s}, %d, {%d, %d}, %s, %s, %s, "
                  "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %s, "
                  "%s, %s, %d, %s, %s, %s, %s, %s, %s, %d, "
                  "%s, %s, %s, %s, %s, %d, %d, %d, %d, {%s}, %d, %d, %d},") % (
            b["name"], b["type"], cat_i, size, flag("solid"), flag("rotate"), buildable,
            cfloat(health), cfloat(build_cost), int(num("itemCapacity", 10)),
            req_s, len(reqs[:4]), cons_s, len(b["consumes"][:3]),
            item_ids[out[0]] if out else -1, out[1] if out else 0,
            cfloat(num("craftTime", 80)), cfloat(b.get("power_use", 0)), cfloat(b.get("power_buffer", 0)),
            b.get("spr", -1), b.get("spr_top", -1), b.get("spr_rot", -1), b.get("spr_team", -1),
            b.get("spr_var", -1), b.get("spr_extra", -1), b.get("variants", 0),
            item_ids.get(drop, -1) if drop else -1, flag("isLiquid"), is_deep, cfloat(num("speedMultiplier", 1)),
            cfloat(num("speed")), cfloat(num("displayedSpeed")), int(num("capacity", 0) or num("bufferCapacity", 0)),
            cfloat(num("range")), cfloat(num("drillTime", 300)), cfloat(num("hardnessDrillMultiplier", 50)),
            cfloat(num("warmupSpeed", 0.015)), cfloat(num("powerProduction")), cfloat(num("itemDuration", 120)),
            int(num("maxNodes", 3)),
            cfloat(num("laserRange", 6)), cfloat(num("reload", 10)), cfloat(num("rotateSpeed", 5)),
            cfloat(num("inaccuracy")), cfloat(num("shootCone", 8)), int(num("maxAmmo", 30)),
            int(shoot.get("shots", 1)), 1 if v.get("targetAir", True) is not False else 0,
            1 if v.get("targetGround", True) is not False else 0, ammo_s, len(b["ammo"][:3]),
            b.get("spr_icon", -1), 1 if b.get("square", True) else 0,
        ))
        b["tier"] = int(num("tier"))
    c.append("};")
    c.append("const unsigned char kBlockTier[BLOCK_COUNT] = {%s};" % ", ".join(str(b.get("tier", 0)) for b in blocks))

    c.append("const UnitDef kUnits[UNIT_COUNT] = {")
    for u in units:
        w = u["weapon"] or {}
        def un(k, d=0):
            x = u.get(k, d)
            return x if isinstance(x, (int, float)) and not isinstance(x, bool) else d
        bl = w.get("bullet")
        # UnitType.init(): range defaults to the max weapon bullet range (speed * lifetime)
        rng = un("range", -1)
        if rng < 0 and bl:
            rng = float(bl.get("speed", 1)) * float(bl.get("lifetime", 40))
        c.append("  {\"%s\", %s, %s, %s, %s, %s, %s, %d, %s, %d, %s, %d, %s, {%s, %s, %s, %d, %s}, %d, %d, %d, %d, %d, %s}," % (
            u["name"], cfloat(un("speed", 1.1)), cfloat(un("health", 200)), cfloat(un("hitSize", 6)),
            cfloat(un("rotateSpeed", 5)), cfloat(un("drag", 0.3)), cfloat(un("accel", 0.5)),
            1 if u.get("flying") is True else 0, cfloat(un("mineSpeed", 1)), int(un("mineTier", -1)),
            cfloat(max(0, un("buildSpeed", -1))), int(un("itemCapacity", -1)), cfloat(rng),
            cfloat(w.get("reload", 1) if isinstance(w.get("reload"), (int, float)) else 1),
            cfloat(w.get("x", 0) if isinstance(w.get("x"), (int, float)) else 0),
            cfloat(w.get("y", 0) if isinstance(w.get("y"), (int, float)) else 0),
            1 if w.get("mirror", True) is not False else 0, bullet(bl),
            u.get("spr", -1), u.get("spr_cell", -1), u.get("spr_base", -1), u.get("spr_leg", -1),
            u.get("spr_weapon", -1), cfloat(un("engineOffset", 5))))
    c.append("};")
    c.append("const SectorDef kSectors[SECTOR_COUNT] = {")
    for s in sectors:
        c.append('  {"%s", %d, %d, %s},' % (s["name"], int(s.get("captureWave", 0) or 0),
                                            int(s.get("difficulty", 0) or 0),
                                            cfloat(s.get("startWaveTimeMultiplier", 2) or 2)))
    c.append("};")
    c.append("}")
    open(os.path.join(gen, "content_gen.cpp"), "w").write("\n".join(c) + "\n")


# --------------------------------------------------------------------------
def write_map(path, m, block_ids, item_ids, unit_ids, blocks, fallback_waves):
    """M3D map: little endian, see source/mapio.cpp for the reader."""
    w, h = m.width, m.height
    btype = {b["name"]: b["type"] for b in blocks}
    dropped = {}

    def bid(name, kinds):
        i = block_ids.get(name)
        if i is None or btype[name] not in kinds:
            dropped[name] = dropped.get(name, 0) + 1
            return 0
        return i

    floors = [bid(n, ("Floor",)) or block_ids["stone"] for n in m.floor]
    ores = [bid(n, ("Ore", "Overlay")) if n != "air" else 0 for n in m.overlay]
    env = []
    for n in m.block:
        if n == "air" or btype.get(n) not in ("StaticWall", "Prop"):
            env.append(0)
        else:
            env.append(block_ids[n])
    builds = []
    for (x, y, name, team, rot) in m.buildings:
        if name in block_ids and btype[name] not in ("StaticWall", "Prop", "Floor", "Ore", "Overlay", "Air"):
            builds.append((x, y, block_ids[name], team, rot))
        else:
            dropped[name] = dropped.get(name, 0) + 1
    spawns = [(i % w, i // w) for i, n in enumerate(m.overlay) if n == "spawn"]
    rules = {}
    try:
        rules = parse_minjson(m.tags.get("rules", "{}"))
    except Exception as e:  # noqa: BLE001
        print("warning: rules of %s unreadable: %s" % (path, e))
    waves = []
    for g in rules.get("spawns", []) or []:
        t = g.get("type", "dagger")
        if t not in unit_ids:
            continue
        waves.append({"type": t, "begin": g.get("begin", 0), "end": g.get("end", 2147483647),
                      "spacing": g.get("spacing", 1), "scaling": g.get("scaling", 2147483647),
                      "amount": g.get("amount", 1), "max": g.get("max", 40),
                      "shields": g.get("shields", 0), "shieldScaling": g.get("shieldScaling", 0)})
    if not waves:
        waves = fallback_waves
    core_items = []
    loadout = rules.get("loadout") or []
    for st in loadout:
        if isinstance(st, dict) and st.get("item") in item_ids:
            core_items.append((item_ids[st["item"]], int(st.get("amount", 0))))
    wave_spacing = float(rules.get("waveSpacing", 7200))
    out = bytearray()
    out += b"M3DM" + struct.pack("<HHH", 1, w, h)
    name = m.tags.get("name", "map").encode()
    out += struct.pack("<H", len(name)) + name
    out += struct.pack("<fBB", wave_spacing, 1 if rules.get("waveTimer", True) else 0,
                       1 if rules.get("waves", True) else 0)
    for arr in (floors, ores, env):
        out += struct.pack("<%dH" % len(arr), *arr)
    out += struct.pack("<I", len(builds))
    for b in builds:
        out += struct.pack("<HHHBB", *b)
    out += struct.pack("<H", len(spawns))
    for s in spawns:
        out += struct.pack("<HH", *s)
    out += struct.pack("<H", len(core_items))
    for it in core_items:
        out += struct.pack("<hi", *it)
    out += struct.pack("<H", len(waves))
    for g in waves:
        out += struct.pack("<hiiiiifff", unit_ids[g["type"]], int(g["begin"]), int(min(g["end"], 2147483647)),
                           max(1, int(g["spacing"])), int(g["amount"]), int(g["max"]),
                           float(min(g["scaling"], 2147483647)), float(g["shields"]), float(g["shieldScaling"]))
    with open(path, "wb") as f:
        f.write(out)
    return {"size": [w, h], "buildings": len(builds), "spawns": len(spawns), "waves": len(waves),
            "dropped": dropped, "waveSpacing": wave_spacing}


def read_bundle(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1)
            out[k.strip()] = re.sub(r"\[[^\]]*\]", "", v.strip()).replace("\\n", "\n")
    return out


def write_lang(up, romfs, blocks, items, units):
    os.makedirs(os.path.join(romfs, "lang"), exist_ok=True)
    keys = ["block.%s.name" % b["name"] for b in blocks if b["type"] != "Air"]
    keys += ["block.%s.description" % b["name"] for b in blocks if b.get("category")]
    keys += ["item.%s.name" % i["name"] for i in items]
    keys += ["unit.%s.name" % u["name"] for u in units]
    keys += ["sector.%s.name" % s for s in SECTORS]
    keys += ["sector.%s.description" % s for s in SECTORS]
    keys += ["wave", "wave.waiting", "wave.waveInProgress", "quit", "ok", "back", "loading",
             "gameover", "sector.captured", "save.quit", "campaign", "play", "paused", "resume",
             "settings", "about.button", "objective.research", "launch.text", "unpause", "skipwave"]
    for code, fn in (("en", "bundle.properties"), ("fr", "bundle_fr.properties"),
                     ("es", "bundle_es.properties"), ("de", "bundle_de.properties"),
                     ("it", "bundle_it.properties"), ("ja", "bundle_ja.properties")):
        base = read_bundle(os.path.join(up.root, "core", "assets", "bundles", "bundle.properties"))
        loc = read_bundle(os.path.join(up.root, "core", "assets", "bundles", fn))
        with open(os.path.join(romfs, "lang", code + ".txt"), "w", encoding="utf-8") as f:
            for k in keys:
                v = loc.get(k) or base.get(k)
                if v:
                    f.write("%s=%s\n" % (k, v.replace("\n", "\\n")))


if __name__ == "__main__":
    main()

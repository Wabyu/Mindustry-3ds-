"""Reader for Mindustry .msav map/save files (format version 7, Mindustry v146).

Mirrors mindustry/io/SaveVersion.java + SaveFileReader.java from the upstream
sources. Only reads what the 3DS port needs: meta tags, content name tables,
floor/overlay/block grids and the team/rotation of buildings.
"""
import struct
import zlib

CONTENT_TYPES = ["item", "block", "mech_UNUSED", "bullet", "liquid", "status", "unit",
                 "weather", "effect_UNUSED", "sector", "loadout_UNUSED", "typeid_UNUSED",
                 "error", "planet", "ammo_UNUSED", "team", "unitCommand", "unitStance"]


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise EOFError("unexpected end of msav data")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self): return self.take(1)[0]
    def i8(self): return struct.unpack(">b", self.take(1))[0]
    def i16(self): return struct.unpack(">h", self.take(2))[0]
    def u16(self): return struct.unpack(">H", self.take(2))[0]
    def i32(self): return struct.unpack(">i", self.take(4))[0]
    def f32(self): return struct.unpack(">f", self.take(4))[0]
    def utf(self):
        n = self.u16()
        return self.take(n).decode("utf-8", "replace")  # Java modified UTF-8; fine for map data


class MapData:
    def __init__(self):
        self.version = 0
        self.tags = {}
        self.content = {}  # type name -> list of names (index = save id)
        self.width = self.height = 0
        self.floor = []    # names per tile index (x + y*w), y=0 is bottom like Mindustry
        self.overlay = []
        self.block = []
        self.buildings = []  # (x, y, block name, team id, rotation)


def read_msav(path):
    raw = open(path, "rb").read()
    data = zlib.decompress(raw)
    r = Reader(data)
    if r.take(4) != b"MSAV":
        raise ValueError("not an MSAV file")
    m = MapData()
    m.version = r.i32()
    if m.version < 4:
        raise ValueError("save version %d not supported (need 4-7)" % m.version)

    # meta region
    r.i32()
    for _ in range(r.i16()):
        k = r.utf(); m.tags[k] = r.utf()

    # content region
    r.i32()
    for _ in range(r.u8()):
        t = CONTENT_TYPES[r.u8()]
        m.content[t] = [r.utf() for _ in range(r.i16())]
    blocks = m.content.get("block", [])

    def bname(i):
        return blocks[i] if 0 <= i < len(blocks) else "air"

    # map region
    r.i32()
    w = m.width = r.u16(); h = m.height = r.u16()
    n = w * h
    floor = [None] * n; ore = [None] * n; blk = ["air"] * n
    i = 0
    while i < n:
        f = bname(r.i16()); o = bname(r.i16()); c = r.u8()
        if f == "air": f = "stone"
        for j in range(i, min(n, i + 1 + c)):
            floor[j] = f; ore[j] = o
        i += 1 + c
    i = 0
    while i < n:
        b = bname(r.i16())
        packed = r.i8()
        had_entity = packed & 1; had_data = packed & 2
        center = True
        if had_entity:
            center = r.u8() != 0
        if center:
            blk[i] = b
        if had_entity:
            if center:
                ln = r.u16()
                start = r.p
                if ln >= 7:
                    r.u8()  # entity revision
                    r.f32()  # health
                    rot = r.u8(); team = r.u8()
                    m.buildings.append((i % w, i // w, b, team, rot & 0x7f))
                r.p = start + ln
            i += 1
        elif had_data:
            blk[i] = b
            r.u8()
            i += 1
        else:
            c = r.u8()
            for j in range(i, min(n, i + 1 + c)):
                blk[j] = b
            i += 1 + c
    m.floor, m.overlay, m.block = floor, ore, blk
    return m


if __name__ == "__main__":
    import sys, collections
    m = read_msav(sys.argv[1])
    print("version", m.version, "size", m.width, m.height)
    for k in ("name", "author", "description", "width", "height"):
        print(k, "=", m.tags.get(k))
    print("rules head:", m.tags.get("rules", "")[:300])
    print("floors", collections.Counter(m.floor).most_common(12))
    print("ores", collections.Counter(m.overlay).most_common(8))
    print("blocks", collections.Counter(m.block).most_common(12))
    print("buildings", collections.Counter((b[2], b[3]) for b in m.buildings).most_common(12))

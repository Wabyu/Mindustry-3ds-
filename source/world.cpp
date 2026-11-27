// World, map loading, placement and item transfer rules.
// Item rules are ports of acceptItem/handleItem from mindustry.world.blocks.*
#include "game.hpp"
#include <algorithm>
#include <cstring>

namespace mdt {

static const int d4x[4] = {1, 0, -1, 0};
static const int d4y[4] = {0, 1, 0, -1};

static uint32_t rngState = 0x12345678u;
float randf() {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return (rngState & 0xffffff) / 16777216.f;
}
float approach(float from, float to, float amount) {
    return from < to ? std::fmin(from + amount, to) : std::fmax(from - amount, to);
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float angleDist(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 360.f);
    return d > 180 ? 360 - d : d;
}
// arc.math.Angles.moveToward
float moveToward(float angle, float to, float speed) {
    if (std::fabs(angleDist(angle, to)) < speed) return std::fmod(to + 360.f, 360.f);
    float diff = std::fmod(to - angle + 540.f, 360.f) - 180.f;
    angle += diff > 0 ? speed : -speed;
    return std::fmod(angle + 360.f, 360.f);
}

int SpawnGroupDef::getSpawned(int w) const {
    int sp = spacing <= 0 ? 1 : spacing;
    if (w < begin || w > end || (w - begin) % sp != 0) return 0;
    return std::min(amount + (int)(((w - begin) / sp) / scaling), max);
}

// ---------------------------------------------------------------- map loading
namespace {
struct In {
    const std::vector<uint8_t>& d; size_t p = 0; bool ok = true;
    explicit In(const std::vector<uint8_t>& v) : d(v) {}
    template <class T> T get() {
        T v{};
        if (p + sizeof(T) > d.size()) { ok = false; return v; }
        std::memcpy(&v, &d[p], sizeof(T)); p += sizeof(T); return v;
    }
};
}

bool Game::loadMap(const std::vector<uint8_t>& data, int sectorId) {
    In in(data);
    if (data.size() < 10 || std::memcmp(data.data(), "M3DM", 4) != 0) return false;
    in.p = 4;
    uint16_t version = in.get<uint16_t>();
    if (version != 1) return false;
    width = in.get<uint16_t>(); height = in.get<uint16_t>();
    uint16_t nlen = in.get<uint16_t>();
    if (in.p + nlen > data.size()) return false;
    mapName.assign((const char*)&data[in.p], nlen); in.p += nlen;
    waveSpacing = in.get<float>();
    waveTimer = in.get<uint8_t>() != 0;
    wavesEnabled = in.get<uint8_t>() != 0;
    size_t n = (size_t)width * height;
    tiles.assign(n, Tile{});
    for (size_t i = 0; i < n; i++) tiles[i].floor = in.get<uint16_t>();
    for (size_t i = 0; i < n; i++) tiles[i].ore = in.get<uint16_t>();
    for (size_t i = 0; i < n; i++) tiles[i].env = in.get<uint16_t>();
    for (size_t i = 0; i < n; i++) {
        if (tiles[i].floor >= BLOCK_COUNT) tiles[i].floor = B_stone;
        if (tiles[i].ore >= BLOCK_COUNT) tiles[i].ore = 0;
        if (tiles[i].env >= BLOCK_COUNT) tiles[i].env = 0;
    }
    builds.clear(); freeBuilds.clear(); units.clear(); bullets.clear(); plans.clear(); effects.clear();
    uint32_t nb = in.get<uint32_t>();
    for (uint32_t i = 0; i < nb && in.ok; i++) {
        uint16_t x = in.get<uint16_t>(), y = in.get<uint16_t>(), b = in.get<uint16_t>();
        uint8_t team = in.get<uint8_t>(), rot = in.get<uint8_t>();
        if (b < BLOCK_COUNT && validPlace((int16_t)b, x, y, rot & 3, team, false))
            addBuilding((int16_t)b, x, y, team, rot & 3);
    }
    spawns.clear();
    uint16_t ns = in.get<uint16_t>();
    for (int i = 0; i < ns && in.ok; i++) { int x = in.get<uint16_t>(); int y = in.get<uint16_t>(); spawns.push_back({x, y}); }
    uint16_t ni = in.get<uint16_t>();
    std::vector<Stack> coreItems;
    for (int i = 0; i < ni && in.ok; i++) { int16_t it = in.get<int16_t>(); int32_t a = in.get<int32_t>(); coreItems.push_back({it, (int16_t)std::min<int32_t>(a, 30000)}); }
    waves.clear();
    uint16_t nw = in.get<uint16_t>();
    for (int i = 0; i < nw && in.ok; i++) {
        SpawnGroupDef g{};
        g.type = in.get<int16_t>(); g.begin = in.get<int32_t>(); g.end = in.get<int32_t>();
        g.spacing = in.get<int32_t>(); g.amount = in.get<int32_t>(); g.max = in.get<int32_t>();
        g.scaling = in.get<float>(); g.shields = in.get<float>(); g.shieldScaling = in.get<float>();
        if (g.type >= 0 && g.type < UNIT_COUNT) waves.push_back(g);
    }
    if (!in.ok) return false;

    sector = sectorId;
    const SectorDef& sd = kSectors[sectorId];
    winWave = sd.captureWave;
    wave = 1;
    // Sector start: the wave countdown is longer for the first wave
    wavetime = waveSpacing * sd.startWaveTimeMultiplier;
    // In the campaign, waves run on a timer; world processors are not ported, so
    // sectors whose objectives would enable the timer (Ground Zero) start it directly.
    waveTimer = true;
    state = GameState::Playing;
    tick = 0;
    Building* c = core(TeamSharded);
    if (c) {
        for (auto& s : coreItems) if (s.item >= 0 && s.item < ITEM_COUNT) c->items.add(s.item, s.amount);
        // SectorPreset.addStartingItems / default loadout: ItemStack.list(Items.copper, 100) for core shard
        if (c->items.total == 0) c->items.add(I_copper, 200);
    }
    powerGraphsDirty = true;
    flowDirty = true;
    player = -1;
    respawnTimer = 0;
    std::memset(itemsDelivered, 0, sizeof(itemsDelivered));
    return true;
}

// ---------------------------------------------------------------- tiles
bool Game::solid(int x, int y) {
    Tile* t = tile(x, y);
    if (!t) return true;
    if (t->env && kBlocks[t->env].type == BType::StaticWall) return true;
    if (t->build >= 0) return builds[t->build].def().solid || builds[t->build].constructing;
    return false;
}
bool Game::solidFor(int x, int y, uint8_t team) {
    (void)team;
    return solid(x, y);
}
bool Game::isDeep(int x, int y) {
    Tile* t = tile(x, y);
    return t && kBlocks[t->floor].isDeep;
}
int Game::drop(int x, int y) {
    Tile* t = tile(x, y);
    if (!t) return -1;
    if (t->ore && kBlocks[t->ore].itemDrop >= 0) return kBlocks[t->ore].itemDrop;
    return kBlocks[t->floor].itemDrop;
}

void Game::linkedTiles(int16_t block, int tx, int ty, int& x0, int& y0, int& x1, int& y1) const {
    int s = kBlocks[block].size;
    int off = -((s - 1) / 2);  // Block.sizeOffset
    x0 = tx + off; y0 = ty + off; x1 = x0 + s - 1; y1 = y0 + s - 1;
}

// Build.validPlace (subset): bounds, static walls, deep water, occupied tiles, drill ore
bool Game::validPlace(int16_t block, int tx, int ty, uint8_t rotation, uint8_t team, bool allowReplace) {
    (void)rotation; (void)team;
    const BlockDef& d = kBlocks[block];
    int x0, y0, x1, y1;
    linkedTiles(block, tx, ty, x0, y0, x1, y1);
    bool ore = false;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Tile* t = tile(x, y);
            if (!t) return false;
            if (t->env && kBlocks[t->env].type == BType::StaticWall) return false;
            if (kBlocks[t->floor].isDeep && !d.floating) return false;  // Build.validPlace: deep water unless floating
            if (t->build >= 0) {
                Building& o = builds[t->build];
                // replacing a conveyor with another of the same size (rotation change / upgrade)
                bool repl = allowReplace && o.def().size == d.size && o.tx == tx && o.ty == ty &&
                            o.def().type == d.type && d.type == BType::Conveyor;
                if (!repl) return false;
            }
            if (d.type == BType::Drill) {
                int it = drop(x, y);
                if (it >= 0 && kItems[it].hardness <= kBlockTier[block]) ore = true;
            }
        }
    if (d.type == BType::Drill && !ore) return false;
    if (d.type == BType::Pump && !pumpPlaceValid(block, tx, ty)) return false;
    return true;
}

Building* Game::core(uint8_t team) {
    for (auto& b : builds) if (b.alive && b.team == team && b.def().type == BType::Core && !b.constructing) return &b;
    return nullptr;
}
Building* Game::closestCore(uint8_t team, float x, float y) {
    Building* best = nullptr; float bd = 1e30f;
    for (auto& b : builds) {
        if (!b.alive || b.team != team || b.def().type != BType::Core || b.constructing) continue;
        float dx = b.tx * tilesize - x, dy = b.ty * tilesize - y, d2 = dx * dx + dy * dy;
        if (d2 < bd) { bd = d2; best = &b; }
    }
    return best;
}

// ---------------------------------------------------------------- buildings
int32_t Game::addBuilding(int16_t block, int tx, int ty, uint8_t team, uint8_t rotation) {
    int x0, y0, x1, y1;
    linkedTiles(block, tx, ty, x0, y0, x1, y1);
    // replacement (conveyor upgrade): ConveyorBuild.overwrote keeps the items on the belt
    Building prevConveyor;
    bool keepConveyor = false;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Tile* t = tile(x, y);
            if (t && t->build >= 0) {
                Building& o = builds[t->build];
                if (o.def().type == BType::Conveyor && kBlocks[block].type == BType::Conveyor && !o.constructing) {
                    prevConveyor = o; keepConveyor = true;
                }
                removeBuilding(t->build, false);
            }
            if (t && t->env && kBlocks[t->env].type == BType::Prop) t->env = 0;  // props are alwaysReplace
        }
    int32_t idx;
    if (!freeBuilds.empty()) { idx = freeBuilds.back(); freeBuilds.pop_back(); builds[idx] = Building{}; }
    else { idx = (int32_t)builds.size(); builds.emplace_back(); }
    Building& b = builds[idx];
    b.alive = true; b.id = idx; b.block = block; b.team = team; b.rotation = rotation & 3;
    b.tx = tx; b.ty = ty;
    b.health = kBlocks[block].health;
    if (kBlocks[block].type == BType::Junction) {
        for (int i = 0; i < 4; i++) { b.jitem[i].clear(); b.jtime[i].clear(); }
    }
    if (keepConveyor) {
        b.len = prevConveyor.len; b.mid = prevConveyor.mid; b.minitem = prevConveyor.minitem; b.clogHeat = prevConveyor.clogHeat;
        for (int i = 0; i < 3; i++) { b.ids[i] = prevConveyor.ids[i]; b.xs[i] = prevConveyor.xs[i]; b.ys[i] = prevConveyor.ys[i]; }
        b.items = prevConveyor.items;
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) tile(x, y)->build = idx;
    refreshAround(idx);
    if (kBlocks[block].type == BType::PowerNode || kBlocks[block].powerUse > 0 ||
        kBlocks[block].powerProduction > 0 || kBlocks[block].powerBuffer > 0) {
        powerGraphsDirty = true;
    }
    // Pathfinder costs only change for solid blocks (walls, turrets, drills...);
    // conveyors are walkable, so placing them does not trigger a 65k-tile Dijkstra on the ARM11.
    if (kBlocks[block].solid) flowDirty = true;
    return idx;
}

void Game::removeBuilding(int32_t idx, bool destroyed) {
    Building& b = builds[idx];
    if (!b.alive) return;
    int x0, y0, x1, y1;
    linkedTiles(b.block, b.tx, b.ty, x0, y0, x1, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) { Tile* t = tile(x, y); if (t && t->build == idx) t->build = -1; }
    if (destroyed) addEffect(b.tx * tilesize, b.ty * tilesize, 0xffaa5f, b.def().size * 6.f, 30, 1);
    // drop power links that point here
    for (auto& o : builds) {
        if (!o.alive || o.links.empty()) continue;
        o.links.erase(std::remove(o.links.begin(), o.links.end(), idx), o.links.end());
    }
    for (auto& o : builds) if (o.alive && o.link >= 0 && o.link == b.ty * width + b.tx) o.link = -1;
    std::vector<int32_t> prox = b.proximity;
    if (b.def().solid) flowDirty = true;
    b.alive = false;
    b.proximity.clear();
    freeBuilds.push_back(idx);
    for (int32_t p : prox) if (builds[p].alive) updateProximity(p);
    // neighbours of neighbours: conveyor blending depends on diagonal context
    for (int32_t p : prox) if (builds[p].alive) for (int32_t q : builds[p].proximity) if (builds[q].alive) updateProximity(q);
    powerGraphsDirty = true;
}

void Game::refreshAround(int32_t idx) {
    updateProximity(idx);
    std::vector<int32_t> prox = builds[idx].proximity;
    for (int32_t p : prox) {
        updateProximity(p);
        for (int32_t q : builds[p].proximity) if (q != idx && builds[q].alive) updateProximity(q);
    }
}

static bool outputsItems(const BlockDef& d) {
    switch (d.type) {
        case BType::Conveyor: case BType::Junction: case BType::Router: case BType::Sorter:
        case BType::OverflowGate: case BType::Bridge: case BType::Drill: case BType::Crafter:
        case BType::Core: case BType::Storage: case BType::ConsumeGen:
            return true;
        default: return false;
    }
}

// Block.hasItems for the ported block types
static bool hasItems(const BlockDef& d) {
    return outputsItems(d) || d.type == BType::ItemTurret;
}

// A neighbour as seen by the autotiler: a building, or a build plan when previewing
struct TileBlock { int16_t block; int tx, ty; uint8_t rotation; };

static bool neighbourAt(Game& g, int x, int y, uint8_t team, bool usePlans, TileBlock& out) {
    Building* o = g.buildAt(x, y);
    if (o && o->team == team && !(o->constructing && !usePlans)) { out = {o->block, o->tx, o->ty, o->rotation}; return true; }
    if (usePlans)
        for (auto& p : g.plans)
            if (!p.breaking) {
                int x0, y0, x1, y1;
                g.linkedTiles(p.block, p.tx, p.ty, x0, y0, x1, y1);
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1) { out = {p.block, p.tx, p.ty, p.rotation}; return true; }
            }
    return false;
}

// Autotiler.blends(tile, rotation, ..., direction) + Conveyor.blends
static bool conveyorBlendsAt(Game& g, int tx, int ty, int rotation, uint8_t team, int direction, bool usePlans, TileBlock* who = nullptr) {
    int dir = ((rotation - direction) % 4 + 4) % 4;
    TileBlock other;
    if (!neighbourAt(g, tx + d4x[dir], ty + d4y[dir], team, usePlans, other)) return false;
    if (who) *who = other;
    const BlockDef& od = kBlocks[other.block];
    // lookingAt: the tile in front of this conveyor belongs to the other block
    int fx = tx + d4x[rotation], fy = ty + d4y[rotation];
    int ox0, oy0, ox1, oy1;
    g.linkedTiles(other.block, other.tx, other.ty, ox0, oy0, ox1, oy1);
    bool lookingAt = fx >= ox0 && fx <= ox1 && fy >= oy0 && fy <= oy1;
    if (!(outputsItems(od) || (lookingAt && hasItems(od)))) return false;
    // lookingAtEither
    if (lookingAt) return true;
    if (!od.rotate) return true;  // !rotatedOutput
    return other.tx + d4x[other.rotation] == tx && other.ty + d4y[other.rotation] == ty;
}

void Game::conveyorTiling(int tx, int ty, int rotation, uint8_t team, bool usePlans,
                          uint8_t& bits, int8_t& sclx, int8_t& scly, uint8_t& blending) {
    bool l = conveyorBlendsAt(*this, tx, ty, rotation, team, 1, usePlans);
    bool back = conveyorBlendsAt(*this, tx, ty, rotation, team, 2, usePlans);
    bool r = conveyorBlendsAt(*this, tx, ty, rotation, team, 3, usePlans);
    // Autotiler.buildBlending + transformCase
    int num = (back && l && r) ? 0 : (l && r) ? 1 : (l && back) ? 2 : (r && back) ? 3 : l ? 4 : r ? 5 : -1;
    bits = 0; sclx = 1; scly = 1;
    switch (num) {
        case 0: bits = 3; break;
        case 1: bits = 4; break;
        case 2: bits = 2; break;
        case 3: bits = 2; scly = -1; break;
        case 4: bits = 1; scly = -1; break;
        case 5: bits = 1; break;
        default: break;
    }
    // blendresult[4]: joints drawn toward non-square neighbours (Conveyor.draw)
    blending = 0;
    for (int i = 0; i < 4; i++) {
        TileBlock who;
        if (conveyorBlendsAt(*this, tx, ty, rotation, team, i, usePlans, &who) && !kBlocks[who.block].squareSprite)
            blending |= (uint8_t)(1 << i);
    }
}

void Game::updateProximity(int32_t idx) {
    Building& b = builds[idx];
    if (!b.alive) return;
    b.proximity.clear();
    int x0, y0, x1, y1;
    linkedTiles(b.block, b.tx, b.ty, x0, y0, x1, y1);
    auto addAt = [&](int x, int y) {
        int o = buildIndexAt(x, y);
        if (o >= 0 && o != idx && builds[o].alive &&
            std::find(b.proximity.begin(), b.proximity.end(), o) == b.proximity.end())
            b.proximity.push_back(o);
    };
    for (int y = y0; y <= y1; y++) addAt(x1 + 1, y);
    for (int x = x1; x >= x0; x--) addAt(x, y1 + 1);
    for (int y = y1; y >= y0; y--) addAt(x0 - 1, y);
    for (int x = x0; x <= x1; x++) addAt(x, y0 - 1);
    if (b.cdump >= (int)b.proximity.size()) b.cdump = 0;
    b.sleeping = false;

    if (b.def().type == BType::Conduit) liquidTiling(b);
    if (b.def().type == BType::Pump) {
        // Pump.onProximityUpdate: liquidDrop and sum of liquidMultiplier of the pumpable tiles
        b.pumpDrop = -1; b.pumpTiles = 0;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                Tile* t = tile(x, y);
                if (t && kBlocks[t->floor].liquidDrop >= 0) { b.pumpDrop = kBlocks[t->floor].liquidDrop; b.pumpTiles += kBlocks[t->floor].liquidMultiplier; }
            }
    }
    if (b.def().type == BType::Conveyor) {
        Building* n = buildAt(b.tx + d4x[b.rotation], b.ty + d4y[b.rotation]);
        int ni = buildIndexAt(b.tx + d4x[b.rotation], b.ty + d4y[b.rotation]);
        b.next = (n && n->team == b.team && !n->constructing) ? ni : -1;
        b.aligned = b.next >= 0 && n->def().type == BType::Conveyor && n->rotation == b.rotation;
        conveyorTiling(b.tx, b.ty, b.rotation, b.team, false, b.blendbits, b.blendsclx, b.blendscly, b.blending);
    }
}

// Pump.canPlaceOn: every pumpable tile must give the same liquid, and at least one must
bool Game::pumpPlaceValid(int16_t block, int tx, int ty) {
    int x0, y0, x1, y1;
    linkedTiles(block, tx, ty, x0, y0, x1, y1);
    int last = -1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Tile* t = tile(x, y);
            if (!t) return false;
            int drop = kBlocks[t->floor].liquidDrop;
            if (drop < 0) continue;
            if (last >= 0 && drop != last) return false;
            last = drop;
        }
    return last >= 0;
}

// Conduit: Autotiler with Conduit.blends = hasLiquids && (outputsLiquid || lookingAt) && lookingAtEither
void Game::liquidTiling(Building& b) {
    auto blends = [&](int direction) {
        int dir = ((b.rotation - direction) % 4 + 4) % 4;
        Building* o = buildAt(b.tx + d4x[dir], b.ty + d4y[dir]);
        if (!o || o->team != b.team || o->constructing || !o->def().hasLiquids) return false;
        int fx = b.tx + d4x[b.rotation], fy = b.ty + d4y[b.rotation];
        int ox0, oy0, ox1, oy1;
        linkedTiles(o->block, o->tx, o->ty, ox0, oy0, ox1, oy1);
        bool lookingAt = fx >= ox0 && fx <= ox1 && fy >= oy0 && fy <= oy1;
        if (!(o->def().outputsLiquid || lookingAt)) return false;
        if (lookingAt || !o->def().rotate) return true;
        return o->tx + d4x[o->rotation] == b.tx && o->ty + d4y[o->rotation] == b.ty;
    };
    bool l = blends(1), back = blends(2), r = blends(3);
    int num = (back && l && r) ? 0 : (l && r) ? 1 : (l && back) ? 2 : (r && back) ? 3 : l ? 4 : r ? 5 : -1;
    b.blendbits = 0; b.blendsclx = 1; b.blendscly = 1; b.blending = 0;
    switch (num) {
        case 0: b.blendbits = 3; break;
        case 1: b.blendbits = 4; break;
        case 2: b.blendbits = 2; break;
        case 3: b.blendbits = 2; b.blendscly = -1; break;
        case 4: b.blendbits = 1; b.blendscly = -1; break;
        case 5: b.blendbits = 1; break;
        default: break;
    }
}

// direction (0-3) from `from` to `to` using the facing edge, -1 if not adjacent
int Game::relativeTo(const Building& from, const Building& to) const {
    int fx0, fy0, fx1, fy1, tx0, ty0, tx1, ty1;
    linkedTiles(from.block, from.tx, from.ty, fx0, fy0, fx1, fy1);
    linkedTiles(to.block, to.tx, to.ty, tx0, ty0, tx1, ty1);
    // Edges.getFacingEdge(from, to): tile of `from` closest to `to`
    int ex = std::clamp(to.tx, fx0, fx1), ey = std::clamp(to.ty, fy0, fy1);
    for (int i = 0; i < 4; i++) {
        int nx = ex + d4x[i], ny = ey + d4y[i];
        if (nx >= tx0 && nx <= tx1 && ny >= ty0 && ny <= ty1) return i;
    }
    return -1;
}

static int itemTurretAmmo(const BlockDef& d, int item) {
    for (int i = 0; i < d.nammo; i++) if (d.ammo[i].item == item) return i;
    return -1;
}

static bool consumesItem(const BlockDef& d, int item) {
    for (int i = 0; i < d.nconsumes; i++) if (d.consumes[i].item == item) return true;
    return false;
}

// Sorter.getTileTarget
static Building* sorterTarget(Game& g, Building& b, Building& source, int item, bool flip) {
    int dir = g.relativeTo(source, b);
    if (dir < 0) return nullptr;
    bool invert = b.block == B_inverted_sorter;
    Building* to = nullptr;
    auto nearby = [&](int d) { return g.buildAt(b.tx + d4x[(d + 4) % 4], b.ty + d4y[(d + 4) % 4]); };
    auto instant = [](Building* x) { return x && (x->def().type == BType::Sorter || x->def().type == BType::OverflowGate); };
    if ((item == b.sortItem) != invert) {
        Building* n = nearby(dir);
        if (instant(&source) && instant(n)) return nullptr;
        to = n;
    } else {
        Building* a = nearby(dir - 1);
        Building* c = nearby(dir + 1);
        bool ac = a && a->team == b.team && !(instant(a) && instant(&source)) && g.acceptItem(*a, b, item);
        bool cc = c && c->team == b.team && !(instant(c) && instant(&source)) && g.acceptItem(*c, b, item);
        if (ac && !cc) to = a;
        else if (cc && !ac) to = c;
        else if (!cc) return nullptr;
        else {
            to = (b.rotation & (1 << dir)) == 0 ? a : c;
            if (flip) b.rotation ^= (1 << dir);
        }
    }
    return to;
}

// OverflowGate.getTileTarget
static Building* overflowTarget(Game& g, Building& b, Building& src, int item, bool flip) {
    int from = g.relativeTo(b, src);
    if (from < 0) return nullptr;
    bool inv = b.block == B_underflow_gate;
    auto nearby = [&](int d) { return g.buildAt(b.tx + d4x[(d + 4) % 4], b.ty + d4y[(d + 4) % 4]); };
    auto instant = [](Building* x) { return x && (x->def().type == BType::Sorter || x->def().type == BType::OverflowGate); };
    Building* to = nearby(from + 2);
    bool fromInst = instant(&src);
    bool canForward = to && to->team == b.team && !(fromInst && instant(to)) && g.acceptItem(*to, b, item);
    if (!canForward || inv) {
        Building* a = nearby(from - 1);
        Building* c = nearby(from + 1);
        bool ac = a && !(fromInst && instant(a)) && a->team == b.team && g.acceptItem(*a, b, item);
        bool cc = c && !(fromInst && instant(c)) && c->team == b.team && g.acceptItem(*c, b, item);
        if (!ac && !cc) return inv && canForward ? to : nullptr;
        if (ac && !cc) to = a;
        else if (cc && !ac) to = c;
        else {
            to = (b.rotation & (1 << from)) == 0 ? a : c;
            if (flip) b.rotation ^= (1 << from);
        }
    }
    return to;
}

bool Game::acceptItem(Building& b, Building& source, int item) {
    if (b.constructing || item < 0) return false;
    const BlockDef& d = b.def();
    switch (d.type) {
        case BType::Conveyor: {
            if (b.len >= 3) return false;
            int dirTo = relativeTo(source, b);
            if (dirTo < 0) return false;
            int direction = std::abs(dirTo - b.rotation);
            bool srcRot = source.def().rotate;
            Building* nx = b.next >= 0 ? &builds[b.next] : nullptr;
            if ((d.variant & 1) && source.def().type != BType::Conveyor && dirTo != b.rotation) return false;  // ArmoredConveyor
            return (((direction == 0) && b.minitem >= 0.4f) || ((direction % 2 == 1) && b.minitem > 0.7f)) &&
                   !(srcRot && nx == &source);
        }
        case BType::Junction: {
            int rel = relativeTo(source, b);
            if (rel < 0 || (int)b.jitem[rel].size() >= std::max(1, d.capacity)) return false;
            Building* to = buildAt(b.tx + d4x[rel], b.ty + d4y[rel]);
            return to && to->team == b.team;
        }
        case BType::Router:
            return b.team == source.team && b.lastItem < 0 && b.items.total == 0;
        case BType::Sorter: {
            Building* to = sorterTarget(*this, b, source, item, false);
            return to && to->team == b.team;
        }
        case BType::OverflowGate:
            return b.team == source.team && overflowTarget(*this, b, source, item, false) != nullptr;
        case BType::Bridge: {
            if (b.team != source.team || b.items.total >= d.itemCapacity) return false;
            if (b.link >= 0) {
                Building* other = buildAt(b.link % width, b.link / width);
                if (other && other->def().type == BType::Bridge) {
                    int rel = relativeTo(b, *other);
                    int rel2 = relativeTo(b, source);
                    return rel != rel2;
                }
            }
            // end of a bridge chain: only accept from bridges linked into it
            return source.def().type == BType::Bridge && source.link == b.ty * width + b.tx;
        }
        case BType::Crafter:
            return consumesItem(d, item) && b.items.amount[item] < d.itemCapacity;
        case BType::ConsumeGen:
            // ConsumeItemFlammable(minFlammability = 0.2)
            return kItems[item].flammability >= 0.2f && b.items.amount[item] < d.itemCapacity;
        case BType::ItemTurret: {
            int a = itemTurretAmmo(d, item);
            return a >= 0 && b.totalAmmo + d.ammo[a].bullet.ammoMultiplier <= d.maxAmmo;
        }
        case BType::Core:
            return true;  // campaign rules: coreIncinerates
        case BType::Storage:
            return b.items.amount[item] < d.itemCapacity;
        default:
            return false;
    }
}

void Game::handleItem(Building& b, Building& source, int item) {
    const BlockDef& d = b.def();
    b.sleeping = false;
    switch (d.type) {
        case BType::Conveyor: {
            if (b.len >= 3) return;
            int dirTo = relativeTo(source, b);
            int ang = dirTo - b.rotation;
            float x = (ang == -1 || ang == 3) ? 1.f : (ang == 1 || ang == -3) ? -1.f : 0.f;
            b.items.add(item, 1);
            bool back = std::abs(dirTo - b.rotation) == 0;
            int at = back ? 0 : b.mid;
            // ConveyorBuild.add(o)
            for (int i = std::max(at + 1, b.len); i > at; i--) {
                if (i < 3) { b.ids[i] = b.ids[i - 1]; b.xs[i] = b.xs[i - 1]; b.ys[i] = b.ys[i - 1]; }
            }
            b.len++;
            b.ids[at] = (int16_t)item; b.xs[at] = x; b.ys[at] = back ? 0.f : 0.5f;
            return;
        }
        case BType::Junction: {
            int rel = relativeTo(source, b);
            if (rel < 0) return;
            b.jitem[rel].push_back((int16_t)item);
            b.jtime[rel].push_back(tick);
            return;
        }
        case BType::Router:
            b.items.add(item, 1);
            b.lastItem = (int16_t)item; b.time = 0;
            b.lastInput = source.ty * width + source.tx;
            return;
        case BType::Sorter: {
            Building* to = sorterTarget(*this, b, source, item, true);
            if (to) handleItem(*to, b, item);
            return;
        }
        case BType::OverflowGate: {
            Building* to = overflowTarget(*this, b, source, item, true);
            if (to) handleItem(*to, b, item);
            return;
        }
        case BType::ItemTurret: {
            int a = itemTurretAmmo(d, item);
            if (a < 0) return;
            // ItemTurret.handleItem: merge into an existing entry and move it to the top
            int mult = (int)d.ammo[a].bullet.ammoMultiplier;
            b.totalAmmo += mult;
            for (size_t i = 0; i < b.ammo.size(); i++) {
                if (b.ammo[i].item == item) {
                    b.ammo[i].amount += mult;
                    AmmoEntry e = b.ammo[i];
                    b.ammo.erase(b.ammo.begin() + i);
                    b.ammo.push_back(e);
                    return;
                }
            }
            b.ammo.push_back({(int16_t)item, mult});
            return;
        }
        case BType::Core:
            if (b.team == TeamSharded) itemsDelivered[item]++;
            if (b.items.amount[item] < d.itemCapacity) b.items.add(item, 1);
            return;
        default:
            b.items.add(item, 1);
            return;
    }
}

// Building.dump(Item)
bool Game::dump(Building& b, int todump) {
    if (b.items.total == 0 || b.proximity.empty() || (todump >= 0 && !b.items.has(todump))) return false;
    int n = (int)b.proximity.size();
    int dmp = b.cdump;
    for (int i = 0; i < n; i++) {
        Building& other = builds[b.proximity[(i + dmp) % n]];
        if (other.team == b.team) {
            if (todump < 0) {
                for (int ii = 0; ii < ITEM_COUNT; ii++) {
                    if (!b.items.has(ii)) continue;
                    if (acceptItem(other, b, ii)) {
                        handleItem(other, b, ii);
                        b.items.remove(ii, 1);
                        b.cdump = (b.cdump + 1) % n;
                        return true;
                    }
                }
            } else if (acceptItem(other, b, todump)) {
                handleItem(other, b, todump);
                b.items.remove(todump, 1);
                b.cdump = (b.cdump + 1) % n;
                return true;
            }
        }
        b.cdump = (b.cdump + 1) % n;
    }
    return false;
}

// Building.offload(Item)
void Game::offload(Building& b, int item) {
    int n = (int)b.proximity.size();
    int dmp = b.cdump;
    for (int i = 0; i < n; i++) {
        b.cdump = (b.cdump + 1) % n;
        Building& other = builds[b.proximity[(i + dmp) % n]];
        if (other.team == b.team && acceptItem(other, b, item)) {
            handleItem(other, b, item);
            return;
        }
    }
    b.items.add(item, 1);
}

bool Game::put(Building& b, int item) {
    int n = (int)b.proximity.size();
    int dmp = b.cdump;
    for (int i = 0; i < n; i++) {
        b.cdump = (b.cdump + 1) % n;
        Building& other = builds[b.proximity[(i + dmp) % n]];
        if (other.team == b.team && acceptItem(other, b, item)) {
            handleItem(other, b, item);
            return true;
        }
    }
    return false;
}

void Game::addEffect(float x, float y, uint32_t color, float size, float life, uint8_t kind) {
    if (effects.size() > 96) return;
    effects.push_back({x, y, 0, life, size, color, kind});
}

}  // namespace mdt

// Save files for sectors in progress. Native format, not compatible with .msav.
#include "save.hpp"
#include <algorithm>
#include <cstring>

namespace mdt {

namespace {
constexpr uint32_t kMagic = 0x5333444d;  // "MD3S"
// 1 = Mindustry 3DS 0.2 (raw block ids of that build, see kLegacyV1Blocks)
// 2 = 0.3: block name table + liquids
constexpr uint16_t kVersion = 2;

struct Out {
    std::vector<uint8_t>& d;
    template <class T> void put(const T& v) { const uint8_t* p = (const uint8_t*)&v; d.insert(d.end(), p, p + sizeof(T)); }
};
struct In {
    const std::vector<uint8_t>& d; size_t p = 0; bool ok = true;
    template <class T> T get() {
        T v{};
        if (p + sizeof(T) > d.size()) { ok = false; return v; }
        std::memcpy(&v, &d[p], sizeof(T)); p += sizeof(T); return v;
    }
};
}

std::vector<uint8_t> saveGame(const Game& g) {
    std::vector<uint8_t> data;
    Out o{data};
    o.put(kMagic); o.put(kVersion);
    o.put((int16_t)g.sector); o.put((int16_t)BLOCK_COUNT); o.put((int16_t)ITEM_COUNT);
    for (int i = 0; i < BLOCK_COUNT; i++) {  // names: ids are remapped on load when the content changes
        uint8_t n = (uint8_t)std::strlen(kBlocks[i].name);
        o.put(n);
        data.insert(data.end(), kBlocks[i].name, kBlocks[i].name + n);
    }
    o.put((uint16_t)g.width); o.put((uint16_t)g.height);
    o.put(g.wave); o.put(g.wavetime); o.put(g.tick); o.put(g.waveSpacing);
    o.put((uint8_t)g.waveTimer); o.put((uint8_t)g.wavesEnabled); o.put(g.winWave);
    for (auto& t : g.tiles) { o.put(t.floor); o.put(t.ore); o.put(t.env); }
    o.put((uint16_t)g.spawns.size());
    for (auto& s : g.spawns) { o.put((uint16_t)s.first); o.put((uint16_t)s.second); }
    o.put((uint16_t)g.waves.size());
    for (auto& w : g.waves) o.put(w);
    // buildings, in index order so links stay valid
    uint32_t n = 0;
    for (auto& b : g.builds) if (b.alive) n++;
    o.put(n);
    for (auto& b : g.builds) {
        if (!b.alive) continue;
        o.put(b.id); o.put(b.block); o.put(b.team); o.put(b.rotation);
        o.put((int16_t)b.tx); o.put((int16_t)b.ty); o.put(b.health);
        o.put((uint8_t)b.constructing); o.put((uint8_t)b.deconstructing); o.put(b.buildProgress);
        for (int i = 0; i < 4; i++) { o.put(b.accumulator[i]); o.put(b.totalAcc[i]); }
        uint8_t ni = 0;
        for (int i = 0; i < ITEM_COUNT; i++) if (b.items.amount[i]) ni++;
        o.put(ni);
        for (int i = 0; i < ITEM_COUNT; i++) if (b.items.amount[i]) { o.put((int16_t)i); o.put(b.items.amount[i]); }
        o.put((uint8_t)b.len);
        for (int i = 0; i < b.len; i++) { o.put(b.ids[i]); o.put(b.xs[i]); o.put(b.ys[i]); }
        o.put(b.lastItem); o.put(b.sortItem); o.put(b.link);
        o.put(b.progress); o.put(b.warmup); o.put(b.generateTime); o.put(b.powerStored);
        o.put(b.turretRotation); o.put(b.reloadCounter);
        o.put((uint8_t)b.ammo.size());
        for (auto& a : b.ammo) { o.put(a.item); o.put(a.amount); }
        o.put((uint8_t)b.links.size());
        for (int32_t l : b.links) o.put(l);
        for (int d = 0; d < 4; d++) {
            o.put((uint8_t)b.jitem[d].size());
            for (size_t k = 0; k < b.jitem[d].size(); k++) o.put(b.jitem[d][k]);
        }
        o.put((uint8_t)b.bufItem.size());
        for (int16_t it : b.bufItem) o.put(it);
        o.put(b.liquid); o.put(b.liquidAmount);
    }
    uint16_t nu = 0;
    for (auto& u : g.units) if (u.alive && !u.isPlayer) nu++;
    o.put(nu);
    for (auto& u : g.units) {
        if (!u.alive || u.isPlayer) continue;
        o.put(u.type); o.put(u.team); o.put(u.x); o.put(u.y); o.put(u.rotation); o.put(u.health); o.put(u.shield);
    }
    o.put((uint16_t)g.plans.size());
    for (auto& p : g.plans) { o.put((int16_t)p.tx); o.put((int16_t)p.ty); o.put(p.block); o.put(p.rotation); o.put((uint8_t)p.breaking); }
    for (int i = 0; i < ITEM_COUNT; i++) o.put((int32_t)g.itemsDelivered[i]);
    return data;
}

bool loadGame(Game& g, const std::vector<uint8_t>& data) {
    In in{data};
    if (in.get<uint32_t>() != kMagic) return false;
    uint16_t version = in.get<uint16_t>();
    if (version != 1 && version != 2) return false;
    int16_t sector = in.get<int16_t>();
    int16_t savedBlocks = in.get<int16_t>();
    if (in.get<int16_t>() != ITEM_COUNT || savedBlocks <= 0) return false;
    // saved block id -> current id, by name
    std::vector<int16_t> blockRemap(savedBlocks, -1);
    auto lookup = [](const char* n, size_t len) -> int16_t {
        for (int k = 0; k < BLOCK_COUNT; k++) if (std::strlen(kBlocks[k].name) == len && !std::memcmp(kBlocks[k].name, n, len)) return (int16_t)k;
        return -1;
    };
    if (version == 1) {
        if (savedBlocks != LEGACY_V1_BLOCK_COUNT) return false;  // only the 0.2 content table is known
        for (int k = 0; k < savedBlocks; k++) blockRemap[k] = lookup(kLegacyV1Blocks[k], std::strlen(kLegacyV1Blocks[k]));
    } else {
        for (int k = 0; k < savedBlocks; k++) {
            uint8_t n = in.get<uint8_t>();
            if (!in.ok || in.p + n > data.size()) return false;
            blockRemap[k] = lookup((const char*)&data[in.p], n);
            in.p += n;
        }
    }
    auto mapId = [&](int v) -> int { return (v >= 0 && v < savedBlocks) ? blockRemap[v] : -1; };
    if (sector < 0 || sector >= SECTOR_COUNT) return false;
    Game ng;
    ng.sector = sector;
    ng.width = in.get<uint16_t>(); ng.height = in.get<uint16_t>();
    ng.wave = in.get<int>(); ng.wavetime = in.get<float>(); ng.tick = in.get<float>(); ng.waveSpacing = in.get<float>();
    ng.waveTimer = in.get<uint8_t>() != 0; ng.wavesEnabled = in.get<uint8_t>() != 0; ng.winWave = in.get<int>();
    size_t n = (size_t)ng.width * ng.height;
    if (!in.ok || n == 0 || n > 1024 * 1024) return false;
    ng.tiles.resize(n);
    for (auto& t : ng.tiles) {
        int f = mapId(in.get<uint16_t>()), o = mapId(in.get<uint16_t>()), e = mapId(in.get<uint16_t>());
        if (f < 0 || o < 0 || e < 0) return false;
        t.floor = (uint16_t)f; t.ore = (uint16_t)o; t.env = (uint16_t)e;
    }
    uint16_t ns = in.get<uint16_t>();
    for (int i = 0; i < ns; i++) { int x = in.get<uint16_t>(); int y = in.get<uint16_t>(); ng.spawns.push_back({x, y}); }
    uint16_t nw = in.get<uint16_t>();
    for (int i = 0; i < nw; i++) ng.waves.push_back(in.get<SpawnGroupDef>());
    uint32_t nb = in.get<uint32_t>();
    if (!in.ok || nb > n) return false;
    struct Pending { int32_t idx; std::vector<int32_t> links; };
    std::vector<int32_t> remap;  // saved id -> new id
    std::vector<std::pair<int32_t, std::vector<int32_t>>> links;
    std::vector<int32_t> savedIds;
    for (uint32_t k = 0; k < nb && in.ok; k++) {
        int32_t id = in.get<int32_t>();
        int16_t block = (int16_t)mapId(in.get<int16_t>());
        uint8_t team = in.get<uint8_t>(), rot = in.get<uint8_t>();
        int tx = in.get<int16_t>(), ty = in.get<int16_t>();
        if (block <= 0 || block >= BLOCK_COUNT || tx < 0 || ty < 0 || tx >= ng.width || ty >= ng.height) return false;
        int32_t idx = ng.addBuilding(block, tx, ty, team, rot & 3);
        Building& b = ng.builds[idx];
        b.rotation = rot;
        b.health = in.get<float>();
        b.constructing = in.get<uint8_t>() != 0; b.deconstructing = in.get<uint8_t>() != 0; b.buildProgress = in.get<float>();
        b.constructBlock = block;
        for (int i = 0; i < 4; i++) { b.accumulator[i] = in.get<float>(); b.totalAcc[i] = in.get<float>(); }
        uint8_t ni = in.get<uint8_t>();
        for (int i = 0; i < ni; i++) {
            int16_t it = in.get<int16_t>(); int32_t a = in.get<int32_t>();
            if (it >= 0 && it < ITEM_COUNT && a > 0) b.items.add(it, a);
        }
        b.len = std::min<int>(in.get<uint8_t>(), 3);
        for (int i = 0; i < b.len; i++) { b.ids[i] = in.get<int16_t>(); b.xs[i] = in.get<float>(); b.ys[i] = in.get<float>(); }
        b.lastItem = in.get<int16_t>(); b.sortItem = in.get<int16_t>(); b.link = in.get<int32_t>();
        b.progress = in.get<float>(); b.warmup = in.get<float>(); b.generateTime = in.get<float>(); b.powerStored = in.get<float>();
        b.turretRotation = in.get<float>(); b.reloadCounter = in.get<float>();
        uint8_t na = in.get<uint8_t>();
        b.totalAmmo = 0;
        for (int i = 0; i < na; i++) { AmmoEntry e; e.item = in.get<int16_t>(); e.amount = in.get<int32_t>(); b.ammo.push_back(e); b.totalAmmo += e.amount; }
        uint8_t nl = in.get<uint8_t>();
        std::vector<int32_t> ls;
        for (int i = 0; i < nl; i++) ls.push_back(in.get<int32_t>());
        links.push_back({idx, ls});
        for (int d = 0; d < 4; d++) {
            uint8_t c = in.get<uint8_t>();
            for (int i = 0; i < c; i++) { b.jitem[d].push_back(in.get<int16_t>()); b.jtime[d].push_back(ng.tick); }
        }
        uint8_t nbuf = in.get<uint8_t>();
        for (int i = 0; i < nbuf; i++) { b.bufItem.push_back(in.get<int16_t>()); b.bufTime.push_back(ng.tick); }
        if (version >= 2) {
            int16_t liq = in.get<int16_t>(); float amount = in.get<float>();
            if (liq >= 0 && liq < LIQUID_COUNT && amount > 0 && b.def().hasLiquids) {
                b.liquid = liq; b.liquidAmount = std::min(amount, b.def().liquidCapacity);
            }
        }
        if (id >= 0) { if ((int)remap.size() <= id) remap.resize(id + 1, -1); remap[id] = idx; }
    }
    for (auto& l : links)
        for (int32_t old : l.second)
            if (old >= 0 && old < (int)remap.size() && remap[old] >= 0) ng.builds[l.first].links.push_back(remap[old]);
    uint16_t nu = in.get<uint16_t>();
    for (int i = 0; i < nu && in.ok; i++) {
        int16_t type = in.get<int16_t>(); uint8_t team = in.get<uint8_t>();
        float x = in.get<float>(), y = in.get<float>(), r = in.get<float>(), h = in.get<float>(), s = in.get<float>();
        if (type < 0 || type >= UNIT_COUNT) return false;
        int32_t u = ng.spawnUnit(type, team, x, y);
        if (u >= 0) { ng.units[u].rotation = r; ng.units[u].health = h; ng.units[u].shield = s; }
    }
    uint16_t np = in.get<uint16_t>();
    for (int i = 0; i < np && in.ok; i++) {
        BuildPlan p;
        p.tx = in.get<int16_t>(); p.ty = in.get<int16_t>(); p.block = (int16_t)mapId(in.get<int16_t>()); p.rotation = in.get<uint8_t>(); p.breaking = in.get<uint8_t>() != 0;
        if (p.block < 0) return false;
        ng.plans.push_back(p);
    }
    for (int i = 0; i < ITEM_COUNT; i++) ng.itemsDelivered[i] = in.get<int32_t>();
    if (!in.ok) return false;
    for (auto& b : ng.builds) if (b.alive) ng.updateProximity(b.id);
    ng.powerGraphsDirty = true;
    ng.flowDirty = true;
    ng.state = GameState::Playing;
    g = std::move(ng);
    return true;
}

}  // namespace mdt

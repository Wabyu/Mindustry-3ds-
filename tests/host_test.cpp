// Host-side regression tests for the native simulation.
// Expected values are derived from upstream formulas (see comments).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include "game.hpp"
#include "save.hpp"

using namespace mdt;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("PASS "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string romfs = "romfs";

static void emptyMap(Game& g, int w, int h) {
    g.width = w; g.height = h;
    g.tiles.assign((size_t)w * h, Tile{});
    for (auto& t : g.tiles) t.floor = B_stone;
    g.builds.clear(); g.freeBuilds.clear(); g.units.clear(); g.bullets.clear(); g.plans.clear();
    g.waves.clear(); g.spawns.clear();
    g.waveTimer = false; g.winWave = 0; g.state = GameState::Playing;
}

static void run(Game& g, float seconds) {
    int steps = (int)(seconds * 60);
    for (int i = 0; i < steps; i++) g.update(1.f);
}

static void testDrillToCore() {
    Game g; emptyMap(g, 40, 20);
    for (int y = 5; y <= 6; y++) for (int x = 5; x <= 6; x++) g.tile(x, y)->ore = B_ore_copper;
    int core = g.addBuilding(B_core_shard, 20, 5, TeamSharded, 0);
    g.addBuilding(B_mechanical_drill, 5, 5, TeamSharded, 0);  // covers 5..6
    for (int x = 7; x <= 18; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);
    g.spawnPlayer = false;
    int before = g.builds[core].items.amount[I_copper];
    run(g, 120);
    int got = g.builds[core].items.amount[I_copper] - before;
    // Drill: delay = 600 + 50 * hardness(1) = 650 ticks, 4 ores -> 162.5 ticks/item at full warmup
    // => ~44 items in 120 s, minus warmup (0.015/tick) and conveyor transit
    CHECK(got >= 38 && got <= 45, "mechanical drill on 4 copper ore -> core: %d items in 120 s (expected ~40-44)", got);
}

static void testConveyorThroughput() {
    Game g; emptyMap(g, 40, 10);
    int src = g.addBuilding(B_container, 2, 5, TeamSharded, 0);  // feeds via router-less dump? use sorter chain
    (void)src;
    int core = g.addBuilding(B_core_shard, 30, 5, TeamSharded, 0);
    for (int x = 4; x <= 28; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);
    g.spawnPlayer = false;
    Building& c = g.builds[core];
    // inject directly at the start of the line every tick when accepted
    int inserted = 0;
    Building* first = g.buildAt(4, 5);
    Building fake; fake.alive = true; fake.block = B_router; fake.team = TeamSharded; fake.tx = 3; fake.ty = 5;
    for (int i = 0; i < 60 * 60; i++) {
        if (g.acceptItem(*first, fake, I_copper)) { g.handleItem(*first, fake, I_copper); inserted++; }
        g.update(1.f);
    }
    int got = c.items.amount[I_copper];
    // Conveyor displayedSpeed = 4.2 items/s for the basic conveyor (speed 0.03, item spacing 0.4).
    // The first item needs 25 tiles / (0.03 * 60) s to arrive; measure the steady state.
    float transit = 25.f / (0.03f * 60.f);
    float rate = got / (60.f - transit);
    CHECK(rate > 3.8f && rate < 4.6f, "basic conveyor throughput %.2f items/s (upstream displayedSpeed 4.2)", rate);
}

static void testCrafterAndPower() {
    Game g; emptyMap(g, 30, 30);
    g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 25, 25, TeamSharded, 0);
    int press = g.addBuilding(B_graphite_press, 10, 10, TeamSharded, 0);
    g.builds[press].items.add(I_coal, 10);
    run(g, 10);
    // craftTime 90 ticks, consumes 2 coal -> 5 crafts available, output stays (no neighbour)
    CHECK(g.builds[press].items.amount[I_graphite] == 5, "graphite press crafted %d graphite from 10 coal", g.builds[press].items.amount[I_graphite]);

    int gen = g.addBuilding(B_combustion_generator, 4, 4, TeamSharded, 0);
    int node = g.addBuilding(B_power_node, 6, 4, TeamSharded, 0);
    int smelter = g.addBuilding(B_silicon_smelter, 9, 4, TeamSharded, 0);
    g.autoLinkPower(node);
    g.builds[gen].items.add(I_coal, 10);
    g.builds[smelter].items.add(I_coal, 5);
    g.builds[smelter].items.add(I_sand, 10);
    run(g, 6);
    CHECK(g.builds[gen].graph >= 0 && g.builds[gen].graph == g.builds[smelter].graph, "generator, node and smelter share a power graph");
    // generator: 1 power/tick * flammability(coal)=1 ; smelter uses 0.5/tick -> satisfied
    CHECK(g.builds[smelter].powerStatus > 0.99f, "smelter power satisfaction %.2f", g.builds[smelter].powerStatus);
    CHECK(g.builds[smelter].items.amount[I_silicon] >= 5, "silicon smelter produced %d silicon", g.builds[smelter].items.amount[I_silicon]);
}

static void testTurretKillsDagger() {
    Game g; emptyMap(g, 40, 40);
    g.spawnPlayer = false;
    int core = g.addBuilding(B_core_shard, 5, 20, TeamSharded, 0);
    (void)core;
    // upstream: dagger range = 2.5 * 60 - 4 = 146 > duo range 110, and its two alternating
    // mounts fire every reload/2: a single duo loses, three graphite-fed duos win.
    int duos[3];
    for (int k = 0; k < 3; k++) {
        duos[k] = g.addBuilding(B_duo, 15, 18 + k * 2, TeamSharded, 0);
        Building fake; fake.alive = true; fake.block = B_router; fake.team = TeamSharded; fake.tx = 14; fake.ty = 18 + k * 2;
        for (int i = 0; i < 12; i++) if (g.acceptItem(g.builds[duos[k]], fake, I_graphite)) g.handleItem(g.builds[duos[k]], fake, I_graphite);
    }
    CHECK(g.builds[duos[0]].totalAmmo == 28, "duo holds %d graphite rounds (ammoMultiplier 4, maxAmmo 30)", g.builds[duos[0]].totalAmmo);
    g.updateFlowField();
    int u = g.spawnUnit(U_dagger, TeamCrux, 35 * tilesize, 20 * tilesize);
    run(g, 30);
    CHECK(!g.units[u].alive, "three duos killed a dagger walking to the core");
}

static void testBuildPlan() {
    Game g; emptyMap(g, 40, 40);
    int core = g.addBuilding(B_core_shard, 10, 10, TeamSharded, 0);
    g.builds[core].items.add(I_copper, 500);
    run(g, 1);  // spawns the alpha
    CHECK(g.player >= 0 && g.units[g.player].type == U_alpha, "core shard spawned an alpha");
    CHECK(g.queuePlan(14, 10, B_duo, 0, false), "duo plan queued");
    int before = g.builds[core].items.amount[I_copper];
    for (int i = 0; i < 600; i++) { g.update(1.f); g.updatePlayer(1.f, 0, 0); }
    Building* b = g.buildAt(14, 10);
    CHECK(b && b->block == B_duo && !b->constructing, "duo constructed by the alpha");
    CHECK(before - g.builds[core].items.amount[I_copper] == 35, "duo cost %d copper (requirements 35)", before - g.builds[core].items.amount[I_copper]);
    CHECK(g.queuePlan(14, 10, 0, 0, true), "deconstruct plan queued");
    for (int i = 0; i < 600; i++) { g.update(1.f); g.updatePlayer(1.f, 0, 0); }
    CHECK(!g.buildAt(14, 10), "duo deconstructed");
}

static void testMaps() {
    for (int s = 0; s < SECTOR_COUNT; s++) {
        std::string path = romfs + "/maps/" + kSectors[s].name + ".m3d";
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Game g;
        bool ok = g.loadMap(data, s);
        CHECK(ok, "load %s (%zu bytes)", path.c_str(), data.size());
        if (!ok) continue;
        CHECK(g.core(TeamSharded) != nullptr, "%s has a player core", kSectors[s].name);
        CHECK(!g.spawns.empty() && !g.waves.empty(), "%s has %zu spawns and %zu spawn groups", kSectors[s].name, g.spawns.size(), g.waves.size());
        g.updateFlowField();
        bool reach = false;
        for (auto& sp : g.spawns) if (g.flowCost[sp.second * g.width + sp.first] >= 0) reach = true;
        CHECK(reach, "%s: ground path from spawn to core exists", kSectors[s].name);
        // simulate a couple of waves with the timer accelerated
        int maxEnemies = 0, w = 0;
        for (; w < 6 && maxEnemies == 0; w++) {
            g.wavetime = 1;
            for (int i = 0; i < 60 * 5; i++) { g.update(1.f); maxEnemies = std::max(maxEnemies, g.enemies()); }
        }
        CHECK(maxEnemies > 0, "%s: first enemies (%d) at wave %d", kSectors[s].name, maxEnemies, g.wave - 1);
        // let them walk: they must make progress toward the core
        float before = 0, after = 0; int n = 0;
        Building* c = g.core(TeamSharded);
        for (auto& u : g.units) if (u.alive && u.team == TeamCrux) { before += std::hypot(u.x - c->tx * tilesize, u.y - c->ty * tilesize); n++; }
        for (int i = 0; i < 60 * 20; i++) g.update(1.f);
        int m = 0;
        for (auto& u : g.units) if (u.alive && u.team == TeamCrux) { after += std::hypot(u.x - c->tx * tilesize, u.y - c->ty * tilesize); m++; }
        CHECK(m == 0 || after / m < before / n, "%s: enemies advance toward the core (%.0f -> %.0f)", kSectors[s].name, n ? before / n : 0.f, m ? after / m : 0.f);
    }
}

static void testSaveRoundTrip() {
    std::string path = romfs + "/maps/groundZero.m3d";
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Game g;
    g.loadMap(data, 0);
    g.spawnPlayer = false;
    Building* c = g.core(TeamSharded);
    int x = c->tx + 4, y = c->ty;
    g.addBuilding(B_conveyor, x, y, TeamSharded, 2);
    int duo = g.addBuilding(B_duo, x, y + 3, TeamSharded, 0);
    Building fake; fake.alive = true; fake.block = B_router; fake.team = TeamSharded; fake.tx = x; fake.ty = y + 2;
    g.handleItem(g.builds[duo], fake, I_graphite);
    g.runWave();
    run(g, 2);
    auto bytes = saveGame(g);
    Game h;
    bool ok = loadGame(h, bytes);
    CHECK(ok, "save (%zu bytes) loads back", bytes.size());
    if (!ok) return;
    CHECK(h.wave == g.wave && h.enemies() == g.enemies(), "wave %d and %d enemies restored", h.wave, h.enemies());
    Building* d = h.buildAt(x, y + 3);
    CHECK(d && d->block == B_duo && d->totalAmmo == g.builds[duo].totalAmmo, "duo and its ammo restored");
    CHECK(h.core(TeamSharded)->items.amount[I_copper] == g.core(TeamSharded)->items.amount[I_copper], "core items restored");
    auto again = saveGame(h);
    CHECK(again.size() == bytes.size(), "re-saving gives the same size (%zu)", again.size());
}

// ---------------------------------------------------------------- conveyors (Conveyor / Autotiler)
static const int cdx[4] = {1, 0, -1, 0}, cdy[4] = {0, 1, 0, -1};

static bool inject(Game& g, int x, int y, int fromDir, int item) {
    Building* b = g.buildAt(x, y);
    if (!b) return false;
    Building fake; fake.alive = true; fake.block = B_router; fake.team = TeamSharded;
    fake.tx = x + cdx[fromDir]; fake.ty = y + cdy[fromDir];
    if (!g.acceptItem(*b, fake, item)) return false;
    g.handleItem(*b, fake, item);
    return true;
}

static long itemsInWorld(Game& g) {
    long n = 0;
    for (auto& b : g.builds) if (b.alive) {
        if (b.def().type == BType::Conveyor) n += b.len;
        else if (b.def().type != BType::Core) n += b.items.total;
        for (int d = 0; d < 4; d++) n += (long)b.jitem[d].size();
        n += (long)b.bufItem.size();
    }
    for (int i = 0; i < ITEM_COUNT; i++) n += g.itemsDelivered[i];
    return n;
}

static void testConveyorDirections() {
    for (int r = 0; r < 4; r++) {
        Game g; emptyMap(g, 30, 30); g.spawnPlayer = false;
        int cx = 15 + cdx[r] * 8, cy = 15 + cdy[r] * 8;
        g.addBuilding(B_core_shard, cx, cy, TeamSharded, 0);
        int sx = 15 - cdx[r] * 6, sy = 15 - cdy[r] * 6;
        for (int k = 0; k < 13; k++) g.addBuilding(B_conveyor, sx + cdx[r] * k, sy + cdy[r] * k, TeamSharded, r);
        long in = 0;
        for (int t = 0; t < 60 * 30; t++) { if (inject(g, sx, sy, (r + 2) % 4, I_copper)) in++; g.update(1.f); }
        long out = g.itemsDelivered[I_copper];
        CHECK(out > 0 && itemsInWorld(g) == in, "direction %d: %ld in, %ld delivered, nothing lost or duplicated", r * 90, in, out);
    }
}

static void testConveyorTiling() {
    // Expected (blendbits, blendscly) from Autotiler.buildBlending + transformCase, where
    // blends(1) looks at rotation-1 and blends(3) at rotation+1.
    // feeds mask: bit0 = feeder on side rotation+1, bit1 = back, bit2 = side rotation-1
    struct Case { int feeds, bits, scly; const char* name; };
    const Case cases[] = {{2, 0, 1, "straight"}, {1, 1, 1, "rot+1 side only (case 5)"}, {4, 1, -1, "rot-1 side only (case 4)"},
                          {5, 4, 1, "both sides (case 1)"}, {3, 2, -1, "back + rot+1 (case 3)"}, {6, 2, 1, "back + rot-1 (case 2)"},
                          {7, 3, 1, "cross (case 0)"}};
    int good = 0, total = 0;
    for (int r = 0; r < 4; r++)
        for (auto& c : cases) {
            Game g; emptyMap(g, 12, 12); g.spawnPlayer = false;
            g.addBuilding(B_conveyor, 6, 6, TeamSharded, r);
            const int dirs[3] = {(r + 1) % 4, (r + 2) % 4, (r + 3) % 4};
            for (int k = 0; k < 3; k++) if (c.feeds & (1 << k))
                g.addBuilding(B_conveyor, 6 + cdx[dirs[k]], 6 + cdy[dirs[k]], TeamSharded, (dirs[k] + 2) % 4);
            Building* b = g.buildAt(6, 6);
            total++;
            if (b->blendbits == c.bits && b->blendscly == c.scly) good++;
            else std::printf("  rot %d %s: bits %d scly %d\n", r, c.name, b->blendbits, b->blendscly);
        }
    CHECK(good == total, "Autotiler cases x 4 directions: %d/%d match buildBlending/transformCase", good, total);
    // a side neighbour that does not output toward the belt must not bend it
    Game g; emptyMap(g, 12, 12); g.spawnPlayer = false;
    g.addBuilding(B_conveyor, 6, 6, TeamSharded, 0);
    g.addBuilding(B_conveyor, 6, 7, TeamSharded, 1);  // above, flowing away
    CHECK(g.buildAt(6, 6)->blendbits == 0, "parallel neighbour flowing away keeps a straight belt");
    // multi-tile source: a drill touching the side bends the belt (Drill outputs items, not rotated)
    g.addBuilding(B_mechanical_drill, 6, 3, TeamSharded, 0);  // tiles 6..7 x 3..4: one tile gap below the belt
    CHECK(g.buildAt(6, 6)->blendbits == 0, "drill one tile away does not bend the belt");
    Game h; emptyMap(h, 12, 12); h.spawnPlayer = false;
    h.addBuilding(B_conveyor, 6, 6, TeamSharded, 0);
    h.addBuilding(B_mechanical_drill, 5, 4, TeamSharded, 0);  // tiles 5..6 x 4..5: touches the rot-1 side
    CHECK(h.buildAt(6, 6)->blendbits == 1 && h.buildAt(6, 6)->blendscly == -1, "2x2 drill touching the rot-1 side gives the case-4 curve (bits %d scly %d)",
          h.buildAt(6, 6)->blendbits, h.buildAt(6, 6)->blendscly);
    // joints toward non-square sprites (Autotiler blendresult[4])
    Game j; emptyMap(j, 12, 12); j.spawnPlayer = false;
    j.addBuilding(B_sorter, 5, 6, TeamSharded, 0);
    j.addBuilding(B_conveyor, 6, 6, TeamSharded, 0);
    j.addBuilding(B_router, 7, 6, TeamSharded, 0);
    Building* m = j.buildAt(6, 6);
    CHECK((m->blending & 4) && !(m->blending & 1), "joint drawn toward the sorter behind (non-square), not toward the router (square): %d", m->blending);
    // the frame keeps animating on an empty belt (Conveyor.draw uses enabled/clogHeat only)
    CHECK(m->len == 0 && conveyorFrame(*m, 0) != conveyorFrame(*m, 10.f / (0.03f * 8.f)), "empty belt still animates");
    m->clogHeat = 1;
    CHECK(conveyorFrame(*m, 10.f / (0.03f * 8.f)) == 0, "clogged belt shows frame 0");
}

static void testConveyorMerging() {
    Game g; emptyMap(g, 30, 20); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 25, 10, TeamSharded, 0);
    for (int x = 5; x <= 23; x++) g.addBuilding(B_conveyor, x, 10, TeamSharded, 0);
    for (int y = 4; y <= 9; y++) g.addBuilding(B_conveyor, 12, y, TeamSharded, 1);     // from below
    for (int y = 16; y >= 11; y--) g.addBuilding(B_conveyor, 14, y, TeamSharded, 3);   // from above
    long in = 0;
    for (int t = 0; t < 60 * 40; t++) {
        if (inject(g, 5, 10, 2, I_copper)) in++;
        if (inject(g, 12, 4, 3, I_lead)) in++;
        if (inject(g, 14, 16, 1, I_scrap)) in++;
        g.update(1.f);
    }
    long out = g.itemsDelivered[I_copper] + g.itemsDelivered[I_lead] + g.itemsDelivered[I_scrap];
    float rate = g.itemsDelivered[I_copper] + g.itemsDelivered[I_lead] + g.itemsDelivered[I_scrap];
    rate /= 30.f;
    CHECK(itemsInWorld(g) == in, "3 merging belts: %ld items in, conserved (%ld delivered)", in, out);
    CHECK(g.itemsDelivered[I_lead] > 0 && g.itemsDelivered[I_scrap] > 0 && g.itemsDelivered[I_copper] > 0, "every input reaches the core (side inputs insert at mid)");
    CHECK(rate < 8.5f, "merged throughput %.1f items/s stays bounded by one belt", rate);
}

static void testConveyorJunctionTitanium() {
    Game g; emptyMap(g, 40, 30); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 30, 10, TeamSharded, 0);
    g.addBuilding(B_core_shard, 15, 25, TeamSharded, 0);
    for (int x = 3; x <= 28; x++) {
        int16_t blk = x < 10 ? B_conveyor : x < 20 ? B_titanium_conveyor : B_conveyor;
        if (x == 15) g.addBuilding(B_junction, x, 10, TeamSharded, 0);
        else g.addBuilding(blk, x, 10, TeamSharded, 0);
    }
    for (int y = 3; y <= 23; y++) if (y != 10) g.addBuilding(B_conveyor, 15, y, TeamSharded, 1);
    long in = 0;
    for (int t = 0; t < 60 * 40; t++) {
        if (inject(g, 3, 10, 2, I_copper)) in++;
        if (inject(g, 15, 3, 3, I_lead)) in++;
        g.update(1.f);
    }
    CHECK(g.itemsDelivered[I_copper] > 50 && g.itemsDelivered[I_lead] > 50, "junction crossing: %ld copper east, %ld lead north",
          g.itemsDelivered[I_copper], g.itemsDelivered[I_lead]);
    CHECK(itemsInWorld(g) == in, "basic -> titanium -> junction -> basic: %ld items conserved", in);
}

static void testConveyorClogAndResume() {
    Game g; emptyMap(g, 30, 10); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 25, 5, TeamSharded, 0);
    for (int x = 3; x <= 10; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);  // ends at nothing
    for (int t = 0; t < 60 * 20; t++) { inject(g, 3, 5, 2, I_copper); g.update(1.f); }
    Building* last = g.buildAt(10, 5);
    CHECK(last->len == 3 && last->clogHeat > 0.5f, "dead end: belt fills up (len %d) and clogs (heat %.2f)", last->len, last->clogHeat);
    long held = itemsInWorld(g);
    for (int x = 11; x <= 23; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);
    for (int t = 0; t < 60 * 20; t++) g.update(1.f);
    CHECK(g.itemsDelivered[I_copper] == held && itemsInWorld(g) == held, "extended belt resumes: all %ld held items delivered", held);
}

static void testConveyorEditing() {
    Game g; emptyMap(g, 30, 10);
    int core = g.addBuilding(B_core_shard, 25, 5, TeamSharded, 0);
    g.builds[core].items.add(I_copper, 500); g.builds[core].items.add(I_lead, 50); g.builds[core].items.add(I_titanium, 50);
    for (int x = 3; x <= 12; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);
    for (int t = 0; t < 60 * 3; t++) { inject(g, 3, 5, 2, I_copper); g.update(1.f); }
    long before = itemsInWorld(g);
    int copper = g.builds[core].items.amount[I_copper];
    // rotate a loaded belt in place (Block.quickRotate): free, items kept
    int lenBefore = g.buildAt(8, 5)->len;
    g.queuePlan(8, 5, B_conveyor, 1, false);
    CHECK(g.buildAt(8, 5)->rotation == 1 && g.buildAt(8, 5)->len == lenBefore && g.builds[core].items.amount[I_copper] == copper,
          "quick rotate keeps %d items and costs nothing", lenBefore);
    CHECK(g.plans.empty(), "rotating a finished belt queues no construction");
    g.queuePlan(8, 5, B_conveyor, 0, false);
    // upgrade to titanium: ConveyorBuild.overwrote keeps the items; titanium cost is charged once
    int lenUp = g.buildAt(9, 5)->len;
    g.spawnPlayer = true;
    g.queuePlan(9, 5, B_titanium_conveyor, 0, false);
    CHECK(!g.plans.empty(), "upgrade queued");
    for (int i = 0; i < 300; i++) { g.update(1.f); g.updatePlayer(1.f, 0, 0); if (g.plans.empty()) break; }
    Building* up = g.buildAt(9, 5);
    CHECK(up && up->block == B_titanium_conveyor && !up->constructing, "belt upgraded to titanium");
    (void)lenUp;
    CHECK(g.builds[core].items.amount[I_titanium] == 49 && g.builds[core].items.amount[I_lead] == 49,
          "titanium conveyor cost charged once (titanium %d, lead %d)", (int)g.builds[core].items.amount[I_titanium], (int)g.builds[core].items.amount[I_lead]);
    // re-placing the same tile/rotation must not charge again
    int cu = g.builds[core].items.amount[I_copper];
    g.queuePlan(9, 5, B_titanium_conveyor, 0, false);
    for (int i = 0; i < 60; i++) { g.update(1.f); g.updatePlayer(1.f, 0, 0); }
    CHECK(g.builds[core].items.amount[I_copper] >= cu - 0 && g.builds[core].items.amount[I_titanium] == 49, "re-placing an identical belt is free");
    (void)before;
}

static void testPowerConfigAndStatus() {
    Game g; emptyMap(g, 40, 30); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 35, 25, TeamSharded, 0);
    int node = g.addBuilding(B_power_node, 10, 10, TeamSharded, 0);
    int solar = g.addBuilding(B_solar_panel, 14, 10, TeamSharded, 0);
    int smelter = g.addBuilding(B_silicon_smelter, 10, 14, TeamSharded, 0);
    int far = g.addBuilding(B_solar_panel, 30, 10, TeamSharded, 0);
    g.update(1.f);
    CHECK(g.status(g.builds[smelter]).reason == Reason::MissingItem, "smelter without items: missing ingredient");
    g.builds[smelter].items.add(I_coal, 5); g.builds[smelter].items.add(I_sand, 10);
    g.update(1.f);
    CHECK(g.status(g.builds[smelter]).reason == Reason::Unpowered, "smelter with items but no network: not connected");
    CHECK(g.togglePowerLink(node, solar) && g.togglePowerLink(node, smelter), "manual links node -> solar, node -> smelter");
    CHECK(!g.togglePowerLink(node, far), "out of laser range (6 tiles) refused");
    for (int i = 0; i < 30; i++) g.update(1.f);
    // solar 0.1/tick vs smelter 0.5/tick: 20% satisfaction (PowerGraph)
    StatusInfo st = g.status(g.builds[smelter]);
    const GraphStats& gs = g.graphStats[g.builds[node].graph];
    CHECK(st.reason == Reason::LowPower && std::fabs(st.value - 0.2f) < 0.01f, "smelter at %.0f%% power (solar 6/s for 30/s)", st.value * 100);
    CHECK(std::fabs(gs.produced - 0.1f) < 1e-4f && std::fabs(gs.needed - 0.5f) < 1e-4f, "network balance +%.2f -%.2f per tick", gs.produced, gs.needed);
    CHECK(g.togglePowerLink(node, solar) && std::find(g.builds[node].links.begin(), g.builds[node].links.end(), solar) == g.builds[node].links.end(),
          "tapping a linked building unlinks it");
    // battery: charges from a surplus, discharges into a deficit
    Game h; emptyMap(h, 40, 30); h.spawnPlayer = false;
    h.addBuilding(B_core_shard, 35, 25, TeamSharded, 0);
    int n2 = h.addBuilding(B_power_node, 10, 10, TeamSharded, 0);
    int bat = h.addBuilding(B_battery, 11, 10, TeamSharded, 0);  // touching the node: conducts
    int sol = h.addBuilding(B_solar_panel, 12, 10, TeamSharded, 0);
    (void)n2; (void)sol;
    for (int i = 0; i < 60; i++) h.update(1.f);
    CHECK(h.status(h.builds[bat]).reason == Reason::Charging && h.builds[bat].powerStored > 5.f, "battery charging from a solar surplus (%.1f stored)", h.builds[bat].powerStored);
    int sm = h.addBuilding(B_silicon_smelter, 9, 7, TeamSharded, 0);
    h.builds[sm].items.add(I_coal, 10); h.builds[sm].items.add(I_sand, 20);
    h.togglePowerLink(n2, sm);
    for (int i = 0; i < 10; i++) h.update(1.f);
    CHECK(h.status(h.builds[bat]).reason == Reason::Discharging, "battery discharging into the smelter");
    // double tap: clear every link, then auto-link again (PowerNode.onConfigureBuildTapped)
    int links = (int)h.builds[n2].links.size();
    h.autoOrClearLinks(n2);
    CHECK(links > 0 && h.builds[n2].links.empty(), "double tap with links clears them (%d -> 0)", links);
    h.autoOrClearLinks(n2);
    CHECK(!h.builds[n2].links.empty(), "double tap without links finds potential links (%zu)", h.builds[n2].links.size());
    // max nodes = 10 (Blocks.powerNode)
    Game m; emptyMap(m, 40, 30); m.spawnPlayer = false;
    m.addBuilding(B_core_shard, 35, 25, TeamSharded, 0);
    int hub = m.addBuilding(B_power_node, 15, 15, TeamSharded, 0);
    int ok = 0;
    for (int k = 0; k < 12; k++) {
        int o = m.addBuilding(B_solar_panel, 10 + (k % 6) * 2, k < 6 ? 12 : 18, TeamSharded, 0);
        if (m.togglePowerLink(hub, o)) ok++;
    }
    CHECK(ok == 10, "power node accepts %d links (maxNodes 10)", ok);
    // generator without fuel
    int gen = m.addBuilding(B_combustion_generator, 20, 20, TeamSharded, 0);
    m.update(1.f);
    CHECK(m.status(m.builds[gen]).reason == Reason::NoFuel, "combustion generator without coal: no fuel");
    // bridges: in line and within range only
    int b1 = m.addBuilding(B_bridge_conveyor, 5, 5, TeamSharded, 0);
    int b2 = m.addBuilding(B_bridge_conveyor, 9, 5, TeamSharded, 0);
    int b3 = m.addBuilding(B_bridge_conveyor, 10, 6, TeamSharded, 0);
    CHECK(m.toggleBridgeLink(b1, b2) && !m.toggleBridgeLink(b2, b3), "bridge links in line (4 tiles), not diagonally");
    CHECK(m.toggleBridgeLink(b1, b2) && m.builds[b1].link < 0, "tapping the linked bridge again unlinks");
}

// ---------------------------------------------------------------- liquids (Pump / Conduit / LiquidRouter / steam)
static double liquidInWorld(Game& g) {
    double v = 0;
    for (auto& b : g.builds) if (b.alive && b.liquid >= 0) v += b.liquidAmount;
    return v;
}

static void liquidMap(Game& g, int w, int h) {
    emptyMap(g, w, h); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, w - 3, h - 3, TeamSharded, 0);
}

static void testLiquids() {
    Game g; liquidMap(g, 40, 20);
    g.tile(2, 5)->floor = B_shallow_water;
    g.tile(2, 10)->floor = B_deep_water;
    CHECK(!g.validPlace(B_mechanical_pump, 5, 5, 0, TeamSharded), "pump refused on stone (Pump.canPlaceOn)");
    CHECK(g.validPlace(B_mechanical_pump, 2, 5, 0, TeamSharded), "pump allowed on shallow water");
    CHECK(g.validPlace(B_mechanical_pump, 2, 10, 0, TeamSharded) && g.validPlace(B_conduit, 2, 10, 0, TeamSharded),
          "pump and conduit allowed on deep water (floating)");
    CHECK(!g.validPlace(B_combustion_generator, 2, 10, 0, TeamSharded), "non-floating block refused on deep water");
    int p1 = g.addBuilding(B_mechanical_pump, 2, 5, TeamSharded, 0);
    int p2 = g.addBuilding(B_mechanical_pump, 2, 10, TeamSharded, 0);
    g.update(1.f);
    // pumpAmount 7/60 per tick * liquidMultiplier (shallow 1, deep 1.5)
    CHECK(std::fabs(g.builds[p1].liquidAmount - 7.f / 60.f) < 1e-4f && std::fabs(g.builds[p2].liquidAmount - 1.5f * 7.f / 60.f) < 1e-4f,
          "pump rate %.4f (shallow) / %.4f (deep) per tick", g.builds[p1].liquidAmount, g.builds[p2].liquidAmount);
    // saturation: no outlet -> stops at liquidCapacity 10, status "tank full", pumping stops
    for (int i = 0; i < 200; i++) g.update(1.f);
    double pumped = g.liquidPumped;
    for (int i = 0; i < 60; i++) g.update(1.f);
    CHECK(std::fabs(g.builds[p1].liquidAmount - 10.f) < 0.02f && g.liquidPumped - pumped < 0.05 &&
          g.status(g.builds[p1]).reason == Reason::LiquidFull, "isolated pump saturates at 10 and stops");

    // transfer: pump -> 8 conduits -> tank, ending against a wall (no leak); volume conserved
    Game h; liquidMap(h, 40, 20);
    h.tile(2, 8)->floor = B_shallow_water;
    h.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    for (int x = 3; x <= 10; x++) h.addBuilding(B_conduit, x, 8, TeamSharded, 0);
    int tank = h.addBuilding(B_liquid_tank, 12, 8, TeamSharded, 0);
    for (int i = 0; i < 60 * 60; i++) h.update(1.f);
    double inWorld = liquidInWorld(h);
    CHECK(h.builds[tank].liquidAmount > 300.f, "tank received %.1f water in 60 s through 8 conduits", h.builds[tank].liquidAmount);
    CHECK(std::fabs(h.liquidPumped - (inWorld + h.liquidLeaked + h.liquidConsumed)) < 1e-4 * h.liquidPumped && h.liquidLeaked == 0,  // float32 amounts
          "volume conserved: pumped %.3f = stored %.3f (no leak, no consumer)", h.liquidPumped, inWorld);
    CHECK(h.buildAt(5, 8)->blendbits == 0 && h.buildAt(3, 8)->liquid == L_water, "conduits straight and carrying water");
    // conduit refuses liquid from the tile it flows into
    Building* c5 = h.buildAt(5, 8); Building* c6 = h.buildAt(6, 8);
    CHECK(!h.acceptLiquid(*c5, *c6, L_water) && h.acceptLiquid(*c6, *c5, L_water), "conduit does not accept from its front");
    // open end: Conduit leaks (puddles not ported, counted as leaked)
    Game k; liquidMap(k, 40, 20);
    k.tile(2, 8)->floor = B_shallow_water;
    k.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    for (int x = 3; x <= 6; x++) k.addBuilding(B_conduit, x, 8, TeamSharded, 0);
    for (int i = 0; i < 600; i++) k.update(1.f);
    CHECK(k.liquidLeaked > 1 && std::fabs(k.liquidPumped - (liquidInWorld(k) + k.liquidLeaked)) < 1e-2,
          "open conduit end leaks %.2f, balance kept", k.liquidLeaked);

    // steam generator: 5.5/tick with coal (flammability 1) and 0.1 water/tick
    Game s; liquidMap(s, 40, 20);
    s.tile(2, 8)->floor = B_shallow_water;
    int pump = s.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    for (int x = 3; x <= 5; x++) s.addBuilding(B_conduit, x, 8, TeamSharded, 0);
    int steam = s.addBuilding(B_steam_generator, 6, 8, TeamSharded, 0);  // tiles 6..7 x 8..9
    s.builds[steam].items.add(I_coal, 10);
    for (int i = 0; i < 300; i++) s.update(1.f);
    Building& sg = s.builds[steam];
    CHECK(sg.productionEfficiency > 0.99f && s.status(sg).status == Status::Active,
          "steam generator running: %.0f power/s", sg.productionEfficiency * 5.5f * 60.f);
    CHECK(s.liquidConsumed > 0 && std::fabs(s.liquidPumped - (liquidInWorld(s) + s.liquidLeaked + s.liquidConsumed)) < 1e-2,
          "steam consumed %.2f water, balance kept", s.liquidConsumed);
    // no water: remove the pump, the generator drains then stops
    s.removeBuilding(pump, false);
    for (int i = 0; i < 600; i++) s.update(1.f);
    StatusInfo st = s.status(s.builds[steam]);
    CHECK(st.reason == Reason::MissingLiquid && s.builds[steam].productionEfficiency < 0.01f, "without water: stopped, reason missing water");
    // no fuel, water present
    Game f; liquidMap(f, 40, 20);
    f.tile(2, 8)->floor = B_shallow_water;
    f.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    f.addBuilding(B_conduit, 3, 8, TeamSharded, 0);
    int st2 = f.addBuilding(B_steam_generator, 4, 8, TeamSharded, 0);
    for (int i = 0; i < 120; i++) f.update(1.f);
    CHECK(f.status(f.builds[st2]).reason == Reason::NoFuel && f.builds[st2].liquidAmount > 1.f && f.liquidConsumed == 0,
          "without coal: no fuel, water stored (%.1f) but not consumed", f.builds[st2].liquidAmount);

    // save / reload keeps liquids (format 2)
    auto bytes = saveGame(h);
    Game r;
    bool ok = loadGame(r, bytes);
    CHECK(ok && std::fabs(liquidInWorld(r) - liquidInWorld(h)) < 1e-3 && r.buildAt(12, 8)->liquid == L_water,
          "save/reload keeps %.1f water", liquidInWorld(r));
    for (int i = 0; i < 60; i++) r.update(1.f);
    CHECK(r.buildAt(12, 8)->liquidAmount > h.builds[tank].liquidAmount, "reloaded chain keeps pumping into the tank");
    std::vector<uint8_t> bad = bytes; bad.resize(bad.size() / 2);
    Game untouched; untouched.width = 7;
    CHECK(!loadGame(untouched, bad) && untouched.width == 7, "truncated save refused without touching the game");
}

static void testLegacySaveV1() {
    std::ifstream f("tests/fixtures/save_v1_0.2.sav", std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Game g;
    bool ok = !d.empty() && loadGame(g, d);
    CHECK(ok, "0.2 save (format 1, %zu bytes) still loads", d.size());
    if (!ok) return;
    Building* c = g.core(TeamSharded);
    CHECK(c && c->items.amount[I_copper] == 321 && c->items.amount[I_lead] == 45, "0.2 save: core items restored by block name remap");
    CHECK(g.buildAt(3, 3) && g.buildAt(3, 3)->block == B_mechanical_drill && g.buildAt(15, 8)->block == B_duo &&
          g.buildAt(12, 12)->block == B_battery && g.tile(10, 15)->env == B_snow_wall, "0.2 save: blocks and walls mapped to the new ids");
    auto again = saveGame(g);
    Game h;
    CHECK(loadGame(h, again) && h.core(TeamSharded)->items.amount[I_copper] == 321, "0.2 save re-saved as format 2 and reloaded");
}

// ---------------------------------------------------------------- 0.4 lot 1: transport and liquids
static void testTransportLot1() {
    // Unloader: filtered item from a container to a belt, 11 items/s (speed 60/11), conserved
    Game g; emptyMap(g, 40, 20); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 35, 15, TeamSharded, 0);
    int cont = g.addBuilding(B_container, 5, 5, TeamSharded, 0);  // 2x2: 5..6 x 5..6
    g.builds[cont].items.add(I_copper, 100); g.builds[cont].items.add(I_lead, 100);
    int un = g.addBuilding(B_unloader, 7, 5, TeamSharded, 0);
    for (int x = 8; x <= 20; x++) g.addBuilding(B_conveyor, x, 5, TeamSharded, 0);
    g.addBuilding(B_container, 21, 5, TeamSharded, 0);
    g.builds[un].sortItem = I_lead;
    for (int i = 0; i < 600; i++) g.update(1.f);
    CHECK(g.builds[cont].items.amount[I_copper] == 100 && g.builds[cont].items.amount[I_lead] < 100,
          "unloader filter: only lead taken (%d lead left, copper untouched)", (int)g.builds[cont].items.amount[I_lead]);
    int taken = 100 - g.builds[cont].items.amount[I_lead];
    CHECK(taken >= 40 && taken <= 56, "unloader rate %d items in 10 s (60/11 ticks per item, belt-limited)", taken);
    long onBelt = 0; for (auto& b : g.builds) if (b.alive && b.def().type == BType::Conveyor) onBelt += b.len;
    int arrived = g.buildAt(21, 5)->items.amount[I_lead];
    CHECK(onBelt + arrived == taken, "unloaded items conserved (%ld on belt + %d arrived)", onBelt, arrived);
    // armored conveyor: no side input from non-conveyors, side input from conveyors allowed
    Game a; emptyMap(a, 20, 20); a.spawnPlayer = false;
    a.addBuilding(B_core_shard, 15, 15, TeamSharded, 0);
    a.addBuilding(B_armored_conveyor, 10, 10, TeamSharded, 0);
    a.addBuilding(B_conveyor, 10, 11, TeamSharded, 3);   // conveyor pointing into its side
    int rt = a.addBuilding(B_router, 10, 9, TeamSharded, 0);
    Building& arm = *a.buildAt(10, 10);
    CHECK(!a.acceptItem(arm, a.builds[rt], I_copper) && a.acceptItem(arm, *a.buildAt(10, 11), I_copper),
          "armored conveyor refuses a side router, accepts a side conveyor");
    // phase conveyor: range 12, needs power
    Game p; emptyMap(p, 40, 20); p.spawnPlayer = false;
    p.addBuilding(B_core_shard, 35, 15, TeamSharded, 0);
    int b1 = p.addBuilding(B_phase_conveyor, 5, 5, TeamSharded, 0);
    int b2 = p.addBuilding(B_phase_conveyor, 17, 5, TeamSharded, 0);
    CHECK(p.toggleBridgeLink(b1, b2), "phase conveyor links over 12 tiles");
    p.builds[b1].items.add(I_copper, 10);
    for (int i = 0; i < 120; i++) p.update(1.f);
    CHECK(p.builds[b2].items.amount[I_copper] == 0, "unpowered phase conveyor moves nothing");
    int sol = p.addBuilding(B_combustion_generator, 5, 4, TeamSharded, 0);
    p.builds[sol].items.add(I_coal, 10);
    int node = p.addBuilding(B_power_node, 8, 3, TeamSharded, 0);
    p.togglePowerLink(node, b2); p.togglePowerLink(node, b1);
    for (int i = 0; i < 300; i++) p.update(1.f);
    CHECK(p.builds[b1].items.total + p.builds[b2].items.total == 10 && p.builds[b2].items.total > 0,
          "powered phase conveyor moved %d of 10 items, none lost", p.builds[b2].items.total);
}

static void testLiquidsLot1() {
    // crossing through a liquid junction: two flows do not mix and both arrive
    Game g; liquidMap(g, 40, 30);
    g.tile(2, 10)->floor = B_shallow_water; g.tile(10, 2)->floor = B_shallow_water;
    g.addBuilding(B_mechanical_pump, 2, 10, TeamSharded, 0);
    g.addBuilding(B_mechanical_pump, 10, 2, TeamSharded, 0);
    for (int x = 3; x <= 9; x++) g.addBuilding(B_conduit, x, 10, TeamSharded, 0);
    g.addBuilding(B_liquid_junction, 10, 10, TeamSharded, 0);
    for (int x = 11; x <= 13; x++) g.addBuilding(B_conduit, x, 10, TeamSharded, 0);
    int east = g.addBuilding(B_liquid_router, 14, 10, TeamSharded, 0);
    for (int y = 3; y <= 9; y++) g.addBuilding(B_conduit, 10, y, TeamSharded, 1);
    for (int y = 11; y <= 13; y++) g.addBuilding(B_conduit, 10, y, TeamSharded, 1);
    int north = g.addBuilding(B_liquid_router, 10, 14, TeamSharded, 0);
    g.addBuilding(B_plated_conduit, 15, 10, TeamSharded, 0);  // router dumps into it; plated end: no leak
    for (int i = 0; i < 60 * 30; i++) g.update(1.f);
    CHECK(g.builds[east].liquidAmount > 1 && g.builds[north].liquidAmount > 1 && g.buildAt(10, 10)->liquidAmount == 0,
          "liquid junction crossing: east %.1f, north %.1f, junction holds nothing", g.builds[east].liquidAmount, g.builds[north].liquidAmount);
    CHECK(std::fabs(g.liquidPumped - (liquidInWorld(g) + g.liquidLeaked)) < 1e-4 * g.liquidPumped && g.liquidLeaked == 0,
          "junction network conserves %.1f water, plated end does not leak", g.liquidPumped);
    // plated conduit refuses side input from a non-conduit, saturates without leaking
    Building& pl = *g.buildAt(15, 10);
    CHECK(pl.liquidAmount > 15.f, "plated conduit saturated (%.1f / 16)", pl.liquidAmount);
    // bridge conduit over 4 tiles, with a dead end -> saturation, conservation
    Game b; liquidMap(b, 40, 20);
    b.tile(2, 8)->floor = B_shallow_water;
    b.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    int c1 = b.addBuilding(B_bridge_conduit, 3, 8, TeamSharded, 0);
    int c2 = b.addBuilding(B_bridge_conduit, 7, 8, TeamSharded, 0);
    CHECK(b.toggleBridgeLink(c1, c2) && !b.toggleBridgeLink(c1, b.addBuilding(B_bridge_conduit, 12, 8, TeamSharded, 0)) ,
          "bridge conduit links at 4 tiles, not beyond");
    int sink = b.addBuilding(B_liquid_tank, 9, 8, TeamSharded, 0);  // 8..10: next to the end bridge
    for (int i = 0; i < 60 * 20; i++) b.update(1.f);
    CHECK(b.builds[sink].liquidAmount > 50, "bridge conduit delivered %.1f water", b.builds[sink].liquidAmount);
    CHECK(std::fabs(b.liquidPumped - liquidInWorld(b)) < 1e-4 * b.liquidPumped, "bridge chain conserves water");
    // rotary pump: 2x2 on water, 0.2/tick/tile, needs power
    Game r; liquidMap(r, 40, 20);
    for (int y = 5; y <= 6; y++) for (int x = 5; x <= 6; x++) r.tile(x, y)->floor = B_shallow_water;
    int rp = r.addBuilding(B_rotary_pump, 5, 5, TeamSharded, 0);
    r.update(1.f);
    CHECK(r.builds[rp].liquidAmount == 0, "unpowered rotary pump does not pump");
    int gen = r.addBuilding(B_combustion_generator, 7, 5, TeamSharded, 0);
    r.builds[gen].items.add(I_coal, 20);
    for (int i = 0; i < 3; i++) r.update(1.f);
    float before = r.builds[rp].liquidAmount;
    r.update(1.f);
    CHECK(std::fabs(r.builds[rp].liquidAmount - before - 4 * 0.2f) < 1e-3f, "powered rotary pump: 4 tiles x 0.2 = %.2f per tick",
          r.builds[rp].liquidAmount - before);
    // save / reload of the lot (names table): unloader filter, links, liquids
    auto bytes = saveGame(g);
    Game h;
    CHECK(loadGame(h, bytes) && std::fabs(liquidInWorld(h) - liquidInWorld(g)) < 1e-3 && h.buildAt(10, 10)->block == B_liquid_junction,
          "lot 1 network saved and reloaded");
    auto bb = saveGame(b); Game hb;
    CHECK(loadGame(hb, bb) && hb.buildAt(3, 8)->link == 8 * hb.width + 7, "bridge conduit link kept after reload");
    for (int i = 0; i < 120; i++) hb.update(1.f);
    CHECK(hb.buildAt(9, 8)->liquidAmount > b.builds[sink].liquidAmount, "reloaded bridge keeps flowing");
}

static void testSaveV2From03() {
    std::ifstream f("tests/fixtures/save_v2_0.3.sav", std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Game g;
    bool ok = !d.empty() && loadGame(g, d);
    CHECK(ok, "0.3 save (format 2, 84-block table) loads in this build (%d blocks)", (int)BLOCK_COUNT);
    if (!ok) return;
    CHECK(g.core(TeamSharded)->items.amount[I_copper] == 123 && std::fabs(liquidInWorld(g) - 70.0) < 1e-3 &&
          g.buildAt(8, 8)->block == B_liquid_tank, "0.3 save: core items, tank and 70 water restored");
}

// ---------------------------------------------------------------- 0.4 lot 2a: drills, water boost, diode, large battery
static long drillOutput(bool water, bool power, float seconds) {
    Game g; emptyMap(g, 40, 20); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 35, 15, TeamSharded, 0);
    for (int y = 4; y <= 6; y++) for (int x = 4; x <= 6; x++) g.tile(x, y)->ore = B_ore_titanium;
    int drill = g.addBuilding(B_laser_drill, 5, 5, TeamSharded, 0);  // 3x3: 4..6
    int box = g.addBuilding(B_vault, 5, 8, TeamSharded, 0);           // 3x3 above: collects the output
    if (power)  // two combustion generators touching the drill: 2/tick for the 1.1/tick drill
        for (int y = 4; y <= 5; y++) { int gen = g.addBuilding(B_combustion_generator, 3, y, TeamSharded, 0); g.builds[gen].items.add(I_coal, 10); }
    for (int i = 0; i < (int)(seconds * 60); i++) {
        if (water) { Building& d = g.builds[drill]; d.liquid = L_water; d.liquidAmount = 5; }  // a pump would keep it full
        for (auto& o : g.builds) if (o.alive && o.block == B_combustion_generator && o.items.total < 5) o.items.add(I_coal, 5);
        g.update(1.f);
    }
    return g.builds[box].items.amount[I_titanium];
}

static void testLot2a() {
    long none = drillOutput(false, false, 60);
    CHECK(none == 0, "unpowered laser drill produces nothing (%ld)", none);
    long dry = drillOutput(false, true, 120), wet = drillOutput(true, true, 120);
    // 60 / (280 + 50 * 3) * 9 tiles = 1.26 items/s; water boost x 1.6^2
    CHECK(dry > 110 && dry < 155, "laser drill on 9 titanium: %ld items in 120 s (1.26/s at full warmup)", dry);
    float ratio = (float)wet / (float)dry;
    CHECK(ratio > 2.2f && ratio < 2.7f, "water boost ratio %.2f (liquidBoostIntensity 1.6, squared)", ratio);
    // large battery capacity and diode
    Game g; emptyMap(g, 40, 20); g.spawnPlayer = false;
    g.addBuilding(B_core_shard, 35, 15, TeamSharded, 0);
    int big = g.addBuilding(B_battery_large, 5, 5, TeamSharded, 0);  // 4..6
    int dio = g.addBuilding(B_diode, 7, 5, TeamSharded, 0);           // back = big (6,5), front = (8,5)
    int small = g.addBuilding(B_battery, 8, 5, TeamSharded, 0);
    g.builds[big].powerStored = 40000;
    g.update(1.f);
    CHECK(g.builds[big].graph != g.builds[small].graph && g.builds[dio].graph < 0, "diode separates two networks and joins none");
    for (int i = 0; i < 60; i++) g.update(1.f);
    float total = g.builds[big].powerStored + g.builds[small].powerStored;
    CHECK(g.builds[small].powerStored > 3900.f && std::fabs(total - 40000.f) < 1.f,
          "diode charged the front battery (%.0f / 4000), energy conserved (%.0f)", g.builds[small].powerStored, total);
    g.builds[small].powerStored = 4000; g.builds[big].powerStored = 0;
    for (int i = 0; i < 60; i++) g.update(1.f);
    CHECK(g.builds[big].powerStored < 1.f, "diode is one-way (front -> back moves nothing)");
    CHECK(std::fabs(kBlocks[B_battery_large].powerBuffer - 50000.f) < 1, "large battery buffer 50000");
    // reload
    auto bytes = saveGame(g); Game h;
    CHECK(loadGame(h, bytes) && h.buildAt(7, 5)->block == B_diode && std::fabs(h.buildAt(8, 5)->powerStored - 4000.f) < 1,
          "diode and battery charge kept after reload");
}

int main(int argc, char** argv) {
    if (argc > 1) romfs = argv[1];
    testDrillToCore();
    testConveyorThroughput();
    testCrafterAndPower();
    testTurretKillsDagger();
    testBuildPlan();
    testMaps();
    testSaveRoundTrip();
    testConveyorDirections();
    testConveyorTiling();
    testConveyorMerging();
    testConveyorJunctionTitanium();
    testConveyorClogAndResume();
    testConveyorEditing();
    testPowerConfigAndStatus();
    testLiquids();
    testLegacySaveV1();
    testTransportLot1();
    testLiquidsLot1();
    testSaveV2From03();
    testLot2a();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}

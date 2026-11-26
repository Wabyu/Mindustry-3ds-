// Host-side regression tests for the native simulation.
// Expected values are derived from upstream formulas (see comments).
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

int main(int argc, char** argv) {
    if (argc > 1) romfs = argv[1];
    testDrillToCore();
    testConveyorThroughput();
    testCrafterAndPower();
    testTurretKillsDagger();
    testBuildPlan();
    testMaps();
    testSaveRoundTrip();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}

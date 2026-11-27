// Host cost of one simulation tick on a loaded base (relative numbers, NOT console timings).
#include <chrono>
#include <cstdio>
#include "game.hpp"
using namespace mdt;
int main() {
    Game g; g.width = 120; g.height = 80; g.tiles.assign(120 * 80, Tile{});
    for (auto& t : g.tiles) t.floor = B_stone;
    g.spawnPlayer = false; g.waveTimer = false;
    int core = g.addBuilding(B_core_shard, 110, 40, TeamSharded, 0);
    g.builds[core].items.add(I_coal, 3000);
    int belts = 0, liquids = 0;
    for (int row = 0; row < 30; row++) {           // 30 drill lines of 2x2 drills + belts into a bus
        int y = 2 + row * 2;
        for (int x = 2; x <= 3; x++) g.tile(x, y)->ore = B_ore_copper, g.tile(x, y + 1)->ore = B_ore_copper;
        g.addBuilding(B_mechanical_drill, 2, y, TeamSharded, 0);
        for (int x = 4; x <= 60; x++) { g.addBuilding(x % 2 ? B_conveyor : B_titanium_conveyor, x, y, TeamSharded, 0); belts++; }
    }
    for (int row = 0; row < 15; row++) {           // 15 water lines: pump -> 40 conduits -> tank
        int y = 2 + row * 5;
        g.tile(64, y)->floor = B_shallow_water;
        g.addBuilding(B_mechanical_pump, 64, y, TeamSharded, 0);
        for (int x = 65; x <= 100; x++) { g.addBuilding(B_conduit, x, y, TeamSharded, 0); liquids++; }
        g.addBuilding(B_liquid_tank, 102, y + 1, TeamSharded, 0);
    }
    int alive = 0; for (auto& b : g.builds) if (b.alive) alive++;
    for (int i = 0; i < 600; i++) g.update(1.f);  // warm up and fill the belts
    auto t0 = std::chrono::steady_clock::now();
    const int N = 3000;
    for (int i = 0; i < N; i++) g.update(1.f);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
    std::printf("buildings %d (belts %d, conduits %d): %.1f us per tick on the host\n", alive, belts, liquids, us);
}

// Builds tests/fixtures/save_v2_0.3.sav with the Mindustry 3DS 0.3.2 sources (commit 3a7cf8a).
#include <cstdio>
#include "game.hpp"
#include "save.hpp"
using namespace mdt;
int main(int argc, char** argv) {
    Game g;
    g.width = 30; g.height = 20;
    g.tiles.assign(600, Tile{});
    for (auto& t : g.tiles) t.floor = B_stone;
    g.tile(2, 8)->floor = B_shallow_water;
    g.spawnPlayer = false; g.winWave = 10; g.sector = 0;
    int core = g.addBuilding(B_core_shard, 25, 10, TeamSharded, 0);
    g.builds[core].items.add(I_copper, 123);
    g.addBuilding(B_mechanical_pump, 2, 8, TeamSharded, 0);
    for (int x = 3; x <= 6; x++) g.addBuilding(B_conduit, x, 8, TeamSharded, 0);
    g.addBuilding(B_liquid_tank, 8, 8, TeamSharded, 0);
    for (int i = 0; i < 600; i++) g.update(1.f);
    auto data = saveGame(g);
    FILE* f = std::fopen(argc > 1 ? argv[1] : "save_v2_0.3.sav", "wb");
    std::fwrite(data.data(), 1, data.size(), f); std::fclose(f);
    double w = 0; for (auto& b : g.builds) if (b.alive && b.liquid >= 0) w += b.liquidAmount;
    std::printf("%zu bytes, water %.3f\n", data.size(), w);
}

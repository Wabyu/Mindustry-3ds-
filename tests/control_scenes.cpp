// Reproducible visual control scenes (fixed camera, slow pans, diagonals, conveyor
// tiling board, buildings on the screen edges). Records the citro2d calls; images are
// composed by tools/render_shots.py and checked by tools/check_scenes.py.
// Usage: control_scenes <romfs> <outdir> [nosnap]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "app.hpp"
#include "gfx.hpp"
#include "gfx_quad.hpp"

namespace hostgfx { extern std::string out, romfs, data, lang; extern bool recording; }
using namespace mdt;

static std::string outDir;

static void record(App& app, const std::string& name) {
    hostgfx::out.clear();
    hostgfx::recording = true;
    app.draw();
    hostgfx::recording = false;
    FILE* f = std::fopen((outDir + "/" + name + ".jsonl").c_str(), "w");
    if (f) { std::fwrite(hostgfx::out.data(), 1, hostgfx::out.size(), f); std::fclose(f); }
}

static void emptyMap(Game& g, int w, int h, int floor) {
    g.width = w; g.height = h;
    g.tiles.assign((size_t)w * h, Tile{});
    for (auto& t : g.tiles) t.floor = (uint16_t)floor;
    g.builds.clear(); g.freeBuilds.clear(); g.units.clear(); g.bullets.clear(); g.plans.clear();
    g.waves.clear(); g.spawns.clear();
    g.waveTimer = false; g.winWave = 0; g.state = GameState::Playing; g.spawnPlayer = false; g.sector = 0;
}

static const int dx4[4] = {1, 0, -1, 0}, dy4[4] = {0, 1, 0, -1};

// One autotiler case around the conveyor at (cx, cy) with rotation r.
// feeds: bit0 = left side (rotation+1), bit1 = back, bit2 = right side (rotation-1)
static void conveyorCase(Game& g, int cx, int cy, int r, int feeds, int16_t block) {
    g.addBuilding(block, cx, cy, TeamSharded, r);
    g.addBuilding(block, cx + dx4[r], cy + dy4[r], TeamSharded, r);  // exit
    const int dirs[3] = {(r + 1) % 4, (r + 2) % 4, (r + 3) % 4};
    for (int k = 0; k < 3; k++) {
        if (!(feeds & (1 << k))) continue;
        int d = dirs[k];
        // feeder sits on side d and points back toward the centre conveyor
        g.addBuilding(block, cx + dx4[d], cy + dy4[d], TeamSharded, (d + 2) % 4);
    }
}

// 7 autotiler cases for one rotation: straight, left, right, T, back+left, back+right, cross
static void boardScene(App& app, int rot, const char* name) {
    Game& g = app.game;
    emptyMap(g, 40, 30, B_metal_floor);
    const int feeds[7] = {2, 1, 4, 5, 3, 6, 7};
    for (int c = 0; c < 7; c++) {
        int col = c % 4, row = c / 4;
        // basic conveyors on the top row, titanium below: same autotiler, other sprites
        conveyorCase(g, 8 + col * 5, 18 - row * 6, rot, feeds[c], row ? B_titanium_conveyor : B_conveyor);
    }
    for (int t = 0; t < 240; t++) {
        for (auto& b : g.builds) if (b.alive && b.def().type == BType::Conveyor && b.len < 2 && (t % 30) == 0) {
            Building fake; fake.alive = true; fake.block = B_router; fake.team = TeamSharded;
            fake.tx = b.tx - dx4[b.rotation]; fake.ty = b.ty - dy4[b.rotation];
            if (g.acceptItem(b, fake, I_copper)) g.handleItem(b, fake, I_copper);
        }
        g.update(1.f);
    }
    app.camX = 15.5f * tilesize; app.camY = 15 * tilesize;
    record(app, name);
}

static void connectionsScene(App& app) {
    Game& g = app.game;
    emptyMap(g, 40, 30, B_stone);
    for (int y = 4; y <= 5; y++) for (int x = 3; x <= 4; x++) g.tile(x, y)->ore = B_ore_copper;
    g.addBuilding(B_mechanical_drill, 3, 4, TeamSharded, 0);
    for (int x = 5; x <= 8; x++) g.addBuilding(x < 7 ? B_conveyor : B_titanium_conveyor, x, 4, TeamSharded, 0);  // basic -> titanium
    g.addBuilding(B_router, 9, 4, TeamSharded, 0);
    g.addBuilding(B_conveyor, 9, 5, TeamSharded, 1);
    g.addBuilding(B_conveyor, 10, 4, TeamSharded, 0);
    g.addBuilding(B_junction, 11, 4, TeamSharded, 0);
    g.addBuilding(B_conveyor, 12, 4, TeamSharded, 0);
    g.addBuilding(B_conveyor, 11, 3, TeamSharded, 1);   // crossing through the junction
    g.addBuilding(B_conveyor, 11, 5, TeamSharded, 1);
    g.addBuilding(B_sorter, 13, 4, TeamSharded, 0);     // non-square: joints are drawn
    g.addBuilding(B_conveyor, 14, 4, TeamSharded, 0);
    g.addBuilding(B_power_node, 15, 6, TeamSharded, 0);  // non-square neighbour
    g.addBuilding(B_conveyor, 15, 5, TeamSharded, 3);
    g.addBuilding(B_conveyor, 15, 4, TeamSharded, 0);
    g.addBuilding(B_core_shard, 18, 4, TeamSharded, 0);  // conveyors into a 3x3 core
    g.addBuilding(B_conveyor, 16, 4, TeamSharded, 0);
    g.addBuilding(B_conveyor, 18, 7, TeamSharded, 3);
    g.addBuilding(B_conveyor, 18, 8, TeamSharded, 3);
    // crafter fed from the side, output dumped onto a belt
    g.addBuilding(B_graphite_press, 6, 9, TeamSharded, 0);
    g.addBuilding(B_conveyor, 5, 9, TeamSharded, 0);
    g.addBuilding(B_conveyor, 8, 9, TeamSharded, 0);
    g.addBuilding(B_conveyor, 9, 9, TeamSharded, 0);
    g.addBuilding(B_scrap_wall, 10, 10, TeamDerelict, 0);
    g.addBuilding(B_conveyor, 10, 9, TeamSharded, 1);
    // drill side outputs at both rows of a 2x2 block
    for (int y = 12; y <= 13; y++) for (int x = 3; x <= 4; x++) g.tile(x, y)->ore = B_ore_lead;
    g.addBuilding(B_mechanical_drill, 3, 12, TeamSharded, 0);
    g.addBuilding(B_conveyor, 5, 12, TeamSharded, 3);
    g.addBuilding(B_conveyor, 5, 13, TeamSharded, 3);
    g.addBuilding(B_conveyor, 5, 11, TeamSharded, 0);
    g.addBuilding(B_conveyor, 6, 11, TeamSharded, 0);
    g.addBuilding(B_battery, 8, 13, TeamSharded, 0);
    g.addBuilding(B_combustion_generator, 9, 13, TeamSharded, 0);
    g.addBuilding(B_duo, 11, 13, TeamSharded, 0);
    g.addBuilding(B_bridge_conveyor, 13, 13, TeamSharded, 0);
    g.addBuilding(B_bridge_conveyor, 16, 13, TeamSharded, 0);
    g.buildAt(13, 13)->link = 13 * g.width + 16;
    g.builds[g.core(TeamSharded)->id].items.add(I_copper, 100);
    for (int t = 0; t < 900; t++) g.update(1.f);
    app.camX = 11 * tilesize; app.camY = 8 * tilesize;
    record(app, "connections");
}

static void dragPreviewScene(App& app) {
    Game& g = app.game;
    emptyMap(g, 40, 30, B_stone);
    g.addBuilding(B_core_shard, 4, 4, TeamSharded, 0);
    // a drag from (18,11) to the core, as queued by holding A: plans only (ghost preview)
    for (int x = 18; x >= 15; x--) g.plans.push_back({x, 11, B_conveyor, 2, false});   // west
    for (int y = 11; y >= 7; y--) g.plans.push_back({14, y, B_conveyor, 3, false});    // south
    for (int x = 14; x >= 7; x--) g.plans.push_back({x, 6, B_conveyor, 2, false});     // west into the core
    g.plans.push_back({10, 9, B_conveyor, 0, false});                                  // side input
    g.plans.push_back({11, 9, B_conveyor, 0, false});
    g.plans.push_back({12, 9, B_conveyor, 3, false});
    g.plans.push_back({12, 8, B_conveyor, 3, false});
    g.plans.push_back({12, 7, B_conveyor, 3, false});
    app.camX = 12 * tilesize; app.camY = 8 * tilesize;
    record(app, "drag_preview");
}

static void edgeScene(App& app, const char* name, float camX, float camY) {
    Game& g = app.game;
    emptyMap(g, 60, 40, B_snow);
    // trees, walls and multi-tile buildings straddling every screen edge
    float halfW = 200 / 2.f / tilesize * 1.f, halfH = 120 / 2.f / tilesize;
    int cx = (int)(camX / tilesize), cy = (int)(camY / tilesize);
    int left = cx - (int)halfW - 1, right = cx + (int)halfW + 1, bottom = cy - (int)halfH - 1, top = cy + (int)halfH + 1;
    for (int y = bottom - 1; y <= top + 1; y += 3) { g.tile(left, y)->env = B_white_tree; g.tile(right, y)->env = B_white_tree_dead; }
    for (int x = left; x <= right; x += 4) { g.tile(x, top)->env = B_snow_wall; g.tile(x + 2, bottom)->env = B_snow_pine; }
    g.addBuilding(B_core_shard, left + 1, cy, TeamSharded, 0);
    g.addBuilding(B_mechanical_drill, right, cy - 2, TeamSharded, 0);
    g.addBuilding(B_scatter, cx, top, TeamSharded, 0);
    g.addBuilding(B_graphite_press, cx - 4, bottom, TeamSharded, 0);
    g.addBuilding(B_duo, right, cy + 2, TeamSharded, 0);
    app.camX = camX; app.camY = camY;
    record(app, name);
}

static void panScene(App& app, const char* prefix, float vx, float vy, int frames) {
    Game& g = app.game;
    emptyMap(g, 60, 40, B_metal_floor);
    for (int x = 10; x < 50; x += 3) g.addBuilding(B_conveyor, x, 20, TeamSharded, 0);
    for (int i = 0; i < frames; i++) {
        app.camX = 30 * tilesize + vx * i; app.camY = 20 * tilesize + vy * i;
        char n[64]; std::snprintf(n, sizeof n, "%s_%02d", prefix, i);
        record(app, n);
    }
}

int main(int argc, char** argv) {
    hostgfx::romfs = argc > 1 ? argv[1] : "romfs";
    outDir = argc > 2 ? argv[2] : "build/scenes";
    if (argc > 3 && !std::strcmp(argv[3], "nosnap")) gfx::snapEnabled = false;
    hostgfx::data = outDir;
    App app;
    app.init();
    app.screen = Screen::Game;
    app.hideHud = true;
    boardScene(app, 0, "board_rot0");
    boardScene(app, 1, "board_rot1");
    boardScene(app, 2, "board_rot2");
    boardScene(app, 3, "board_rot3");
    connectionsScene(app);
    dragPreviewScene(app);
    edgeScene(app, "edges", 30 * tilesize + 0.3f, 20 * tilesize + 0.7f);
    panScene(app, "pan_x", 0.25f, 0.f, 8);          // slow horizontal pan, quarter world unit per frame
    panScene(app, "pan_diag", 0.37f, 0.21f, 8);     // diagonal sub-pixel pan
    // power configuration and status sheets (HUD visible)
    {
        Game& g = app.game;
        emptyMap(g, 40, 30, B_stone);
        int core = g.addBuilding(B_core_shard, 30, 20, TeamSharded, 0);
        g.builds[core].items.add(I_copper, 300);
        int node = g.addBuilding(B_power_node, 15, 15, TeamSharded, 0);
        int gen = g.addBuilding(B_combustion_generator, 11, 15, TeamSharded, 0);
        int sol = g.addBuilding(B_solar_panel, 15, 19, TeamSharded, 0);
        int bat = g.addBuilding(B_battery, 16, 15, TeamSharded, 0);
        int sm = g.addBuilding(B_silicon_smelter, 19, 12, TeamSharded, 0);
        g.addBuilding(B_graphite_press, 11, 11, TeamSharded, 0);
        g.togglePowerLink(node, gen); g.togglePowerLink(node, sol); g.togglePowerLink(node, sm);
        g.builds[gen].items.add(I_coal, 3);
        g.builds[sm].items.add(I_coal, 4); g.builds[sm].items.add(I_sand, 8);
        (void)bat;
        for (int t = 0; t < 120; t++) g.update(1.f);
        app.hideHud = false;
        app.camX = 15 * tilesize; app.camY = 15 * tilesize;
        app.cursorDX = 0; app.cursorDY = 0;
        app.configuring = node; app.cursorTX = 15; app.cursorTY = 15;
        record(app, "power_config");
        app.configuring = -1; app.cursorTX = 19; app.cursorTY = 12;
        record(app, "inspect_smelter");
        app.cursorTX = 11; app.cursorTY = 11;
        record(app, "inspect_press");
        // node placement preview
        app.category = CatPower; app.selected[CatPower] = 0;
        for (size_t k = 0; k < app.blocksIn(CatPower).size(); k++) if (app.blocksIn(CatPower)[k] == B_power_node) app.selected[CatPower] = (int)k;
        app.cursorTX = 20; app.cursorTY = 17;
        record(app, "node_preview");
        app.hideHud = true;
    }
    // palette of every category (bottom screen)
    {
        Game& g = app.game;
        emptyMap(g, 30, 20, B_stone);
        int core = g.addBuilding(B_core_shard, 15, 10, TeamSharded, 0);
        g.builds[core].items.add(I_copper, 400); g.builds[core].items.add(I_lead, 120); g.builds[core].items.add(I_graphite, 20);
        app.camX = 15 * tilesize; app.camY = 10 * tilesize;
        for (int c = 0; c < CAT_COUNT; c++) {
            if (app.blocksIn(c).empty()) continue;
            app.category = c;
            app.selected[c] = (int)app.blocksIn(c).size() - 1;
            char n[48]; std::snprintf(n, sizeof n, "palette_%d", c);
            record(app, n);
        }
    }
    std::printf("scenes written to %s\n", outDir.c_str());
    return 0;
}

// Scripted session on the real Ground Zero map, recording draw commands for screenshots.
// Usage: screenshot <romfs> <outdir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
#include "app.hpp"
#include "gfx.hpp"

namespace hostgfx { extern std::string out, romfs, data, lang; extern bool recording; }
using namespace mdt;

static std::string outDir = "build/shots";

static void shot(App& app, const char* name) {
    hostgfx::out.clear();
    hostgfx::recording = true;
    app.draw();
    hostgfx::recording = false;
    std::string path = outDir + "/" + name + ".jsonl";
    FILE* f = std::fopen(path.c_str(), "w");
    if (f) { std::fwrite(hostgfx::out.data(), 1, hostgfx::out.size(), f); std::fclose(f); }
    std::printf("wrote %s\n", path.c_str());
}

static void step(App& app, int frames, float sx = 0, float sy = 0) {
    platform::Input in;
    in.stickX = sx; in.stickY = sy;
    for (int i = 0; i < frames; i++) app.frame(in, 1.f);
}

// BFS conveyor path from a building edge to the core edge over free tiles
static std::vector<std::pair<int, int>> path(Game& g, Building& from, Building& to) {
    int w = g.width, h = g.height;
    std::vector<int> prev((size_t)w * h, -2);
    std::deque<int> q;
    int x0, y0, x1, y1;
    g.linkedTiles(from.block, from.tx, from.ty, x0, y0, x1, y1);
    auto freeTile = [&](int x, int y) {
        Tile* t = g.tile(x, y);
        return t && t->build < 0 && !(t->env && kBlocks[t->env].type == BType::StaticWall) && !kBlocks[t->floor].isDeep && !t->ore;
    };
    for (int y = y0 - 1; y <= y1 + 1; y++)
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            bool edge = (x == x0 - 1 || x == x1 + 1) != (y == y0 - 1 || y == y1 + 1);
            if (edge && freeTile(x, y)) { prev[y * w + x] = -1; q.push_back(y * w + x); }
        }
    int tx0, ty0, tx1, ty1;
    g.linkedTiles(to.block, to.tx, to.ty, tx0, ty0, tx1, ty1);
    const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        int x = c % w, y = c / w;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx >= tx0 && nx <= tx1 && ny >= ty0 && ny <= ty1) {
                std::vector<std::pair<int, int>> p;
                for (int k = c; k >= 0; k = prev[k]) p.insert(p.begin(), {k % w, k / w});
                return p;
            }
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || prev[ny * w + nx] != -2 || !freeTile(nx, ny)) continue;
            prev[ny * w + nx] = c;
            q.push_back(ny * w + nx);
        }
    }
    return {};
}

int main(int argc, char** argv) {
    if (argc > 1) hostgfx::romfs = argv[1];
    if (argc > 2) outDir = argv[2];
    hostgfx::data = outDir;
    App app;
    app.init();
    app.frame(platform::Input{}, 1.f);
    shot(app, "menu");

    app.startSector(0, false);
    Game& g = app.game;
    Building* core = g.core(TeamSharded);
    if (!core) { std::printf("no core\n"); return 1; }
    int coreId = core->id;
    step(app, 30);
    // pick the two best copper drill spots near the core
    struct Spot { int x, y, ore; float d; };
    std::vector<Spot> spots;
    for (int y = core->ty - 22; y <= core->ty + 22; y++)
        for (int x = core->tx - 22; x <= core->tx + 22; x++) {
            if (!g.validPlace(B_mechanical_drill, x, y, 0, TeamSharded)) continue;
            int n = 0;
            for (int k = 0; k < 4; k++) if (g.drop(x + k % 2, y + k / 2) == I_copper) n++;
            if (n >= 3) spots.push_back({x, y, n, (float)((x - core->tx) * (x - core->tx) + (y - core->ty) * (y - core->ty))});
        }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.ore != b.ore ? a.ore > b.ore : a.d < b.d; });
    std::vector<Spot> chosen;
    for (auto& s : spots) {
        bool overlap = false;
        for (auto& c : chosen) if (std::abs(c.x - s.x) < 3 && std::abs(c.y - s.y) < 3) overlap = true;
        if (!overlap) chosen.push_back(s);
        if (chosen.size() == 3) break;
    }
    std::printf("drill spots: %zu\n", chosen.size());
    for (size_t i = 0; i < chosen.size(); i++) {
        auto& s = chosen[i];
        int bi = g.addBuilding(B_mechanical_drill, s.x, s.y, TeamSharded, 0);  // placed instantly for the setup
        auto p = path(g, g.builds[bi], g.builds[coreId]);
        for (size_t k = 0; k < p.size(); k++) {
            int nx, ny;
            if (k + 1 < p.size()) { nx = p[k + 1].first; ny = p[k + 1].second; }
            else {  // last tile: point into the core
                int x0, y0, x1, y1; g.linkedTiles(B_core_shard, g.builds[coreId].tx, g.builds[coreId].ty, x0, y0, x1, y1);
                nx = std::clamp(p[k].first, x0, x1); ny = std::clamp(p[k].second, y0, y1);
            }
            int ddx = nx - p[k].first, ddy = ny - p[k].second;
            uint8_t rot = ddx > 0 ? 0 : ddx < 0 ? 2 : ddy > 0 ? 1 : 3;
            g.queuePlan(p[k].first, p[k].second, B_conveyor, rot, false);
        }
        std::printf("drill %zu at %d,%d ore=%d conveyor=%zu\n", i, s.x, s.y, s.ore, p.size());
    }
    // duos fed by a drill next to them, between the core and the enemy spawn
    if (!g.spawns.empty()) {
        float sx = g.spawns[0].first - core->tx, sy = g.spawns[0].second - core->ty;
        float len = std::sqrt(sx * sx + sy * sy);
        int placed = 0;
        for (int r = 6; r < 14 && placed < 2; r++)
            for (int off = -3; off <= 3 && placed < 2; off += 3) {
                int x = core->tx + (int)(sx / len * r) + (int)(-sy / len * off), y = core->ty + (int)(sy / len * r) + (int)(sx / len * off);
                if (g.validPlace(B_duo, x, y, 0, TeamSharded) && g.queuePlan(x, y, B_duo, 0, false)) placed++;
            }
    }
    g.builds[coreId].items.add(I_copper, 300);
    g.builds[coreId].items.add(I_lead, 100);
    // let the alpha build everything (it hovers over the core)
    for (int i = 0; i < 60 * 90 && !g.plans.empty(); i++) step(app, 1);
    std::printf("plans left: %zu\n", g.plans.size());
    // feed the duos: graphite from the core (players carry items to turrets)
    for (auto& b : g.builds) if (b.alive && b.block == B_duo) {
        Building& c = g.builds[coreId];
        for (int k = 0; k < 10; k++) if (g.acceptItem(b, c, I_copper) && c.items.amount[I_copper] > 0) { c.items.remove(I_copper, 1); g.handleItem(b, c, I_copper); }
    }
    step(app, 60 * 40);
    int copper = g.builds[coreId].items.amount[I_copper];
    std::printf("core copper after 40 s: %d (delivered total %ld)\n", copper, g.itemsDelivered[I_copper]);
    app.cursorDX = 3; app.cursorDY = -2;
    app.category = CatDistribution;
    shot(app, "base");

    // wave
    g.wavetime = 1;
    step(app, 2);
    std::printf("enemies: %d\n", g.enemies());
    int frames = 0;
    for (; frames < 60 * 120; frames++) {
        step(app, 1);
        bool near = false;
        for (auto& u : g.units) if (u.alive && u.team == TeamCrux) {
            float dx = u.x - g.builds[coreId].tx * tilesize, dy = u.y - g.builds[coreId].ty * tilesize;
            if (dx * dx + dy * dy < 200 * 200) near = true;
        }
        if (near) break;
    }
    std::printf("enemy reached the base after %d frames\n", frames);
    app.category = CatTurret;
    app.mode = Mode::Build;
    // fly the alpha toward the closest enemy so it is in view
    for (int k = 0; k < 90; k++) {
        float bx = 0, by = 0, bd = 1e30f;
        Unit& p = g.units[g.player];
        for (auto& u : g.units) if (u.alive && u.team == TeamCrux) {
            float dx = u.x - p.x, dy = u.y - p.y, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; bx = dx; by = dy; }
        }
        if (bd > 1e29f || bd < 90 * 90) { step(app, 1); continue; }
        float l = std::sqrt(bd);
        step(app, 1, bx / l, by / l);
    }
    shot(app, "combat");
    app.screen = Screen::Pause;
    shot(app, "pause");
    return 0;
}

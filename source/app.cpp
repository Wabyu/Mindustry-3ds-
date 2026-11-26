// Application: menus, controls, world rendering and the touch screen HUD.
#include "app.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "gfx.hpp"
#include "save.hpp"
#include "sprites_gen.hpp"

namespace mdt {

using namespace platform;

static const uint32_t kTeamColor[3] = {0x4d4e58ff, 0xffd37fff, 0xf25555ff};  // Team.derelict/sharded/crux
static const uint32_t kAccent = 0xffd37fff;    // Pal.accent
static const uint32_t kPanel = 0x252525e6;
static const uint32_t kPanelLight = 0x454545ff;
static const uint32_t kGray = 0x9a9a9aff;
static const uint32_t kRed = 0xe55454ff;      // Pal.remove
static const float kZoom = 2.f;               // screen pixels per world unit (16 px per tile)

// ---------------------------------------------------------------- language
void Lang::load(const std::string& c) {
    code = c;
    strings.clear();
    std::vector<uint8_t> data;
    if (!readFile(assetPath("lang/" + c + ".txt"), data) && !readFile(assetPath("lang/en.txt"), data)) return;
    std::string s(data.begin(), data.end());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(pos, e - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string v = line.substr(eq + 1);
            size_t k;
            while ((k = v.find("\\n")) != std::string::npos) v.replace(k, 2, "\n");
            strings[line.substr(0, eq)] = v;
        }
        pos = e + 1;
    }
}

std::string Lang::get(const std::string& key, const std::string& fallback) const {
    auto it = strings.find(key);
    return it == strings.end() ? fallback : it->second;
}
std::string Lang::blockName(int b) const { return get(std::string("block.") + kBlocks[b].name + ".name", kBlocks[b].name); }
std::string Lang::itemName(int i) const { return get(std::string("item.") + kItems[i].name + ".name", kItems[i].name); }
std::string Lang::sectorName(int s) const { return get(std::string("sector.") + kSectors[s].name + ".name", kSectors[s].name); }

struct UiString { const char* key; const char* en; const char* fr; };
static const UiString kUi[] = {
    {"build", "Build", "Construire"},
    {"break", "Remove", "Démolir"},
    {"mine", "Mine", "Miner"},
    {"nextwave", "Next wave", "Vague suiv."},
    {"wave", "Wave", "Vague"},
    {"in", "in", "dans"},
    {"enemies", "enemies", "ennemis"},
    {"paused", "Paused", "Pause"},
    {"resume", "Resume", "Reprendre"},
    {"savequit", "Save and quit", "Sauvegarder et quitter"},
    {"quit", "Quit without saving", "Quitter sans sauvegarder"},
    {"captured", "Sector captured!", "Secteur capturé !"},
    {"lost", "Core destroyed", "Noyau détruit"},
    {"continue", "Continue", "Continuer"},
    {"new", "New", "Nouvelle partie"},
    {"sectors", "Serpulo sectors", "Secteurs de Serpulo"},
    {"menu", "Back to sectors", "Retour aux secteurs"},
    {"saved", "Saved to SD", "Sauvegardé sur la SD"},
    {"invalid", "Cannot place here", "Placement impossible"},
    {"survive", "Survive", "Survivre"},
    {"waves", "waves", "vagues"},
    {"done", "captured", "capturé"},
    {"rotate", "X: rotate", "X : tourner"},
    {"exit", "Exit", "Quitter"},
    {"help", "Circle pad: fly  D-pad: cursor  A: place  B: remove\nY: mine  X: rotate  L/R: block  SELECT: category",
             "Stick : voler  Croix : curseur  A : poser  B : démolir\nY : miner  X : tourner  L/R : bloc  SELECT : catégorie"},
    {"power", "Power", "Énergie"},
    {"ammo", "Ammo", "Munitions"},
};
std::string Lang::ui(const char* key) const {
    for (auto& u : kUi)
        if (!std::strcmp(u.key, key)) return code == "fr" ? u.fr : u.en;
    return key;
}

static const char* kCategoryFr[CAT_COUNT] = {"Tourelles", "Production", "Distribution", "Liquides", "Énergie",
                                             "Défense", "Fabrication", "Unités", "Effets", "Logique"};
static const char* kCategoryEn[CAT_COUNT] = {"Turrets", "Production", "Distribution", "Liquid", "Power",
                                             "Defense", "Crafting", "Units", "Effect", "Logic"};

// ---------------------------------------------------------------- app
void App::init() {
    lang.load(languageCode());
    loadCampaign();
}

void App::loadCampaign() {
    std::vector<uint8_t> d;
    if (readFile(dataPath("campaign.txt"), d)) {
        std::string s(d.begin(), d.end());
        for (int i = 0; i < SECTOR_COUNT; i++)
            if (s.find(std::string(kSectors[i].name) + "=captured") != std::string::npos) captured[i] = true;
    }
    for (int i = 0; i < SECTOR_COUNT; i++) {
        std::vector<uint8_t> tmp;
        hasSave[i] = readFile(dataPath(std::string("sector-") + kSectors[i].name + ".sav"), tmp);
    }
}

void App::saveCampaign() {
    std::string s;
    for (int i = 0; i < SECTOR_COUNT; i++) s += std::string(kSectors[i].name) + "=" + (captured[i] ? "captured" : "open") + "\n";
    writeFileAtomic(dataPath("campaign.txt"), std::vector<uint8_t>(s.begin(), s.end()));
}

void App::saveCurrent() {
    if (game.tiles.empty() || game.state != GameState::Playing) return;
    auto data = saveGame(game);
    if (writeFileAtomic(dataPath(std::string("sector-") + kSectors[game.sector].name + ".sav"), data)) {
        hasSave[game.sector] = true;
        toast = lang.ui("saved"); toastTime = 120;
    }
}

void App::startSector(int s, bool resume) {
    bool ok = false;
    if (resume) {
        std::vector<uint8_t> d;
        if (readFile(dataPath(std::string("sector-") + kSectors[s].name + ".sav"), d)) ok = loadGame(game, d);
    }
    if (!ok) {
        std::vector<uint8_t> d;
        if (!readFile(assetPath(std::string("maps/") + kSectors[s].name + ".m3d"), d) || !game.loadMap(d, s)) {
            toast = "map load failed"; toastTime = 240;
            return;
        }
    }
    Building* c = game.core(TeamSharded);
    if (c) { camX = c->tx * tilesize; camY = c->ty * tilesize; }
    screen = Screen::Game;
    autosaveTimer = 0;
    toast = lang.ui("help"); toastTime = 420;
}

std::vector<int> App::blocksIn(int cat) const {
    std::vector<int> out;
    for (int b = 0; b < BLOCK_COUNT; b++)
        if (kBlocks[b].buildable && kBlocks[b].category == cat) out.push_back(b);
    return out;
}

int App::selectedBlock() const {
    auto list = blocksIn(category);
    if (list.empty()) return 0;
    return list[std::clamp(selected[category], 0, (int)list.size() - 1)];
}

static std::vector<int> usedCategories() {
    std::vector<int> c;
    for (int cat = 0; cat < CAT_COUNT; cat++)
        for (int b = 0; b < BLOCK_COUNT; b++)
            if (kBlocks[b].buildable && kBlocks[b].category == cat) { c.push_back(cat); break; }
    return c;
}

void App::frame(const Input& in, float delta) {
    if (toastTime > 0) toastTime -= delta;
    switch (screen) {
        case Screen::Menu: updateMenu(in); break;
        case Screen::Game: updateGame(in, delta); break;
        case Screen::Pause: updatePause(in); break;
        case Screen::End:
            if (in.down & (KA | KB | KStart) || in.touchStart) { screen = Screen::Menu; loadCampaign(); }
            break;
    }
}

// ---------------------------------------------------------------- menu
void App::updateMenu(const Input& in) {
    int rows = SECTOR_COUNT + 1;
    if (in.down & KDown) menuIndex = (menuIndex + 1) % rows;
    if (in.down & KUp) menuIndex = (menuIndex + rows - 1) % rows;
    auto activate = [&](int i, bool resume) {
        if (i == SECTOR_COUNT) { exitRequested = true; return; }
        // campaign order: a sector opens once the previous one is captured
        bool open = i == 0 || captured[i - 1] || captured[i] || hasSave[i];
        if (!open) { toast = lang.ui("survive") + " " + lang.sectorName(i - 1); toastTime = 120; return; }
        startSector(i, resume && hasSave[i]);
    };
    if (in.down & KA) activate(menuIndex, true);
    if (in.down & KX) activate(menuIndex, false);
    if (in.touchStart) {
        for (int i = 0; i < rows; i++) {
            int y = 34 + i * 34;
            if (in.touchY >= y && in.touchY < y + 30) {
                menuIndex = i;
                activate(i, true);
            }
        }
    }
}

void App::updatePause(const Input& in) {
    if (in.down & KDown) pauseIndex = (pauseIndex + 1) % 3;
    if (in.down & KUp) pauseIndex = (pauseIndex + 2) % 3;
    int choice = -1;
    if (in.down & KA) choice = pauseIndex;
    if (in.down & (KB | KStart)) choice = 0;
    if (in.touchStart)
        for (int i = 0; i < 3; i++) if (in.touchY >= 70 + i * 44 && in.touchY < 108 + i * 44) choice = i;
    if (choice == 0) screen = Screen::Game;
    if (choice == 1) { saveCurrent(); screen = Screen::Menu; loadCampaign(); }
    if (choice == 2) { screen = Screen::Menu; loadCampaign(); }
}

// ---------------------------------------------------------------- game
void App::updateGame(const Input& in, float delta) {
    if (in.down & KStart) { screen = Screen::Pause; pauseIndex = 0; return; }
    game.updatePlayer(delta, in.stickX, in.stickY);
    game.update(delta);
    if (game.state == GameState::Won) {
        captured[game.sector] = true;
        saveCampaign();
        std::remove(dataPath(std::string("sector-") + kSectors[game.sector].name + ".sav").c_str());
        hasSave[game.sector] = false;
        screen = Screen::End;
        return;
    }
    if (game.state == GameState::Lost) {
        std::remove(dataPath(std::string("sector-") + kSectors[game.sector].name + ".sav").c_str());
        hasSave[game.sector] = false;
        screen = Screen::End;
        return;
    }
    autosaveTimer += delta;
    if (autosaveTimer > 60 * 120) { autosaveTimer = 0; saveCurrent(); }

    // camera follows the player (or the core while respawning)
    float tx = camX, ty = camY;
    if (game.player >= 0) { tx = game.units[game.player].x; ty = game.units[game.player].y; }
    camX = lerp(camX, tx, std::fmin(1.f, 0.25f * delta));
    camY = lerp(camY, ty, std::fmin(1.f, 0.25f * delta));

    // cursor movement with key repeat
    uint32_t dirs = in.held & (KUp | KDown | KLeft | KRight);
    bool step = (in.down & (KUp | KDown | KLeft | KRight)) != 0;
    if (dirs) { repeatTimer += delta; if (repeatTimer > 14) { step = true; repeatTimer = 10; } }
    else repeatTimer = 0;
    if (step) {
        if (dirs & KRight) cursorDX++;
        if (dirs & KLeft) cursorDX--;
        if (dirs & KUp) cursorDY++;
        if (dirs & KDown) cursorDY--;
        cursorDX = std::clamp(cursorDX, -12, 12);
        cursorDY = std::clamp(cursorDY, -7, 7);
    }
    int px = (int)std::floor(camX / tilesize + 0.5f), py = (int)std::floor(camY / tilesize + 0.5f);
    int cx = px + cursorDX, cy = py + cursorDY;

    auto cats = usedCategories();
    if (in.down & KSelect) {
        auto it = std::find(cats.begin(), cats.end(), category);
        int i = it == cats.end() ? 0 : (int)(it - cats.begin());
        category = cats[(i + 1) % cats.size()];
    }
    auto list = blocksIn(category);
    if (!list.empty()) {
        if (in.down & KR) selected[category] = (selected[category] + 1) % (int)list.size();
        if (in.down & KL) selected[category] = (selected[category] + (int)list.size() - 1) % (int)list.size();
    }
    if (in.down & KX) rotation = (rotation + 1) % 4;

    // A: place (held: drag lines, e.g. conveyors), B: remove, Y: mine
    if (in.held & KA) {
        if (mode == Mode::Mine) {
            if (in.down & KA && game.player >= 0) game.units[game.player].mineTile = cy * game.width + cx;
        } else if (mode == Mode::Break) {
            game.queuePlan(cx, cy, 0, 0, true);
        } else if ((in.down & KA) || cx != lastPlaceX || cy != lastPlaceY) {
            // dragging a rotating block orients it along the drag direction
            if (!(in.down & KA) && kBlocks[selectedBlock()].rotate) {
                int ddx = cx - lastPlaceX, ddy = cy - lastPlaceY;
                if (std::abs(ddx) + std::abs(ddy) == 1) {
                    rotation = ddx > 0 ? 0 : ddx < 0 ? 2 : ddy > 0 ? 1 : 3;
                    int pb = game.buildIndexAt(lastPlaceX, lastPlaceY);
                    if (pb >= 0 && game.builds[pb].block == selectedBlock()) {
                        game.builds[pb].rotation = rotation;
                        game.refreshAround(pb);
                    }
                    for (auto& p : game.plans) if (p.tx == lastPlaceX && p.ty == lastPlaceY && !p.breaking) p.rotation = rotation;
                }
            }
            if (!game.queuePlan(cx, cy, (int16_t)selectedBlock(), rotation, false) && (in.down & KA)) {
                toast = lang.ui("invalid"); toastTime = 60;
            }
        }
        lastPlaceX = cx; lastPlaceY = cy;
    } else {
        lastPlaceX = lastPlaceY = -9999;
    }
    if (in.down & KB) game.queuePlan(cx, cy, 0, 0, true);
    if (in.down & KY && game.player >= 0) {
        Unit& u = game.units[game.player];
        int t = cy * game.width + cx;
        u.mineTile = (u.mineTile == t) ? -1 : t;
        if (u.mineTile >= 0 && !game.canMine(u, t)) u.mineTile = -1;
    }
    if (in.touchStart) touchUi(in.touchX, in.touchY);
}

// bottom screen layout
static const int kTabY = 58, kTabH = 26, kGridY = 88, kCellW = 45, kCellH = 38, kCols = 7;

void App::touchUi(int x, int y) {
    if (y < 20 && x > 236) {  // next wave
        if (game.wavesEnabled && !game.waves.empty()) { game.runWave(); game.wavetime = game.waveSpacing; }
        return;
    }
    auto cats = usedCategories();
    if (y >= kTabY && y < kTabY + kTabH && !cats.empty()) {
        int w = 320 / (int)cats.size();
        int i = std::min((int)cats.size() - 1, x / w);
        category = cats[i];
        mode = Mode::Build;
        return;
    }
    if (y >= kGridY && y < kGridY + kCellH * 2) {
        auto list = blocksIn(category);
        int i = ((y - kGridY) / kCellH) * kCols + x / kCellW;
        if (i < (int)list.size()) { selected[category] = i; mode = Mode::Build; }
        return;
    }
    if (y >= 212) {
        int i = x / 80;
        if (i == 0) mode = Mode::Build;
        if (i == 1) mode = Mode::Break;
        if (i == 2) mode = Mode::Mine;
        if (i == 3) rotation = (rotation + 1) % 4;
    }
}

// ---------------------------------------------------------------- drawing
static char buf[256];

static void worldToScreen(const App& a, float wx, float wy, float& sx, float& sy) {
    sx = (wx - a.camX) * kZoom + gfx::TopW / 2.f;
    sy = gfx::H / 2.f - (wy - a.camY) * kZoom;
}

static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static int variantSprite(const BlockDef& d, int x, int y) {
    if (d.variants > 0 && d.sprVar >= 0) return d.sprVar + (int)(hash2(x, y) % (uint32_t)d.variants);
    return d.spr;
}

static uint32_t withAlpha(uint32_t c, float a) { return (c & 0xffffff00u) | (uint32_t)(std::clamp(a, 0.f, 1.f) * 255); }

void App::draw() {
    if (screen == Screen::Menu) { drawMenu(); return; }
    drawWorld();
    drawHud();
    drawBottom();
}

void App::drawWorld() {
    Game& g = game;
    gfx::begin(gfx::Top, 0x000000ff);
    float halfW = gfx::TopW / 2.f / kZoom, halfH = gfx::H / 2.f / kZoom;
    int x0 = std::max(0, (int)std::floor((camX - halfW) / tilesize) - 1);
    int x1 = std::min(g.width - 1, (int)std::ceil((camX + halfW) / tilesize) + 1);
    int y0 = std::max(0, (int)std::floor((camY - halfH) / tilesize) - 1);
    int y1 = std::min(g.height - 1, (int)std::ceil((camY + halfH) / tilesize) + 1);
    float sx, sy;
    // floor + ore overlay
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Tile& t = g.tiles[y * g.width + x];
            worldToScreen(*this, x * tilesize, y * tilesize, sx, sy);
            int fs = variantSprite(kBlocks[t.floor], x, y);
            if (fs >= 0) gfx::sprite(fs, sx, sy);
            if (t.ore) { int os = variantSprite(kBlocks[t.ore], x, y); if (os >= 0) gfx::sprite(os, sx, sy); }
        }
    // static walls, props (shadows first)
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Tile& t = g.tiles[y * g.width + x];
            if (!t.env) continue;
            const BlockDef& d = kBlocks[t.env];
            worldToScreen(*this, x * tilesize, y * tilesize, sx, sy);
            if (d.sprExtra >= 0 && d.type == BType::StaticWall && gfx::spriteW(d.sprExtra) > 16) gfx::sprite(d.sprExtra, sx, sy, 0, 1, 1, 0x00000080, 1);
            int s = variantSprite(d, x, y);
            if (s >= 0) {
                float rot = (gfx::spriteW(s) > 16) ? (float)(hash2(x, y) % 4) * 90.f : 0.f;  // trees: Mathf.randomSeed rotation
                gfx::sprite(s, sx, sy, rot);
            }
        }
    // buildings
    for (int y = y0 - 2; y <= y1 + 2; y++)
        for (int x = x0 - 2; x <= x1 + 2; x++) {
            int bi = g.buildIndexAt(x, y);
            if (bi < 0) continue;
            Building& b = g.builds[bi];
            if (b.tx != x || b.ty != y) continue;
            const BlockDef& d = b.def();
            float off = d.size % 2 == 0 ? tilesize / 2 : 0;
            worldToScreen(*this, x * tilesize + off, y * tilesize + off, sx, sy);
            uint32_t tint = 0xffffffff; float blend = 0;
            if (b.constructing) { tint = 0xffd37f90; blend = 0.6f; }
            switch (d.type) {
                case BType::Conveyor: {
                    int frame = (!b.constructing && b.clogHeat <= 0.5f && b.len > 0) ? ((int)(g.tick * d.speed * 8.f)) % 4 : 0;
                    gfx::sprite(d.sprVar + b.blendbits * 4 + frame, sx, sy, b.rotation * 90.f, b.blendsclx, b.blendscly, tint, blend);
                    break;
                }
                case BType::Drill:
                    gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                    if (d.sprRot >= 0) gfx::sprite(d.sprRot, sx, sy, b.timeDrilled * 2.f, 1, 1, tint, blend);
                    if (d.sprTop >= 0) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, tint, blend);
                    if (b.dominantItem >= 0 && !b.constructing)
                        gfx::rect(sx - 2, sy - 2, 4, 4, (kItems[b.dominantItem].color << 8) | 0xff);
                    break;
                case BType::ItemTurret: {
                    gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                    float r = b.turretRotation * 0.0174533f, rec = -b.recoil * 1.f * kZoom;
                    gfx::sprite(d.sprTop, sx + std::cos(r) * rec, sy - std::sin(r) * rec, b.turretRotation - 90, 1, 1, tint, blend);
                    break;
                }
                case BType::Core:
                    gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                    if (d.sprTeam >= 0) gfx::sprite(d.sprTeam, sx, sy, 0, 1, 1, kTeamColor[b.team % 3], 1.f);
                    break;
                case BType::Wall: {
                    int s = d.variants > 0 ? d.sprVar + (int)(hash2(x, y) % (uint32_t)d.variants) : d.spr;
                    gfx::sprite(s, sx, sy, 0, 1, 1, tint, blend);
                    break;
                }
                case BType::Bridge:
                    gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                    break;
                default:
                    if (d.spr >= 0) gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                    if (d.sprTop >= 0 && !b.constructing) {
                        float a = d.type == BType::Battery ? (d.powerBuffer > 0 ? b.powerStored / d.powerBuffer : 0) : b.warmup;
                        if (a > 0.01f) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha(d.type == BType::Battery ? 0xfbd367ff : 0xffffffff, a));
                    }
                    break;
            }
            if (b.constructing) {
                float w = d.size * tilesize * kZoom;
                gfx::rect(sx - w / 2, sy + w / 2 - 2, w * b.buildProgress, 2, kAccent);
            } else if (b.health < d.health && b.team != TeamDerelict) {
                float w = d.size * tilesize * kZoom;
                gfx::rect(sx - w / 2, sy + w / 2 - 2, w, 2, 0x000000a0);
                gfx::rect(sx - w / 2, sy + w / 2 - 2, w * b.health / d.health, 2, b.team == TeamSharded ? kAccent : kRed);
            }
        }
    // items on conveyors (ConveyorBuild.draw) and bridges
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Building* b = g.buildAt(x, y);
            if (!b || b->def().type != BType::Conveyor || b->tx != x) continue;
            float r = b->rotation * 1.5707963f;
            float c = std::cos(r), s = std::sin(r);
            for (int i = 0; i < b->len; i++) {
                if (b->ids[i] < 0) continue;
                float v1x = c * tilesize, v1y = s * tilesize;
                // Tmp.v2.trns(rotation * 90, -tilesize / 2, xs[i] * tilesize / 2)
                float ax = -tilesize / 2, ay = b->xs[i] * tilesize / 2;
                float v2x = ax * c - ay * s, v2y = ax * s + ay * c;
                worldToScreen(*this, x * tilesize + v1x * b->ys[i] + v2x, y * tilesize + v1y * b->ys[i] + v2y, sx, sy);
                gfx::sprite(kItems[b->ids[i]].spr, sx, sy, 0, 0.5f, 0.5f);
            }
        }
    // bridges
    for (auto& b : g.builds) {
        if (!b.alive || b.def().type != BType::Bridge || b.link < 0) continue;
        float ax, ay, bx, by;
        worldToScreen(*this, b.tx * tilesize, b.ty * tilesize, ax, ay);
        worldToScreen(*this, (b.link % g.width) * tilesize, (b.link / g.width) * tilesize, bx, by);
        if ((ax < -40 && bx < -40) || (ax > 440 && bx > 440) || (ay < -40 && by < -40) || (ay > 280 && by > 280)) continue;
        gfx::line(ax, ay, bx, by, 6, 0x6e7080c0);
        if (b.def().sprTop >= 0) { gfx::sprite(b.def().sprTop, ax, ay); gfx::sprite(b.def().sprTop, bx, by); }
    }
    // power lasers (PowerNode.drawLaser)
    for (auto& b : g.builds) {
        if (!b.alive || b.links.empty()) continue;
        for (int32_t l : b.links) {
            if (l < b.id && g.builds[l].def().type == BType::PowerNode) continue;
            Building& o = g.builds[l];
            if (!o.alive) continue;
            float ox = o.def().size % 2 == 0 ? tilesize / 2 : 0;
            float ax, ay, bx, by;
            worldToScreen(*this, b.tx * tilesize, b.ty * tilesize, ax, ay);
            worldToScreen(*this, o.tx * tilesize + ox, o.ty * tilesize + ox, bx, by);
            float sat = b.powerStatus;
            gfx::line(ax, ay, bx, by, 2.5f, sat > 0.5f ? 0xfbd367c0 : 0xa0a0a0a0);  // Pal.powerLight
        }
    }
    // units
    for (size_t i = 0; i < g.units.size(); i++) {
        Unit& u = g.units[i];
        if (!u.alive) continue;
        const UnitDef& d = u.def();
        worldToScreen(*this, u.x, u.y, sx, sy);
        if (sx < -40 || sx > 440 || sy < -40 || sy > 280) continue;
        uint32_t flash = u.hitTime > 0 ? 0xffffffff : 0;
        if (d.flying) {
            // shadow (Drawf.shadow offset)
            gfx::sprite(d.spr, sx + 6, sy + 6, u.rotation - 90, 1, 1, 0x00000060, 1);
        } else {
            if (d.sprLeg >= 0) {
                float sw = std::sin(u.walkTime * 0.6f) * 3.f;
                float r = (u.rotation) * 0.0174533f;
                for (int side = -1; side <= 1; side += 2) {
                    float lx = sx + std::cos(r) * sw * side, ly = sy - std::sin(r) * sw * side;
                    gfx::sprite(d.sprLeg, lx, ly, u.rotation - 90, (float)side, 1);
                }
            }
            if (d.sprBase >= 0) gfx::sprite(d.sprBase, sx, sy, u.rotation - 90);
        }
        if (d.sprWeapon >= 0) {
            float r = (u.rotation - 90) * 0.0174533f;
            for (int side = -1; side <= 1; side += 2) {
                if (!d.weapon.mirror && side < 0) continue;
                float wx = d.weapon.x * side * kZoom, wy = d.weapon.y * kZoom;
                float ox = wx * std::cos(r) - wy * std::sin(r), oy = wx * std::sin(r) + wy * std::cos(r);
                gfx::sprite(d.sprWeapon, sx + ox, sy - oy, u.rotation - 90, (float)side, 1);
            }
        }
        gfx::sprite(d.spr, sx, sy, u.rotation - 90, 1, 1, flash ? flash : 0xffffffff, flash ? 0.5f : 0);
        if (d.sprCell >= 0) gfx::sprite(d.sprCell, sx, sy, u.rotation - 90, 1, 1, kTeamColor[u.team % 3], 1.f);
        if (u.mineTile >= 0) {
            float mx, my;
            worldToScreen(*this, (u.mineTile % g.width) * tilesize, (u.mineTile / g.width) * tilesize, mx, my);
            gfx::line(sx, sy, mx, my, 1.5f, 0xffd37fc0);
            gfx::rectLine(mx - 8, my - 8, 16, 16, 1, 0xffd37fff);
        }
        if (u.team == TeamCrux && u.health < d.health) {
            gfx::rect(sx - 8, sy - d.hitSize - 4, 16, 2, 0x000000a0);
            gfx::rect(sx - 8, sy - d.hitSize - 4, 16 * u.health / d.health, 2, kRed);
        }
    }
    // bullets
    for (auto& b : g.bullets) {
        if (!b.alive) continue;
        worldToScreen(*this, b.x, b.y, sx, sy);
        if (sx < -10 || sx > 410 || sy < -10 || sy > 250) continue;
        float rot = angleDeg(b.vx, b.vy) - 90;
        uint32_t back = b.team == TeamCrux ? 0xf25555ff : 0xf9c27aff;  // Pal.bulletYellowBack
        if (SPR_bullet_back >= 0) gfx::sprite(SPR_bullet_back, sx, sy, rot, 0.5f, 0.6f, back, 1);
        gfx::sprite(SPR_bullet, sx, sy, rot, 0.5f, 0.6f, 0xffffffff, 1);
    }
    // effects
    for (auto& e : g.effects) {
        worldToScreen(*this, e.x, e.y, sx, sy);
        float f = e.time / e.life;
        float r = e.size * kZoom * (e.kind ? (0.4f + f) : 1.f);
        uint32_t c = withAlpha((e.color << 8) | 0xff, 1 - f);
        if (e.kind == 0) gfx::rect(sx - r / 2, sy - r / 2, r, r, c);
        else gfx::rectLine(sx - r, sy - r, r * 2, r * 2, 2, c);
    }
    // build plans
    for (auto& p : g.plans) {
        const BlockDef& d = kBlocks[p.block];
        float off = d.size % 2 == 0 ? tilesize / 2 : 0;
        worldToScreen(*this, p.tx * tilesize + off, p.ty * tilesize + off, sx, sy);
        float w = d.size * tilesize * kZoom;
        if (p.breaking) { gfx::rectLine(sx - w / 2, sy - w / 2, w, w, 1.5f, kRed); continue; }
        int s = d.type == BType::Conveyor ? d.sprVar : d.type == BType::ItemTurret ? d.sprTop : d.variants ? d.sprVar : d.spr;
        if (s >= 0) gfx::sprite(s, sx, sy, d.rotate ? p.rotation * 90.f : 0, 1, 1, 0xffffff70, 0);
        gfx::rectLine(sx - w / 2, sy - w / 2, w, w, 1, 0xffd37f90);
    }
    // cursor
    if (screen == Screen::Game) {
        int px = (int)std::floor(camX / tilesize + 0.5f), py = (int)std::floor(camY / tilesize + 0.5f);
        int cx = px + cursorDX, cy = py + cursorDY;
        if (mode == Mode::Build) {
            int blk = selectedBlock();
            const BlockDef& d = kBlocks[blk];
            float off = d.size % 2 == 0 ? tilesize / 2 : 0;
            worldToScreen(*this, cx * tilesize + off, cy * tilesize + off, sx, sy);
            bool ok = g.validPlace((int16_t)blk, cx, cy, rotation, TeamSharded);
            float w = d.size * tilesize * kZoom;
            int s = d.type == BType::Conveyor ? d.sprVar : d.type == BType::ItemTurret ? d.sprTop : d.variants ? d.sprVar : d.spr;
            if (s >= 0) gfx::sprite(s, sx, sy, d.rotate ? rotation * 90.f : 0, 1, 1, ok ? 0xffffffa0 : 0xe55454a0, ok ? 0 : 0.5f);
            gfx::rectLine(sx - w / 2 - 1, sy - w / 2 - 1, w + 2, w + 2, 1.5f, ok ? kAccent : kRed);
            if (d.rotate) {
                float r = rotation * 1.5707963f;
                gfx::line(sx, sy, sx + std::cos(r) * w * 0.7f, sy - std::sin(r) * w * 0.7f, 2, kAccent);
            }
            if (d.type == BType::ItemTurret) {
                // turret range circle (approximated by a square outline)
                float rr = d.range * kZoom;
                for (int k = 0; k < 32; k++) {
                    float a0 = k / 32.f * 6.2831853f, a1 = (k + 1) / 32.f * 6.2831853f;
                    gfx::line(sx + std::cos(a0) * rr, sy + std::sin(a0) * rr, sx + std::cos(a1) * rr, sy + std::sin(a1) * rr, 1, 0xffd37f80);
                }
            }
        } else {
            worldToScreen(*this, cx * tilesize, cy * tilesize, sx, sy);
            gfx::rectLine(sx - 9, sy - 9, 18, 18, 1.5f, mode == Mode::Break ? kRed : 0x8ae3dfff);
        }
    }
}

static std::string fmtTime(float ticks) {
    int s = (int)(ticks / 60);
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

void App::drawHud() {
    Game& g = game;
    // wave panel, like the desktop HUD's top-left wave box
    gfx::rect(0, 0, 150, 30, 0x000000a0);
    if (g.winWave > 0) std::snprintf(buf, sizeof buf, "%s %d/%d", lang.ui("wave").c_str(), g.wave, g.winWave);
    else std::snprintf(buf, sizeof buf, "%s %d", lang.ui("wave").c_str(), g.wave);
    gfx::text(4, 2, 0.5f, kAccent, buf);
    int en = g.enemies();
    if (en > 0) std::snprintf(buf, sizeof buf, "%d %s", en, lang.ui("enemies").c_str());
    else std::snprintf(buf, sizeof buf, "%s %s", lang.ui("in").c_str(), fmtTime(g.wavetime).c_str());
    gfx::text(4, 16, 0.4f, 0xffffffff, buf);
    if (g.player >= 0) {
        Unit& u = g.units[g.player];
        if (u.stackAmount > 0) {
            gfx::sprite(kItems[u.stackItem].spr, 380, 12);
            std::snprintf(buf, sizeof buf, "%d", u.stackAmount);
            gfx::text(368, 4, 0.45f, 0xffffffff, buf, 2);
        }
    }
    if (screen == Screen::Pause) {
        gfx::rect(0, 0, 400, 240, 0x00000090);
        gfx::text(200, 100, 0.9f, kAccent, lang.ui("paused").c_str(), 1);
    }
    if (screen == Screen::End) {
        gfx::rect(0, 80, 400, 70, 0x000000c0);
        bool won = g.state == GameState::Won;
        gfx::text(200, 92, 0.8f, won ? kAccent : kRed, lang.ui(won ? "captured" : "lost").c_str(), 1);
        gfx::text(200, 126, 0.45f, 0xffffffff, lang.sectorName(g.sector).c_str(), 1);
    }
    if (toastTime > 0 && !toast.empty()) {
        float a = std::fmin(1.f, toastTime / 30.f);
        int lines = 1 + (int)std::count(toast.begin(), toast.end(), '\n');
        gfx::rect(0, 240 - 8 - lines * 14, 400, 8 + lines * 14, withAlpha(0x000000ff, 0.6f * a));
        gfx::text(200, 240 - 4 - lines * 14, 0.42f, withAlpha(0xffffffff, a), toast.c_str(), 1);
    }
}

void App::drawBottom() {
    Game& g = game;
    gfx::begin(gfx::Bottom, 0x1a1a1aff);
    if (screen == Screen::Pause) {
        const char* items[3] = {"resume", "savequit", "quit"};
        gfx::text(160, 24, 0.7f, kAccent, lang.ui("paused").c_str(), 1);
        for (int i = 0; i < 3; i++) {
            float y = 70 + i * 44;
            gfx::rect(30, y, 260, 38, i == pauseIndex ? kPanelLight : kPanel);
            if (i == pauseIndex) gfx::rectLine(30, y, 260, 38, 2, kAccent);
            gfx::text(160, y + 10, 0.55f, 0xffffffff, lang.ui(items[i]).c_str(), 1);
        }
        return;
    }
    if (screen == Screen::End) {
        gfx::text(160, 100, 0.55f, 0xffffffff, lang.ui("menu").c_str(), 1);
        gfx::text(160, 130, 0.45f, kGray, "A", 1);
        return;
    }
    // top bar: wave + next wave button (HudFragment "skip wave")
    gfx::rect(0, 0, 320, 20, kPanel);
    std::snprintf(buf, sizeof buf, "%s  %s %d", lang.sectorName(g.sector).c_str(), lang.ui("wave").c_str(), g.wave);
    gfx::text(4, 3, 0.42f, kAccent, buf);
    gfx::rect(238, 1, 80, 18, kPanelLight);
    gfx::text(278, 3, 0.4f, 0xffffffff, lang.ui("nextwave").c_str(), 1);

    // core items (CoreItemsDisplay)
    Building* c = g.core(TeamSharded);
    if (c) {
        int col = 0, row = 0;
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (c->items.amount[i] <= 0) continue;
            float x = 4 + col * 79, y = 24 + row * 16;
            gfx::sprite(kItems[i].spr, x + 7, y + 7, 0, 0.85f, 0.85f);
            std::snprintf(buf, sizeof buf, "%d", (int)c->items.amount[i]);
            gfx::text(x + 17, y, 0.4f, 0xffffffff, buf);
            if (++col == 4) { col = 0; if (++row == 2) break; }
        }
    }
    // category tabs
    auto cats = usedCategories();
    int tw = cats.empty() ? 320 : 320 / (int)cats.size();
    for (size_t i = 0; i < cats.size(); i++) {
        float x = i * tw;
        bool sel = cats[i] == category;
        gfx::rect(x + 1, kTabY, tw - 2, kTabH, sel ? kPanelLight : kPanel);
        if (sel) gfx::rect(x + 1, kTabY + kTabH - 2, tw - 2, 2, kAccent);
        auto list = blocksIn(cats[i]);
        if (!list.empty()) {
            const BlockDef& d = kBlocks[list[0]];
            int s = d.type == BType::Conveyor ? d.sprVar : d.type == BType::ItemTurret ? d.sprTop : d.variants ? d.sprVar : d.spr;
            if (s >= 0) { float sc = 18.f / std::max(gfx::spriteW(s), gfx::spriteH(s)); gfx::sprite(s, x + tw / 2.f, kTabY + kTabH / 2.f, 0, sc, sc); }
        }
    }
    // block grid
    auto list = blocksIn(category);
    int selIndex = std::clamp(selected[category], 0, std::max(0, (int)list.size() - 1));
    for (size_t i = 0; i < list.size() && i < 14; i++) {
        float x = (i % kCols) * kCellW + 2, y = kGridY + (i / kCols) * kCellH;
        const BlockDef& d = kBlocks[list[i]];
        bool afford = c != nullptr;
        for (int r = 0; r < d.nreq && c; r++) if (c->items.amount[d.req[r].item] < d.req[r].amount) afford = false;
        bool sel = (int)i == selIndex && mode == Mode::Build;
        gfx::rect(x, y, kCellW - 3, kCellH - 3, sel ? kPanelLight : kPanel);
        if (sel) gfx::rectLine(x, y, kCellW - 3, kCellH - 3, 2, kAccent);
        int s = d.type == BType::Conveyor ? d.sprVar : d.type == BType::ItemTurret ? d.sprTop : d.variants ? d.sprVar : d.spr;
        if (s >= 0) {
            float sc = 28.f / std::max(gfx::spriteW(s), gfx::spriteH(s));
            if (sc > 1.75f) sc = 1.75f;
            gfx::sprite(s, x + (kCellW - 3) / 2.f, y + (kCellH - 3) / 2.f, 0, sc, sc, afford ? 0xffffffff : 0x808080ff, afford ? 0 : 0.5f);
        }
    }
    // selected block info: localized name + requirements (have/need)
    if (!list.empty()) {
        int blk = list[selIndex];
        const BlockDef& d = kBlocks[blk];
        gfx::rect(0, 166, 320, 44, kPanel);
        gfx::text(4, 168, 0.45f, kAccent, lang.blockName(blk).c_str());
        gfx::text(316, 169, 0.38f, kGray, (lang.code == "fr" ? kCategoryFr : kCategoryEn)[category], 2);
        float x = 4;
        for (int r = 0; r < d.nreq; r++) {
            int it = d.req[r].item;
            bool ok = c && c->items.amount[it] >= d.req[r].amount;
            gfx::sprite(kItems[it].spr, x + 7, 196, 0, 0.8f, 0.8f);
            std::snprintf(buf, sizeof buf, "%d", d.req[r].amount);
            gfx::text(x + 16, 189, 0.42f, ok ? 0xffffffff : kRed, buf);
            x += 22 + gfx::textWidth(buf, 0.42f);
        }
        if (d.powerUse > 0) { std::snprintf(buf, sizeof buf, "-%.0f/s", d.powerUse * 60); gfx::text(316, 189, 0.4f, 0xfbd367ff, buf, 2); }
        if (d.powerProduction > 0) { std::snprintf(buf, sizeof buf, "+%.0f/s", d.powerProduction * 60); gfx::text(316, 189, 0.4f, 0xfbd367ff, buf, 2); }
    }
    // mode buttons
    const char* modes[4] = {"build", "break", "mine", "rotate"};
    for (int i = 0; i < 4; i++) {
        bool sel = i < 3 && (int)mode == i;
        gfx::rect(i * 80 + 1, 213, 78, 26, sel ? kPanelLight : kPanel);
        if (sel) gfx::rect(i * 80 + 1, 237, 78, 2, i == 1 ? kRed : kAccent);
        if (i == 3) {
            gfx::text(i * 80 + 32, 219, 0.45f, 0xffffffff, "X", 1);
            float cx = i * 80 + 56, cy = 226, r = rotation * 1.5707963f;
            gfx::line(cx, cy, cx + std::cos(r) * 9, cy - std::sin(r) * 9, 2.5f, kAccent);
        } else {
            gfx::text(i * 80 + 40, 219, 0.45f, 0xffffffff, lang.ui(modes[i]).c_str(), 1);
        }
    }
}

void App::drawMenu() {
    gfx::begin(gfx::Top, 0x1a1a1aff);
    // scrolling backdrop of upstream floor sprites
    static float t = 0; t += 0.25f;
    for (int y = -1; y < 16; y++)
        for (int x = -1; x < 26; x++) {
            int tx = x + (int)(t / 16), ty = y;
            const BlockDef& d = kBlocks[(hash2(tx / 6, ty / 5) % 3) == 0 ? B_snow : (hash2(tx / 7, ty / 4) % 2 ? B_basalt : B_ice)];
            int s = variantSprite(d, tx, ty);
            if (s >= 0) gfx::sprite(s, x * 16 - std::fmod(t, 16.f) + 8, y * 16 + 8, 0, 1, 1, 0x707070ff, 0.35f);
        }
    gfx::sprite(SPR_logo, 200, 70);
    gfx::text(200, 112, 0.45f, 0xffffffff, "Old 3DS / New 3DS  -  Mindustry v146 (Anuke)", 1);
    gfx::text(200, 214, 0.38f, kGray, "github.com/Anuken/Mindustry  -  GPLv3", 1);
    if (toastTime > 0) gfx::text(200, 190, 0.45f, kRed, toast.c_str(), 1);

    gfx::begin(gfx::Bottom, 0x1a1a1aff);
    gfx::text(160, 6, 0.55f, kAccent, lang.ui("sectors").c_str(), 1);
    for (int i = 0; i <= SECTOR_COUNT; i++) {
        float y = 34 + i * 34;
        bool sel = i == menuIndex;
        gfx::rect(8, y, 304, 30, sel ? kPanelLight : kPanel);
        if (sel) gfx::rectLine(8, y, 304, 30, 2, kAccent);
        if (i == SECTOR_COUNT) { gfx::text(160, y + 7, 0.5f, 0xffffffff, lang.ui("exit").c_str(), 1); continue; }
        bool open = i == 0 || captured[i - 1] || captured[i] || hasSave[i];
        gfx::text(16, y + 3, 0.5f, open ? 0xffffffff : kGray, lang.sectorName(i).c_str());
        if (captured[i]) std::snprintf(buf, sizeof buf, "%s", lang.ui("done").c_str());
        else std::snprintf(buf, sizeof buf, "%s %d %s", lang.ui("survive").c_str(), kSectors[i].captureWave, lang.ui("waves").c_str());
        gfx::text(16, y + 17, 0.36f, captured[i] ? kAccent : kGray, buf);
        if (hasSave[i]) {
            gfx::rect(250, y + 2, 60, 26, 0x3a3a3aff);
            gfx::text(280, y + 8, 0.4f, kAccent, lang.ui("continue").c_str(), 1);
        }
    }
}

}  // namespace mdt

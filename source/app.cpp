// Application: menus, controls, world rendering and the touch screen HUD.
#include "app.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "gfx.hpp"
#include "gfx_quad.hpp"
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
    {"drawcpu", "draw", "dessin"},
    {"prevloaded", "Save unreadable: previous copy loaded (file kept aside)", "Sauvegarde illisible : copie précédente chargée (fichier conservé)"},
    {"autosave", "X: autosave", "X : autosauvegarde"},
    {"off", "off", "désactivée"},
    {"saveunreadable", "Save unreadable: kept on the SD. X = new game (old file kept aside)",
                       "Sauvegarde illisible : conservée sur la SD. X = nouvelle partie (ancien fichier mis de côté)"},
    {"st.noliquid", "Missing", "Manque"},
    {"st.liquidfull", "Tank full", "Réservoir plein"},
    {"st.nofloor", "No liquid under the pump", "Aucun liquide sous la pompe"},
    {"pumping", "pumping", "pompage"},
    {"st.overheat", "Overheating", "Surchauffe"},
    {"heat", "heat", "chaleur"},
    {"reactoralert", "Reactor overheating! Feed cryofluid", "Réacteur en surchauffe ! Ajouter du cryofluide"},
    {"maxlinks", "Too many links (max 10)", "Trop de liens (10 max.)"},
    {"st.active", "Active", "Actif"},
    {"st.nofuel", "No fuel", "Sans combustible"},
    {"st.nopower", "No power", "Sans courant"},
    {"st.unpowered", "Not connected to power", "Non relié au réseau"},
    {"st.lowpower", "Insufficient power", "Courant insuffisant"},
    {"st.missing", "Missing", "Manque"},
    {"st.outputfull", "Output full", "Sortie pleine"},
    {"st.noore", "No ore under the drill", "Aucun minerai dessous"},
    {"st.noammo", "No ammo", "Sans munitions"},
    {"st.charging", "Charging", "En charge"},
    {"st.discharging", "Discharging", "En décharge"},
    {"st.full", "Full", "Pleine"},
    {"st.empty", "Empty", "Vide"},
    {"st.building", "Under construction", "En construction"},
    {"net", "Network", "Réseau"},
    {"stored", "stored", "stock"},
    {"cfg.node", "A: link/unlink  A on node: auto/clear  B: done", "A : lier/délier  A sur le nœud : auto/tout délier  B : fini"},
    {"cfg.bridge", "A on a bridge in line (4 tiles): link  B: done", "A sur un pont aligné (4 cases) : lier  B : fini"},
    {"cfg.sorter", "L/R: sorted item  B: done", "L/R : objet trié  B : fini"},
    {"help.power", "Power: generator -> node (laser) -> machine, or touching blocks.",
                   "Énergie : générateur -> nœud (laser) -> machine, ou blocs collés."},
    {"none", "none", "aucun"},
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
    std::vector<uint8_t> setting;
    if (readFile(dataPath("volume.bin"), setting) && setting.size() == 1 && setting[0] <= 100)
        musicVolume = setting[0];
    std::vector<uint8_t> as;
    if (readFile(dataPath("autosave.bin"), as) && as.size() == 1 && as[0] <= 60) autosaveMinutes = as[0];
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
    std::string path = dataPath(std::string("sector-") + kSectors[game.sector].name + ".sav");
    // the previous save stays recoverable as .prev
    std::vector<uint8_t> old;
    if (readFile(path, old)) {
        std::string prev = path + ".prev";
        std::remove(prev.c_str());
        std::rename(path.c_str(), prev.c_str());
    }
    if (writeFileAtomic(path, data)) {
        hasSave[game.sector] = true;
        toast = lang.ui("saved"); toastTime = 120;
    }
}

void App::startSector(int s, bool resume) {
    std::string savePath = dataPath(std::string("sector-") + kSectors[s].name + ".sav");
    std::vector<uint8_t> existing;
    bool hasFile = readFile(savePath, existing);
    bool ok = false;
    if (resume && hasFile) {
        ok = loadGame(game, existing);
        if (!ok) {
            // recoverable copy: load .prev, and set the unreadable file aside so nothing overwrites either
            std::vector<uint8_t> prev;
            if (readFile(savePath + ".prev", prev) && loadGame(game, prev)) {
                std::rename(savePath.c_str(), (savePath + ".unreadable-" + std::to_string(millis())).c_str());
                toast = lang.ui("prevloaded"); toastTime = 300;
                ok = true; hasFile = false;
            } else {
                // never fall back to a fresh map here: the next autosave would overwrite the file
                toast = lang.ui("saveunreadable"); toastTime = 300;
                return;
            }
        }
    }
    if (!ok) {
        if (hasFile) {
            // new game over an existing save: keep the old file aside instead of overwriting it
            Game probe;
            bool readable = loadGame(probe, existing);
            std::string aside = savePath + (readable ? ".prev" : ".unreadable-" + std::to_string(millis()));
            std::remove(aside.c_str());
            std::rename(savePath.c_str(), aside.c_str());
            hasSave[s] = false;
        }
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
    uint64_t t0 = micros();
    frameInner(in, delta);
    perf.simUs += micros() - t0;
    perf.frames++;
    uint64_t now = micros();
    if (perf.windowStart == 0) perf.windowStart = now;
    if (now - perf.windowStart >= 2000000) {
        float secs = (now - perf.windowStart) / 1e6f;
        perf.fps = perf.frames / secs;
        perf.simMs = perf.simUs / 1000.f / perf.frames;
        perf.drawMs = perf.drawUs / 1000.f / perf.frames;
        perf.windowStart = now; perf.frames = 0; perf.simUs = perf.drawUs = 0;
    }
}

void App::frameInner(const Input& in, float delta) {
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
    if ((in.down & (KL | KR)) || (in.touchStart && in.touchY >= 201 && in.touchY < 238)) {
        int step = (in.down & KL) || (in.touchStart && in.touchX < 160) ? -25 : 25;
        musicVolume = std::clamp(int(musicVolume) + step, 0, 100);
        writeFileAtomic(dataPath("volume.bin"), {static_cast<uint8_t>(musicVolume)});
        return;
    }
    if (in.down & KX) {  // autosave interval: 2, 5, 10 minutes or off
        autosaveMinutes = autosaveMinutes == 2 ? 5 : autosaveMinutes == 5 ? 10 : autosaveMinutes == 10 ? 0 : 2;
        writeFileAtomic(dataPath("autosave.bin"), {autosaveMinutes});
        return;
    }
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
    if (autosaveMinutes > 0 && autosaveTimer > 60.f * 60.f * autosaveMinutes) { autosaveTimer = 0; saveCurrent(); }

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

    cursorTX = cx; cursorTY = cy;
    auto cats = usedCategories();
    if (in.down & KSelect) {
        auto it = std::find(cats.begin(), cats.end(), category);
        int i = it == cats.end() ? 0 : (int)(it - cats.begin());
        category = cats[(i + 1) % cats.size()];
    }
    auto list = blocksIn(category);
    Building* conf = (configuring >= 0 && game.builds[configuring].alive) ? &game.builds[configuring] : nullptr;
    if (!conf) configuring = -1;
    if (conf && (conf->def().type == BType::Sorter || conf->def().type == BType::Unloader)) {
        // Sorter config: L/R cycle the sorted item (none, then every item)
        if (in.down & KR) conf->sortItem = (int16_t)(conf->sortItem + 1 >= ITEM_COUNT ? -1 : conf->sortItem + 1);
        if (in.down & KL) conf->sortItem = (int16_t)(conf->sortItem <= -1 ? ITEM_COUNT - 1 : conf->sortItem - 1);
    } else if (!list.empty()) {
        if (in.down & KR) selected[category] = (selected[category] + 1) % (int)list.size();
        if (in.down & KL) selected[category] = (selected[category] + (int)list.size() - 1) % (int)list.size();
    }
    if (in.down & KX) rotation = (rotation + 1) % 4;

    Building* under = game.buildAt(cx, cy);
    bool configurable = under && under->team == TeamSharded && !under->constructing &&
                        (under->def().type == BType::PowerNode || under->def().type == BType::Bridge || under->def().type == BType::Sorter ||
                         under->def().type == BType::Unloader || under->def().type == BType::LiquidBridge);
    // A: configure / place (held: drag lines), B: cancel or remove, Y: mine
    if ((in.down & KA) && mode == Mode::Build && (conf || configurable)) {
        configureTap(under);
        lastPlaceX = lastPlaceY = -9999;
    } else if ((in.held & KA) && !conf) {
        if (mode == Mode::Mine) {
            if (in.down & KA && game.player >= 0) game.units[game.player].mineTile = cy * game.width + cx;
        } else if (mode == Mode::Break) {
            game.queuePlan(cx, cy, 0, 0, true);
        } else if (in.down & KA) {
            if (!game.queuePlan(cx, cy, (int16_t)selectedBlock(), rotation, false)) { toast = lang.ui("invalid"); toastTime = 60; }
            lastPlaceX = cx; lastPlaceY = cy;
        } else if (lastPlaceX != -9999 && (cx != lastPlaceX || cy != lastPlaceY)) {
            // a diagonal cursor step is split into two single-axis steps (no hole in the line)
            if (cx != lastPlaceX && cy != lastPlaceY) dragStep(cx, lastPlaceY);
            dragStep(cx, cy);
        }
    } else {
        lastPlaceX = lastPlaceY = -9999;
    }
    if (in.down & KB) {
        if (conf) configuring = -1;
        else game.queuePlan(cx, cy, 0, 0, true);
    }
    if (in.down & KY && game.player >= 0) {
        Unit& u = game.units[game.player];
        int t = cy * game.width + cx;
        u.mineTile = (u.mineTile == t) ? -1 : t;
        if (u.mineTile >= 0 && !game.canMine(u, t)) u.mineTile = -1;
    }
    if (in.touchStart) touchUi(in.touchX, in.touchY);
}

// One step of a held-A line. The previous belt is turned toward the new cell only if the new
// cell could be placed (an obstacle leaves the line as it was, nothing is charged).
void App::dragStep(int x, int y) {
    int blk = selectedBlock();
    int ddx = x - lastPlaceX, ddy = y - lastPlaceY;
    uint8_t dir = rotation;
    if (kBlocks[blk].rotate && std::abs(ddx) + std::abs(ddy) == 1) dir = ddx > 0 ? 0 : ddx < 0 ? 2 : ddy > 0 ? 1 : 3;
    if (!game.queuePlan(x, y, (int16_t)blk, dir, false)) return;  // stop: lastPlace stays on the last good cell
    if (kBlocks[blk].rotate && std::abs(ddx) + std::abs(ddy) == 1) {
        rotation = dir;
        Building* prev = game.buildAt(lastPlaceX, lastPlaceY);
        if (prev && prev->block == blk && prev->tx == lastPlaceX && prev->ty == lastPlaceY && prev->rotation != dir) {
            prev->rotation = dir;  // quick rotate: free, items kept
            game.refreshAround(prev->id);
        }
        for (auto& p : game.plans) if (p.tx == lastPlaceX && p.ty == lastPlaceY && !p.breaking) p.rotation = dir;
    }
    lastPlaceX = x; lastPlaceY = y;
}

// Block configuration taps (PowerNode.onConfigureBuildTapped, ItemBridge, Sorter)
void App::configureTap(Building* target) {
    Building* conf = configuring >= 0 ? &game.builds[configuring] : nullptr;
    if (!conf) {
        if (target) configuring = target->id;
        return;
    }
    if (!target) { configuring = -1; return; }
    if (conf->def().type == BType::PowerNode) {
        if (target == conf) { game.autoOrClearLinks(conf->id); configuring = -1; return; }  // double tap
        if (game.powerLinkValid(*conf, *target, false)) {
            if (!game.togglePowerLink(conf->id, target->id)) { toast = lang.ui("maxlinks"); toastTime = 90; }
            return;
        }
    } else if (conf->def().type == BType::Bridge || conf->def().type == BType::LiquidBridge) {
        if (target != conf && target->block == conf->block) {
            if (!game.toggleBridgeLink(conf->id, target->id)) { toast = lang.ui("invalid"); toastTime = 60; }
            else configuring = -1;
            return;
        }
    }
    // tapping anything else selects it (if configurable) or closes the configuration
    bool configurable = target->team == TeamSharded && (target->def().type == BType::PowerNode ||
                        target->def().type == BType::Bridge || target->def().type == BType::Sorter ||
                        target->def().type == BType::Unloader || target->def().type == BType::LiquidBridge);
    configuring = (configurable && target != conf) ? target->id : -1;
}

// bottom screen layout
struct PalettePage {
    static constexpr int capacity = 14;
    int page, pages;
    PalettePage(int count, int selection)
        : page(std::clamp(selection, 0, std::max(0, count - 1)) / capacity),
          pages(std::max(1, (count + capacity - 1) / capacity)) {}
    int first() const { return page * capacity; }
    int turn(int delta) const { return ((page + pages + delta) % pages) * capacity; }
    int margin() const { return pages > 1 ? 40 : 0; }
};

static const int kTabY = 56, kTabH = 26, kGridY = 84, kCellW = 45, kCellH = 40, kCols = 7, kInfoY = 166;

void App::touchUi(int x, int y) {
    if (y < 20 && x > 236) {  // next wave
        if (game.wavesEnabled && !game.waves.empty()) { game.runWave(); game.wavetime = game.waveSpacing; }
        return;
    }
    auto cats = usedCategories();
    if (y >= kTabY && y < kTabY + kTabH && !cats.empty()) {
        PalettePage page(blocksIn(category).size(), selected[category]);
        int margin = page.margin();
        if (x < margin || x >= 320 - margin) {
            selected[category] = page.turn(x < margin ? -1 : 1);
            configuring = -1;
            mode = Mode::Build;
            return;
        }
        int w = (320 - 2 * margin) / (int)cats.size();
        int i = std::min((int)cats.size() - 1, (x - margin) / w);
        category = cats[i];
        mode = Mode::Build;
        return;
    }
    if (configuring >= 0 && game.builds[configuring].alive &&
        (game.builds[configuring].def().type == BType::Sorter || game.builds[configuring].def().type == BType::Unloader) &&
        y >= kGridY && y < kGridY + kCellH * 2) {
        // Sorter / Unloader configuration: tap an item, tap it again to clear (ItemSelection)
        int i = ((y - kGridY) / (kCellH)) * 8 + x / 40;
        if (i >= 0 && i < ITEM_COUNT) {
            Building& b = game.builds[configuring];
            b.sortItem = (int16_t)(b.sortItem == i ? -1 : i);
        }
        return;
    }
    if (y >= kGridY && y < kGridY + kCellH * 2) {
        auto list = blocksIn(category);
        if (x < 0 || x >= kCols * kCellW) return;
        PalettePage page(list.size(), selected[category]);
        int i = page.first() + ((y - kGridY) / kCellH) * kCols + x / kCellW;
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

// The camera is snapped to whole screen pixels (0.5 world unit at 16 px per tile) so every
// layer shares the same sub-pixel phase while the camera glides.
static void worldToScreen(const App& a, float wx, float wy, float& sx, float& sy) {
    float cx = gfx::snapEnabled ? std::floor(a.camX * kZoom + 0.5f) / kZoom : a.camX;
    float cy = gfx::snapEnabled ? std::floor(a.camY * kZoom + 0.5f) / kZoom : a.camY;
    sx = (wx - cx) * kZoom + gfx::TopW / 2.f;
    sy = gfx::H / 2.f - (wy - cy) * kZoom;
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

// Conveyor.draw: frame = enabled && clogHeat <= 0.5 ? (int)(Time.time * speed * 8 * efficiency) % 4 : 0
// (no item needed: an empty belt keeps scrolling)
int App::conveyorFrame(const Building& b) const { return mdt::conveyorFrame(b, game.tick); }

int App::envMargin() const {
    static int margin = -1;
    if (margin < 0) {
        int m = 16;
        for (int k = 0; k < BLOCK_COUNT; k++) {
            const BlockDef& d = kBlocks[k];
            if (d.type != BType::StaticWall && d.type != BType::Prop) continue;
            for (int v = 0; v < std::max(1, d.variants); v++) {
                int s = d.variants ? d.sprVar + v : d.spr;
                m = std::max(m, std::max(gfx::spriteW(s), gfx::spriteH(s)));
            }
        }
        margin = (m / 16 + 1) / 2 + 1;  // tiles a sprite can overhang its own tile, plus one
    }
    return margin;
}

// Draws a block the way its upstream draw() layers it. `b` is null for ghosts (plans, cursor).
void App::drawBlock(int block, float sx, float sy, int rot, const Building* b, uint32_t tint, float blend, int tx, int ty) {
    const BlockDef& d = kBlocks[block];
    if (d.type == BType::Reactor) {
        // NuclearReactor.draw: region, top tinted with the coolant (alpha = fill), lights (alpha = heat, red)
        gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
        if (b && b->liquid >= 0 && d.sprTop >= 0 && b->liquidAmount > 0.01f)
            gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha((kLiquids[b->liquid].color << 8) | 0xff, b->liquidAmount / d.liquidCapacity), 1.f);
        if (b && d.sprExtra >= 0 && b->heat > 0.01f) {
            float a = b->heat > 0.46f ? (0.6f + 0.4f * std::sin(game.tick * 0.3f)) * b->heat : b->heat;  // flashThreshold
            gfx::sprite(d.sprExtra, sx, sy, 0, 1, 1, withAlpha(0xff1919ff, a), 1.f);
        }
        return;
    }
    if (d.nlayers > 0) {
        // DrawMulti layers in upstream order
        int tiles = 0;
        for (int i = 0; i < d.nlayers; i++) {
            const auto& L = d.layers[i];
            float t = b ? b->totalProgress : 0;
            switch (L.kind) {
                case 0: gfx::sprite(L.spr, sx, sy, 0, 1, 1, tint, blend); break;
                case 1: gfx::sprite(L.spr, sx, sy, t * 2.f, 1, 1, tint, blend); break;           // DrawRegion rotateSpeed 2
                case 4: gfx::sprite(L.spr, sx, sy, 45.f - t * 2.f, 1, 1, tint, blend); break;    // rotateSpeed -2, rotation 45
                case 2: if (b && b->warmup > 0.01f) gfx::sprite(L.spr, sx, sy, 0, 1, 1, withAlpha(0xffffffff, b->warmup)); break;
                case 3: {  // DrawLiquidRegion: produced liquid if any, else the consumed one
                    if (!b) break;
                    int lq = b->outLiquid >= 0 ? b->outLiquid : b->liquid;
                    float amt = b->outLiquid >= 0 ? b->outAmount : b->liquidAmount;
                    if (lq >= 0 && amt > 0.001f) gfx::sprite(L.spr, sx, sy, 0, 1, 1, withAlpha((kLiquids[lq].color << 8) | 0xff, amt / d.liquidCapacity), 1.f);
                    break;
                }
                case 5: {  // DrawLiquidTile: first = consumed liquid, second = produced liquid
                    if (!b) { tiles++; break; }
                    int lq = tiles == 0 ? b->liquid : b->outLiquid;
                    float amt = tiles == 0 ? b->liquidAmount : b->outAmount;
                    tiles++;
                    if (lq >= 0 && amt > 0.001f) {
                        float w = d.size * tilesize * kZoom - 4;
                        gfx::rect(sx - w / 2, sy - w / 2, w, w, withAlpha((kLiquids[lq].color << 8) | 0xff, amt / d.liquidCapacity));
                    }
                    break;
                }
            }
        }
        return;
    }
    switch (d.type) {
        case BType::Conveyor: {
            int frame = b ? conveyorFrame(*b) : 0;
            uint8_t bits = b ? b->blendbits : 0, joints = 0;
            int8_t scx = b ? b->blendsclx : 1, scy = b ? b->blendscly : 1;
            if (!b && tx >= 0)  // placement preview: Autotiler.getTiling with queued plans as neighbours
                game.conveyorTiling(tx, ty, rot, TeamSharded, true, bits, scx, scy, joints);
            gfx::sprite(d.sprVar + bits * 4 + frame, sx, sy, rot * 90.f, scx, scy, tint, blend);
            break;
        }
        case BType::Drill:  // Drill.draw: region, rotator (timeDrilled * rotateSpeed), top
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            gfx::sprite(d.sprRot, sx, sy, b ? b->timeDrilled * 2.f : 0, 1, 1, tint, blend);
            gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, tint, blend);
            if (b && b->dominantItem >= 0 && !b->constructing)
                gfx::sprite(kItems[b->dominantItem].spr, sx, sy, 0, 0.5f, 0.5f);  // drawMineItem-like ore marker
            break;
        case BType::ItemTurret: {  // DrawTurret: base, then the outlined turret (region + parts)
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            float tr = b ? b->turretRotation : 90.f;
            float r = tr * 0.0174533f, rec = b ? -b->recoil * kZoom : 0;
            gfx::sprite(d.sprTop, sx + std::cos(r) * rec, sy - std::sin(r) * rec, tr - 90, 1, 1, tint, blend);
            break;
        }
        case BType::Core:  // region + team region multiplied by the team colour
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            gfx::sprite(d.sprTeam + (b ? std::min<int>(b->team, 2) : 1), sx, sy, 0, 1, 1, tint, blend);
            break;
        case BType::Pump: {  // DrawDefault + DrawPumpLiquid (liquid region tinted, alpha = fill)
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            if (b && b->liquid >= 0 && b->liquidAmount > 0.001f)
                gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha((kLiquids[b->liquid].color << 8) | 0xff, b->liquidAmount / d.liquidCapacity), 1.f);
            break;
        }
        case BType::Conduit: {  // Conduit.drawAt: bottom (botColor), liquid (smoothLiquid), top
            int bits = b ? b->blendbits : 0;
            float scx = b ? b->blendsclx : 1, scy = b ? b->blendscly : 1;
            gfx::sprite(d.sprVar + bits, sx, sy, rot * 90.f, scx, scy, tint, blend);
            if (b && b->liquid >= 0 && b->smoothLiquid > 0.01f) {
                float h = tilesize * kZoom;  // fluid frame under the top region's window
                gfx::rect(sx - h / 2, sy - h / 2, h, h, withAlpha((kLiquids[b->liquid].color << 8) | 0xff, b->smoothLiquid));
            }
            gfx::sprite(d.sprVar + 5 + bits, sx, sy, rot * 90.f, scx, scy, tint, blend);
            break;
        }
        case BType::LiquidRouter: {  // LiquidRouter.draw: bottom, drawTiledFrames(alpha = fill), region
            gfx::sprite(d.sprVar, sx, sy, 0, 1, 1, tint, blend);
            if (b && b->liquid >= 0 && b->liquidAmount > 0.001f) {
                float w = d.size * tilesize * kZoom;
                gfx::rect(sx - w / 2, sy - w / 2, w, w, withAlpha((kLiquids[b->liquid].color << 8) | 0xff, b->liquidAmount / d.liquidCapacity));
            }
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            break;
        }
        case BType::ConsumeGen:
            if (d.sprRot >= 0) {  // steam generator DrawMulti
                gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
                if (b && d.sprTop >= 0 && b->warmup > 0.01f) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha(0xffffffff, b->warmup));
                float tt = b ? b->totalProgress : 0;
                gfx::sprite(d.sprRot, sx, sy, tt * 2.f, 1, 1, tint, blend);          // DrawRegion("-turbine"){rotateSpeed = 2}
                gfx::sprite(d.sprRot, sx, sy, 45.f - tt * 2.f, 1, 1, tint, blend);   // {rotateSpeed = -2, rotation = 45}
                if (d.sprExtra >= 0) gfx::sprite(d.sprExtra, sx, sy, 0, 1, 1, tint, blend);  // -cap
                if (b && d.sprVar >= 0 && b->liquid >= 0 && b->liquidAmount > 0.001f)        // DrawLiquidRegion
                    gfx::sprite(d.sprVar, sx, sy, 0, 1, 1, withAlpha((kLiquids[b->liquid].color << 8) | 0xff, b->liquidAmount / d.liquidCapacity), 1.f);
                break;
            }
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            if (b && d.sprTop >= 0 && !b->constructing && b->warmup > 0.01f) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha(0xffffffff, b->warmup));
            break;
        case BType::Wall: {
            int s = d.variants > 0 ? d.sprVar + (b ? (int)(hash2(b->tx, b->ty) % (uint32_t)d.variants) : 0) : d.spr;
            gfx::sprite(s, sx, sy, 0, 1, 1, tint, blend);
            break;
        }
        default:
            gfx::sprite(d.spr, sx, sy, 0, 1, 1, tint, blend);
            if (d.type == BType::Crafter) {
                // DrawRegion("-rotator"){spinSprite, rotateSpeed = 2}: totalProgress * rotateSpeed
                if (d.sprRot >= 0) gfx::sprite(d.sprRot, sx, sy, b ? b->totalProgress * 2.f : 0, 1, 1, tint, blend);
                if (d.sprExtra >= 0) gfx::sprite(d.sprExtra, sx, sy, 0, 1, 1, tint, blend);
            }
            if (d.type == BType::Diode) {  // PowerDiode.draw: region, arrow along the rotation
                if (d.sprTop >= 0) gfx::sprite(d.sprTop, sx, sy, rot * 90.f, 1, 1, tint, blend);
                break;
            }
            if (d.type == BType::Unloader) {  // Unloader.draw: center tinted with the sort item colour
                if (b && b->sortItem >= 0) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, (kItems[b->sortItem].color << 8) | 0xff, 1.f);
                break;
            }
            if (d.sprTop >= 0 && d.type != BType::Bridge && d.type != BType::LiquidBridge) {
                if (!b) {  // ghost: same layers as the icon (Battery: DrawRegion("-top"))
                    if (d.type == BType::Battery) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, tint, blend);
                } else if (!b->constructing) {
                    float a = d.type == BType::Battery ? (d.powerBuffer > 0 ? b->powerStored / d.powerBuffer : 0) : b->warmup;
                    if (a > 0.01f) gfx::sprite(d.sprTop, sx, sy, 0, 1, 1, withAlpha(d.type == BType::Battery ? 0xfbd367ff : 0xffffffff, a));
                }
            }
            break;
    }
}

static std::string fmtRate(float perTick) {
    char b[32];
    float v = perTick * 60.f;
    if (std::fabs(v) >= 100) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

void App::drawInspect(const Building& b) {
    const BlockDef& d = b.def();
    StatusInfo st = game.status(b);
    gfx::rect(0, 166, 320, 45, kPanel);
    if (d.sprIcon >= 0) gfx::sprite(d.sprIcon, 21, 188);
    gfx::text(42, 168, 0.43f, kAccent, lang.blockName(b.block).c_str());
    // BlockStatus colours: active 5ce677, noOutput orange, noInput Pal.remove
    uint32_t led = st.status == Status::Active ? 0x5ce677ff : st.status == Status::NoOutput ? 0xffa500ff
                 : st.status == Status::Constructing ? kAccent : kRed;
    const char* key = nullptr;
    switch (st.reason) {
        case Reason::NoFuel: key = "st.nofuel"; break;
        case Reason::NoPower: key = "st.nopower"; break;
        case Reason::Unpowered: key = "st.unpowered"; break;
        case Reason::LowPower: key = "st.lowpower"; break;
        case Reason::MissingItem: key = "st.missing"; break;
        case Reason::OutputFull: key = "st.outputfull"; break;
        case Reason::NoOre: key = "st.noore"; break;
        case Reason::NoAmmo: key = "st.noammo"; break;
        case Reason::Charging: key = "st.charging"; break;
        case Reason::Discharging: key = "st.discharging"; break;
        case Reason::BatteryFull: key = "st.full"; break;
        case Reason::BatteryEmpty: key = "st.empty"; break;
        case Reason::MissingLiquid: key = "st.noliquid"; break;
        case Reason::LiquidFull: key = "st.liquidfull"; break;
        case Reason::NoLiquidFloor: key = "st.nofloor"; break;
        case Reason::Overheat: key = "st.overheat"; break;
        default: key = st.status == Status::Constructing ? "st.building" : "st.active"; break;
    }
    std::string label = lang.ui(key);
    if (st.reason == Reason::MissingItem && st.item >= 0) label += " : " + std::to_string((int)st.value) + " " + lang.itemName(st.item);
    if (st.reason == Reason::LowPower || st.status == Status::Constructing) label += " " + std::to_string((int)(st.value * 100)) + "%";
    if (st.reason == Reason::Overheat) label += " " + std::to_string((int)(st.value * 100)) + "%";
    if (st.reason == Reason::MissingLiquid && st.item >= 0)
        label += " " + lang.get(std::string("liquid.") + kLiquids[st.item].name + ".name", kLiquids[st.item].name) + " (" + std::to_string((int)(st.value * 100)) + "%)";
    gfx::rect(42, 186, 8, 8, led);
    gfx::text(54, 182, 0.4f, 0xffffffff, label.c_str());
    // detail line: contents, recipe or network balance
    std::string detail;
    const GraphStats* gs = (b.graph >= 0 && b.graph < (int)game.graphStats.size()) ? &game.graphStats[b.graph] : nullptr;
    if (configuring == b.id) {
        detail = lang.ui(d.type == BType::PowerNode ? "cfg.node" : (d.type == BType::Bridge || d.type == BType::LiquidBridge) ? "cfg.bridge" : "cfg.sorter");
        if (d.type == BType::Sorter || d.type == BType::Unloader)
            detail = (b.sortItem >= 0 ? lang.itemName(b.sortItem) : lang.ui("none")) + "   " + detail;
    } else if (gs && (d.powerUse > 0 || d.powerProduction > 0 || d.powerBuffer > 0 || d.type == BType::PowerNode)) {
        detail = lang.ui("net") + "  +" + fmtRate(gs->produced) + "  -" + fmtRate(gs->needed) + "/s";
        if (gs->capacity > 0) detail += "  " + lang.ui("stored") + " " + std::to_string((int)gs->stored) + "/" + std::to_string((int)gs->capacity);
    } else if (d.powerUse > 0) {
        detail = lang.ui("help.power");
    } else if (d.type == BType::Drill && b.dominantItem >= 0) {
        // Drill: 60 / (drillTime + 50 * hardness) * ore count items per second at full speed
        float rate = 60.f / (d.drillTime + d.hardnessMul * kItems[b.dominantItem].hardness) * b.dominantItems;
        char t[48]; std::snprintf(t, sizeof t, "%.2f/s ", rate);
        detail = std::string(t) + lang.itemName(b.dominantItem);
    } else if (d.type == BType::ItemTurret) {
        detail = lang.ui("ammo") + " " + std::to_string(b.totalAmmo) + "/" + std::to_string(d.maxAmmo);
    }
    if (d.type == BType::Crafter && st.reason != Reason::MissingItem && configuring != b.id && !gs) {
        detail.clear();
        for (int i = 0; i < d.nconsumes; i++) detail += std::to_string(d.consumes[i].amount) + " " + lang.itemName(d.consumes[i].item) + (i + 1 < d.nconsumes ? " + " : "");
        if (d.output.item >= 0) detail += " -> " + lang.itemName(d.output.item);
    }
    if (d.hasLiquids && configuring != b.id) {
        // liquid line: name, amount / capacity, pump rate (Pump stat: 60 * pumpAmount * tiles per second)
        char t[96];
        std::string lname = b.liquid >= 0 ? lang.get(std::string("liquid.") + kLiquids[b.liquid].name + ".name", kLiquids[b.liquid].name) : lang.ui("none");
        std::snprintf(t, sizeof t, "%s %.1f/%.0f", lname.c_str(), b.liquidAmount, d.liquidCapacity);
        std::string line = t;
        if (d.type == BType::Pump && b.pumpDrop >= 0) {
            std::snprintf(t, sizeof t, "  %s %.1f/s", lang.ui("pumping").c_str(), b.pumpTiles * d.pumpAmount * 60.f);
            line += t;
        }
        if (d.type == BType::ConsumeGen || d.type == BType::Reactor) {
            std::snprintf(t, sizeof t, "  +%.0f/s", d.powerProduction * b.productionEfficiency * 60.f);
            line += t;
        }
        if (d.type == BType::Reactor) {
            std::snprintf(t, sizeof t, "  %s %d%%", lang.ui("heat").c_str(), (int)(b.heat * 100));
            line += t;
        }
        if (d.outputLiquid >= 0) {  // produced liquid slot
            std::string on = lang.get(std::string("liquid.") + kLiquids[d.outputLiquid].name + ".name", kLiquids[d.outputLiquid].name);
            std::snprintf(t, sizeof t, "  -> %s %.1f", on.c_str(), b.outLiquid >= 0 ? b.outAmount : 0.f);
            line += t;
        }
        detail = detail.empty() ? line : line + "   " + detail;
    }
    if (!detail.empty()) gfx::text(42, 196, 0.36f, kGray, detail.c_str());
    // contents (items held), right aligned, 16 px icons
    float x = 316;
    for (int i = ITEM_COUNT - 1; i >= 0 && d.type != BType::Core && d.type != BType::Conveyor; i--) {
        if (b.items.amount[i] <= 0) continue;
        std::snprintf(buf, sizeof buf, "%d", (int)b.items.amount[i]);
        float w = gfx::textWidth(buf, 0.38f);
        gfx::text(x, 168, 0.38f, 0xffffffff, buf, 2);
        gfx::sprite(kItems[i].spr, x - w - 9, 175);
        x -= w + 22;
        if (x < 200) break;
    }
}

void App::draw() {
    uint64_t t0 = micros();
    drawInner();
    perf.drawUs += micros() - t0;
}

void App::drawInner() {
    if (screen == Screen::Menu) { drawMenu(); return; }
    drawWorld();
    if (!hideHud) drawHud();
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
    // static walls, props: every shadow first, then the bodies (Layer order of
    // StaticWall/TreeBlock), with a margin covering sprites larger than one tile
    int em = envMargin();
    int ex0 = std::max(0, x0 - em), ex1 = std::min(g.width - 1, x1 + em);
    int ey0 = std::max(0, y0 - em), ey1 = std::min(g.height - 1, y1 + em);
    for (int pass = 0; pass < 2; pass++)
        for (int y = ey0; y <= ey1; y++)
            for (int x = ex0; x <= ex1; x++) {
                Tile& t = g.tiles[y * g.width + x];
                if (!t.env) continue;
                const BlockDef& d = kBlocks[t.env];
                worldToScreen(*this, x * tilesize, y * tilesize, sx, sy);
                if (pass == 0) {
                    if (d.sprExtra >= 0 && d.type == BType::StaticWall && gfx::spriteW(d.sprExtra) > 16)
                        gfx::sprite(d.sprExtra, sx, sy, 0, 1, 1, 0x00000080, 1);
                    continue;
                }
                int s = variantSprite(d, x, y);
                if (s >= 0) {
                    float rot = (gfx::spriteW(s) > 16) ? (float)(hash2(x, y) % 4) * 90.f : 0.f;  // trees: Mathf.randomSeed rotation
                    gfx::sprite(s, sx, sy, rot);
                }
            }
    // conveyor joints under the blocks (Conveyor.draw at Layer.blockUnder)
    for (int y = y0 - 1; y <= y1 + 1; y++)
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            Building* b = g.buildAt(x, y);
            if (!b || b->def().type != BType::Conveyor || b->tx != x || b->ty != y || !b->blending || b->constructing) continue;
            const BlockDef& d = b->def();
            int frame = conveyorFrame(*b);
            for (int i = 0; i < 4; i++) {
                if (!(b->blending & (1 << i))) continue;
                int dir = ((b->rotation - i) % 4 + 4) % 4;
                float rot = (i == 0 ? b->rotation : dir) * 90.f;
                static const int dx4[4] = {1, 0, -1, 0}, dy4[4] = {0, 1, 0, -1};
                worldToScreen(*this, x * tilesize + dx4[dir] * tilesize * 0.75f, y * tilesize + dy4[dir] * tilesize * 0.75f, sx, sy);
                gfx::sprite(d.sprExtra + frame * 2 + (i != 0 ? 1 : 0), sx, sy, rot);
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
            drawBlock(b.block, sx, sy, b.rotation, &b, tint, blend);
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
        if (!b.alive || (b.def().type != BType::Bridge && b.def().type != BType::LiquidBridge) || b.link < 0) continue;
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
        if (sx < -w || sx > gfx::TopW + w || sy < -w || sy > gfx::H + w) continue;  // off-screen plan
        if (p.breaking) { gfx::rectLine(sx - w / 2, sy - w / 2, w, w, 1.5f, kRed); continue; }
        // BuildPlan preview: the block as it will look (conveyor curves included), lightly
        // accent-tinted like Mindustry's plan shader; outline only for non-belt blocks
        drawBlock(p.block, sx, sy, d.rotate ? p.rotation : 0, nullptr, 0xffd37fd0, 0.25f, p.tx, p.ty);
        if (d.type != BType::Conveyor) gfx::rectLine(sx - w / 2, sy - w / 2, w, w, 1, 0xffd37f90);
    }
    // configuration / hover overlays: laser range, links, candidates (PowerNode.drawConfigure)
    if (screen == Screen::Game && !hideHud) {
        auto centre = [&](const Building& o, float& ox, float& oy) {
            float off = o.def().size % 2 == 0 ? tilesize / 2 : 0;
            worldToScreen(*this, o.tx * tilesize + off, o.ty * tilesize + off, ox, oy);
        };
        auto box = [&](const Building& o, uint32_t c) {
            float ox, oy; centre(o, ox, oy);
            float w = o.def().size * tilesize * kZoom;
            gfx::rectLine(ox - w / 2 - 1, oy - w / 2 - 1, w + 2, w + 2, 1.5f, c);
        };
        auto circle = [&](float x, float y, float r, uint32_t c) {
            for (int k = 0; k < 40; k++) {
                float a0 = k / 40.f * 6.2831853f, a1 = (k + 1) / 40.f * 6.2831853f;
                gfx::line(x + std::cos(a0) * r, y + std::sin(a0) * r, x + std::cos(a1) * r, y + std::sin(a1) * r, 1, c);
            }
        };
        Building* sel = configuring >= 0 && g.builds[configuring].alive ? &g.builds[configuring] : g.buildAt(cursorTX, cursorTY);
        if (sel && sel->team == TeamSharded) {
            const BlockDef& d = sel->def();
            float ox, oy; centre(*sel, ox, oy);
            if (d.type == BType::PowerNode) {
                circle(ox, oy, d.laserRange * tilesize * kZoom + d.size * tilesize, configuring >= 0 ? kAccent : 0xffd37f80);
                if (configuring >= 0)
                    for (auto& o : g.builds)
                        if (o.alive && o.id != sel->id && g.powerLinkValid(*sel, o, false)) box(o, 0xffffff50);
                for (int32_t l : sel->links) if (g.builds[l].alive) box(g.builds[l], kAccent);
            } else if (d.type == BType::Bridge || d.type == BType::LiquidBridge) {
                for (int dir = 0; dir < 4; dir++)
                    for (int k = 1; k <= (int)d.range; k++) {
                        static const int dx4[4] = {1, 0, -1, 0}, dy4[4] = {0, 1, 0, -1};
                        float bx, by;
                        worldToScreen(*this, (sel->tx + dx4[dir] * k) * tilesize, (sel->ty + dy4[dir] * k) * tilesize, bx, by);
                        gfx::rect(bx - 2, by - 2, 4, 4, configuring >= 0 ? 0xffd37fb0 : 0xffd37f50);
                    }
                if (sel->link >= 0) { Building* t = g.buildAt(sel->link % g.width, sel->link / g.width); if (t) box(*t, kAccent); }
            } else if (sel->graph >= 0) {
                for (auto& o : g.builds)
                    if (o.alive && o.def().type == BType::PowerNode && std::find(o.links.begin(), o.links.end(), sel->id) != o.links.end()) box(o, kAccent);
            }
            if (configuring >= 0) box(*sel, 0xffffffff);
        }
        // placing a power block: range and the links it will make (PowerNode.getPotentialLinks)
        int blk = selectedBlock();
        if (mode == Mode::Build && configuring < 0 && !g.buildAt(cursorTX, cursorTY) && kBlocks[blk].type == BType::PowerNode) {
            const BlockDef& d = kBlocks[blk];
            float px0, py0;
            worldToScreen(*this, cursorTX * tilesize, cursorTY * tilesize, px0, py0);
            circle(px0, py0, d.laserRange * tilesize * kZoom + tilesize, 0xffd37f90);
            for (int32_t l : g.potentialPowerLinks((int16_t)blk, cursorTX, cursorTY, TeamSharded)) {
                float lx, ly; centre(g.builds[l], lx, ly);
                gfx::line(px0, py0, lx, ly, 1.5f, 0xfbd367a0);
                box(g.builds[l], 0xfbd367c0);
            }
        }
    }
    // cursor
    if (screen == Screen::Game && !hideHud) {
        int px = (int)std::floor(camX / tilesize + 0.5f), py = (int)std::floor(camY / tilesize + 0.5f);
        int cx = px + cursorDX, cy = py + cursorDY;
        Building* hover = g.buildAt(cx, cy);
        if (mode == Mode::Build && hover && (configuring >= 0 || hover->block != selectedBlock() || !kBlocks[hover->block].rotate)) {
            // over an existing building: selection outline (Mindustry hover), not a red ghost
            float off = hover->def().size % 2 == 0 ? tilesize / 2 : 0;
            worldToScreen(*this, hover->tx * tilesize + off, hover->ty * tilesize + off, sx, sy);
            float w = hover->def().size * tilesize * kZoom;
            gfx::rectLine(sx - w / 2 - 1, sy - w / 2 - 1, w + 2, w + 2, 1.5f, 0xffffffd0);
        } else if (mode == Mode::Build) {
            int blk = selectedBlock();
            const BlockDef& d = kBlocks[blk];
            float off = d.size % 2 == 0 ? tilesize / 2 : 0;
            worldToScreen(*this, cx * tilesize + off, cy * tilesize + off, sx, sy);
            bool ok = g.validPlace((int16_t)blk, cx, cy, rotation, TeamSharded);
            float w = d.size * tilesize * kZoom;
            drawBlock(blk, sx, sy, d.rotate ? rotation : 0, nullptr, ok ? 0xffffffa0 : 0xe55454a0, ok ? 0 : 0.5f, cx, cy);
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
    for (auto& rb : g.builds)
        if (rb.alive && rb.team == TeamSharded && rb.def().type == BType::Reactor && rb.heat > 0.46f) {
            gfx::rect(0, 32, 400, 16, ((int)(g.tick / 15) % 2) ? 0xe55454c0 : 0x80202080);
            gfx::text(200, 33, 0.4f, 0xffffffff, lang.ui("reactoralert").c_str(), 1);
            break;
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
        gfx::text(160, 18, 0.7f, kAccent, lang.ui("paused").c_str(), 1);
        // measured on this console: frames per second, CPU time of the simulation and of the draw calls
        std::snprintf(buf, sizeof buf, "%.1f FPS   sim %.1f ms   %s %.1f ms", perf.fps, perf.simMs, lang.ui("drawcpu").c_str(), perf.drawMs);
        gfx::text(160, 48, 0.4f, kGray, buf, 1);
        for (int i = 0; i < 3; i++) {
            float y = 70 + i * 44;
            gfx::rect(30, y, 260, 38, i == pauseIndex ? kPanelLight : kPanel);
            if (i == pauseIndex) gfx::rectLine(30, y, 260, 38, 2, kAccent);
            gfx::text(160, y + 10, 0.55f, 0xffffffff, lang.ui(items[i]).c_str(), 1);
        }
        if (autosaveMinutes) std::snprintf(buf, sizeof buf, "%s %u min", lang.ui("autosave").c_str(), autosaveMinutes);
        else std::snprintf(buf, sizeof buf, "%s %s", lang.ui("autosave").c_str(), lang.ui("off").c_str());
        gfx::text(160, 60, 0.4f, kGray, buf, 1);
        std::snprintf(buf, sizeof buf, "L   -       BGM  %u%%       +   R", musicVolume);
        gfx::text(160, 209, 0.5f, kAccent, buf, 1);
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

    // core items (CoreItemsDisplay): 16 px item icons, drawn 1:1
    Building* c = g.core(TeamSharded);
    if (c) {
        int col = 0, row = 0;
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (c->items.amount[i] <= 0) continue;
            float x = 4 + col * 79, y = 22 + row * 16;
            gfx::sprite(kItems[i].spr, x + 8, y + 8);
            std::snprintf(buf, sizeof buf, "%d", (int)c->items.amount[i]);
            gfx::text(x + 19, y + 1, 0.4f, 0xffffffff, buf);
            if (++col == 4) { col = 0; if (++row == 2) break; }
        }
    }
    // category tabs: icon of the first block, 16x16 composite (Block.icons())
    auto cats = usedCategories();
    auto list = blocksIn(category);
    PalettePage page(list.size(), selected[category]);
    int margin = page.margin();
    int tw = (320 - 2 * margin) / std::max(1, (int)cats.size());
    if (margin) {
        gfx::rect(0, kTabY, margin - 1, kTabH, kPanel);
        gfx::text(margin / 2, kTabY + 5, 0.50f, kAccent, "<", 1);
        gfx::rect(321 - margin, kTabY, margin - 1, kTabH, kPanel);
        std::snprintf(buf, sizeof buf, "%d/%d >", page.page + 1, page.pages);
        gfx::text(320 - margin / 2, kTabY + 5, 0.40f, kAccent, buf, 1);
    }
    for (size_t i = 0; i < cats.size(); i++) {
        float x = margin + i * tw;
        bool sel = cats[i] == category;
        gfx::rect(x + 1, kTabY, tw - 2, kTabH, sel ? kPanelLight : kPanel);
        if (sel) gfx::rect(x + 1, kTabY + kTabH - 2, tw - 2, 2, kAccent);
        auto list = blocksIn(cats[i]);
        if (!list.empty() && kBlocks[list[0]].sprIcon >= 0)
            gfx::sprite(kBlocks[list[0]].sprIcon + 1, x + tw / 2.f, kTabY + (kTabH - 2) / 2.f);
    }
    bool itemGrid = configuring >= 0 && g.builds[configuring].alive &&
                    (g.builds[configuring].def().type == BType::Sorter || g.builds[configuring].def().type == BType::Unloader);
    if (itemGrid) {
        // ItemSelection: 8 x 2 grid of item icons, the selected one framed in accent
        int sel = g.builds[configuring].sortItem;
        for (int i = 0; i < ITEM_COUNT; i++) {
            float x = (i % 8) * 40 + 2, y = kGridY + (i / 8) * kCellH;
            gfx::rect(x, y, 37, kCellH - 3, i == sel ? kPanelLight : kPanel);
            if (i == sel) gfx::rectLine(x, y, 37, kCellH - 3, 2, kAccent);
            gfx::sprite(kItems[i].spr, x + 18.5f, y + (kCellH - 3) / 2.f, 0, 1, 1);
        }
    }
    // block grid: 32x32 composite icons at 1:1
    int selIndex = std::clamp(selected[category], 0, std::max(0, (int)list.size() - 1));
    for (int slot = 0; slot < PalettePage::capacity && !itemGrid; ++slot) {
        int i = page.first() + slot;
        if (i >= (int)list.size()) break;
        float x = (slot % kCols) * kCellW + 3, y = kGridY + (slot / kCols) * kCellH;
        const BlockDef& d = kBlocks[list[i]];
        bool afford = c != nullptr;
        for (int r = 0; r < d.nreq && c; r++) if (c->items.amount[d.req[r].item] < d.req[r].amount) afford = false;
        bool sel = (int)i == selIndex && mode == Mode::Build;
        gfx::rect(x, y, kCellW - 4, kCellH - 3, sel ? kPanelLight : kPanel);
        if (sel) gfx::rectLine(x, y, kCellW - 4, kCellH - 3, 2, kAccent);
        if (d.sprIcon >= 0)
            gfx::sprite(d.sprIcon, x + (kCellW - 4) / 2.f, y + (kCellH - 3) / 2.f, 0, 1, 1,
                        afford ? 0xffffffff : 0x9a9a9aff, afford ? 0 : 0.55f);  // Pal.gray tint when unaffordable
    }
    // configured or hovered building: status sheet (Building.status / BlockStatus)
    Building* insp = configuring >= 0 && g.builds[configuring].alive ? &g.builds[configuring] : g.buildAt(cursorTX, cursorTY);
    if (insp && insp->team != TeamCrux && mode != Mode::Mine) {
        drawInspect(*insp);
    } else
    // selected block sheet: same icon, localized name, category, requirements (have/need)
    if (!list.empty()) {
        int blk = list[selIndex];
        const BlockDef& d = kBlocks[blk];
        gfx::rect(0, kInfoY, 320, 45, kPanel);
        if (d.sprIcon >= 0) gfx::sprite(d.sprIcon, 21, kInfoY + 22);
        gfx::text(42, kInfoY + 2, 0.45f, kAccent, lang.blockName(blk).c_str());
        gfx::text(316, kInfoY + 3, 0.38f, kGray, (lang.code == "fr" ? kCategoryFr : kCategoryEn)[category], 2);
        float x = 42;
        for (int r = 0; r < d.nreq; r++) {
            int it = d.req[r].item;
            bool ok = c && c->items.amount[it] >= d.req[r].amount;
            gfx::sprite(kItems[it].spr, x + 8, kInfoY + 32);
            std::snprintf(buf, sizeof buf, "%d", d.req[r].amount);
            gfx::text(x + 18, kInfoY + 24, 0.42f, ok ? 0xffffffff : kRed, buf);
            x += 26 + gfx::textWidth(buf, 0.42f);
        }
        if (d.powerUse > 0) { std::snprintf(buf, sizeof buf, "-%.0f/s", d.powerUse * 60); gfx::text(316, kInfoY + 24, 0.4f, 0xfbd367ff, buf, 2); }
        if (d.powerProduction > 0) { std::snprintf(buf, sizeof buf, "+%.0f/s", d.powerProduction * 60); gfx::text(316, kInfoY + 24, 0.4f, 0xfbd367ff, buf, 2); }
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

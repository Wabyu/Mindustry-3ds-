#pragma once
#include <map>
#include <string>
#include "game.hpp"
#include "platform.hpp"

namespace mdt {

class Lang {
public:
    void load(const std::string& code);
    std::string get(const std::string& key, const std::string& fallback = "") const;
    std::string blockName(int block) const;
    std::string itemName(int item) const;
    std::string sectorName(int sector) const;
    std::string ui(const char* key) const;  // port-specific UI strings
    std::string code = "en";
private:
    std::map<std::string, std::string> strings;
};

enum class Mode : uint8_t { Build, Break, Mine };
enum class Screen : uint8_t { Menu, Game, Pause, End };

class App {
public:
    void init();
    void frame(const platform::Input& in, float delta);  // delta in 60 Hz ticks
    void draw();
    void frameInner(const platform::Input& in, float delta);
    void drawInner();
    bool wantsExit() const { return exitRequested; }

    Game game;
    Lang lang;
    Screen screen = Screen::Menu;
    Mode mode = Mode::Build;
    int category = 2;         // distribution
    int selected[CAT_COUNT] = {};
    uint8_t rotation = 0;
    int cursorDX = 3, cursorDY = 0;  // cursor offset from the player, in tiles
    float camX = 0, camY = 0;
    unsigned musicVolume = 75;
    int menuIndex = 0, pauseIndex = 0;
    bool captured[SECTOR_COUNT] = {};
    bool hasSave[SECTOR_COUNT] = {};
    float repeatTimer = 0;
    float autosaveTimer = 0;
    uint8_t autosaveMinutes = 5;  // pause screen, X: 2 / 5 / 10 / off
    float fps = 0;
    bool exitRequested = false;
    bool hideHud = false;
    // performance counters over a 2 s window (shown in the pause screen)
    struct Perf { uint64_t windowStart = 0, simUs = 0, drawUs = 0; int frames = 0; float fps = 0, simMs = 0, drawMs = 0; } perf;  // control scenes: world only
    std::string toast; float toastTime = 0;
    int lastPlaceX = -9999, lastPlaceY = -9999;
    int32_t configuring = -1;
    int cursorTX = 0, cursorTY = 0;  // cursor tile of the last update  // building being configured (power node, bridge, sorter)

    std::vector<int> blocksIn(int cat) const;
    int selectedBlock() const;
    void startSector(int s, bool resume);
    void saveCurrent();
    void loadCampaign();
    void saveCampaign();

private:
    void updateMenu(const platform::Input& in);
    void updateGame(const platform::Input& in, float delta);
    void updatePause(const platform::Input& in);
    void touchUi(int x, int y);
    void dragStep(int x, int y);
    void configureTap(Building* target);
    void drawInspect(const Building& b);
    void drawWorld();
    void drawHud();
    void drawMenu();
    void drawBottom();
    void drawBlock(int block, float sx, float sy, int rot, const Building* b, uint32_t tint, float blend, int tx = -1, int ty = -1);
    int conveyorFrame(const Building& b) const;
    int envMargin() const;
};

}  // namespace mdt

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
    int menuIndex = 0, pauseIndex = 0;
    bool captured[SECTOR_COUNT] = {};
    bool hasSave[SECTOR_COUNT] = {};
    float repeatTimer = 0;
    float autosaveTimer = 0;
    float fps = 0;
    bool exitRequested = false;
    std::string toast; float toastTime = 0;
    int lastPlaceX = -9999, lastPlaceY = -9999;

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
    void drawWorld();
    void drawHud();
    void drawMenu();
    void drawBottom();
};

}  // namespace mdt

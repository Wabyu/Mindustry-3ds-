// Native simulation of a subset of Mindustry, ported from upstream Java logic.
// Platform independent: compiled for the 3DS and for host tests.
#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include "content_gen.hpp"

namespace mdt {

enum Team : uint8_t { TeamDerelict = 0, TeamSharded = 1, TeamCrux = 2 };

struct Tile {
    uint16_t floor = 0, ore = 0, env = 0;  // env: static wall / prop block id
    int32_t build = -1;                     // index in Game::builds, -1 none
};

struct ItemModule {
    int32_t amount[ITEM_COUNT] = {};
    int32_t total = 0;
    bool has(int i, int n = 1) const { return amount[i] >= n; }
    void add(int i, int n) { amount[i] += n; total += n; }
    void remove(int i, int n) { amount[i] -= n; total -= n; }
    int first() const { for (int i = 0; i < ITEM_COUNT; i++) if (amount[i] > 0) return i; return -1; }
};

struct AmmoEntry { int16_t item; int32_t amount; };

struct Building {
    bool alive = false;
    int32_t id = -1;
    int16_t block = 0;
    uint8_t team = 0, rotation = 0;
    int tx = 0, ty = 0;           // centre tile
    float health = 0;
    ItemModule items;
    std::vector<int32_t> proximity;
    int cdump = 0;
    float dumpTimer = 0;
    float efficiency = 1, optionalEfficiency = 0;
    bool sleeping = false;
    // Conveyor (Conveyor.ConveyorBuild)
    int len = 0, mid = 0;
    int16_t ids[3] = {-1, -1, -1};
    float xs[3] = {}, ys[3] = {};
    float minitem = 1, clogHeat = 0;
    int32_t next = -1;
    bool aligned = false;
    uint8_t blendbits = 0, blending = 0; int8_t blendsclx = 1, blendscly = 1;  // Autotiler results
    // Router / sorter
    int16_t lastItem = -1;
    int32_t lastInput = -1;    // tile index
    float time = 0;
    int16_t sortItem = -1;
    // Junction buffers: 4 directions x capacity, (item, insertion time)
    std::vector<int16_t> jitem[4];
    std::vector<float> jtime[4];
    // Bridge (BufferedItemBridge)
    int32_t link = -1;        // tile index of linked bridge
    std::vector<int16_t> bufItem; std::vector<float> bufTime;
    // Drill / crafter
    float progress = 0, warmup = 0, timeDrilled = 0, totalProgress = 0;
    int16_t dominantItem = -1; int dominantItems = 0;
    // Generator
    float generateTime = 0, productionEfficiency = 0;
    // Power
    int32_t graph = -1;
    std::vector<int32_t> links;  // power node lasers (building indices)
    float powerStored = 0;       // battery
    float powerStatus = 0;       // satisfaction for consumers
    // Turret
    float turretRotation = 90, reloadCounter = 0, recoil = 0;
    std::vector<AmmoEntry> ammo; int totalAmmo = 0;
    int32_t targetUnit = -1, targetBuild = -1;
    float targetTimer = 0;
    int shotCounter = 0;
    // Construction (ConstructBlock)
    bool constructing = false;
    float buildProgress = 1;
    float accumulator[4] = {}, totalAcc[4] = {};
    int16_t constructBlock = -1;
    bool deconstructing = false;

    const BlockDef& def() const { return kBlocks[block]; }
};

struct Unit {
    bool alive = false;
    int16_t type = 0;
    uint8_t team = 0;
    float x = 0, y = 0, vx = 0, vy = 0, rotation = 90;
    float health = 0, shield = 0;
    float reload[2] = {0, 0}; int side = 0;
    float weaponRotation = 0;
    int32_t targetUnit = -1, targetBuild = -1;
    float retarget = 0;
    // mining / items
    int16_t stackItem = -1; int stackAmount = 0;
    int32_t mineTile = -1; float mineTimer = 0;
    float walkTime = 0;
    bool isPlayer = false;
    float hitTime = 0;
    const UnitDef& def() const { return kUnits[type]; }
};

struct Bullet {
    bool alive = false;
    uint8_t team = 0;
    float x = 0, y = 0, vx = 0, vy = 0, time = 0;
    float targetX = 0, targetY = 0;  // artillery aim point
    BulletDef def{};
    int32_t owner = -1;  // unit index (-1 building)
};

struct BuildPlan {
    int tx, ty; int16_t block; uint8_t rotation; bool breaking;
};

struct SpawnGroupDef {
    int16_t type; int begin, end, spacing, amount, max;
    float scaling, shields, shieldScaling;
    int getSpawned(int wave) const;
    float getShield(int wave) const { return std::fmax(shields + shieldScaling * (wave - begin), 0.f); }
};

// mindustry.world.meta.BlockStatus + the reason shown to the player
enum class Status : uint8_t { Active, NoOutput, NoInput, Constructing };
enum class Reason : uint8_t {
    None, NoFuel, NoPower, LowPower, MissingItem, OutputFull, NoOre, NoAmmo,
    Charging, Discharging, BatteryFull, BatteryEmpty, Unpowered
};
struct StatusInfo { Status status; Reason reason; int16_t item; float value; };

// per power graph totals of the last update, per tick (PowerGraph.update)
struct GraphStats { float produced = 0, needed = 0, stored = 0, capacity = 0, batteryDelta = 0, satisfaction = 0; int members = 0; };

struct Effect { float x, y, time, life, size; uint32_t color; uint8_t kind; };

enum class GameState : uint8_t { Playing, Won, Lost };

class Game {
public:
    int width = 0, height = 0;
    std::vector<Tile> tiles;
    std::deque<Building> builds;  // deque: references stay valid when buildings are added
    std::vector<int32_t> freeBuilds;
    std::vector<Unit> units;
    std::vector<Bullet> bullets;
    std::vector<Effect> effects;
    std::vector<BuildPlan> plans;
    std::vector<std::pair<int, int>> spawns;
    std::vector<SpawnGroupDef> waves;
    std::string mapName;
    int sector = 0;

    // rules / state (mindustry.game.Rules, GameState)
    float waveSpacing = 7200, wavetime = 0;
    bool waveTimer = true, wavesEnabled = true;
    int wave = 1;
    int winWave = 0;
    float tick = 0;
    GameState state = GameState::Playing;
    int32_t player = -1;
    bool spawnPlayer = true;
    float respawnTimer = 0;
    float flowTimer = 0;
    bool flowDirty = true;
    float deconstructRefund = 0.5f;

    // pathfinding (Pathfinder.fieldCore) for each team that attacks
    std::vector<int32_t> flowCost;   // cost to reach a sharded core, per tile

    bool loadMap(const std::vector<uint8_t>& data, int sectorId);

    Tile* tile(int x, int y) { return (x < 0 || y < 0 || x >= width || y >= height) ? nullptr : &tiles[y * width + x]; }
    Building* buildAt(int x, int y) { Tile* t = tile(x, y); return (t && t->build >= 0) ? &builds[t->build] : nullptr; }
    int buildIndexAt(int x, int y) { Tile* t = tile(x, y); return t ? t->build : -1; }
    bool solid(int x, int y);
    bool solidFor(int x, int y, uint8_t team);
    bool isDeep(int x, int y);
    int drop(int x, int y);  // Tile.drop(): overlay item or floor item

    // building management
    int32_t addBuilding(int16_t block, int tx, int ty, uint8_t team, uint8_t rotation);
    void removeBuilding(int32_t idx, bool destroyed);
    void updateProximity(int32_t idx);
    // Autotiler tiling for a conveyor at (tx, ty); usePlans also counts queued plans (placement preview)
    void conveyorTiling(int tx, int ty, int rotation, uint8_t team, bool usePlans,
                        uint8_t& bits, int8_t& sclx, int8_t& scly, uint8_t& blending);
    void refreshAround(int32_t idx);
    bool validPlace(int16_t block, int tx, int ty, uint8_t rotation, uint8_t team, bool allowReplace = true);
    void linkedTiles(int16_t block, int tx, int ty, int& x0, int& y0, int& x1, int& y1) const;
    Building* core(uint8_t team);
    Building* closestCore(uint8_t team, float x, float y);

    // item logic (Building.acceptItem / handleItem ports)
    bool acceptItem(Building& b, Building& source, int item);
    void handleItem(Building& b, Building& source, int item);
    bool dump(Building& b, int item);
    void offload(Building& b, int item);
    bool put(Building& b, int item);
    int relativeTo(const Building& from, const Building& to) const;

    // simulation
    void update(float delta);
    void updateBuilding(int32_t idx, float delta);
    void updatePower(float delta);
    void rebuildPowerGraphs();
    void autoLinkPower(int32_t idx);
    bool powerGraphsDirty = true;
    std::vector<float> graphSatisfaction;
    std::vector<GraphStats> graphStats;
    StatusInfo status(const Building& b) const;
    // PowerNode.linkValid(this, other, checkMaxNodes)
    bool powerLinkValid(const Building& node, const Building& other, bool checkMaxNodes) const;
    // PowerNode.configure(pos): toggle a laser link; returns true if anything changed
    bool togglePowerLink(int32_t node, int32_t other);
    // PowerNode double tap: link to potential targets, or clear every link
    void autoOrClearLinks(int32_t node);
    // buildings a node placed at (tx, ty) would link to (placement preview)
    std::vector<int32_t> potentialPowerLinks(int16_t block, int tx, int ty, uint8_t team);
    // ItemBridge.linkValid + configure
    bool bridgeLinkValid(const Building& a, const Building& b) const;
    bool toggleBridgeLink(int32_t a, int32_t b);

    // units
    int32_t spawnUnit(int16_t type, uint8_t team, float x, float y);
    void updateUnits(float delta);
    void updateBullets(float delta);
    void damageBuilding(int32_t idx, float amount);
    void damageUnit(int32_t idx, float amount);
    void createBullet(const BulletDef& def, uint8_t team, float x, float y, float angle, int32_t owner, float aimX = 0, float aimY = 0);
    void updateFlowField();
    void runWave();
    int enemies() const;
    void addEffect(float x, float y, uint32_t color, float size, float life, uint8_t kind);

    // player / building
    void updatePlayer(float delta, float moveX, float moveY);
    void updateBuilder(Unit& u, float delta);
    void updateMining(Unit& u, float delta);
    bool queuePlan(int tx, int ty, int16_t block, uint8_t rotation, bool breaking);
    bool canMine(const Unit& u, int tileIdx);

    float buildRange = 220.f;      // Vars.buildingRange
    float mineRange = 70.f;        // UnitType.mineRange
    float mineTransferRange = 220.f;
    int lastBridge = -1;
    std::string lastMessage; float messageTime = 0;
    void message(const std::string& s) { lastMessage = s; messageTime = 240; }

    // statistics (host tests / HUD)
    long itemsDelivered[ITEM_COUNT] = {};
    int unitsKilled = 0;
};

// Conveyor.draw: frame = enabled && clogHeat <= 0.5 ? (int)(Time.time * speed * 8 * efficiency) % 4 : 0.
// No item is required: an empty belt keeps scrolling.
inline int conveyorFrame(const Building& b, float time) {
    if (b.constructing || b.clogHeat > 0.5f) return 0;
    return ((int)(time * b.def().speed * 8.f * b.efficiency)) % 4;
}

inline float angleDeg(float x, float y) { float a = std::atan2(y, x) * 57.29578f; return a < 0 ? a + 360.f : a; }
float moveToward(float angle, float to, float speed);
float angleDist(float a, float b);
float approach(float from, float to, float amount);
float lerp(float a, float b, float t);
float randf();  // [0,1)

}  // namespace mdt

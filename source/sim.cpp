// Building updates and power graphs: ports of updateTile() from upstream classes.
#include "game.hpp"
#include <algorithm>
#include <cstring>

namespace mdt {

static const int d4x[4] = {1, 0, -1, 0};
static const int d4y[4] = {0, 1, 0, -1};
constexpr float dumpTime = 5.f;  // Block.dumpTime

static bool timer(float& t, float interval, float delta) {
    t += delta;
    if (t >= interval) { t = 0; return true; }
    return false;
}

static bool usesPower(const BlockDef& d) {
    return d.powerUse > 0 || d.powerProduction > 0 || d.powerBuffer > 0 || d.type == BType::PowerNode;
}

// ---------------------------------------------------------------- per type
static void updateConveyor(Game& g, Building& b, float delta) {
    // Conveyor.ConveyorBuild.updateTile
    b.minitem = 1.f;
    b.mid = 0;
    if (b.len == 0) { b.clogHeat = 0; return; }
    Building* nextc = (b.next >= 0 && g.builds[b.next].alive) ? &g.builds[b.next] : nullptr;
    bool aligned = b.aligned && nextc && nextc->def().type == BType::Conveyor;
    float nextMax = aligned ? 1.f - std::fmax(0.4f - nextc->minitem, 0.f) : 1.f;
    float moved = b.def().speed * b.efficiency * delta;
    for (int i = b.len - 1; i >= 0; i--) {
        float nextpos = (i == b.len - 1 ? 100.f : b.ys[i + 1]) - 0.4f;
        float maxmove = std::clamp(nextpos - b.ys[i], 0.f, moved);
        b.ys[i] += maxmove;
        if (b.ys[i] > nextMax) b.ys[i] = nextMax;
        if (b.ys[i] > 0.5f && i > 0) b.mid = i - 1;
        b.xs[i] = approach(b.xs[i], 0, moved * 2);
        bool passed = false;
        if (b.ys[i] >= 1.f && nextc && b.ids[i] >= 0 && nextc->team == b.team && g.acceptItem(*nextc, b, b.ids[i])) {
            int item = b.ids[i];
            g.handleItem(*nextc, b, item);
            if (aligned && nextc->len > 0) {
                // nextc.xs[nextc.lastInserted] = xs[i]: inserted at index 0 when passing forward
                nextc->xs[0] = b.xs[i];
            }
            // items.remove(ids[i], len - i); len = min(i, len)
            for (int k = i; k < b.len; k++) if (b.ids[k] >= 0) b.items.remove(b.ids[k], 1);
            b.len = std::min(i, b.len);
            passed = true;
        }
        if (!passed && b.ys[i] < b.minitem) b.minitem = b.ys[i];
    }
    if (b.minitem < 0.4f + (b.blendbits == 1 ? 0.3f : 0.f)) b.clogHeat = approach(b.clogHeat, 1.f, delta / 60.f);
    else b.clogHeat = 0;
}

static void updateJunction(Game& g, Building& b, float delta) {
    (void)delta;
    const BlockDef& d = b.def();
    for (int i = 0; i < 4; i++) {
        if (b.jitem[i].empty()) continue;
        float t = b.jtime[i].front();
        if (g.tick >= t + d.speed || g.tick < t) {
            int item = b.jitem[i].front();
            Building* dest = g.buildAt(b.tx + d4x[i], b.ty + d4y[i]);
            if (!dest || dest->team != b.team || !g.acceptItem(*dest, b, item)) continue;
            g.handleItem(*dest, b, item);
            b.jitem[i].erase(b.jitem[i].begin());
            b.jtime[i].erase(b.jtime[i].begin());
        }
    }
}

static void updateRouter(Game& g, Building& b, float delta) {
    // Router.RouterBuild.updateTile / getTileTarget
    if (b.lastItem < 0 && b.items.total > 0) b.lastItem = (int16_t)b.items.first();
    if (b.lastItem < 0) return;
    b.time += 1.f / b.def().speed * delta;
    int n = (int)b.proximity.size();
    if (n == 0) return;
    auto target = [&](bool set) -> Building* {
        int counter = b.rotation;
        for (int i = 0; i < n; i++) {
            Building& other = g.builds[b.proximity[(i + counter) % n]];
            if (set) b.rotation = (uint8_t)((b.rotation + 1) % n);
            if (other.ty * g.width + other.tx == b.lastInput && other.def().type == BType::OverflowGate) continue;
            if (other.team == b.team && g.acceptItem(other, b, b.lastItem)) return &other;
        }
        return nullptr;
    };
    Building* t = target(false);
    if (t && (b.time >= 1.f || t->def().type != BType::Router)) {
        t = target(true);
        if (t) {
            int item = b.lastItem;
            b.items.remove(item, 1);
            b.lastItem = -1;
            g.handleItem(*t, b, item);
        }
    }
}

static void updateBridge(Game& g, Building& b, float delta) {
    // BufferedItemBridge: items enter a buffer and travel `speed` ticks to the linked bridge
    const BlockDef& d = b.def();
    Building* other = b.link >= 0 ? g.buildAt(b.link % g.width, b.link / g.width) : nullptr;
    if (other && (other->def().type != BType::Bridge || other->team != b.team)) { other = nullptr; b.link = -1; }
    if (!other) {
        // end of the chain: dump to neighbours that are not feeding bridges
        if (timer(b.dumpTimer, dumpTime, delta)) {
            int n = (int)b.proximity.size();
            for (int k = 0; k < n && b.items.total > 0; k++) {
                Building& o = g.builds[b.proximity[(k + b.cdump) % n]];
                if (o.def().type == BType::Bridge && o.link == b.ty * g.width + b.tx) continue;
                int it = b.items.first();
                if (o.team == b.team && g.acceptItem(o, b, it)) {
                    g.handleItem(o, b, it); b.items.remove(it, 1);
                    b.cdump = (b.cdump + k + 1) % n;
                    break;
                }
            }
        }
        return;
    }
    int cap = std::max(1, d.capacity);
    if ((int)b.bufItem.size() < cap && b.items.total > 0) {
        int it = b.items.first();
        b.items.remove(it, 1);
        b.bufItem.push_back((int16_t)it);
        b.bufTime.push_back(g.tick);
    }
    if (!b.bufItem.empty() && g.tick >= b.bufTime.front() + d.speed) {
        int it = b.bufItem.front();
        if (g.acceptItem(*other, b, it)) {
            g.handleItem(*other, b, it);
            b.bufItem.erase(b.bufItem.begin());
            b.bufTime.erase(b.bufTime.begin());
        }
    }
}

static void countOre(Game& g, Building& b) {
    // Drill.countOre: dominant item = not lowPriority first, then count, then id
    int counts[ITEM_COUNT] = {};
    int x0, y0, x1, y1;
    g.linkedTiles(b.block, b.tx, b.ty, x0, y0, x1, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int it = g.drop(x, y);
            if (it >= 0 && kItems[it].hardness <= kBlockTier[b.block]) counts[it]++;
        }
    int best = -1;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (!counts[i]) continue;
        if (best < 0) { best = i; continue; }
        bool lpI = i == I_sand, lpB = best == I_sand;
        if (lpI != lpB) { if (!lpI) best = i; continue; }
        if (counts[i] > counts[best] || (counts[i] == counts[best] && i > best)) best = i;
    }
    b.dominantItem = (int16_t)best;
    b.dominantItems = best >= 0 ? counts[best] : 0;
}

static void updateDrill(Game& g, Building& b, float delta) {
    const BlockDef& d = b.def();
    if (b.dominantItem < 0 && b.dominantItems == 0) countOre(g, b);
    if (timer(b.dumpTimer, dumpTime, delta))
        g.dump(b, (b.dominantItem >= 0 && b.items.has(b.dominantItem)) ? b.dominantItem : -1);
    if (b.dominantItem < 0) return;
    b.timeDrilled += b.warmup * delta;
    // Drill.getDrillTime(item)
    float delay = d.drillTime + d.hardnessMul * kItems[b.dominantItem].hardness;
    if (b.items.total < d.itemCapacity && b.dominantItems > 0 && b.efficiency > 0) {
        float speed = b.efficiency;  // liquid boost not ported
        b.warmup = approach(b.warmup, speed, d.warmupSpeed * delta);
        b.progress += delta * b.dominantItems * speed * b.warmup;
    } else {
        b.warmup = approach(b.warmup, 0, d.warmupSpeed * delta);
    }
    if (b.dominantItems > 0 && b.progress >= delay && b.items.total < d.itemCapacity) {
        g.offload(b, b.dominantItem);
        b.progress = std::fmod(b.progress, delay);
    }
}

static void updateCrafter(Game& g, Building& b, float delta) {
    // GenericCrafter.GenericCrafterBuild.updateTile
    const BlockDef& d = b.def();
    bool shouldConsume = d.output.item < 0 || b.items.amount[d.output.item] + d.output.amount <= d.itemCapacity;
    bool hasItems = true;
    for (int i = 0; i < d.nconsumes; i++) if (!b.items.has(d.consumes[i].item, d.consumes[i].amount)) hasItems = false;
    float eff = (shouldConsume && hasItems) ? 1.f : 0.f;
    if (d.powerUse > 0) eff = std::fmin(eff, b.powerStatus);
    b.efficiency = eff;
    if (eff > 0) {
        b.progress += 1.f / d.craftTime * eff * delta;
        b.warmup = approach(b.warmup, 1.f, d.warmupSpeed * delta);
    } else {
        b.warmup = approach(b.warmup, 0.f, d.warmupSpeed * delta);
    }
    b.totalProgress += b.warmup * delta;
    if (b.progress >= 1.f) {
        for (int i = 0; i < d.nconsumes; i++) b.items.remove(d.consumes[i].item, d.consumes[i].amount);
        if (d.output.item >= 0)
            for (int i = 0; i < d.output.amount; i++) g.offload(b, d.output.item);
        b.progress = std::fmod(b.progress, 1.f);
        g.addEffect(b.tx * tilesize + 4, b.ty * tilesize + 4, 0x7f7f7f, 6, 20, 0);
    }
    if (d.output.item >= 0 && timer(b.dumpTimer, dumpTime, delta)) g.dump(b, d.output.item);
}

static void updateGenerator(Game& g, Building& b, float delta) {
    (void)g;
    // ConsumeGenerator + ConsumeItemFlammable
    const BlockDef& d = b.def();
    int fuel = -1; float best = -1;
    for (int i = 0; i < ITEM_COUNT; i++)
        if (b.items.has(i) && kItems[i].flammability >= 0.2f && kItems[i].flammability > best) { best = kItems[i].flammability; fuel = i; }
    float eff = fuel >= 0 ? 1.f : 0.f;
    float mult = fuel >= 0 ? kItems[fuel].flammability : 0.f;
    bool valid = eff > 0;
    b.warmup = approach(b.warmup, valid ? 1.f : 0.f, 0.05f * delta);
    b.productionEfficiency = eff * mult;
    if (valid && b.generateTime <= 0.f) {
        b.items.remove(fuel, 1);
        b.generateTime = 1.f;
    }
    b.generateTime -= delta / d.itemDuration;
    if (b.generateTime < 0) b.generateTime = 0;
}

static int peekAmmo(const Building& b) { return b.ammo.empty() ? -1 : b.ammo.back().item; }

static void updateTurret(Game& g, Building& b, int32_t idx, float delta) {
    const BlockDef& d = b.def();
    float x = b.tx * tilesize + (d.size % 2 == 0 ? tilesize / 2 : 0);
    float y = b.ty * tilesize + (d.size % 2 == 0 ? tilesize / 2 : 0);
    b.recoil = approach(b.recoil, 0, delta / d.reload);
    // validateTarget
    if (b.targetUnit >= 0) {
        Unit& u = g.units[b.targetUnit];
        float dx = u.x - x, dy = u.y - y;
        if (!u.alive || u.team == b.team || dx * dx + dy * dy > (d.range + u.def().hitSize / 2) * (d.range + u.def().hitSize / 2)) b.targetUnit = -1;
    }
    if (b.targetBuild >= 0 && (!g.builds[b.targetBuild].alive || g.builds[b.targetBuild].team == b.team)) b.targetBuild = -1;
    if (timer(b.targetTimer, 20.f, delta) || (b.targetUnit < 0 && b.targetBuild < 0 && b.targetTimer == 0)) {
        // Units.bestTarget: closest enemy unit matching air/ground filters, else building
        float bestD = 1e30f; int best = -1;
        for (size_t i = 0; i < g.units.size(); i++) {
            Unit& u = g.units[i];
            if (!u.alive || u.team == b.team || u.team == TeamDerelict) continue;
            bool fly = u.def().flying;
            if ((fly && !d.targetAir) || (!fly && !d.targetGround)) continue;
            float r = d.range + u.def().hitSize / 2;
            float dx = u.x - x, dy = u.y - y, d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 < bestD) { bestD = d2; best = (int)i; }
        }
        b.targetUnit = best;
        b.targetBuild = -1;
    }
    bool has = !b.ammo.empty();
    if (has && b.reloadCounter < d.reload) {
        int a = peekAmmo(b);
        float rm = 1;
        for (int i = 0; i < d.nammo; i++) if (d.ammo[i].item == a) rm = d.ammo[i].bullet.reloadMultiplier;
        b.reloadCounter += delta * rm;
    }
    if (b.targetUnit < 0) return;
    Unit& t = g.units[b.targetUnit];
    float targetRot = angleDeg(t.x - x, t.y - y);
    b.turretRotation = moveToward(b.turretRotation, targetRot, d.rotateSpeed * delta);
    if (angleDist(b.turretRotation, targetRot) < d.shootCone && has && b.reloadCounter >= d.reload) {
        int a = peekAmmo(b);
        const BulletDef* bd = nullptr;
        for (int i = 0; i < d.nammo; i++) if (d.ammo[i].item == a) bd = &d.ammo[i].bullet;
        if (bd) {
            for (int s = 0; s < std::max(1, d.shots); s++) {
                float ang = b.turretRotation + (randf() * 2 - 1) * d.inaccuracy;
                float rad = b.turretRotation * 0.0174533f;
                float sx = x + std::cos(rad) * (d.size * tilesize / 2.f);
                float sy = y + std::sin(rad) * (d.size * tilesize / 2.f);
                g.createBullet(*bd, b.team, sx, sy, ang, -1 - idx, t.x, t.y);
            }
            // ItemTurret.useAmmo (consumeAmmoOnce)
            AmmoEntry& e = b.ammo.back();
            e.amount -= 1; b.totalAmmo -= 1;
            if (e.amount <= 0) b.ammo.pop_back();
            b.recoil = 1;
        }
        b.reloadCounter = std::fmod(b.reloadCounter, d.reload);
    }
}

void Game::updateBuilding(int32_t idx, float delta) {
    Building& b = builds[idx];
    if (!b.alive || b.constructing) return;
    switch (b.def().type) {
        case BType::Conveyor: updateConveyor(*this, b, delta); break;
        case BType::Junction: updateJunction(*this, b, delta); break;
        case BType::Router: updateRouter(*this, b, delta); break;
        case BType::Bridge: updateBridge(*this, b, delta); break;
        case BType::Drill: updateDrill(*this, b, delta); break;
        case BType::Crafter: updateCrafter(*this, b, delta); break;
        case BType::ConsumeGen: updateGenerator(*this, b, delta); break;
        case BType::ItemTurret: updateTurret(*this, b, idx, delta); break;
        default: break;
    }
}

// ---------------------------------------------------------------- power
// PowerNode.getNodeLinks / laser range check (overlaps with the other building)
static bool inLaserRange(Game& g, const Building& node, const Building& other) {
    float r = node.def().laserRange * tilesize;
    float ox = other.tx * tilesize, oy = other.ty * tilesize;
    float nx = node.tx * tilesize, ny = node.ty * tilesize;
    float half = other.def().size * tilesize / 2.f;
    float dx = std::fmax(std::fabs(ox - nx) - half, 0.f), dy = std::fmax(std::fabs(oy - ny) - half, 0.f);
    (void)g;
    return dx * dx + dy * dy <= r * r;
}

void Game::autoLinkPower(int32_t idx) {
    Building& b = builds[idx];
    if (!usesPower(b.def())) return;
    rebuildPowerGraphs();
    if (b.def().type == BType::PowerNode) {
        // PowerNode.placed(): link to other power buildings in range, nearest first
        std::vector<std::pair<float, int32_t>> cand;
        for (size_t i = 0; i < builds.size(); i++) {
            Building& o = builds[i];
            if ((int32_t)i == idx || !o.alive || o.team != b.team || !usesPower(o.def())) continue;
            if (!inLaserRange(*this, b, o)) continue;
            if (std::find(b.proximity.begin(), b.proximity.end(), (int32_t)i) != b.proximity.end()) continue;
            float dx = (float)(o.tx - b.tx), dy = (float)(o.ty - b.ty);
            cand.push_back({dx * dx + dy * dy, (int32_t)i});
        }
        std::sort(cand.begin(), cand.end());
        std::vector<int32_t> graphsLinked;
        for (auto& c : cand) {
            if ((int)b.links.size() >= b.def().maxNodes) break;
            Building& o = builds[c.second];
            int gid = o.graph;
            if (gid >= 0 && (gid == b.graph || std::find(graphsLinked.begin(), graphsLinked.end(), gid) != graphsLinked.end())) continue;
            if (o.def().type == BType::PowerNode && (int)o.links.size() >= o.def().maxNodes) continue;
            b.links.push_back(c.second);
            if (o.def().type == BType::PowerNode) o.links.push_back(idx);
            graphsLinked.push_back(gid);
        }
    } else {
        // PowerNode.getNodeLinks: nearby nodes with free slots link the new building
        for (size_t i = 0; i < builds.size(); i++) {
            Building& o = builds[i];
            if (!o.alive || o.team != b.team || o.def().type != BType::PowerNode) continue;
            if ((int)o.links.size() >= o.def().maxNodes || !inLaserRange(*this, o, b)) continue;
            if (o.graph >= 0 && o.graph == b.graph) continue;
            o.links.push_back(idx);
            rebuildPowerGraphs();
            break;
        }
    }
    powerGraphsDirty = true;
}

void Game::rebuildPowerGraphs() {
    for (auto& b : builds) b.graph = -1;
    int gid = 0;
    std::vector<int32_t> stack;
    for (size_t i = 0; i < builds.size(); i++) {
        Building& s = builds[i];
        if (!s.alive || s.graph >= 0 || s.constructing || !usesPower(s.def())) continue;
        s.graph = gid;
        stack.clear(); stack.push_back((int32_t)i);
        while (!stack.empty()) {
            int32_t c = stack.back(); stack.pop_back();
            Building& b = builds[c];
            auto visit = [&](int32_t o) {
                Building& ob = builds[o];
                if (!ob.alive || ob.graph >= 0 || ob.constructing || ob.team != b.team || !usesPower(ob.def())) return;
                ob.graph = gid; stack.push_back(o);
            };
            for (int32_t o : b.links) visit(o);
            for (size_t k = 0; k < builds.size(); k++) {
                Building& ob = builds[k];
                if (ob.alive && std::find(ob.links.begin(), ob.links.end(), c) != ob.links.end()) visit((int32_t)k);
            }
            for (int32_t o : b.proximity) {
                const BlockDef& a = b.def(); const BlockDef& od = builds[o].def();
                // Building.getPowerConnections: consumer-consumer contact does not conduct
                bool aCons = a.powerUse > 0 && a.powerProduction <= 0 && a.powerBuffer <= 0;
                bool oCons = od.powerUse > 0 && od.powerProduction <= 0 && od.powerBuffer <= 0;
                if (aCons && oCons) continue;
                visit(o);
            }
        }
        gid++;
    }
    graphSatisfaction.assign(gid, 0.f);
    powerGraphsDirty = false;
}

void Game::updatePower(float delta) {
    if (powerGraphsDirty) rebuildPowerGraphs();
    int n = (int)graphSatisfaction.size();
    if (!n) return;
    std::vector<float> produced(n, 0), needed(n, 0), stored(n, 0), capacity(n, 0);
    for (auto& b : builds) {
        if (!b.alive || b.graph < 0 || b.constructing) continue;
        const BlockDef& d = b.def();
        if (d.powerProduction > 0) produced[b.graph] += d.powerProduction * b.productionEfficiency * delta;
        if (d.powerUse > 0) {
            bool should = true;
            if (d.type == BType::Crafter) {
                should = d.output.item < 0 || b.items.amount[d.output.item] + d.output.amount <= d.itemCapacity;
                for (int i = 0; i < d.nconsumes; i++) if (!b.items.has(d.consumes[i].item, d.consumes[i].amount)) should = false;
            }
            if (should) needed[b.graph] += d.powerUse * delta;
        }
        if (d.powerBuffer > 0) { stored[b.graph] += b.powerStored; capacity[b.graph] += d.powerBuffer; }
    }
    // PowerGraph.update: batteries cover deficits and absorb surplus
    std::vector<float> batteryDelta(n, 0);
    for (int g = 0; g < n; g++) {
        if (needed[g] > produced[g]) {
            float used = std::fmin(stored[g], needed[g] - produced[g]);
            produced[g] += used; batteryDelta[g] = -used;
        } else if (produced[g] > needed[g]) {
            float charge = std::fmin(capacity[g] - stored[g], produced[g] - needed[g]);
            batteryDelta[g] = charge;
        }
        graphSatisfaction[g] = needed[g] <= 0 ? 1.f : std::fmin(1.f, produced[g] / needed[g]);
    }
    for (auto& b : builds) {
        if (!b.alive || b.graph < 0) continue;
        const BlockDef& d = b.def();
        b.powerStatus = graphSatisfaction[b.graph];
        if (d.powerBuffer > 0 && capacity[b.graph] > 0) {
            int g = b.graph;
            if (batteryDelta[g] < 0 && stored[g] > 0) b.powerStored += batteryDelta[g] * (b.powerStored / stored[g]);
            else if (batteryDelta[g] > 0) b.powerStored += batteryDelta[g] * ((d.powerBuffer - b.powerStored) / std::fmax(1e-6f, capacity[g] - stored[g]));
            b.powerStored = std::clamp(b.powerStored, 0.f, d.powerBuffer);
        }
    }
}

// ---------------------------------------------------------------- main loop
void Game::update(float delta) {
    if (state != GameState::Playing) return;
    tick += delta;
    if (messageTime > 0) messageTime -= delta;

    if (wavesEnabled && waveTimer && !waves.empty()) {
        wavetime -= delta;
        if (wavetime <= 0) {
            runWave();
            wavetime = waveSpacing;
        }
    }

    updatePower(delta);
    for (size_t i = 0; i < builds.size(); i++) updateBuilding((int32_t)i, delta);
    flowTimer -= delta;
    if (flowDirty && flowTimer <= 0) { updateFlowField(); flowDirty = false; flowTimer = 90; }
    updateUnits(delta);
    updateBullets(delta);
    for (auto& e : effects) e.time += delta;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.time >= e.life; }), effects.end());

    // Logic.checkGameState
    if (!core(TeamSharded)) state = GameState::Lost;
    else if (winWave > 0 && wave >= winWave && enemies() == 0) state = GameState::Won;
}

void Game::runWave() {
    // WaveSpawner.spawnEnemies: group.getSpawned(state.wave - 1) units at each ground spawn
    for (auto& g : waves) {
        int n = g.getSpawned(wave - 1);
        for (auto& s : spawns) {
            for (int i = 0; i < n; i++) {
                float spread = tilesize * 2;
                float x = s.first * tilesize + (randf() * 2 - 1) * spread;
                float y = s.second * tilesize + (randf() * 2 - 1) * spread;
                int32_t u = spawnUnit(g.type, TeamCrux, x, y);
                if (u >= 0) units[u].shield = g.getShield(wave - 1);
            }
        }
    }
    wave++;
}

int Game::enemies() const {
    int n = 0;
    for (auto& u : units) if (u.alive && u.team == TeamCrux) n++;
    return n;
}

}  // namespace mdt

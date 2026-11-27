// Units, AI, bullets, pathfinding, player building and mining.
// Ports (simplified) of GroundAI, FlyingAI, BuilderComp, MinerComp, Pathfinder.
#include "game.hpp"
#include <algorithm>
#include <queue>

namespace mdt {

static const int d8x[8] = {1, 0, -1, 0, 1, 1, -1, -1};
static const int d8y[8] = {0, 1, 0, -1, 1, -1, 1, -1};

int32_t Game::spawnUnit(int16_t type, uint8_t team, float x, float y) {
    int alive = 0;
    for (auto& u : units) if (u.alive) alive++;
    if (alive >= 160) return -1;  // Old 3DS budget, see docs/LIMITES.md
    int32_t idx = -1;
    for (size_t i = 0; i < units.size(); i++) if (!units[i].alive) { idx = (int32_t)i; break; }
    if (idx < 0) { idx = (int32_t)units.size(); units.emplace_back(); }
    Unit& u = units[idx];
    u = Unit{};
    u.alive = true; u.type = type; u.team = team; u.x = x; u.y = y;
    u.health = kUnits[type].health;
    u.rotation = 90;
    return idx;
}

// ---------------------------------------------------------------- pathfinding
// Pathfinder.costGround for the wave team; Dijkstra from the player cores.
void Game::updateFlowField() {
    size_t n = (size_t)width * height;
    flowCost.assign(n, -1);
    std::vector<int32_t> cost(n, -1);
    for (size_t i = 0; i < n; i++) {
        Tile& t = tiles[i];
        int x = (int)(i % width), y = (int)(i / width);
        bool deep = kBlocks[t.floor].isDeep;
        bool wall = t.env && kBlocks[t.env].type == BType::StaticWall;
        int c = -1;
        if (!deep && !wall) {
            c = 1;
            if (t.build >= 0) {
                Building& b = builds[t.build];
                if (b.team == TeamCrux && b.def().solid) c = -1;             // own team solid
                else if (b.team == TeamDerelict && b.def().solid) c = -1;     // derelict solid
                else c += std::min((int)(b.health / 40), 80) * 5 / 100 + (b.def().solid ? 70 : 0);
            }
            if (c > 0) {
                for (int k = 0; k < 4; k++) {
                    int nx = x + d8x[k], ny = y + d8y[k];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    Tile& o = tiles[ny * width + nx];
                    if (o.env && kBlocks[o.env].type == BType::StaticWall) { c += 2; break; }
                }
                if (kBlocks[t.floor].isLiquid) c += 6;
            }
        }
        cost[i] = c;
    }
    using Node = std::pair<int32_t, int32_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> q;
    for (auto& b : builds) {
        if (!b.alive || b.team != TeamSharded || b.def().type != BType::Core) continue;
        int x0, y0, x1, y1;
        linkedTiles(b.block, b.tx, b.ty, x0, y0, x1, y1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) { int i = y * width + x; flowCost[i] = 0; q.push({0, i}); }
    }
    while (!q.empty()) {
        auto [d, i] = q.top(); q.pop();
        if (d > flowCost[i]) continue;
        int x = i % width, y = i / width;
        for (int k = 0; k < 4; k++) {
            int nx = x + d8x[k], ny = y + d8y[k];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            int j = ny * width + nx;
            if (cost[j] < 0) continue;
            int nd = d + cost[j];
            if (flowCost[j] < 0 || nd < flowCost[j]) { flowCost[j] = nd; q.push({nd, j}); }
        }
    }
}

// ---------------------------------------------------------------- bullets
void Game::createBullet(const BulletDef& def, uint8_t team, float x, float y, float angle, int32_t owner, float aimX, float aimY) {
    if (bullets.size() > 400) return;
    Bullet b;
    b.alive = true; b.team = team; b.x = x; b.y = y; b.def = def; b.owner = owner;
    float r = angle * 0.0174533f;
    b.vx = std::cos(r) * def.speed; b.vy = std::sin(r) * def.speed;
    b.targetX = aimX; b.targetY = aimY;
    if (def.kind == 2) {  // ArtilleryBulletType: scaleLife, lands on the aim point
        float dx = aimX - x, dy = aimY - y;
        float dist = std::sqrt(dx * dx + dy * dy);
        b.def.lifetime = std::fmin(def.lifetime, dist / std::fmax(0.01f, def.speed));
    }
    for (auto& o : bullets) if (!o.alive) { o = b; return; }
    bullets.push_back(b);
}

void Game::damageUnit(int32_t idx, float amount) {
    Unit& u = units[idx];
    if (!u.alive) return;
    if (u.shield > 0) { float s = std::fmin(u.shield, amount); u.shield -= s; amount -= s; }
    u.health -= amount;
    u.hitTime = 1;
    if (u.health <= 0) {
        u.alive = false;
        addEffect(u.x, u.y, 0xffaa5f, u.def().hitSize, 25, 1);
        if (u.team == TeamCrux) unitsKilled++;
        if (idx == player) { player = -1; respawnTimer = 60; }
    }
}

void Game::damageBuilding(int32_t idx, float amount) {
    Building& b = builds[idx];
    if (!b.alive) return;
    b.health -= amount;
    if (b.health <= 0) removeBuilding(idx, true);
}

static void splash(Game& g, const Bullet& b) {
    const BulletDef& d = b.def;
    if (d.splashDamage <= 0 || d.splashRadius <= 0) return;
    // Damage.damage(team, x, y, radius, damage, air, ground)
    for (size_t i = 0; i < g.units.size(); i++) {
        Unit& u = g.units[i];
        if (!u.alive || u.team == b.team) continue;
        bool fly = u.def().flying;
        if ((fly && !d.collidesAir) || (!fly && !d.collidesGround)) continue;
        float dx = u.x - b.x, dy = u.y - b.y, dist = std::sqrt(dx * dx + dy * dy);
        if (dist > d.splashRadius + u.def().hitSize / 2) continue;
        float amount = d.splashDamage * lerp(1.f - dist / d.splashRadius, 1.f, 0.4f);
        g.damageUnit((int32_t)i, amount);
    }
    if (d.collidesGround) {
        int r = (int)(d.splashRadius / tilesize) + 1;
        int cx = (int)std::floor(b.x / tilesize + 0.5f), cy = (int)std::floor(b.y / tilesize + 0.5f);
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                int bi = g.buildIndexAt(x, y);
                if (bi < 0 || g.builds[bi].team == b.team) continue;
                float dx = x * tilesize - b.x, dy = y * tilesize - b.y, dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= d.splashRadius) g.damageBuilding(bi, d.splashDamage * d.buildingDamageMultiplier * 0.25f);
            }
    }
    g.addEffect(b.x, b.y, 0xffd37f, d.splashRadius, 18, 2);
}

void Game::updateBullets(float delta) {
    for (auto& b : bullets) {
        if (!b.alive) continue;
        const BulletDef& d = b.def;
        // homing (BulletType.homingPower) toward the closest enemy unit
        if (d.homingPower > 0) {
            float best = 50.f * 50.f; Unit* tgt = nullptr;
            for (auto& u : units) {
                if (!u.alive || u.team == b.team) continue;
                float dx = u.x - b.x, dy = u.y - b.y, d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; tgt = &u; }
            }
            if (tgt) {
                float ang = angleDeg(b.vx, b.vy);
                float want = angleDeg(tgt->x - b.x, tgt->y - b.y);
                ang = moveToward(ang, want, d.homingPower * delta * 50.f);
                float r = ang * 0.0174533f;
                b.vx = std::cos(r) * d.speed; b.vy = std::sin(r) * d.speed;
            }
        }
        b.x += b.vx * delta; b.y += b.vy * delta;
        b.time += delta;
        bool hit = false;
        if (d.kind == 2) {  // artillery only hits on landing
            if (b.time >= d.lifetime) { splash(*this, b); b.alive = false; }
            continue;
        }
        if (d.kind == 1) {  // FlakBulletType: explodeRange 30 against air units
            for (auto& u : units) {
                if (!u.alive || u.team == b.team || !u.def().flying) continue;
                float dx = u.x - b.x, dy = u.y - b.y;
                if (dx * dx + dy * dy < 30.f * 30.f) { hit = true; break; }
            }
            if (hit) { splash(*this, b); b.alive = false; continue; }
        }
        for (size_t i = 0; i < units.size() && !hit; i++) {
            Unit& u = units[i];
            if (!u.alive || u.team == b.team) continue;
            bool fly = u.def().flying;
            if ((fly && !d.collidesAir) || (!fly && !d.collidesGround)) continue;
            float r = u.def().hitSize / 2 + 3;
            float dx = u.x - b.x, dy = u.y - b.y;
            if (dx * dx + dy * dy < r * r) {
                damageUnit((int32_t)i, d.damage);
                splash(*this, b);
                hit = true;
            }
        }
        if (!hit && d.collidesGround) {
            int tx = (int)std::floor(b.x / tilesize + 0.5f), ty = (int)std::floor(b.y / tilesize + 0.5f);
            int bi = buildIndexAt(tx, ty);
            if (bi >= 0 && builds[bi].team != b.team) {
                damageBuilding(bi, d.damage * d.buildingDamageMultiplier);
                splash(*this, b);
                hit = true;
            }
        }
        if (hit) { addEffect(b.x, b.y, 0xffd37f, 3, 10, 0); b.alive = false; continue; }
        if (b.time >= d.lifetime) { splash(*this, b); b.alive = false; }
    }
}

// ---------------------------------------------------------------- units
static bool groundBlocked(Game& g, float x, float y, float half) {
    const float pts[4][2] = {{-half, -half}, {half, -half}, {-half, half}, {half, half}};
    for (auto& p : pts) {
        int tx = (int)std::floor((x + p[0]) / tilesize + 0.5f), ty = (int)std::floor((y + p[1]) / tilesize + 0.5f);
        if (g.solid(tx, ty) || g.isDeep(tx, ty)) return true;
    }
    return false;
}

static float buildX(const Building& b) { return b.tx * tilesize + (b.def().size % 2 == 0 ? tilesize / 2 : 0); }
static float buildY(const Building& b) { return b.ty * tilesize + (b.def().size % 2 == 0 ? tilesize / 2 : 0); }

void Game::updateUnits(float delta) {
    if (player < 0 || !units[player].alive) {
        player = -1;
        respawnTimer -= delta;
        Building* c = core(TeamSharded);
        if (c && respawnTimer <= 0 && spawnPlayer) {
            // CoreBlock.playerSpawn: the core produces its unit type (alpha for a shard)
            player = spawnUnit(c->block == B_core_foundation ? U_beta : U_alpha, TeamSharded, buildX(*c), buildY(*c));
            if (player >= 0) units[player].isPlayer = true;
        }
    }
    for (size_t i = 0; i < units.size(); i++) {
        Unit& u = units[i];
        if (!u.alive) continue;
        const UnitDef& def = u.def();
        if (u.hitTime > 0) u.hitTime -= delta / 9.f;
        if (u.isPlayer) continue;  // moved by updatePlayer()
        // --- targeting (AIController.findMainTarget / FlyingAI.findMainTarget)
        u.retarget -= delta;
        if (u.retarget <= 0) {
            u.retarget = 20;
            u.targetUnit = -1; u.targetBuild = -1;
            float range = def.range;
            float bestD = range * range; int best = -1;
            for (size_t k = 0; k < units.size(); k++) {
                Unit& o = units[k];
                if (!o.alive || o.team == u.team || o.team == TeamDerelict) continue;
                if (o.def().flying && !def.weapon.bullet.collidesAir) continue;
                float dx = o.x - u.x, dy = o.y - u.y, d2 = dx * dx + dy * dy;
                if (d2 < bestD) { bestD = d2; best = (int)k; }
            }
            u.targetUnit = best;
            if (best < 0) {
                float bd = 1e30f; int bb = -1;
                bool anywhere = false;
                if (def.flying) {
                    // flare: targetFlags = {generator, null}
                    for (size_t k = 0; k < builds.size(); k++) {
                        Building& b = builds[k];
                        if (!b.alive || b.team != TeamSharded || b.def().type != BType::ConsumeGen) continue;
                        float dx = buildX(b) - u.x, dy = buildY(b) - u.y, d2 = dx * dx + dy * dy;
                        if (d2 < bd) { bd = d2; bb = (int)k; anywhere = true; }
                    }
                }
                if (bb < 0) {
                    float r = range + 8;
                    int rr = (int)(r / tilesize) + 2;
                    int cx = (int)(u.x / tilesize), cy = (int)(u.y / tilesize);
                    for (int y = cy - rr; y <= cy + rr; y++)
                        for (int x = cx - rr; x <= cx + rr; x++) {
                            int bi = buildIndexAt(x, y);
                            if (bi < 0) continue;
                            Building& b = builds[bi];
                            if (b.team != TeamSharded) continue;
                            float dx = buildX(b) - u.x, dy = buildY(b) - u.y, d2 = dx * dx + dy * dy;
                            float rb = r + b.def().size * tilesize / 2;
                            // turrets are prioritised (TargetPriority.turret)
                            if (b.def().type == BType::ItemTurret) d2 *= 0.5f;
                            if (d2 < rb * rb && d2 < bd) { bd = d2; bb = bi; }
                        }
                }
                if (bb < 0 || (def.flying && !anywhere && bb < 0)) {
                    Building* c = closestCore(TeamSharded, u.x, u.y);
                    if (c && def.flying) bb = c->id;
                }
                u.targetBuild = bb;
            }
        }
        float tgx = 0, tgy = 0; bool hasTarget = false;
        if (u.targetUnit >= 0 && units[u.targetUnit].alive) { tgx = units[u.targetUnit].x; tgy = units[u.targetUnit].y; hasTarget = true; }
        else if (u.targetBuild >= 0 && builds[u.targetBuild].alive) { tgx = buildX(builds[u.targetBuild]); tgy = buildY(builds[u.targetBuild]); hasTarget = true; }
        else { u.targetUnit = u.targetBuild = -1; }

        // --- movement
        float mx = 0, my = 0;
        if (def.flying) {
            // FlyingAI.moveTo(target, range * 0.8)
            float gx = tgx, gy = tgy;
            if (!hasTarget) { Building* c = closestCore(TeamSharded, u.x, u.y); if (c) { gx = buildX(*c); gy = buildY(*c); hasTarget = true; tgx = gx; tgy = gy; } }
            if (hasTarget) {
                float dx = gx - u.x, dy = gy - u.y, dist = std::sqrt(dx * dx + dy * dy);
                float stop = def.range * 0.8f;
                if (dist > stop) { mx = dx / dist; my = dy / dist; }
            }
            float ax = mx * def.speed, ay = my * def.speed;
            u.vx = lerp(u.vx, ax, std::fmin(1.f, def.accel * delta));
            u.vy = lerp(u.vy, ay, std::fmin(1.f, def.accel * delta));
            u.x += u.vx * delta; u.y += u.vy * delta;
        } else {
            // GroundAI: follow the flow field unless the core is within range / 1.3
            Building* c = closestCore(TeamSharded, u.x, u.y);
            bool nearCore = false;
            if (c) {
                float dx = buildX(*c) - u.x, dy = buildY(*c) - u.y;
                float r = def.range / 1.3f + c->def().size * tilesize / 2;
                nearCore = dx * dx + dy * dy < r * r;
                if (nearCore) { tgx = buildX(*c); tgy = buildY(*c); hasTarget = true; u.targetBuild = c->id; }
            }
            int tx = (int)std::floor(u.x / tilesize + 0.5f), ty = (int)std::floor(u.y / tilesize + 0.5f);
            if (!nearCore && !flowCost.empty() && tx >= 0 && ty >= 0 && tx < width && ty < height) {
                int cur = flowCost[ty * width + tx];
                int bx = tx, by = ty, bc = cur < 0 ? 1 << 30 : cur;
                for (int k = 0; k < 8; k++) {
                    int nx = tx + d8x[k], ny = ty + d8y[k];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    int fc = flowCost[ny * width + nx];
                    if (fc < 0) continue;
                    if (k >= 4) {  // no diagonal corner cutting
                        if (flowCost[ty * width + nx] < 0 || flowCost[ny * width + tx] < 0) continue;
                    }
                    if (fc < bc) { bc = fc; bx = nx; by = ny; }
                }
                float gx = bx * tilesize - u.x, gy = by * tilesize - u.y;
                float dist = std::sqrt(gx * gx + gy * gy);
                if (dist > 0.5f) { mx = gx / dist; my = gy / dist; }
            }
            Tile* ft = tile(tx, ty);
            float sm = ft ? kBlocks[ft->floor].speedMultiplier : 1.f;
            float vx = mx * def.speed * sm * delta, vy = my * def.speed * sm * delta;
            float half = def.hitSize * 0.33f;
            bool movedX = false, movedY = false;
            if (vx != 0 && !groundBlocked(*this, u.x + vx, u.y, half)) { u.x += vx; movedX = true; }
            if (vy != 0 && !groundBlocked(*this, u.x, u.y + vy, half)) { u.y += vy; movedY = true; }
            if ((vx != 0 || vy != 0) && !movedX && !movedY) {
                // wedged on a wall corner: re-centre on the current tile (EntityCollisions slides along walls)
                float step = def.speed * sm * delta;
                float cxw = tx * tilesize, cyw = ty * tilesize;
                float nx = approach(u.x, cxw, step), ny = approach(u.y, cyw, step);
                if (!groundBlocked(*this, nx, u.y, half)) u.x = nx;
                if (!groundBlocked(*this, u.x, ny, half)) u.y = ny;
            } else if (mx != 0 && my == 0 && !movedX) {
                u.y = approach(u.y, ty * tilesize, def.speed * sm * delta);
            } else if (my != 0 && mx == 0 && !movedY) {
                u.x = approach(u.x, tx * tilesize, def.speed * sm * delta);
            }
            if (mx != 0 || my != 0) u.walkTime += delta * def.speed;
            // blocked by a player building: attack it (bullets collide with buildings)
            if (!hasTarget) {
                int ax = (int)std::floor((u.x + mx * tilesize) / tilesize + 0.5f), ay = (int)std::floor((u.y + my * tilesize) / tilesize + 0.5f);
                int bi = buildIndexAt(ax, ay);
                if (bi >= 0 && builds[bi].team == TeamSharded) { u.targetBuild = bi; tgx = buildX(builds[bi]); tgy = buildY(builds[bi]); hasTarget = true; }
            }
        }
        // --- rotation & weapons
        float faceAngle = hasTarget ? angleDeg(tgx - u.x, tgy - u.y) : (mx != 0 || my != 0) ? angleDeg(mx, my) : u.rotation;
        u.rotation = moveToward(u.rotation, faceAngle, def.rotateSpeed * delta);
        float period = def.weapon.mirror ? def.weapon.reload / 2 : def.weapon.reload;
        u.reload[0] -= delta;
        if (hasTarget && u.reload[0] <= 0) {
            float dx = tgx - u.x, dy = tgy - u.y;
            float r = def.range + 8;
            if (dx * dx + dy * dy <= r * r && angleDist(u.rotation, faceAngle) < 25) {
                float side = def.weapon.mirror ? (u.side ? 1.f : -1.f) : 0.f;
                u.side ^= 1;
                float rad = (u.rotation - 90) * 0.0174533f;
                float ox = def.weapon.x * side, oy = def.weapon.y;
                float sx = u.x + ox * std::cos(rad) - oy * std::sin(rad);
                float sy = u.y + ox * std::sin(rad) + oy * std::cos(rad);
                createBullet(def.weapon.bullet, u.team, sx, sy, angleDeg(tgx - sx, tgy - sy), (int32_t)i, tgx, tgy);
                u.reload[0] = period;
            }
        }
    }
    // separation (unit physics, same layer only)
    for (size_t i = 0; i < units.size(); i++) {
        Unit& a = units[i];
        if (!a.alive) continue;
        for (size_t j = i + 1; j < units.size(); j++) {
            Unit& b = units[j];
            if (!b.alive || a.def().flying != b.def().flying) continue;
            float dx = b.x - a.x, dy = b.y - a.y, d2 = dx * dx + dy * dy;
            float r = (a.def().hitSize + b.def().hitSize) * 0.5f;
            if (d2 < r * r && d2 > 0.0001f) {
                float d = std::sqrt(d2), push = (r - d) * 0.25f;
                float px = dx / d * push, py = dy / d * push;
                if (!a.isPlayer && (a.def().flying || !groundBlocked(*this, a.x - px, a.y - py, a.def().hitSize * 0.33f))) { a.x -= px; a.y -= py; }
                if (!b.isPlayer && (b.def().flying || !groundBlocked(*this, b.x + px, b.y + py, b.def().hitSize * 0.33f))) { b.x += px; b.y += py; }
            }
        }
    }
}

// ---------------------------------------------------------------- player
bool Game::canMine(const Unit& u, int tileIdx) {
    if (tileIdx < 0 || tileIdx >= width * height) return false;
    Tile& t = tiles[tileIdx];
    if (t.build >= 0 || (t.env && kBlocks[t.env].type == BType::StaticWall)) return false;
    int item = -1;
    if (t.ore && kBlocks[t.ore].itemDrop >= 0) item = kBlocks[t.ore].itemDrop;
    else if (kBlocks[t.floor].itemDrop >= 0 && kBlocks[t.floor].itemDrop != I_sand) item = kBlocks[t.floor].itemDrop;  // playerUnmineable
    if (item < 0) return false;
    return u.def().mineTier >= kItems[item].hardness;
}

void Game::updateMining(Unit& u, float delta) {
    if (u.mineTile < 0) return;
    float mx = (u.mineTile % width) * tilesize, my = (u.mineTile / width) * tilesize;
    float dx = mx - u.x, dy = my - u.y;
    if (!canMine(u, u.mineTile) || dx * dx + dy * dy > mineRange * mineRange) { u.mineTile = -1; u.mineTimer = 0; return; }
    Tile& t = tiles[u.mineTile];
    int item = (t.ore && kBlocks[t.ore].itemDrop >= 0) ? kBlocks[t.ore].itemDrop : kBlocks[t.floor].itemDrop;
    u.mineTimer += delta * u.def().mineSpeed;
    if (u.mineTimer > 50.f + kItems[item].hardness * 15.f) {
        u.mineTimer = 0;
        Building* c = closestCore(u.team, u.x, u.y);
        float cx = c ? buildX(*c) - u.x : 1e9f, cy = c ? buildY(*c) - u.y : 1e9f;
        if (c && cx * cx + cy * cy <= mineTransferRange * mineTransferRange) {
            handleItem(*c, *c, item);  // offloadImmediately for player units
            addEffect(mx, my, kItems[item].color, 2, 12, 0);
        } else if ((u.stackItem == item || u.stackItem < 0) && u.stackAmount < u.def().itemCapacity) {
            u.stackItem = (int16_t)item; u.stackAmount++;
        } else {
            u.mineTile = -1;
        }
    }
}

bool Game::queuePlan(int tx, int ty, int16_t block, uint8_t rotation, bool breaking) {
    for (auto it = plans.begin(); it != plans.end(); ++it)
        if (it->tx == tx && it->ty == ty) { plans.erase(it); break; }
    if (breaking) {
        int bi = buildIndexAt(tx, ty);
        if (bi < 0 || builds[bi].team != TeamSharded || builds[bi].def().type == BType::Core) return false;
        plans.push_back({builds[bi].tx, builds[bi].ty, builds[bi].block, builds[bi].rotation, true});
        return true;
    }
    if (block <= 0 || !kBlocks[block].buildable) return false;
    // Same block already there (built or under construction): Block.quickRotate changes the
    // rotation in place, free of charge, keeping items and construction progress.
    Building* ex = buildAt(tx, ty);
    if (ex && ex->block == block && ex->tx == tx && ex->ty == ty) {
        if (kBlocks[block].rotate && ex->rotation != rotation) { ex->rotation = rotation; refreshAround(ex->id); }
        if (ex->constructing && !ex->deconstructing) plans.push_back({tx, ty, block, rotation, false});
        return true;
    }
    if (!validPlace(block, tx, ty, rotation, TeamSharded)) return false;
    plans.push_back({tx, ty, block, rotation, false});
    return true;
}

// BuilderComp.update + ConstructBlock.construct / deconstruct
void Game::updateBuilder(Unit& u, float delta) {
    if (plans.empty()) return;
    Building* c = core(u.team);
    if (!c) return;
    // pick the first plan in range
    size_t pi = plans.size();
    for (size_t i = 0; i < plans.size(); i++) {
        float dx = plans[i].tx * tilesize - u.x, dy = plans[i].ty * tilesize - u.y;
        if (dx * dx + dy * dy <= buildRange * buildRange) { pi = i; break; }
    }
    if (pi == plans.size()) return;
    BuildPlan p = plans[pi];
    int bi = buildIndexAt(p.tx, p.ty);
    if (p.breaking) {
        if (bi < 0 || builds[bi].team != u.team) { plans.erase(plans.begin() + pi); return; }
        Building& b = builds[bi];
        if (!b.constructing) {
            b.constructing = true; b.deconstructing = true; b.buildProgress = 1.f;
            b.constructBlock = b.block;
            for (int i = 0; i < 4; i++) { b.accumulator[i] = 0; b.totalAcc[i] = 0; }
            powerGraphsDirty = true; refreshAround(bi);
        }
        const BlockDef& d = kBlocks[b.constructBlock];
        float bs = 1.f / std::fmax(1.f, d.buildCost) * u.def().buildSpeed * delta;
        float clamped = std::fmin(bs, b.buildProgress);
        for (int i = 0; i < d.nreq; i++) {
            float req = (float)d.req[i].amount;
            b.accumulator[i] += std::fmin(clamped * deconstructRefund * req, deconstructRefund * req - b.totalAcc[i]);
            b.totalAcc[i] = std::fmin(b.totalAcc[i] + req * clamped * deconstructRefund, req);
            int acc = (int)b.accumulator[i];
            if (acc > 0) { c->items.add(d.req[i].item, acc); b.accumulator[i] -= acc; }
        }
        b.buildProgress = std::clamp(b.buildProgress - bs, 0.f, 1.f);
        if (b.buildProgress <= 0) {
            plans.erase(plans.begin() + pi);
            removeBuilding(bi, false);
        }
        return;
    }
    // start construction on a free tile, or replace (Block.canReplace: conveyor upgrade)
    if (bi < 0 || (builds[bi].block != p.block && !builds[bi].constructing)) {
        if (!validPlace(p.block, p.tx, p.ty, p.rotation, u.team)) { plans.erase(plans.begin() + pi); return; }
        bi = addBuilding(p.block, p.tx, p.ty, u.team, p.rotation);
        Building& nb = builds[bi];
        nb.constructing = true; nb.buildProgress = 0; nb.constructBlock = p.block;
        nb.health = 10;
        powerGraphsDirty = true;
        refreshAround(bi);
    }
    Building& b = builds[bi];
    if (!b.constructing || b.block != p.block) {
        plans.erase(plans.begin() + pi);
        return;
    }
    const BlockDef& d = b.def();
    float amount = 1.f / std::fmax(1.f, d.buildCost) * u.def().buildSpeed * delta;
    // checkRequired(core.items, amount, remove)
    auto checkRequired = [&](float maxProgress, bool remove) {
        for (int i = 0; i < d.nreq; i++) {
            int required = (int)b.accumulator[i];
            int have = c->items.amount[d.req[i].item];
            if (have == 0 && d.req[i].amount != 0) maxProgress = 0;
            else if (required > 0) {
                int maxUse = std::min(required, have);
                float fraction = maxUse / (float)required;
                maxProgress = std::fmin(maxProgress, maxProgress * fraction);
                b.accumulator[i] -= maxUse;
                if (remove) c->items.remove(d.req[i].item, maxUse);
            }
        }
        return maxProgress;
    };
    float maxProgress = checkRequired(amount, false);
    for (int i = 0; i < d.nreq; i++) {
        float req = (float)d.req[i].amount;
        b.accumulator[i] += std::fmin(req * maxProgress, req - b.totalAcc[i] + 0.00001f);
        b.totalAcc[i] = std::fmin(b.totalAcc[i] + req * maxProgress, req);
    }
    maxProgress = checkRequired(maxProgress, true);
    b.buildProgress = std::clamp(b.buildProgress + maxProgress, 0.f, 1.f);
    b.health = std::fmax(b.health, d.health * b.buildProgress);
    if (b.buildProgress >= 1.f) {
        b.constructing = false;
        b.health = d.health;
        plans.erase(plans.begin() + pi);
        refreshAround(bi);
        if (d.type == BType::Bridge || d.type == BType::LiquidBridge) {
            // ItemBridge.lastBuild: link the previous bridge to this one when aligned and in range
            if (lastBridge >= 0 && lastBridge != bi && builds[lastBridge].alive && builds[lastBridge].block == b.block) {
                Building& lb = builds[lastBridge];
                int dx = b.tx - lb.tx, dy = b.ty - lb.ty;
                if ((dx == 0) != (dy == 0) && std::abs(dx + dy) <= (int)d.range && lb.link < 0)
                    lb.link = b.ty * width + b.tx;
            }
            lastBridge = bi;
        }
        autoLinkPower(bi);
        powerGraphsDirty = true;
    }
}

void Game::updatePlayer(float delta, float moveX, float moveY) {
    if (player < 0 || !units[player].alive) return;
    Unit& u = units[player];
    const UnitDef& def = u.def();
    float len = std::sqrt(moveX * moveX + moveY * moveY);
    if (len > 1) { moveX /= len; moveY /= len; }
    // UnitComp.moveAt with accel / drag (flying core unit)
    float tx = moveX * def.speed, ty = moveY * def.speed;
    u.vx = lerp(u.vx, tx, std::fmin(1.f, def.accel * delta));
    u.vy = lerp(u.vy, ty, std::fmin(1.f, def.accel * delta));
    u.x = std::clamp(u.x + u.vx * delta, 0.f, (width - 1) * tilesize);
    u.y = std::clamp(u.y + u.vy * delta, 0.f, (height - 1) * tilesize);
    if (len > 0.1f) u.rotation = moveToward(u.rotation, angleDeg(moveX, moveY), def.rotateSpeed * delta);
    updateBuilder(u, delta);
    updateMining(u, delta);
    // drop a carried stack into the core when close (InputHandler.transferInventory)
    if (u.stackAmount > 0) {
        Building* c = closestCore(u.team, u.x, u.y);
        if (c) {
            float dx = buildX(*c) - u.x, dy = buildY(*c) - u.y;
            if (dx * dx + dy * dy < mineTransferRange * mineTransferRange) {
                for (int k = 0; k < u.stackAmount; k++) handleItem(*c, *c, u.stackItem);
                u.stackAmount = 0; u.stackItem = -1;
            }
        }
    }
    // auto-target for the player weapon (mobile style auto-aim)
    u.reload[0] -= delta;
    if (u.reload[0] <= 0 && plans.empty()) {
        float r = def.range; float bd = r * r; int best = -1;
        for (size_t k = 0; k < units.size(); k++) {
            Unit& o = units[k];
            if (!o.alive || o.team != TeamCrux) continue;
            float dx = o.x - u.x, dy = o.y - u.y, d2 = dx * dx + dy * dy;
            if (d2 < bd) { bd = d2; best = (int)k; }
        }
        if (best >= 0) {
            Unit& o = units[best];
            createBullet(def.weapon.bullet, u.team, u.x, u.y, angleDeg(o.x - u.x, o.y - u.y), player, o.x, o.y);
            u.reload[0] = def.weapon.mirror ? def.weapon.reload / 2 : def.weapon.reload;
        }
    }
}

}  // namespace mdt

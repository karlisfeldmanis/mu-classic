#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "PathFinder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// ─── Constructor / Destructor ────────────────────────────────────────────────

GameWorld::GameWorld() : m_pathFinder(std::make_unique<PathFinder>()) {}

GameWorld::~GameWorld() = default;

// ─── Static monster type definitions (OpenMU Version075) ─────────────────────

static const MonsterTypeDef s_monsterDefs[] = {
    // type hp    def  defR atkMn atkMx atkR lvl  atkCD  mvDel mR vR aR aggro
    // All values from OpenMU Version075 (canonical reference)
    // --- Lorencia monsters ---
    {0, 100, 6, 6, 16, 20, 28, 6, 1.6f, 0.4f, 3, 5, 1, true}, // Bull Fighter
    {1, 140, 9, 9, 22, 27, 39, 9, 1.6f, 0.4f, 3, 5, 1, true}, // Hound
    {2, 60, 3, 3, 10, 13, 18, 4, 2.0f, 0.4f, 3, 4, 1, true},  // Budge Dragon
    {3, 30, 1, 1, 4, 7, 8, 2, 1.8f, 0.6f, 2, 5, 1, true},     // Spider (slower)
    {4, 190, 12, 12, 31, 36, 50, 12, 1.4f, 0.4f, 3, 4, 1,
     true}, // Elite Bull Fighter
    {6, 255, 14, 14, 41, 46, 62, 14, 2.0f, 0.4f, 3, 7, 4, true}, // Lich
    {7, 400, 18, 18, 57, 62, 80, 17, 2.2f, 0.4f, 2, 3, 2, true}, // Giant
    {14, 525, 22, 22, 68, 74, 93, 19, 1.4f, 0.4f, 2, 4, 1,
     true}, // Skeleton Warrior
    {15, 600, 25, 25, 72, 78, 100, 22, 1.6f, 0.4f, 2, 5, 4,
     true}, // Skeleton Archer (ranged)
    {16, 800, 30, 30, 85, 92, 115, 25, 1.4f, 0.4f, 2, 4, 1,
     true}, // Skeleton Captain
    // --- Dungeon monsters ---
    {5, 1400, 55, 55, 125, 130, 165, 38, 1.4f, 0.4f, 3, 5, 1, true},  // Hell Hound
    {8, 2500, 75, 75, 145, 150, 190, 46, 1.6f, 0.4f, 2, 4, 1, true},  // Poison Bull
    {9, 2000, 70, 70, 140, 145, 180, 44, 1.8f, 0.4f, 2, 7, 4, true},  // Thunder Lich (ranged)
    {10, 3000, 80, 80, 150, 155, 200, 48, 1.4f, 0.4f, 2, 4, 1, true}, // Dark Knight
    {11, 1000, 40, 40, 110, 115, 145, 32, 1.6f, 0.4f, 3, 5, 1, true}, // Ghost
    {12, 750, 31, 31, 90, 95, 120, 25, 1.8f, 0.4f, 3, 4, 1, true},    // Larva
    {13, 1600, 60, 60, 130, 135, 170, 40, 1.6f, 0.4f, 2, 7, 4, true}, // Hell Spider (ranged)
    {17, 850, 35, 35, 100, 105, 130, 28, 2.0f, 0.4f, 2, 3, 2, true},  // Cyclops
    {18, 6000, 100, 100, 165, 175, 220, 55, 1.2f, 0.4f, 2, 5, 1, true}, // Gorgon
    // --- Devias monsters ---
    {19, 900, 37, 37, 105, 110, 150, 30, 1.6f, 0.4f, 3, 5, 1, true},    // Yeti
    {20, 1200, 50, 50, 120, 125, 180, 36, 1.4f, 0.4f, 3, 5, 1, true},   // Elite Yeti
    {21, 800, 33, 33, 95, 100, 130, 26, 1.4f, 0.4f, 3, 5, 1, true},     // Assassin
    {22, 650, 27, 27, 80, 85, 110, 22, 1.8f, 0.4f, 3, 5, 1, true},      // Ice Monster
    {23, 700, 29, 29, 85, 90, 120, 24, 1.6f, 0.4f, 3, 5, 1, true},      // Hommerd
    {24, 600, 25, 25, 75, 80, 100, 20, 1.8f, 0.4f, 3, 4, 1, true},      // Worm
    {25, 4000, 90, 90, 155, 165, 260, 52, 1.2f, 0.4f, 2, 5, 4, true},   // Ice Queen
};
static constexpr int NUM_MONSTER_DEFS =
    sizeof(s_monsterDefs) / sizeof(s_monsterDefs[0]);

// World-space melee range threshold (squared) — 1.5 grid cells.
// Melee monsters (attackRange <= 1) must be within this distance to attack.
static constexpr float MELEE_ATTACK_DIST_SQ = 200.0f * 200.0f;

static float WorldDistSq(const MonsterInstance &mon,
                         const GameWorld::PlayerTarget &target) {
  float dx = mon.worldX - target.worldX;
  float dz = mon.worldZ - target.worldZ;
  return dx * dx + dz * dz;
}

const MonsterTypeDef *GameWorld::FindMonsterTypeDef(uint16_t type) {
  for (int i = 0; i < NUM_MONSTER_DEFS; i++) {
    if (s_monsterDefs[i].type == type)
      return &s_monsterDefs[i];
  }
  return nullptr;
}

// ─── Terrain attribute loading (same decrypt as client TerrainParser) ─────

static const uint8_t MAP_XOR_KEY[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                        0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                        0x37, 0xB3, 0xE7, 0xA2};

static std::vector<uint8_t> DecryptMapFile(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> out(data.size());
  uint8_t wKey = 0x5E;
  for (size_t i = 0; i < data.size(); i++) {
    uint8_t src = data[i];
    out[i] = (src ^ MAP_XOR_KEY[i % 16]) - wKey;
    wKey = src + 0x3D;
  }
  return out;
}

static bool ParseTerrainAttributeFile(const std::string &attFilePath,
                                      std::vector<uint8_t> &outAttributes) {
  std::ifstream file(attFilePath, std::ios::binary);
  if (!file) {
    printf("[World] Cannot open terrain attribute file: %s\n",
           attFilePath.c_str());
    return false;
  }

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

  std::vector<uint8_t> data = DecryptMapFile(raw);

  static const uint8_t bux[3] = {0xFC, 0xCF, 0xAB};
  for (size_t i = 0; i < data.size(); i++)
    data[i] ^= bux[i % 3];

  const size_t cells = GameWorld::TERRAIN_SIZE * GameWorld::TERRAIN_SIZE;
  outAttributes.resize(cells, 0);

  const size_t wordSize = 4 + cells * 2;
  const size_t byteSize = 4 + cells;

  if (data.size() >= wordSize) {
    for (size_t i = 0; i < cells; i++)
      outAttributes[i] = data[4 + i * 2];
    printf("[World] Loaded terrain attributes (WORD format, %zu bytes) from %s\n",
           data.size(), attFilePath.c_str());
  } else if (data.size() >= byteSize) {
    for (size_t i = 0; i < cells; i++)
      outAttributes[i] = data[4 + i];
    printf("[World] Loaded terrain attributes (BYTE format, %zu bytes) from %s\n",
           data.size(), attFilePath.c_str());
  } else {
    printf("[World] Terrain attribute file too small: %zu bytes\n",
           data.size());
    return false;
  }

  int blocked = 0, safeZone = 0;
  for (size_t i = 0; i < cells; i++) {
    if (outAttributes[i] & GameWorld::TW_NOMOVE)
      blocked++;
    if (outAttributes[i] & GameWorld::TW_SAFEZONE)
      safeZone++;
  }
  printf("[World] Terrain: %d blocked cells, %d safe zone cells, %zu total\n",
         blocked, safeZone, cells);
  return true;
}

bool GameWorld::LoadTerrainAttributes(const std::string &attFilePath) {
  return ParseTerrainAttributeFile(attFilePath, m_terrainAttributes);
}

bool GameWorld::LoadTerrainAttributesForMap(uint8_t mapId,
                                            const std::string &attFilePath) {
  auto &attrs = m_mapTerrainAttributes[mapId];
  bool ok = ParseTerrainAttributeFile(attFilePath, attrs);
  if (ok)
    printf("[World] Stored terrain attributes for map %d\n", mapId);
  return ok;
}

void GameWorld::SetActiveMap(uint8_t mapId) {
  auto it = m_mapTerrainAttributes.find(mapId);
  if (it != m_mapTerrainAttributes.end()) {
    m_terrainAttributes = it->second;
    m_activeMapId = mapId;
    // Rebuild pathfinder with new terrain
    m_pathFinder = std::make_unique<PathFinder>();
    rebuildOccupancyGrid();
    printf("[World] Switched active map to %d\n", mapId);
  } else {
    printf("[World] WARNING: No terrain attributes for map %d\n", mapId);
  }
}

void GameWorld::ClearWorldData() {
  m_npcs.clear();
  m_monsterInstances.clear();
  m_drops.clear();
  m_nextMonsterIndex = 2001;
  m_nextDropIndex = 1;
  std::memset(m_monsterOccupancy, 0, sizeof(m_monsterOccupancy));
  printf("[World] Cleared all NPCs, monsters, and drops\n");
}

bool GameWorld::IsWalkable(float worldX, float worldZ) const {
  if (m_terrainAttributes.empty())
    return true;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
    return false;
  return (m_terrainAttributes[gz * TERRAIN_SIZE + gx] & (TW_NOMOVE | TW_NOGROUND)) == 0;
}

bool GameWorld::IsSafeZone(float worldX, float worldZ) const {
  if (m_terrainAttributes.empty())
    return false;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
    return false;
  uint8_t attr = m_terrainAttributes[gz * TERRAIN_SIZE + gx];
  return (attr & TW_SAFEZONE) != 0;
}

bool GameWorld::IsWalkableGrid(uint8_t gx, uint8_t gy) const {
  if (m_terrainAttributes.empty())
    return true;
  return (m_terrainAttributes[gy * TERRAIN_SIZE + gx] & (TW_NOMOVE | TW_NOGROUND)) == 0;
}

bool GameWorld::IsSafeZoneGrid(uint8_t gx, uint8_t gy) const {
  if (m_terrainAttributes.empty())
    return false;
  uint8_t attr = m_terrainAttributes[gy * TERRAIN_SIZE + gx];
  return (attr & TW_SAFEZONE) != 0;
}

// Guard patrol still uses tryMove (will be refactored separately)
bool GameWorld::tryMove(float &x, float &z, float sX, float sZ) const {
  if (std::abs(sX) < 0.001f && std::abs(sZ) < 0.001f)
    return true;

  if (IsWalkable(x + sX, z + sZ)) {
    x += sX;
    z += sZ;
    return true;
  }
  if (std::abs(sX) > 0.01f && IsWalkable(x + sX, z)) {
    x += sX;
    return true;
  }
  if (std::abs(sZ) > 0.01f && IsWalkable(x, z + sZ)) {
    z += sZ;
    return true;
  }
  float dist = std::sqrt(sX * sX + sZ * sZ);
  float angle = std::atan2(sZ, sX);
  for (float offset : {0.785f, -0.785f}) {
    float probeX = std::cos(angle + offset) * dist;
    float probeZ = std::sin(angle + offset) * dist;
    if (IsWalkable(x + probeX, z + probeZ)) {
      x += probeX;
      z += probeZ;
      return true;
    }
  }
  return false;
}

// ─── Monster Occupancy Grid ──────────────────────────────────────────────────

void GameWorld::setOccupied(uint8_t gx, uint8_t gy, bool val) {
  m_monsterOccupancy[gy * TERRAIN_SIZE + gx] = val;
}

bool GameWorld::isOccupied(uint8_t gx, uint8_t gy) const {
  return m_monsterOccupancy[gy * TERRAIN_SIZE + gx];
}

void GameWorld::rebuildOccupancyGrid() {
  std::memset(m_monsterOccupancy, 0, sizeof(m_monsterOccupancy));
  for (const auto &mon : m_monsterInstances) {
    if (mon.aiState != MonsterInstance::AIState::DYING &&
        mon.aiState != MonsterInstance::AIState::DEAD) {
      setOccupied(mon.gridX, mon.gridY, true);
    }
  }
}

// ─── Direction from grid delta ───────────────────────────────────────────────
// Maps grid movement direction to MU 0-7 facing.
// Derived from: angle = atan2(gridDX, gridDY) in the MU world coordinate system
// where worldX = gridY*100, worldZ = gridX*100.

uint8_t GameWorld::dirFromDelta(int dx, int dy) {
  if (dx == 0 && dy == 0)
    return 0;
  int sx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
  int sy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
  // Row=sx+1, Col=sy+1 → direction
  static const uint8_t DIR_TABLE[3][3] = {
      {5, 6, 7}, // dx=-1: NW, N, NE
      {4, 0, 0}, // dx= 0: W,  -, E
      {3, 2, 1}, // dx=+1: SW, S, SE
  };
  return DIR_TABLE[sx + 1][sy + 1];
}

// ─── Broadcast dedup (emit only when target/state changes) ───────────────────

void GameWorld::emitMoveIfChanged(MonsterInstance &mon, uint8_t targetX,
                                  uint8_t targetY, bool chasing, bool moving,
                                  std::vector<MonsterMoveUpdate> &outMoves) {
  if (targetX != mon.lastBroadcastTargetX ||
      targetY != mon.lastBroadcastTargetY ||
      chasing != mon.lastBroadcastChasing ||
      moving != mon.lastBroadcastIsMoving) {
    mon.lastBroadcastTargetX = targetX;
    mon.lastBroadcastTargetY = targetY;
    mon.lastBroadcastChasing = chasing;
    mon.lastBroadcastIsMoving = moving;
    outMoves.push_back(
        {mon.index, targetX, targetY, static_cast<uint8_t>(chasing ? 1 : 0)});
  }
}

// ─── NPC Loading ─────────────────────────────────────────────────────────────

void GameWorld::LoadNpcsFromDB(Database &db, uint8_t mapId) {
  auto spawns = db.GetNpcSpawns(mapId);

  uint16_t nextIndex = 1001;
  for (auto &s : spawns) {
    NpcSpawn npc;
    npc.index = nextIndex++;
    npc.type = s.type;
    npc.x = s.posX;
    npc.y = s.posY;
    npc.dir = s.direction;
    npc.name = s.name;

    if (npc.type >= 245 && npc.type <= 249) {
      npc.isGuard = true;
      npc.worldX = ((float)npc.y + 0.5f) * 100.0f;
      npc.worldZ = ((float)npc.x + 0.5f) * 100.0f;
      npc.spawnX = npc.worldX;
      npc.spawnZ = npc.worldZ;
      npc.lastBroadcastX = npc.x;
      npc.lastBroadcastY = npc.y;
    }

    m_npcs.push_back(npc);
    printf("[World] NPC #%d: type=%d pos=(%d,%d) dir=%d %s%s\n", npc.index,
           npc.type, npc.x, npc.y, npc.dir, npc.name.c_str(),
           npc.isGuard ? " [GUARD]" : "");
  }

  printf("[World] Loaded %zu NPCs for map %d from database\n", m_npcs.size(),
         mapId);
}

std::vector<uint8_t> GameWorld::BuildNpcViewportPacket() const {
  size_t npcSize = sizeof(PMSG_VIEWPORT_NPC);
  size_t totalSize = sizeof(PMSG_VIEWPORT_HEAD) + m_npcs.size() * npcSize;

  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PMSG_VIEWPORT_HEAD *>(packet.data());
  head->h = MakeC2Header(static_cast<uint16_t>(totalSize), 0x13);
  head->count = static_cast<uint8_t>(m_npcs.size());

  auto *entries = reinterpret_cast<PMSG_VIEWPORT_NPC *>(
      packet.data() + sizeof(PMSG_VIEWPORT_HEAD));
  for (size_t i = 0; i < m_npcs.size(); i++) {
    const auto &npc = m_npcs[i];
    auto &e = entries[i];
    e.indexH = 0x80 | static_cast<uint8_t>(npc.index >> 8);
    e.indexL = static_cast<uint8_t>(npc.index & 0xFF);
    e.typeH = static_cast<uint8_t>(npc.type >> 8);
    e.typeL = static_cast<uint8_t>(npc.type & 0xFF);
    e.x = npc.x;
    e.y = npc.y;
    e.tx = npc.x;
    e.ty = npc.y;
    e.dirAndPk = static_cast<uint8_t>(npc.dir << 4);
  }

  return packet;
}

// ─── Monster Loading (uses MonsterTypeDef lookup table) ──────────────────────

void GameWorld::LoadMonstersFromDB(Database &db, uint8_t mapId) {
  auto spawns = db.GetMonsterSpawns(mapId);

  for (auto &s : spawns) {
    MonsterInstance mon{};
    mon.index = m_nextMonsterIndex++;
    mon.type = s.type;
    mon.gridX = s.posX;
    mon.gridY = s.posY;
    mon.spawnGridX = s.posX;
    mon.spawnGridY = s.posY;
    mon.dir = s.direction;
    mon.worldX = s.posY * 100.0f; // MU grid Y → world X
    mon.worldZ = s.posX * 100.0f; // MU grid X → world Z
    mon.spawnX = mon.worldX;
    mon.spawnZ = mon.worldZ;
    mon.aiState = MonsterInstance::AIState::IDLE;

    const MonsterTypeDef *def = FindMonsterTypeDef(mon.type);
    if (def) {
      mon.hp = def->hp;
      mon.maxHp = def->hp;
      mon.defense = def->defense;
      mon.defenseRate = def->defenseRate;
      mon.attackMin = def->attackMin;
      mon.attackMax = def->attackMax;
      mon.attackRate = def->attackRate;
      mon.level = def->level;
      mon.atkCooldownTime = def->atkCooldown;
      mon.moveDelay = def->moveDelay;
      mon.moveRange = def->moveRange;
      mon.viewRange = def->viewRange;
      mon.attackRange = def->attackRange;
      mon.aggressive = def->aggressive;
    } else {
      // Fallback for unknown types
      mon.hp = 30;
      mon.maxHp = 30;
      mon.defense = 3;
      mon.defenseRate = 3;
      mon.attackMin = 4;
      mon.attackMax = 7;
      mon.attackRate = 8;
      mon.level = 4;
    }

    // Validate spawn position — if on blocked/void tile, relocate to nearest walkable cell
    if (!IsWalkableGrid(mon.gridX, mon.gridY)) {
      bool relocated = false;
      for (int radius = 1; radius <= 10 && !relocated; radius++) {
        for (int dy = -radius; dy <= radius && !relocated; dy++) {
          for (int dx = -radius; dx <= radius && !relocated; dx++) {
            if (std::abs(dx) != radius && std::abs(dy) != radius)
              continue; // Only check perimeter of current radius ring
            int nx = (int)mon.gridX + dx;
            int ny = (int)mon.gridY + dy;
            if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
              continue;
            if (IsWalkableGrid((uint8_t)nx, (uint8_t)ny) &&
                !IsSafeZoneGrid((uint8_t)nx, (uint8_t)ny)) {
              printf("[World] Relocated monster type %d spawn (%d,%d)->(%d,%d)\n",
                     s.type, mon.gridX, mon.gridY, nx, ny);
              mon.gridX = (uint8_t)nx;
              mon.gridY = (uint8_t)ny;
              mon.spawnGridX = mon.gridX;
              mon.spawnGridY = mon.gridY;
              mon.worldX = mon.gridY * 100.0f;
              mon.worldZ = mon.gridX * 100.0f;
              mon.spawnX = mon.worldX;
              mon.spawnZ = mon.worldZ;
              relocated = true;
            }
          }
        }
      }
      if (!relocated) {
        printf("[World] WARNING: Cannot relocate monster type %d at (%d,%d) — skipping\n",
               s.type, s.posX, s.posY);
        continue; // Skip this monster entirely
      }
    }

    // Stagger initial idle timers so all monsters don't move at once
    mon.stateTimer = 1.0f + (float)(rand() % 5000) / 1000.0f;

    m_monsterInstances.push_back(mon);
  }

  // Build initial occupancy grid
  rebuildOccupancyGrid();

  printf("[World] Loaded %zu monsters for map %d (indices %d-%d)\n",
         m_monsterInstances.size(), mapId,
         m_monsterInstances.empty() ? 0 : m_monsterInstances.front().index,
         m_monsterInstances.empty() ? 0 : m_monsterInstances.back().index);
}

// ─── Grid-step path advancement ──────────────────────────────────────────────

bool GameWorld::advancePathStep(MonsterInstance &mon, float dt,
                                std::vector<MonsterMoveUpdate> &outMoves,
                                bool chasing) {
  mon.moveTimer += dt;
  if (mon.moveTimer < mon.moveDelay)
    return false;
  mon.moveTimer -= mon.moveDelay;

  if (mon.pathStep >= (int)mon.currentPath.size())
    return false;

  GridPoint next = mon.currentPath[mon.pathStep];

  // Clear old occupancy
  setOccupied(mon.gridX, mon.gridY, false);

  // Update direction
  int dx = (int)next.x - (int)mon.gridX;
  int dy = (int)next.y - (int)mon.gridY;
  mon.dir = dirFromDelta(dx, dy);

  // Move to next cell
  mon.gridX = next.x;
  mon.gridY = next.y;
  mon.worldX = mon.gridY * 100.0f; // gridY → worldX
  mon.worldZ = mon.gridX * 100.0f; // gridX → worldZ

  // Set new occupancy
  setOccupied(mon.gridX, mon.gridY, true);

  mon.pathStep++;

  // Broadcast: target is path endpoint
  GridPoint pathEnd = mon.currentPath.back();
  emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, chasing, true, outMoves);

  return true;
}

// ─── Find best target within viewRange ───────────────────────────────────────

GameWorld::PlayerTarget *
GameWorld::findBestTarget(const MonsterInstance &mon,
                          std::vector<PlayerTarget> &players) const {
  PlayerTarget *best = nullptr;
  int bestDist = 999;

  // Priority 1: explicit aggro target (always honored, even for passive mobs)
  if (mon.aggroTargetFd != -1) {
    for (auto &p : players) {
      if (p.fd == mon.aggroTargetFd && !p.dead) {
        int dist =
            PathFinder::ChebyshevDist(mon.gridX, mon.gridY, p.gridX, p.gridY);
        if (dist <= mon.viewRange * 2)
          return &p;
        break; // Found but too far
      }
    }
  }

  // Priority 2: closest player in viewRange (aggressive monsters only)
  // Skip during respawn immunity (aggroTimer < 0)
  if (mon.aggressive && mon.aggroTimer >= 0.0f) {
    for (auto &p : players) {
      if (p.dead)
        continue;
      if (IsSafeZoneGrid(p.gridX, p.gridY))
        continue;
      // Skip if player is 10+ levels above the monster
      if (p.level >= mon.level + 10)
        continue;
      int dist =
          PathFinder::ChebyshevDist(mon.gridX, mon.gridY, p.gridX, p.gridY);
      if (dist <= mon.viewRange && dist < bestDist) {
        bestDist = dist;
        best = &p;
      }
    }
  }

  return best;
}

// ─── AI State Handlers ───────────────────────────────────────────────────────

void GameWorld::processIdle(MonsterInstance &mon, float dt,
                            std::vector<PlayerTarget> &players,
                            std::vector<MonsterMoveUpdate> &outMoves) {
  mon.stateTimer -= dt;

  // Check for target (proximity aggro or explicit)
  const PlayerTarget *target = findBestTarget(mon, players);
  if (target) {
    mon.aggroTargetFd = target->fd;
    mon.aggroTimer = 15.0f;
    mon.aiState = MonsterInstance::AIState::CHASING;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    mon.repathTimer = 0.0f;
    printf("[AI] Mon %d (type %d): IDLE→CHASING fd=%d\n", mon.index, mon.type,
           target->fd);
    return;
  }

  // Wander when idle timer expires
  if (mon.stateTimer <= 0.0f) {
    for (int tries = 0; tries < 5; tries++) {
      int rx = (int)mon.spawnGridX + (rand() % (2 * mon.moveRange + 1)) -
               mon.moveRange;
      int ry = (int)mon.spawnGridY + (rand() % (2 * mon.moveRange + 1)) -
               mon.moveRange;
      if (rx < 0 || ry < 0 || rx >= TERRAIN_SIZE || ry >= TERRAIN_SIZE)
        continue;
      uint8_t gx = (uint8_t)rx, gy = (uint8_t)ry;

      bool walkable = IsWalkableGrid(gx, gy);
      bool safezone = IsSafeZoneGrid(gx, gy);
      if (!walkable) {
        printf("[AI] Mon %d target (%d,%d) NOT walkable\n", mon.index, gx, gy);
        continue;
      }
      if (safezone) {
        printf("[AI] Mon %d target (%d,%d) is SAFEZONE\n", mon.index, gx, gy);
        continue;
      }
      if (gx == mon.gridX && gy == mon.gridY)
        continue;

      // A* pathfind to wander target
      GridPoint start{mon.gridX, mon.gridY};
      GridPoint end{gx, gy};

      // Temporarily clear own occupancy for pathfinding
      setOccupied(mon.gridX, mon.gridY, false);
      auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                         16, 500, false, m_monsterOccupancy);
      setOccupied(mon.gridX, mon.gridY, true);

      if (!path.empty()) {
        mon.currentPath = std::move(path);
        mon.pathStep = 0;
        mon.moveTimer = 0.0f;
        mon.aiState = MonsterInstance::AIState::WANDERING;
        // Emit wander target immediately so client starts moving
        GridPoint pathEnd = mon.currentPath.back();
        emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, false, true, outMoves);
        // Wander target emitted — no log needed (was spammy for 94 monsters)
        return;
      }
    }
    // Failed to find wander target — retry later
    mon.stateTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
  }
}

void GameWorld::processWandering(MonsterInstance &mon, float dt,
                                 std::vector<PlayerTarget> &players,
                                 std::vector<MonsterMoveUpdate> &outMoves) {
  // Check for target (interrupt wander to chase)
  const PlayerTarget *target = findBestTarget(mon, players);
  if (target) {
    mon.aggroTargetFd = target->fd;
    mon.aggroTimer = 15.0f;
    mon.aiState = MonsterInstance::AIState::CHASING;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    mon.repathTimer = 0.0f;
    printf("[AI] Mon %d (type %d): WANDERING→CHASING fd=%d\n", mon.index,
           mon.type, target->fd);
    return;
  }

  // Advance along path
  if (mon.pathStep < (int)mon.currentPath.size()) {
    advancePathStep(mon, dt, outMoves, false);
  } else {
    // Path exhausted — return to idle
    mon.aiState = MonsterInstance::AIState::IDLE;
    mon.stateTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
  }
}

void GameWorld::processChasing(MonsterInstance &mon, float dt,
                               std::vector<PlayerTarget> &players,
                               std::vector<MonsterMoveUpdate> &outMoves,
                               std::vector<MonsterAttackResult> &attacks) {
  // Find the aggro target
  PlayerTarget *target = nullptr;
  for (auto &p : players) {
    if (p.fd == mon.aggroTargetFd && !p.dead) {
      target = &p;
      break;
    }
  }

  // Helper: transition to RETURNING (evade mode: invulnerable until spawn)
  auto beginReturn = [&]() {
    mon.aiState = MonsterInstance::AIState::RETURNING;
    mon.evading = true;
    mon.aggroTargetFd = -1;
    mon.aggroTimer = 0.0f;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
  };

  // Lost target → full return to spawn
  if (!target) {
    beginReturn();
    return;
  }

  // Target entered safe zone → stop chasing, idle in place (no full evade)
  if (IsSafeZoneGrid(target->gridX, target->gridY)) {
    mon.aiState = MonsterInstance::AIState::IDLE;
    mon.aggroTargetFd = -1;
    mon.aggroTimer = -3.0f; // Brief cooldown before re-aggro
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    mon.stateTimer = 2.0f + (float)(rand() % 2000) / 1000.0f;
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
    return;
  }

  // Leash check — chase limit from spawn point
  int leashDist = std::max(12, mon.viewRange * 3);
  int distFromSpawn = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                                mon.spawnGridX, mon.spawnGridY);
  if (distFromSpawn > leashDist) {
    printf("[AI] Mon %d LEASHED (dist=%d, limit=%d)\n", mon.index,
           distFromSpawn, leashDist);
    beginReturn();
    return;
  }

  // De-aggro if target too far from monster
  int distToTarget = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                               target->gridX, target->gridY);
  if (distToTarget > leashDist) {
    printf("[AI] Mon %d DE-AGGRO (dist=%d) monGrid=(%d,%d) targetGrid=(%d,%d)\n",
           mon.index, distToTarget, mon.gridX, mon.gridY,
           target->gridX, target->gridY);
    beginReturn();
    return;
  }

  // In attack range → APPROACHING (brief delay for client walk anim to finish)
  if (distToTarget <= mon.attackRange &&
      (mon.attackRange > 1 ||
       WorldDistSq(mon, *target) <= MELEE_ATTACK_DIST_SQ)) {
    mon.aiState = MonsterInstance::AIState::APPROACHING;
    mon.approachTimer = 0.0f;
    mon.staggerDelay = calculateStaggerDelay(mon.aggroTargetFd);
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, false, outMoves);
    return;
  }

  // Already in attack range? Don't re-path — the check above will handle it
  // (only for ranged monsters where attackRange > 1; melee needs to keep pathing)
  if (mon.attackRange > 1 && distToTarget <= mon.attackRange) {
    mon.chaseFailCount = 0;
    mon.currentPath.clear();
    mon.pathStep = 0;
    return;
  }

  // Re-pathfind periodically or when path exhausted
  mon.repathTimer -= dt;
  bool pathExhausted = mon.currentPath.empty() ||
                       mon.pathStep >= (int)mon.currentPath.size();
  bool needsRepath = pathExhausted || mon.repathTimer <= 0.0f;

  // Skip re-path if current path endpoint is still near the target (within 2 cells)
  if (!pathExhausted && mon.repathTimer <= 0.0f && !mon.currentPath.empty()) {
    GridPoint pathEnd = mon.currentPath.back();
    int endDist = PathFinder::ChebyshevDist(pathEnd.x, pathEnd.y,
                                            target->gridX, target->gridY);
    if (endDist <= 2) {
      mon.repathTimer = 1.0f; // Path still valid, skip re-path
      needsRepath = false;
    }
  }

  if (needsRepath) {
    GridPoint start{mon.gridX, mon.gridY};
    GridPoint end{target->gridX, target->gridY};

    // Melee monsters: spread to different adjacent cells around the player
    // using monster index as a rotational offset to prevent all converging
    // on the same cell
    if (mon.attackRange <= 1) {
      static const int dx8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
      static const int dy8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
      int preferred = mon.index % 8; // Each monster prefers a different direction
      int bestDist = 999;
      GridPoint bestEnd = end;
      // Try preferred direction first, then others
      for (int j = 0; j < 8; j++) {
        int i = (preferred + j) % 8;
        int nx = (int)target->gridX + dx8[i];
        int ny = (int)target->gridY + dy8[i];
        if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
          continue;
        if (m_terrainAttributes[ny * TERRAIN_SIZE + nx] & (TW_NOMOVE | TW_NOGROUND))
          continue;
        int dist = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                             (uint8_t)nx, (uint8_t)ny);
        // Strongly prefer the monster's assigned direction (bias of -10)
        int score = dist + (j > 0 ? 0 : -10);
        if (score < bestDist) {
          bestDist = score;
          bestEnd = {(uint8_t)nx, (uint8_t)ny};
        }
      }
      end = bestEnd;
    }

    // Don't use occupancy grid during chase — monsters can overlap when
    // attacking the same target (matches original MU behavior)
    if (start == end) {
      mon.chaseFailCount = 0;
    } else {
      auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                         16, 500, false, nullptr);
      if (!path.empty()) {
        mon.currentPath = std::move(path);
        mon.pathStep = 0;
        mon.chaseFailCount = 0;
        GridPoint pathEnd = mon.currentPath.back();
        emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, true, true, outMoves);
      } else {
        mon.chaseFailCount++;
        if (mon.chaseFailCount >= 10) {
          printf("[AI] Mon %d: gave up chasing (no path %d times)\n", mon.index,
                 mon.chaseFailCount);
          beginReturn();
          return;
        }
      }
    }
    mon.repathTimer = 1.0f; // Re-pathfind every 1s
  }

  // Advance along path
  if (mon.pathStep < (int)mon.currentPath.size()) {
    advancePathStep(mon, dt, outMoves, true);
  }
}

// ─── Attack stagger: offset attack timers for multi-monster encounters ───────

float GameWorld::calculateStaggerDelay(int targetFd) const {
  int count = 0;
  for (const auto &m : m_monsterInstances) {
    if (m.aggroTargetFd == targetFd &&
        (m.aiState == MonsterInstance::AIState::APPROACHING ||
         m.aiState == MonsterInstance::AIState::ATTACKING))
      count++;
  }
  // First monster: no delay. Each additional: 0.3-0.6s stagger
  if (count <= 1)
    return 0.0f;
  return 0.3f + (float)(rand() % 300) / 1000.0f;
}

// ─── APPROACHING: brief delay before first attack (WoW-style) ───────────────

void GameWorld::processApproaching(MonsterInstance &mon, float dt,
                                   std::vector<PlayerTarget> &players,
                                   std::vector<MonsterMoveUpdate> &outMoves,
                                   std::vector<MonsterAttackResult> &attacks) {
  // Find target
  PlayerTarget *target = nullptr;
  for (auto &p : players) {
    if (p.fd == mon.aggroTargetFd && !p.dead) {
      target = &p;
      break;
    }
  }

  // Lost target or safezone → return
  if (!target || IsSafeZoneGrid(target->gridX, target->gridY)) {
    mon.aiState = MonsterInstance::AIState::RETURNING;
    mon.evading = true;
    mon.aggroTargetFd = -1;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    return;
  }

  // Target moved out of range → resume chasing (with +1 tolerance)
  int dist = PathFinder::ChebyshevDist(mon.gridX, mon.gridY, target->gridX,
                                       target->gridY);
  int rechaseRange = mon.attackRange + 1;
  if (dist > rechaseRange ||
      (mon.attackRange <= 1 && WorldDistSq(mon, *target) > MELEE_ATTACK_DIST_SQ * 1.5f)) {
    mon.aiState = MonsterInstance::AIState::CHASING;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.repathTimer = 0.0f;
    return;
  }

  // Wait for approach delay (moveDelay ensures client walk anim finishes + stagger)
  mon.approachTimer += dt;
  float requiredDelay = mon.moveDelay + mon.staggerDelay;
  if (mon.approachTimer >= requiredDelay) {
    // Transition to ATTACKING — can attack immediately
    mon.aiState = MonsterInstance::AIState::ATTACKING;
    mon.attackCooldown = 0.0f;
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, false, outMoves);
    printf("[AI] Mon %d: APPROACHING→ATTACKING (delay=%.2fs)\n", mon.index,
           requiredDelay);
  }
}

void GameWorld::processAttacking(MonsterInstance &mon, float dt,
                                 std::vector<PlayerTarget> &players,
                                 std::vector<MonsterMoveUpdate> &outMoves,
                                 std::vector<MonsterAttackResult> &attacks) {
  // Find target
  PlayerTarget *target = nullptr;
  for (auto &p : players) {
    if (p.fd == mon.aggroTargetFd && !p.dead) {
      target = &p;
      break;
    }
  }

  // Lost target or entered safezone → return
  if (!target || IsSafeZoneGrid(target->gridX, target->gridY)) {
    mon.aiState = MonsterInstance::AIState::RETURNING;
    mon.evading = true;
    mon.aggroTargetFd = -1;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    return;
  }

  // Out of attack range → resume chasing (with +1 tolerance to prevent
  // constant re-chase when player shifts 1 cell — reduces jostling)
  int dist = PathFinder::ChebyshevDist(mon.gridX, mon.gridY, target->gridX,
                                       target->gridY);
  int rechaseRange = mon.attackRange + 1;
  if (dist > rechaseRange ||
      (mon.attackRange <= 1 && WorldDistSq(mon, *target) > MELEE_ATTACK_DIST_SQ * 1.5f)) {
    mon.aiState = MonsterInstance::AIState::CHASING;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.repathTimer = 0.0f;
    return;
  }

  // Wait for cooldown
  if (mon.attackCooldown > 0.0f)
    return;

  // Face the target
  int dx = (int)target->gridX - (int)mon.gridX;
  int dy = (int)target->gridY - (int)mon.gridY;
  if (dx != 0 || dy != 0)
    mon.dir = dirFromDelta(dx, dy);

  // Execute attack
  int dmg = mon.attackMin + (mon.attackMax > mon.attackMin
                                 ? rand() % (mon.attackMax - mon.attackMin + 1)
                                 : 0);

  // Level-based auto-miss: monster 10+ levels below player always misses
  bool missed = false;
  if (target->level >= mon.level + 10) {
    missed = true;
  }

  // OpenMU hit chance: hitChance = 1 - defenseRate/attackRate (min 3%)
  if (!missed) {
    float hitChance = 0.03f; // 3% minimum (OpenMU AttackableExtensions.cs)
    if (mon.attackRate > 0 && target->defenseRate < mon.attackRate) {
      hitChance = 1.0f - (float)target->defenseRate / (float)mon.attackRate;
    }
    if ((rand() % 100) >= (int)(hitChance * 100.0f)) {
      missed = true;
    }
  }

  if (missed) {
    dmg = 0;
  } else {
    dmg = std::max(0, dmg - target->defense);
    // OpenMU "Overrate" penalty: if player defenseRate >= monster attackRate,
    // damage is reduced to 30% (AttackableExtensions.cs line 188)
    if (target->defenseRate >= mon.attackRate && dmg > 0) {
      dmg = std::max(1, dmg * 3 / 10);
    }
    // Level-based damage reduction: 10% less per level above 4-level gap
    // (smooth ramp before the 10+ auto-miss cutoff)
    int levelGap = (int)target->level - (int)mon.level;
    if (levelGap >= 5 && dmg > 0) {
      // 5 levels above: 90%, 6: 80%, 7: 70%, 8: 60%, 9: 50%
      int reduction = (levelGap - 4) * 10; // 10-60%
      if (reduction > 60) reduction = 60;
      dmg = std::max(1, dmg * (100 - reduction) / 100);
    }
    // Pet damage reduction (Guardian Angel: 20%)
    if (target->petDamageReduction > 0.0f && dmg > 0) {
      dmg = std::max(1, (int)(dmg * (1.0f - target->petDamageReduction)));
    }
  }

  printf("[AI] Mon %d (type %d) -> Player fd=%d: ATK %d - DEF %d (Rate %d/%d) "
         "= %d%s\n",
         mon.index, mon.type, target->fd,
         dmg +
             (missed ? 0 : target->defense), // Show raw damage before reduction
         target->defense, target->defenseRate, mon.attackRate, dmg,
         missed ? " (MISS)" : "");

  // Subtract damage from local target state for correct HP sync in packet
  target->life -= dmg;

  MonsterAttackResult result;
  result.targetFd = target->fd;
  result.monsterIndex = mon.index;
  result.damage = static_cast<uint16_t>(dmg);
  result.damageType = missed ? (uint8_t)0 : (uint8_t)1;
  result.remainingHp = static_cast<uint16_t>(std::max(0, target->life));
  attacks.push_back(result);

  mon.attackCooldown = mon.atkCooldownTime;
  mon.aggroTimer = 10.0f;
}

void GameWorld::processReturning(MonsterInstance &mon, float dt,
                                 std::vector<MonsterMoveUpdate> &outMoves) {
  // Path exhausted — check if arrived or need to re-pathfind
  if (mon.currentPath.empty() || mon.pathStep >= (int)mon.currentPath.size()) {
    if (mon.gridX == mon.spawnGridX && mon.gridY == mon.spawnGridY) {
      // Arrived at spawn — heal to full (WoW evade behavior)
      mon.hp = mon.maxHp;
      mon.evading = false;
      mon.aiState = MonsterInstance::AIState::IDLE;
      mon.stateTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
      mon.aggroTargetFd = -1;
      // Re-aggro cooldown after evade return (prevents heal→chase→fail loop)
      mon.aggroTimer = -5.0f;
      mon.chaseFailCount = 0;
      emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
      printf("[AI] Mon %d returned to spawn, healed to %d/%d\n", mon.index,
             mon.hp, mon.maxHp);
      return;
    }

    // Re-pathfind toward spawn (path may be >16 steps, multiple cycles)
    GridPoint start{mon.gridX, mon.gridY};
    GridPoint end{mon.spawnGridX, mon.spawnGridY};
    setOccupied(mon.gridX, mon.gridY, false);
    auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                       16, 500, false, m_monsterOccupancy);
    setOccupied(mon.gridX, mon.gridY, true);
    if (!path.empty()) {
      mon.currentPath = std::move(path);
      mon.pathStep = 0;
    } else {
      // Can't pathfind — teleport to spawn as fallback
      setOccupied(mon.gridX, mon.gridY, false);
      mon.gridX = mon.spawnGridX;
      mon.gridY = mon.spawnGridY;
      mon.worldX = mon.spawnX;
      mon.worldZ = mon.spawnZ;
      setOccupied(mon.gridX, mon.gridY, true);
      mon.hp = mon.maxHp; // Heal to full (WoW evade)
      mon.evading = false;
      mon.aiState = MonsterInstance::AIState::IDLE;
      mon.stateTimer = 2.0f;
      mon.aggroTimer = -5.0f; // Re-aggro cooldown after evade
      mon.chaseFailCount = 0;
      emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
      return;
    }
  }

  // Advance along path
  if (mon.pathStep < (int)mon.currentPath.size()) {
    advancePathStep(mon, dt, outMoves, false);
  }
}

// ─── Game tick ───────────────────────────────────────────────────────────────

void GameWorld::Update(float dt,
                       std::function<void(uint16_t)> dropExpiredCallback,
                       std::vector<MonsterMoveUpdate> *outWanderMoves,
                       std::vector<NpcMoveUpdate> *outNpcMoves,
                       std::function<void(uint16_t)> guardKillCallback) {
  // Update monster DYING/DEAD timers and respawn
  for (auto &mon : m_monsterInstances) {
    if (mon.attackCooldown > 0)
      mon.attackCooldown -= dt;

    switch (mon.aiState) {
    case MonsterInstance::AIState::DYING:
      mon.stateTimer += dt;
      mon.aggroTargetFd = -1;
      if (mon.stateTimer >= DYING_DURATION) {
        mon.aiState = MonsterInstance::AIState::DEAD;
        mon.stateTimer = 0.0f;
        setOccupied(mon.gridX, mon.gridY, false);
      }
      break;
    case MonsterInstance::AIState::DEAD:
      mon.stateTimer += dt;
      if (mon.stateTimer >= RESPAWN_DELAY) {
        // Respawn at original position
        setOccupied(mon.gridX, mon.gridY, false);
        mon.aiState = MonsterInstance::AIState::IDLE;
        mon.stateTimer = 1.0f + (float)(rand() % 3000) / 1000.0f;
        mon.hp = mon.maxHp;
        mon.gridX = mon.spawnGridX;
        mon.gridY = mon.spawnGridY;
        mon.worldX = mon.spawnX;
        mon.worldZ = mon.spawnZ;
        mon.currentPath.clear();
        mon.pathStep = 0;
        mon.lastBroadcastTargetX = mon.spawnGridX;
        mon.lastBroadcastTargetY = mon.spawnGridY;
        mon.lastBroadcastChasing = false;
        mon.lastBroadcastIsMoving = false;
        mon.aggroTargetFd = -1;
        mon.aggroTimer = -3.0f; // 3s respawn immunity (negative = immune)
        mon.attackCooldown = 1.5f;
        mon.chaseFailCount = 0;
        mon.poisoned = false;
        mon.evading = false;
        mon.justRespawned = true;
        setOccupied(mon.gridX, mon.gridY, true);
      }
      break;
    default:
      break; // IDLE/WANDERING/CHASING/ATTACKING/RETURNING handled in
             // ProcessMonsterAI
    }
  }

  // ── Guard patrol AI: waypoint routes + monster killing ──
  for (auto &npc : m_npcs) {
    if (!npc.isGuard)
      continue;

    // ── Guard kills nearby monsters (within GUARD_ATTACK_RANGE grid cells) ──
    uint8_t guardGX = static_cast<uint8_t>(npc.worldZ / 100.0f);
    uint8_t guardGY = static_cast<uint8_t>(npc.worldX / 100.0f);
    for (auto &mon : m_monsterInstances) {
      if (mon.aiState == MonsterInstance::AIState::DYING ||
          mon.aiState == MonsterInstance::AIState::DEAD)
        continue;
      int dist =
          PathFinder::ChebyshevDist(guardGX, guardGY, mon.gridX, mon.gridY);
      if (dist <= GUARD_ATTACK_RANGE) {
        // Guard instakills the monster
        mon.hp = 0;
        mon.aiState = MonsterInstance::AIState::DYING;
        mon.stateTimer = 0.0f;
        mon.aggroTargetFd = -1;
        printf(
            "[Guard] Guard #%d killed monster %d (type %d) at grid (%d,%d)\n",
            npc.index, mon.index, mon.type, mon.gridX, mon.gridY);
        // Death broadcast handled by the caller (Server.cpp) when it
        // sees the monster transition to DYING
        if (guardKillCallback)
          guardKillCallback(mon.index);
      }
    }

    // Guards stand in place (no patrol movement)
  }

  // Age ground drops and remove expired ones
  for (auto it = m_drops.begin(); it != m_drops.end();) {
    it->age += dt;
    if (it->age >= DROP_DESPAWN_TIME) {
      if (dropExpiredCallback)
        dropExpiredCallback(it->index);
      it = m_drops.erase(it);
    } else {
      ++it;
    }
  }
}

// ─── Guard NPC interaction ────────────────────────────────────────────────────

void GameWorld::SetGuardInteracting(uint16_t npcType, int playerFd,
                                     bool interact) {
  for (auto &npc : m_npcs) {
    if (npc.type == npcType && npc.isGuard) {
      if (interact) {
        npc.interactingFd = playerFd;
      } else if (npc.interactingFd == playerFd) {
        npc.interactingFd = -1;
      }
      return; // Only affect the first matching guard
    }
  }
}

void GameWorld::ClearGuardInteractionsForPlayer(int playerFd) {
  for (auto &npc : m_npcs) {
    if (npc.isGuard && npc.interactingFd == playerFd) {
      npc.interactingFd = -1;
    }
  }
}

// ─── Monster AI processing (state machine dispatch) ──────────────────────────

std::vector<GameWorld::MonsterAttackResult>
GameWorld::ProcessMonsterAI(float dt, std::vector<PlayerTarget> &players,
                            std::vector<MonsterMoveUpdate> &outMoves) {
  std::vector<MonsterAttackResult> attacks;

  for (auto &mon : m_monsterInstances) {
    // Only process alive states
    if (mon.aiState == MonsterInstance::AIState::DYING ||
        mon.aiState == MonsterInstance::AIState::DEAD)
      continue;

    // Tick respawn immunity timer toward 0 (negative = immune)
    if (mon.aggroTimer < 0.0f) {
      mon.aggroTimer += dt;
      if (mon.aggroTimer >= 0.0f)
        mon.aggroTimer = 0.0f;
    }

    // Tick aggro timer (positive = active aggro duration)
    // Only decay when idle/wandering with aggro — not while actively chasing/attacking
    if (mon.aggroTargetFd != -1 && mon.aggroTimer > 0.0f) {
      bool activelyEngaged =
          mon.aiState == MonsterInstance::AIState::CHASING ||
          mon.aiState == MonsterInstance::AIState::APPROACHING ||
          mon.aiState == MonsterInstance::AIState::ATTACKING;
      if (!activelyEngaged) {
        mon.aggroTimer -= dt;
        if (mon.aggroTimer <= 0.0f) {
          mon.aggroTargetFd = -1;
          mon.aggroTimer = 0.0f;
        }
      }
    }

    // Passive HP regeneration (idle, out of combat): 1% maxHP per second
    if (mon.aiState == MonsterInstance::AIState::IDLE &&
        mon.aggroTimer <= 0.0f && mon.hp > 0 && mon.hp < mon.maxHp) {
      mon.regenTimer += dt;
      if (mon.regenTimer >= 1.0f) {
        mon.regenTimer -= 1.0f;
        int heal = std::max(1, mon.maxHp / 100);
        mon.hp = std::min(mon.maxHp, mon.hp + heal);
      }
    } else {
      mon.regenTimer = 0.0f;
    }

    // If no players and currently in combat, return to spawn
    if (players.empty()) {
      if (mon.aiState == MonsterInstance::AIState::CHASING ||
          mon.aiState == MonsterInstance::AIState::ATTACKING ||
          mon.aiState == MonsterInstance::AIState::APPROACHING) {
        mon.aiState = MonsterInstance::AIState::RETURNING;
        mon.evading = true;
        mon.aggroTargetFd = -1;
        mon.currentPath.clear();
        mon.pathStep = 0;
        mon.moveTimer = 0.0f;
      }
    }

    // Main 5.2: StormTime stun — pause AI while spinning
    if (mon.stormTime > 0) {
      int prevStorm = mon.stormTime;
      mon.stormTickTimer += dt;
      while (mon.stormTickTimer >= 0.04f && mon.stormTime > 0) {
        mon.stormTickTimer -= 0.04f;
        mon.stormTime--;
      }
      if (mon.stormTime <= 0)
        printf("[Twister] Mon %d StormTime expired, AI resumed (was %d)\n",
               mon.index, prevStorm);
      continue; // Skip AI state machine while stunned
    }

    // Dispatch to state handler
    switch (mon.aiState) {
    case MonsterInstance::AIState::IDLE:
      processIdle(mon, dt, players, outMoves);
      break;
    case MonsterInstance::AIState::WANDERING:
      processWandering(mon, dt, players, outMoves);
      break;
    case MonsterInstance::AIState::CHASING:
      processChasing(mon, dt, players, outMoves, attacks);
      break;
    case MonsterInstance::AIState::APPROACHING:
      processApproaching(mon, dt, players, outMoves, attacks);
      break;
    case MonsterInstance::AIState::ATTACKING:
      processAttacking(mon, dt, players, outMoves, attacks);
      break;
    case MonsterInstance::AIState::RETURNING:
      processReturning(mon, dt, outMoves);
      break;
    default:
      break;
    }
  }
  return attacks;
}

// ─── Find monster by index ───────────────────────────────────────────────────

MonsterInstance *GameWorld::FindMonster(uint16_t index) {
  for (auto &mon : m_monsterInstances) {
    if (mon.index == index)
      return &mon;
  }
  return nullptr;
}

// ─── Poison DoT processing ───────────────────────────────────────────────────

std::vector<GameWorld::PoisonTickResult>
GameWorld::ProcessPoisonTicks(float dt) {
  std::vector<PoisonTickResult> results;

  for (auto &mon : m_monsterInstances) {
    if (!mon.poisoned)
      continue;
    if (mon.aiState == MonsterInstance::AIState::DYING ||
        mon.aiState == MonsterInstance::AIState::DEAD) {
      mon.poisoned = false;
      continue;
    }

    mon.poisonDuration -= dt;
    if (mon.poisonDuration <= 0.0f) {
      mon.poisoned = false;
      printf("[Poison] Mon %d: poison expired\n", mon.index);
      continue;
    }

    mon.poisonTickTimer += dt;
    if (mon.poisonTickTimer >= 3.0f) { // 3-second tick (OpenMU)
      mon.poisonTickTimer -= 3.0f;

      int dmg = mon.poisonDamage;
      mon.hp -= dmg;
      bool killed = mon.hp <= 0;
      if (killed)
        mon.hp = 0;

      printf("[Poison] Mon %d tick: %d dmg, HP=%d/%d%s\n", mon.index, dmg,
             mon.hp, mon.maxHp, killed ? " KILLED" : "");

      PoisonTickResult r;
      r.monsterIndex = mon.index;
      r.damage = static_cast<uint16_t>(dmg);
      r.remainingHp = static_cast<uint16_t>(std::max(0, mon.hp));
      r.attackerFd = mon.poisonAttackerFd;
      results.push_back(r);

      if (killed) {
        mon.poisoned = false;
        mon.aiState = MonsterInstance::AIState::DYING;
        mon.stateTimer = 0.0f;
      }
    }
  }
  return results;
}

// ─── Viewport packets ────────────────────────────────────────────────────────

// Map AIState to wire protocol state (0=alive, 1=dying, 2=dead)
static uint8_t aiStateToWire(MonsterInstance::AIState s) {
  switch (s) {
  case MonsterInstance::AIState::DYING:
    return 1;
  case MonsterInstance::AIState::DEAD:
    return 2;
  default:
    return 0;
  }
}

// Legacy monster viewport (0x1F) — grid positions, no HP
std::vector<uint8_t> GameWorld::BuildMonsterViewportPacket() const {
  if (m_monsterInstances.empty())
    return {};

  size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY);
  size_t totalSize = sizeof(PMSG_MONSTER_VIEWPORT_HEAD) +
                     m_monsterInstances.size() * entrySize;

  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PMSG_MONSTER_VIEWPORT_HEAD *>(packet.data());
  head->h = MakeC1Header(static_cast<uint8_t>(totalSize), 0x1F);
  head->count = static_cast<uint8_t>(m_monsterInstances.size());

  auto *entries = reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY *>(
      packet.data() + sizeof(PMSG_MONSTER_VIEWPORT_HEAD));
  for (size_t i = 0; i < m_monsterInstances.size(); i++) {
    const auto &mon = m_monsterInstances[i];
    auto &e = entries[i];
    e.typeH = static_cast<uint8_t>(mon.type >> 8);
    e.typeL = static_cast<uint8_t>(mon.type & 0xFF);
    e.x = mon.gridX;
    e.y = mon.gridY;
    e.dir = mon.dir;
  }

  return packet;
}

// New v2 monster viewport (0x34) — includes index, HP, state
std::vector<uint8_t> GameWorld::BuildMonsterViewportV2Packet() const {
  if (m_monsterInstances.empty())
    return {};

  // Count field is uint8_t, so split into batches of 255 max
  size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY_V2);
  size_t total = m_monsterInstances.size();
  std::vector<uint8_t> result;

  for (size_t offset = 0; offset < total; offset += 255) {
    size_t batchCount = std::min<size_t>(255, total - offset);
    size_t pktSize = 5 + batchCount * entrySize;

    size_t base = result.size();
    result.resize(base + pktSize, 0);
    uint8_t *pkt = result.data() + base;

    auto *head = reinterpret_cast<PWMSG_HEAD *>(pkt);
    *head = MakeC2Header(static_cast<uint16_t>(pktSize), 0x34);
    pkt[4] = static_cast<uint8_t>(batchCount);

    auto *entries =
        reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY_V2 *>(pkt + 5);
    for (size_t i = 0; i < batchCount; i++) {
      const auto &mon = m_monsterInstances[offset + i];
      auto &e = entries[i];
      e.indexH = static_cast<uint8_t>(mon.index >> 8);
      e.indexL = static_cast<uint8_t>(mon.index & 0xFF);
      e.typeH = static_cast<uint8_t>(mon.type >> 8);
      e.typeL = static_cast<uint8_t>(mon.type & 0xFF);
      e.x = mon.gridX;
      e.y = mon.gridY;
      e.dir = mon.dir;
      e.hp = static_cast<uint16_t>(mon.hp);
      e.maxHp = static_cast<uint16_t>(mon.maxHp);
      e.state = aiStateToWire(mon.aiState);
    }
  }

  return result;
}

// ─── 0.97d Lorencia Drop Tables ──────────────────────────────────────────────

struct DropEntry {
  int16_t defIndex;
  int weight;
  uint8_t maxPlus;
};

static const DropEntry &PickWeighted(const std::vector<DropEntry> &pool) {
  int total = 0;
  for (auto &e : pool)
    total += e.weight;
  int roll = rand() % total;
  int acc = 0;
  for (auto &e : pool) {
    acc += e.weight;
    if (roll < acc)
      return e;
  }
  return pool.back();
}

// Spider (type 3, Lv2)
static const std::vector<DropEntry> s_spiderDrops = {
    {14 * 32 + 1, 30, 0}, {14 * 32 + 4, 20, 0}, {0 * 32 + 0, 5, 0},
    {1 * 32 + 0, 5, 0},   {15 * 32 + 3, 3, 0},
};

// Budge Dragon (type 2, Lv4)
static const std::vector<DropEntry> s_budgeDrops = {
    {14 * 32 + 1, 25, 0}, {14 * 32 + 4, 20, 0}, {0 * 32 + 0, 8, 0},
    {0 * 32 + 1, 6, 0},   {1 * 32 + 0, 6, 0},   {1 * 32 + 1, 4, 0},
    {5 * 32 + 0, 5, 0},   {6 * 32 + 0, 4, 0},   {10 * 32 + 2, 3, 0},
    {11 * 32 + 2, 3, 0},  {15 * 32 + 3, 3, 0},  {15 * 32 + 10, 2, 0},
};

// Bull Fighter (type 0, Lv6)
static const std::vector<DropEntry> s_bullDrops = {
    {14 * 32 + 1, 20, 0}, {14 * 32 + 2, 10, 0}, {14 * 32 + 4, 15, 0},
    {0 * 32 + 0, 6, 1},   {0 * 32 + 1, 5, 1},   {0 * 32 + 2, 3, 0},
    {1 * 32 + 0, 5, 1},   {1 * 32 + 1, 4, 0},   {2 * 32 + 0, 3, 0},
    {5 * 32 + 0, 4, 1},   {6 * 32 + 0, 4, 1},   {6 * 32 + 4, 3, 0},
    {7 * 32 + 2, 3, 0},   {8 * 32 + 2, 3, 0},   {9 * 32 + 2, 2, 0},
    {10 * 32 + 2, 3, 0},  {11 * 32 + 2, 3, 0},  {7 * 32 + 5, 2, 0},
    {8 * 32 + 5, 2, 0},   {15 * 32 + 3, 3, 0},  {15 * 32 + 10, 2, 0},
};

// Hound (type 1, Lv9)
static const std::vector<DropEntry> s_houndDrops = {
    {14 * 32 + 2, 15, 0}, {14 * 32 + 5, 12, 0}, {0 * 32 + 1, 5, 1},
    {0 * 32 + 2, 5, 1},   {0 * 32 + 4, 3, 0},   {1 * 32 + 1, 4, 1},
    {1 * 32 + 2, 3, 0},   {2 * 32 + 0, 4, 1},   {2 * 32 + 1, 3, 0},
    {4 * 32 + 0, 4, 0},   {4 * 32 + 1, 3, 0},   {4 * 32 + 8, 3, 0},
    {5 * 32 + 0, 3, 1},   {6 * 32 + 0, 3, 1},   {6 * 32 + 1, 3, 0},
    {6 * 32 + 4, 3, 1},   {7 * 32 + 2, 3, 1},   {7 * 32 + 5, 3, 0},
    {7 * 32 + 10, 2, 0},  {8 * 32 + 2, 3, 1},   {8 * 32 + 5, 3, 0},
    {8 * 32 + 10, 2, 0},  {15 * 32 + 2, 3, 0},  {15 * 32 + 5, 2, 0},
    {13 * 32 + 8, 1, 0},
};

// Elite Bull Fighter (type 4, Lv12)
static const std::vector<DropEntry> s_eliteBullDrops = {
    {14 * 32 + 2, 12, 0}, {14 * 32 + 5, 10, 0}, {0 * 32 + 2, 4, 1},
    {0 * 32 + 3, 4, 1},   {0 * 32 + 4, 3, 0},   {1 * 32 + 2, 4, 1},
    {1 * 32 + 3, 3, 0},   {2 * 32 + 1, 4, 1},   {2 * 32 + 2, 3, 0},
    {3 * 32 + 5, 3, 0},   {3 * 32 + 2, 3, 0},   {4 * 32 + 1, 3, 1},
    {4 * 32 + 9, 3, 0},   {5 * 32 + 1, 3, 0},   {6 * 32 + 1, 3, 1},
    {6 * 32 + 2, 3, 0},   {6 * 32 + 6, 2, 0},   {7 * 32 + 0, 3, 0},
    {7 * 32 + 4, 3, 0},   {8 * 32 + 0, 3, 0},   {8 * 32 + 4, 3, 0},
    {9 * 32 + 0, 2, 0},   {10 * 32 + 0, 2, 0},  {11 * 32 + 0, 2, 0},
    {15 * 32 + 2, 3, 0},  {15 * 32 + 6, 2, 0},  {13 * 32 + 9, 1, 0},
};

// Lich (type 6, Lv14)
static const std::vector<DropEntry> s_lichDrops = {
    {14 * 32 + 2, 10, 0}, {14 * 32 + 3, 5, 0},  {14 * 32 + 5, 8, 0},
    {14 * 32 + 6, 4, 0},  {0 * 32 + 3, 3, 1},   {0 * 32 + 5, 3, 0},
    {0 * 32 + 6, 3, 0},   {1 * 32 + 3, 3, 1},   {1 * 32 + 4, 2, 0},
    {2 * 32 + 2, 3, 1},   {3 * 32 + 1, 3, 0},   {3 * 32 + 6, 3, 0},
    {4 * 32 + 2, 3, 0},   {4 * 32 + 10, 2, 0},  {5 * 32 + 1, 3, 1},
    {5 * 32 + 2, 3, 0},   {6 * 32 + 2, 3, 1},   {6 * 32 + 3, 2, 0},
    {7 * 32 + 4, 3, 1},   {7 * 32 + 11, 2, 0},  {8 * 32 + 4, 3, 1},
    {8 * 32 + 11, 2, 0},  {15 * 32 + 0, 3, 0},  {15 * 32 + 6, 3, 0},
    {15 * 32 + 7, 2, 0},  {13 * 32 + 12, 1, 0}, {13 * 32 + 8, 1, 0},
};

// Giant (type 7, Lv17)
static const std::vector<DropEntry> s_giantDrops = {
    {14 * 32 + 3, 8, 0},  {14 * 32 + 6, 6, 0},  {0 * 32 + 5, 3, 1},
    {0 * 32 + 6, 3, 1},   {0 * 32 + 7, 2, 0},   {1 * 32 + 4, 3, 1},
    {1 * 32 + 5, 2, 0},   {2 * 32 + 2, 3, 1},   {2 * 32 + 3, 2, 0},
    {3 * 32 + 6, 3, 1},   {3 * 32 + 7, 2, 0},   {4 * 32 + 2, 3, 1},
    {4 * 32 + 3, 2, 0},   {4 * 32 + 11, 2, 0},  {5 * 32 + 2, 3, 1},
    {5 * 32 + 3, 2, 0},   {6 * 32 + 6, 3, 1},   {6 * 32 + 9, 2, 0},
    {6 * 32 + 10, 2, 0},  {7 * 32 + 0, 3, 1},   {7 * 32 + 6, 2, 0},
    {7 * 32 + 12, 2, 0},  {8 * 32 + 0, 3, 1},   {8 * 32 + 6, 2, 0},
    {8 * 32 + 12, 2, 0},  {9 * 32 + 5, 2, 0},   {9 * 32 + 6, 2, 0},
    {10 * 32 + 5, 2, 0},  {11 * 32 + 5, 2, 0},  {15 * 32 + 7, 2, 0},
    {13 * 32 + 12, 1, 0}, {13 * 32 + 13, 1, 0},
};

// Skeleton Warrior (type 14, Lv19)
static const std::vector<DropEntry> s_skelDrops = {
    {14 * 32 + 3, 8, 0},  {14 * 32 + 6, 6, 0},  {0 * 32 + 6, 3, 2},
    {0 * 32 + 7, 3, 1},   {0 * 32 + 8, 2, 0},   {1 * 32 + 4, 3, 1},
    {1 * 32 + 5, 3, 0},   {1 * 32 + 6, 2, 0},   {2 * 32 + 2, 3, 1},
    {2 * 32 + 3, 2, 0},   {3 * 32 + 7, 3, 1},   {3 * 32 + 3, 2, 0},
    {4 * 32 + 3, 3, 0},   {4 * 32 + 11, 3, 0},  {5 * 32 + 2, 3, 1},
    {5 * 32 + 3, 2, 0},   {6 * 32 + 7, 3, 0},   {6 * 32 + 9, 2, 0},
    {7 * 32 + 6, 3, 1},   {7 * 32 + 8, 2, 0},   {7 * 32 + 12, 2, 0},
    {7 * 32 + 13, 1, 0},  {8 * 32 + 6, 3, 1},   {8 * 32 + 8, 2, 0},
    {8 * 32 + 12, 2, 0},  {9 * 32 + 6, 2, 1},   {9 * 32 + 8, 2, 0},
    {10 * 32 + 6, 2, 1},  {11 * 32 + 6, 2, 1},  {15 * 32 + 7, 2, 0},
    {15 * 32 + 8, 1, 0},  {13 * 32 + 8, 1, 0},  {13 * 32 + 9, 1, 0},
    {13 * 32 + 12, 1, 0}, {13 * 32 + 13, 1, 0},
};

static const std::vector<DropEntry> &GetDropPool(uint16_t monsterType) {
  switch (monsterType) {
  case 3:
    return s_spiderDrops;
  case 2:
    return s_budgeDrops;
  case 0:
    return s_bullDrops;
  case 1:
    return s_houndDrops;
  case 4:
    return s_eliteBullDrops;
  case 6:
    return s_lichDrops;
  case 7:
    return s_giantDrops;
  case 14:
    return s_skelDrops;
  default:
    return s_bullDrops;
  }
}

std::vector<GroundDrop> GameWorld::SpawnDrops(float worldX, float worldZ,
                                              int monsterLevel,
                                              uint16_t monsterType,
                                              Database &db) {
  std::vector<GroundDrop> spawned;

  auto makeDrop = [&](int16_t defIndex, uint8_t qty, uint8_t lvl) {
    GroundDrop drop{};
    drop.index = m_nextDropIndex++;
    drop.defIndex = defIndex;
    drop.quantity = qty;
    drop.itemLevel = lvl;
    drop.worldX = worldX + (float)(rand() % 60 - 30);
    drop.worldZ = worldZ + (float)(rand() % 60 - 30);
    drop.age = 0.0f;
    m_drops.push_back(drop);
    spawned.push_back(drop);
  };

  // 1. Zen Drop — 40% chance
  if (rand() % 100 < 40) {
    uint32_t zenAmount = monsterLevel * 10 + (rand() % (monsterLevel * 10 + 1));
    if (zenAmount < 1)
      zenAmount = 1;
    uint8_t zen = std::min(255, (int)zenAmount);
    makeDrop(-1, zen, 0);
    return spawned;
  }

  // 2. Jewel Drops — Rare
  {
    int jewelRoll = rand() % 10000;
    if (jewelRoll < 10) {
      makeDrop(12 * 32 + 15, 1, 0);
      printf("[World] RARE: Jewel of Chaos from MonType %d!\n", monsterType);
      return spawned;
    } else if (monsterLevel >= 10 && jewelRoll < 15) {
      makeDrop(14 * 32 + 13, 1, 0);
      printf("[World] RARE: Jewel of Bless from MonType %d!\n", monsterType);
      return spawned;
    } else if (monsterLevel >= 10 && jewelRoll < 20) {
      makeDrop(14 * 32 + 14, 1, 0);
      printf("[World] RARE: Jewel of Soul from MonType %d!\n", monsterType);
      return spawned;
    }
  }

  // 3. Item Drop — 8-15%
  int itemChance = 8 + monsterLevel / 3;
  if (rand() % 100 < itemChance) {
    const auto &pool = GetDropPool(monsterType);
    const auto &picked = PickWeighted(pool);
    uint8_t dropLvl = 0;
    if (picked.maxPlus > 0) {
      dropLvl = rand() % (picked.maxPlus + 1);
    }
    makeDrop(picked.defIndex, 1, dropLvl);
    printf("[World] Drop: defIdx=%d +%d from MonType %d (Lv%d)\n",
           picked.defIndex, dropLvl, monsterType, monsterLevel);
    return spawned;
  }

  // 4. Potion Drop — 20% fallback
  if (rand() % 5 == 0) {
    int16_t potCode;
    if (monsterLevel <= 5)
      potCode = 14 * 32 + 1;
    else if (monsterLevel <= 12)
      potCode = 14 * 32 + 2;
    else
      potCode = 14 * 32 + 3;

    if (rand() % 2 == 0) {
      if (monsterLevel <= 5)
        potCode = 14 * 32 + 4;
      else if (monsterLevel <= 12)
        potCode = 14 * 32 + 5;
      else
        potCode = 14 * 32 + 6;
    }
    makeDrop(potCode, 1, 0);
  }

  return spawned;
}

GroundDrop *GameWorld::FindDrop(uint16_t dropIndex) {
  for (auto &drop : m_drops) {
    if (drop.index == dropIndex)
      return &drop;
  }
  return nullptr;
}

bool GameWorld::RemoveDrop(uint16_t dropIndex) {
  for (auto it = m_drops.begin(); it != m_drops.end(); ++it) {
    if (it->index == dropIndex) {
      m_drops.erase(it);
      return true;
    }
  }
  return false;
}

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
    // type hp    def  defR atkMn atkMx atkR lvl  atkCD  mvDel mR vR aR aggro respawn
    // All values from OpenMU Version075 (canonical reference)
    // --- Lorencia monsters ---
    {0, 100, 6, 6, 16, 20, 28, 6, 1.6f, 0.4f, 3, 5, 1, true, 3.0f},  // Bull Fighter
    {1, 140, 9, 9, 22, 27, 39, 9, 1.6f, 0.4f, 3, 5, 1, true, 10.0f}, // Hound
    {2, 60, 3, 3, 10, 13, 18, 4, 2.0f, 0.4f, 3, 4, 1, true, 3.0f},   // Budge Dragon
    {3, 30, 1, 1, 4, 7, 8, 2, 1.8f, 0.6f, 2, 5, 1, true, 10.0f},     // Spider (slower)
    {4, 190, 12, 12, 31, 36, 50, 12, 1.4f, 0.4f, 3, 4, 1,
     true, 10.0f}, // Elite Bull Fighter
    {6, 255, 14, 14, 41, 46, 62, 14, 2.0f, 0.4f, 3, 7, 4, true, 10.0f}, // Lich
    {7, 400, 18, 18, 57, 62, 80, 17, 2.2f, 0.4f, 2, 3, 2, true, 10.0f}, // Giant
    {14, 525, 22, 22, 68, 74, 93, 19, 1.4f, 0.4f, 2, 4, 1,
     true, 10.0f}, // Skeleton Warrior
    {15, 600, 25, 25, 72, 78, 100, 22, 1.6f, 0.4f, 2, 5, 4,
     true, 10.0f}, // Skeleton Archer (ranged)
    {16, 800, 30, 30, 85, 92, 115, 25, 1.4f, 0.4f, 2, 4, 1,
     true, 10.0f}, // Skeleton Captain
    // --- Dungeon monsters (OpenMU Version075 Dungeon.cs) ---
    {5, 1400, 55, 45, 125, 130, 190, 38, 1.2f, 0.4f, 3, 4, 1, true, 10.0f},  // Hell Hound
    {8, 2500, 75, 61, 145, 150, 230, 46, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},  // Poison Bull
    {9, 2000, 70, 55, 140, 145, 220, 44, 2.2f, 0.4f, 3, 7, 4, true, 10.0f},  // Thunder Lich (ranged)
    {10, 3000, 80, 70, 150, 155, 240, 48, 1.4f, 0.4f, 3, 5, 1, true, 10.0f}, // Dark Knight
    {11, 1000, 40, 39, 110, 115, 160, 32, 1.4f, 0.4f, 3, 5, 1, true, 10.0f}, // Ghost
    {12, 750, 31, 31, 90, 95, 125, 25, 1.8f, 0.4f, 3, 4, 1, true, 10.0f},    // Larva
    {13, 1600, 60, 47, 130, 135, 200, 40, 1.6f, 0.4f, 3, 7, 4, true, 10.0f}, // Hell Spider (ranged)
    {17, 850, 35, 35, 100, 105, 140, 28, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},  // Cyclops
    {18, 6000, 100, 82, 165, 175, 275, 55, 1.6f, 0.4f, 3, 7, 1, true, 10.0f}, // Gorgon
    // --- Devias monsters (OpenMU Version075 Devias.cs) ---
    {19, 900, 37, 37, 105, 110, 150, 30, 2.0f, 0.4f, 2, 6, 4, true, 3.0f},     // Yeti (EnergyBall skill)
    {20, 1200, 50, 43, 120, 125, 180, 36, 1.4f, 0.4f, 3, 6, 1, true, 3.0f},    // Elite Yeti
    {21, 800, 33, 33, 95, 100, 130, 26, 2.0f, 0.4f, 2, 7, 1, true, 10.0f},     // Assassin
    {22, 650, 27, 27, 80, 85, 110, 22, 2.0f, 0.4f, 2, 5, 1, true, 10.0f},      // Ice Monster (Ice skill)
    {23, 700, 29, 29, 85, 90, 120, 24, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},      // Hommerd
    {24, 600, 25, 25, 75, 80, 100, 20, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},      // Worm
    {25, 4000, 90, 76, 155, 165, 260, 52, 1.4f, 0.4f, 3, 7, 4, true, 50.0f},   // Ice Queen (PowerWave skill)
    // --- Noria monsters (OpenMU Version075) ---
    {26, 45, 2, 2, 7, 10, 13, 3, 1.8f, 0.4f, 2, 4, 1, true, 10.0f},      // Goblin
    {27, 80, 4, 4, 13, 17, 23, 5, 1.8f, 0.4f, 3, 4, 1, true, 10.0f},     // Chain Scorpion
    {28, 165, 10, 10, 26, 31, 44, 10, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},  // Beetle Monster
    {29, 220, 13, 13, 36, 41, 56, 13, 1.6f, 0.4f, 3, 4, 4, true, 10.0f},  // Hunter (ranged)
    {30, 295, 15, 15, 46, 51, 68, 15, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},  // Forest Monster
    {31, 340, 16, 16, 51, 57, 74, 16, 1.4f, 0.4f, 2, 4, 1, true, 10.0f},  // Agon
    {32, 465, 20, 20, 62, 68, 86, 18, 2.2f, 0.4f, 2, 3, 2, true, 10.0f},  // Stone Golem
    {33, 120, 8, 8, 19, 23, 33, 8, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},    // Elite Goblin
    // --- Summon-only monsters (Elf summoning skills) ---
    {150, 5000, 100, 75, 165, 170, 260, 52, 1.6f, 0.4f, 3, 1, 7, false, 10.0f}, // Bali
};
static constexpr int NUM_MONSTER_DEFS =
    sizeof(s_monsterDefs) / sizeof(s_monsterDefs[0]);

// World-space melee range threshold (squared) — must be adjacent.
// Adjacent diagonal = ~141 units, so 150 allows adjacent but not 2-cell hits.
// Max world distance for melee attacks. Grid cells are 100 units wide, and
// the worst case for adjacent diagonal cells (Chebyshev dist=1) is ~281 units
// (monster at cell corner, player at far corner of adjacent cell). Use 300 to
// ensure all adjacent-cell configurations pass the world-distance check.
static constexpr float MELEE_ATTACK_DIST_SQ = 300.0f * 300.0f;

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
      mon.respawnDelay = def->respawnDelay;
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

    // Validate spawn position — if on blocked/void/safezone tile, relocate to nearest valid cell
    if (!IsWalkableGrid(mon.gridX, mon.gridY) || IsSafeZoneGrid(mon.gridX, mon.gridY)) {
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

  // Process multiple steps per frame if dt spike exceeded moveDelay
  // (prevents monsters freezing during server lag bursts)
  bool moved = false;
  static constexpr int MAX_STEPS_PER_FRAME = 4; // Cap to prevent runaway
  int steps = 0;
  while (mon.moveTimer >= mon.moveDelay && steps < MAX_STEPS_PER_FRAME) {
    mon.moveTimer -= mon.moveDelay;
    steps++;

    if (mon.pathStep >= (int)mon.currentPath.size())
      break;

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
    moved = true;
  }

  // Cap leftover to prevent accumulation across frames
  if (mon.moveTimer > mon.moveDelay)
    mon.moveTimer = mon.moveDelay;

  if (moved) {
    // Broadcast: target is path endpoint
    GridPoint pathEnd = mon.currentPath.back();
    emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, chasing, true, outMoves);
  }

  return moved;
}

// ─── Find best target within viewRange ───────────────────────────────────────

GameWorld::PlayerTarget *
GameWorld::findBestTarget(const MonsterInstance &mon,
                          std::vector<PlayerTarget> &players) const {
  PlayerTarget *best = nullptr;
  int bestDist = 999;

  // Priority 1: explicit aggro target (always honored, even for passive mobs)
  // No range limit — if monster was hit by player, chase regardless of distance
  if (mon.aggroTargetFd != -1) {
    for (auto &p : players) {
      if (p.fd == mon.aggroTargetFd && !p.dead) {
        return &p;
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
  // If chaseFailCount > 0 (returned to IDLE after failed chase), wait for
  // stateTimer cooldown before re-engaging. Prevents rapid IDLE↔CHASING loop.
  const PlayerTarget *target = findBestTarget(mon, players);
  if (target && (mon.chaseFailCount == 0 || mon.stateTimer <= 0.0f)) {
    mon.aggroTargetFd = target->fd;
    mon.aggroTimer = 15.0f;
    mon.aiState = MonsterInstance::AIState::CHASING;
    mon.currentPath.clear();
    mon.pathStep = 0;
    mon.moveTimer = 0.0f;
    mon.repathTimer = 0.0f;
    mon.chaseFailCount = 0; // Reset on re-engage
    return;
  }

  // Wander when idle timer expires
  if (mon.stateTimer <= 0.0f) {
    // Pre-check: collect up to 5 valid walkable candidates, then pathfind
    // only the best one. Saves A* calls on blocked/safezone cells.
    struct WanderCandidate { uint8_t gx, gy; int dist; };
    WanderCandidate candidates[5];
    int numCandidates = 0;

    for (int tries = 0; tries < 10 && numCandidates < 5; tries++) {
      int rx = (int)mon.spawnGridX + (rand() % (2 * mon.moveRange + 1)) -
               mon.moveRange;
      int ry = (int)mon.spawnGridY + (rand() % (2 * mon.moveRange + 1)) -
               mon.moveRange;
      if (rx < 0 || ry < 0 || rx >= TERRAIN_SIZE || ry >= TERRAIN_SIZE)
        continue;
      uint8_t gx = (uint8_t)rx, gy = (uint8_t)ry;
      if (!IsWalkableGrid(gx, gy) || IsSafeZoneGrid(gx, gy))
        continue;
      if (gx == mon.gridX && gy == mon.gridY)
        continue;
      candidates[numCandidates++] = {gx, gy,
          PathFinder::ChebyshevDist(mon.gridX, mon.gridY, gx, gy)};
    }

    // Sort by distance (closest first) — shorter paths are cheaper to compute
    for (int i = 0; i < numCandidates - 1; i++)
      for (int j = i + 1; j < numCandidates; j++)
        if (candidates[j].dist < candidates[i].dist)
          std::swap(candidates[i], candidates[j]);

    for (int c = 0; c < numCandidates; c++) {
      uint8_t gx = candidates[c].gx, gy = candidates[c].gy;

      // A* pathfind to wander target
      GridPoint start{mon.gridX, mon.gridY};
      GridPoint end{gx, gy};

      // Monsters can ghost through each other (original MU behavior)
      auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                         16, 500, false, nullptr);

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
  // Damage-aggro'd monsters get a much larger leash to chase attackers
  int leashDist = std::max(25, mon.viewRange * 3);
  int distFromSpawn = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                                mon.spawnGridX, mon.spawnGridY);
  if (distFromSpawn > leashDist) {
    beginReturn();
    return;
  }

  int distToTarget = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                               target->gridX, target->gridY);

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
    // on the same cell. Prefer cardinal cells (100 units) over diagonal
    // (141 units) for visually tighter attacks.
    if (mon.attackRange <= 1) {
      // Cardinals first (indices 0-3), then diagonals (4-7)
      static const int dx8[] = {0, 0, -1, 1, -1, -1, 1, 1};
      static const int dy8[] = {-1, 1, 0, 0, -1, 1, -1, 1};
      int preferred = mon.index % 8;
      int bestScore = 999;
      GridPoint bestEnd = end;
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
        // Prefer assigned direction (-10), prefer cardinal over diagonal (-3)
        bool isCardinal = (i < 4);
        int score = dist + (j == 0 ? -10 : 0) + (isCardinal ? -3 : 0);
        if (score < bestScore) {
          bestScore = score;
          bestEnd = {(uint8_t)nx, (uint8_t)ny};
        }
      }
      end = bestEnd;
    }

    // If target is beyond the 16-cell A* segment limit, compute an
    // intermediate waypoint ~12 cells toward the target so the monster
    // can chase incrementally in multiple steps.
    int distToEnd = PathFinder::ChebyshevDist(start.x, start.y, end.x, end.y);
    if (distToEnd > 14) {
      float dx = (float)end.x - (float)start.x;
      float dy = (float)end.y - (float)start.y;
      float len = std::max(std::abs(dx), std::abs(dy));
      if (len > 0) {
        float ratio = 12.0f / len;
        int ix = (int)start.x + (int)(dx * ratio);
        int iy = (int)start.y + (int)(dy * ratio);
        ix = std::max(0, std::min(ix, TERRAIN_SIZE - 1));
        iy = std::max(0, std::min(iy, TERRAIN_SIZE - 1));
        end = {(uint8_t)ix, (uint8_t)iy};
      }
      // Re-path more frequently when stepping incrementally
      mon.repathTimer = 0.3f;
    }

    // Don't use occupancy grid during chase — monsters can overlap when
    // attacking the same target (matches original MU behavior)
    if (start == end) {
      mon.chaseFailCount = 0;
    } else {
      // Allow safe zone traversal during chase (monsters walk through, just
      // don't aggro there). Original MU behavior — prevents stuck monsters
      // near town borders.
      auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                         16, 500, true, nullptr);
      if (!path.empty()) {
        mon.currentPath = std::move(path);
        mon.pathStep = 0;
        mon.chaseFailCount = 0;
        GridPoint pathEnd = mon.currentPath.back();
        emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, true, true, outMoves);
      } else {
        mon.chaseFailCount++;
        if (mon.chaseFailCount >= 10) {
          // If explicitly aggro'd by damage, don't return — stay in place
          // so the monster doesn't heal to full when hit from range by Elf bow.
          // Just stop chasing and idle (aggro timer handles de-aggro after 15s).
          if (mon.aggroTargetFd != -1) {
            mon.aiState = MonsterInstance::AIState::IDLE;
            mon.currentPath.clear();
            mon.pathStep = 0;
            mon.stateTimer = 2.0f;
            // Keep aggroTargetFd and aggroTimer so findBestTarget still returns target
          } else {
            beginReturn();
          }
          return;
        }
      }
    }
    // Scale repath interval by distance: close targets repath more often
    // (smoother tracking), distant targets less often (saves CPU)
    if (mon.repathTimer <= 0.0f) {
      int distNow = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                              target->gridX, target->gridY);
      if (distNow <= 3)
        mon.repathTimer = 0.5f;  // Very close: responsive tracking
      else if (distNow <= 8)
        mon.repathTimer = 1.0f;  // Medium: standard rate
      else
        mon.repathTimer = 1.5f;  // Far away: relax (intermediate steps use 0.3s)
    }
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
      mon.playerThreat = 0.0f;
      mon.summonThreat = 0.0f;
      mon.aggroSummonIdx = 0;
      emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
      return;
    }

    // Re-pathfind toward spawn (path may be >16 steps, multiple cycles)
    GridPoint start{mon.gridX, mon.gridY};
    GridPoint end{mon.spawnGridX, mon.spawnGridY};

    // Intermediate waypoint if spawn is beyond pathfinder segment limit
    int distToSpawn = PathFinder::ChebyshevDist(start.x, start.y, end.x, end.y);
    if (distToSpawn > 14) {
      float ddx = (float)end.x - (float)start.x;
      float ddy = (float)end.y - (float)start.y;
      float len = std::max(std::abs(ddx), std::abs(ddy));
      if (len > 0) {
        float ratio = 12.0f / len;
        int ix = (int)start.x + (int)(ddx * ratio);
        int iy = (int)start.y + (int)(ddy * ratio);
        ix = std::max(0, std::min(ix, TERRAIN_SIZE - 1));
        iy = std::max(0, std::min(iy, TERRAIN_SIZE - 1));
        end = {(uint8_t)ix, (uint8_t)iy};
      }
    }

    // Don't use occupancy grid — returning monsters ghost through others
    // (matches chase behavior and prevents stuck evading monsters)
    auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                       16, 500, false, nullptr);
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
      mon.playerThreat = 0.0f;
      mon.summonThreat = 0.0f;
      mon.aggroSummonIdx = 0;
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
    if (mon.attackCooldown > 0) {
      mon.attackCooldown -= dt;
      if (mon.attackCooldown < 0) mon.attackCooldown = 0;
    }

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
      if (mon.isSummon())
        break; // Summons don't respawn — removed in sweep below
      mon.stateTimer += dt;
      if (mon.stateTimer >= mon.respawnDelay * ServerConfig::RESPAWN_MULTIPLIER) {
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
        mon.playerThreat = 0.0f;
        mon.summonThreat = 0.0f;
        mon.aggroSummonIdx = 0;
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
      if (mon.isSummon())
        continue; // Guards don't attack summons
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

  // Sweep dead summons (they don't respawn)
  for (auto it = m_monsterInstances.begin(); it != m_monsterInstances.end();) {
    if (it->isSummon() && it->aiState == MonsterInstance::AIState::DEAD) {
      setOccupied(it->gridX, it->gridY, false);
      printf("[Summon] Removed dead summon index=%d type=%d\n", it->index,
             it->type);
      it = m_monsterInstances.erase(it);
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
                            std::vector<MonsterMoveUpdate> &outMoves,
                            std::vector<SummonHitResult> *outSummonHits,
                            std::vector<MonsterHitSummonResult> *outMonsterHitSummon) {
  std::vector<MonsterAttackResult> attacks;

  for (auto &mon : m_monsterInstances) {
    // Only process alive states
    if (mon.aiState == MonsterInstance::AIState::DYING ||
        mon.aiState == MonsterInstance::AIState::DEAD)
      continue;

    // Summons use separate AI
    if (mon.isSummon()) {
      processSummonAI(mon, dt, players, outMoves, attacks, outSummonHits);
      continue;
    }

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

    // Summon-targeting: if this monster has aggro on a summon, handle it
    if (mon.aggroSummonIdx > 0) {
      MonsterInstance *summon = nullptr;
      for (auto &m : m_monsterInstances) {
        if (m.index == mon.aggroSummonIdx && m.isSummon() && m.hp > 0 &&
            m.aiState != MonsterInstance::AIState::DYING &&
            m.aiState != MonsterInstance::AIState::DEAD) {
          summon = &m;
          break;
        }
      }
      if (summon) {
        processSummonTargeting(mon, *summon, dt, outMoves, outMonsterHitSummon);
        continue; // Skip normal AI
      } else {
        // Summon gone — clear and resume normal player targeting
        mon.aggroSummonIdx = 0;
      }
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

// ─── Summon AI (follows owner, attacks nearby wild monsters) ─────────────────

void GameWorld::processSummonAI(MonsterInstance &mon, float dt,
                                 std::vector<PlayerTarget> &players,
                                 std::vector<MonsterMoveUpdate> &outMoves,
                                 std::vector<MonsterAttackResult> &attacks,
                                 std::vector<SummonHitResult> *outSummonHits) {
  // Find owner
  PlayerTarget *owner = nullptr;
  for (auto &p : players) {
    if (p.fd == mon.ownerFd) {
      owner = &p;
      break;
    }
  }

  // Owner disconnected or dead → mark summon for death
  if (!owner || owner->dead) {
    mon.hp = 0;
    mon.aiState = MonsterInstance::AIState::DYING;
    mon.stateTimer = 0.0f;
    return;
  }

  // Tick attack cooldown
  if (mon.attackCooldown > 0)
    mon.attackCooldown -= dt;

  // Spawn grace period: summon stays still for 1s after spawning so the client
  // can show the appear animation before it starts chasing
  if (mon.stateTimer > 0.0f) {
    mon.stateTimer -= dt;
    return;
  }

  // HP regen: 2% of maxHP per second (always, even during chase)
  if (mon.hp > 0 && mon.hp < mon.maxHp) {
    mon.regenTimer += dt;
    if (mon.regenTimer >= 1.0f) {
      mon.regenTimer -= 1.0f;
      int regen = std::max(1, mon.maxHp / 50);
      mon.hp = std::min(mon.maxHp, mon.hp + regen);
    }
  }

  // Calculate distance to owner (grid)
  int ownerGX = static_cast<int>(owner->worldZ / 100.0f);
  int ownerGY = static_cast<int>(owner->worldX / 100.0f);
  int distToOwner =
      PathFinder::ChebyshevDist(mon.gridX, mon.gridY, ownerGX, ownerGY);

  // Leash: teleport to owner if too far (>8 cells)
  if (distToOwner > 8) {
    // Find walkable cell near owner
    for (int r = 1; r <= 3; r++) {
      for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
          int nx = ownerGX + dx, ny = ownerGY + dy;
          if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
            continue;
          if (IsWalkableGrid((uint8_t)nx, (uint8_t)ny)) {
            setOccupied(mon.gridX, mon.gridY, false);
            mon.gridX = (uint8_t)nx;
            mon.gridY = (uint8_t)ny;
            mon.worldX = mon.gridY * 100.0f;
            mon.worldZ = mon.gridX * 100.0f;
            setOccupied(mon.gridX, mon.gridY, true);
            mon.currentPath.clear();
            mon.pathStep = 0;
            mon.aggroTargetFd = -1; // Reset chase state after teleport
            mon.attackCooldown = std::max(mon.attackCooldown, 0.8f); // Delay before attacking after teleport
            emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false,
                              outMoves);
            goto leash_done;
          }
        }
      }
    }
  leash_done:
    distToOwner =
        PathFinder::ChebyshevDist(mon.gridX, mon.gridY, ownerGX, ownerGY);
  }

  // Check for attack target FIRST — attacking takes priority over following
  MonsterInstance *bestTarget = nullptr;
  int bestTargetDist = 999;

  if (owner->attackTargetMonsterIdx > 0) {
    for (auto &other : m_monsterInstances) {
      if (other.index == owner->attackTargetMonsterIdx && !other.isSummon() &&
          other.aiState != MonsterInstance::AIState::DYING &&
          other.aiState != MonsterInstance::AIState::DEAD &&
          other.aggroTimer >= 0.0f && // Skip freshly respawned monsters
          other.hp > 0) {
        bestTarget = &other;
        bestTargetDist = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                                    other.gridX, other.gridY);
        break;
      }
    }
    // Clear stale target if monster is dead/respawned
    if (!bestTarget)
      owner->attackTargetMonsterIdx = 0;
  }

  // Summons must walk close before attacking — force melee range regardless
  // of monster type's attackRange (even Stone Golem/Bali walk up first)
  static constexpr float SUMMON_MELEE_DIST_SQ = 120.0f * 120.0f;
  bool inMeleeRange = false;
  if (bestTarget && bestTargetDist <= 1) {
    float dx = mon.worldX - bestTarget->worldX;
    float dz = mon.worldZ - bestTarget->worldZ;
    inMeleeRange = (dx * dx + dz * dz) <= SUMMON_MELEE_DIST_SQ;
  }

  // If target is in attack range, interrupt path and attack immediately
  if (bestTarget && inMeleeRange) {
    // Stop any current movement
    mon.currentPath.clear();
    mon.pathStep = 0;

    // Approach delay: first arrival in melee range after chasing —
    // wait for client walk animation to finish before attack plays
    if (mon.aggroTargetFd > 0) {
      if (mon.attackCooldown < 0.8f)
        mon.attackCooldown = 0.8f;
      mon.aggroTargetFd = -1;
    }

    // Face the target
    int ddx = (int)bestTarget->gridX - (int)mon.gridX;
    int ddy = (int)bestTarget->gridY - (int)mon.gridY;
    mon.dir = dirFromDelta(ddx, ddy);
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, false,
                      outMoves);

    if (mon.attackCooldown <= 0.0f) {
      // Calculate damage
      int damage = mon.attackMin + (rand() % (mon.attackMax - mon.attackMin + 1));
      int defense = bestTarget->defense;
      damage -= defense;
      if (damage < 1)
        damage = 1;

      bestTarget->hp -= damage;
      mon.attackCooldown = mon.atkCooldownTime;
      bool killed = bestTarget->hp <= 0;

      // Track summon threat for aggro system
      bestTarget->summonThreat += static_cast<float>(damage);
      // If summon outpaces player damage by 10%, monster switches aggro to summon
      if (bestTarget->aggroSummonIdx == 0 &&
          bestTarget->summonThreat > bestTarget->playerThreat * 1.1f + 10.0f) {
        bestTarget->aggroSummonIdx = mon.index;
        printf("[Aggro] Mon %d switches to summon %d (summonThreat=%.0f > "
               "playerThreat=%.0f)\n",
               bestTarget->index, mon.index, bestTarget->summonThreat,
               bestTarget->playerThreat);
      }

      if (killed) {
        bestTarget->hp = 0;
        bestTarget->aiState = MonsterInstance::AIState::DYING;
        bestTarget->stateTimer = 0.0f;
        bestTarget->aggroTargetFd = -1;
        bestTarget->playerThreat = 0.0f;
        bestTarget->summonThreat = 0.0f;
        bestTarget->aggroSummonIdx = 0;
        owner->attackTargetMonsterIdx = 0;
      }

      // Push result for Server.cpp to broadcast damage/death/XP/drops
      if (outSummonHits) {
        SummonHitResult hit{};
        hit.ownerFd = mon.ownerFd;
        hit.monsterIndex = bestTarget->index;
        hit.damage = static_cast<uint16_t>(damage);
        hit.remainingHp = static_cast<uint16_t>(std::max(0, bestTarget->hp));
        hit.killed = killed;
        outSummonHits->push_back(hit);
      }
    }
    return; // In range — stay here (wait for cooldown or attack)
  }

  // If target exists but out of range, chase it with A* pathfinding
  if (bestTarget) {
    // Mark as chasing so approach delay triggers on arrival
    if (mon.aggroTargetFd < 0) {
      mon.aggroTargetFd = 1; // Flag: chasing target
      if (!mon.currentPath.empty()) {
        mon.currentPath.clear();
        mon.pathStep = 0;
        mon.repathTimer = 0.0f;
      }
    }
    mon.repathTimer -= dt;
    if (mon.currentPath.empty() || mon.repathTimer <= 0.0f) {
      mon.repathTimer = 0.3f; // Fast repath for combat chasing
      mon.currentPath.clear();
      mon.pathStep = 0;

      GridPoint start{mon.gridX, mon.gridY};

      // Find best walkable cell adjacent to target — prefer cardinal
      // neighbors (100 units) over diagonal (141 units) for closer approach
      GridPoint end{bestTarget->gridX, bestTarget->gridY};
      int bestAdjDist = 999;
      bool foundCardinal = false;

      // Priority 1: Cardinal neighbors (N/S/E/W — 100 world units)
      static const int cardinalDirs[][2] = {{1,0},{-1,0},{0,1},{0,-1}};
      for (auto &dir : cardinalDirs) {
        int nx = (int)bestTarget->gridX + dir[0];
        int ny = (int)bestTarget->gridY + dir[1];
        if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
          continue;
        if (!IsWalkableGrid((uint8_t)nx, (uint8_t)ny))
          continue;
        int d = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                          (uint8_t)nx, (uint8_t)ny);
        if (d < bestAdjDist) {
          bestAdjDist = d;
          end = {(uint8_t)nx, (uint8_t)ny};
          foundCardinal = true;
        }
      }

      // Priority 2: Diagonal neighbors only if no cardinal cell is reachable
      if (!foundCardinal) {
        static const int diagDirs[][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
        for (auto &dir : diagDirs) {
          int nx = (int)bestTarget->gridX + dir[0];
          int ny = (int)bestTarget->gridY + dir[1];
          if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
            continue;
          if (!IsWalkableGrid((uint8_t)nx, (uint8_t)ny))
            continue;
          int d = PathFinder::ChebyshevDist(mon.gridX, mon.gridY,
                                            (uint8_t)nx, (uint8_t)ny);
          if (d < bestAdjDist) {
            bestAdjDist = d;
            end = {(uint8_t)nx, (uint8_t)ny};
          }
        }
      }

      // Summons ghost through other monsters when chasing (no occupancy)
      auto path = m_pathFinder->FindPath(start, end,
                                         m_terrainAttributes.data(), 16, 500,
                                         true, nullptr);

      if (!path.empty()) {
        mon.currentPath = std::move(path);
        mon.pathStep = 0;
        mon.moveTimer = 0.0f;
        mon.aggroTargetFd = 1; // Signal that we're chasing
      }
    }

    // Advance chase path
    if (!mon.currentPath.empty() &&
        mon.pathStep < (int)mon.currentPath.size()) {
      advancePathStep(mon, dt, outMoves, true);
      if (mon.pathStep >= (int)mon.currentPath.size()) {
        mon.currentPath.clear();
        mon.pathStep = 0;
      }
    }
    return;
  }

  // ── No combat target — follow owner like a pet (direct grid stepping) ──

  // Clear any stale chase state
  mon.currentPath.clear();
  mon.pathStep = 0;
  mon.aggroTargetFd = -1;

  // Direct grid stepping toward owner (no A*, always responsive)
  if (distToOwner > 2) {
    mon.moveTimer += dt;
    if (mon.moveTimer >= mon.moveDelay) {
      mon.moveTimer -= mon.moveDelay;
      int dx = (ownerGX > (int)mon.gridX) ? 1
             : (ownerGX < (int)mon.gridX) ? -1
                                          : 0;
      int dy = (ownerGY > (int)mon.gridY) ? 1
             : (ownerGY < (int)mon.gridY) ? -1
                                          : 0;

      // Try direct diagonal, then each cardinal fallback
      struct {
        int dx, dy;
      } tries[] = {{dx, dy}, {dx, 0}, {0, dy}};

      for (auto &t : tries) {
        if (t.dx == 0 && t.dy == 0)
          continue;
        int nx = (int)mon.gridX + t.dx;
        int ny = (int)mon.gridY + t.dy;
        if (nx >= 0 && ny >= 0 && nx < TERRAIN_SIZE && ny < TERRAIN_SIZE &&
            IsWalkableGrid((uint8_t)nx, (uint8_t)ny)) {
          setOccupied(mon.gridX, mon.gridY, false);
          mon.gridX = (uint8_t)nx;
          mon.gridY = (uint8_t)ny;
          mon.worldX = mon.gridY * 100.0f;
          mon.worldZ = mon.gridX * 100.0f;
          setOccupied(mon.gridX, mon.gridY, true);
          mon.dir = dirFromDelta(t.dx, t.dy);
          // Always emit for summon follow (bypass dedup so client tracks each step)
          outMoves.push_back({mon.index, (uint8_t)ownerGX, (uint8_t)ownerGY, 0});
          mon.lastBroadcastTargetX = (uint8_t)ownerGX;
          mon.lastBroadcastTargetY = (uint8_t)ownerGY;
          mon.lastBroadcastChasing = false;
          mon.lastBroadcastIsMoving = true;
          break;
        }
      }
    }
  }
}

// ─── Monster targeting summon (threat-based aggro switch) ────────────────────

static float WorldDistSqMon(const MonsterInstance &a, const MonsterInstance &b) {
  float dx = a.worldX - b.worldX;
  float dz = a.worldZ - b.worldZ;
  return dx * dx + dz * dz;
}

void GameWorld::processSummonTargeting(
    MonsterInstance &mon, MonsterInstance &summon, float dt,
    std::vector<MonsterMoveUpdate> &outMoves,
    std::vector<MonsterHitSummonResult> *outResults) {
  // Tick attack cooldown
  if (mon.attackCooldown > 0)
    mon.attackCooldown -= dt;

  int dist = PathFinder::ChebyshevDist(mon.gridX, mon.gridY, summon.gridX,
                                       summon.gridY);

  // In attack range — face and attack the summon
  if (dist <= mon.attackRange &&
      (mon.attackRange > 1 || WorldDistSqMon(mon, summon) <= MELEE_ATTACK_DIST_SQ)) {
    // Face the summon
    int dx = (int)summon.gridX - (int)mon.gridX;
    int dy = (int)summon.gridY - (int)mon.gridY;
    if (dx != 0 || dy != 0)
      mon.dir = dirFromDelta(dx, dy);
    emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, false, outMoves);

    // Wait for cooldown
    if (mon.attackCooldown > 0.0f)
      return;

    // Calculate damage (simplified — no hit chance, summon always gets hit)
    int dmg = mon.attackMin + (mon.attackMax > mon.attackMin
                                   ? rand() % (mon.attackMax - mon.attackMin + 1)
                                   : 0);
    dmg = std::max(0, dmg - summon.defense);
    if (dmg < 1)
      dmg = 1;

    summon.hp -= dmg;
    mon.attackCooldown = mon.atkCooldownTime;
    bool killed = summon.hp <= 0;

    if (killed) {
      summon.hp = 0;
      summon.aiState = MonsterInstance::AIState::DYING;
      summon.stateTimer = 0.0f;
      mon.aggroSummonIdx = 0; // Resume normal player targeting
    }

    if (outResults) {
      MonsterHitSummonResult result{};
      result.attackerIndex = mon.index;
      result.summonIndex = summon.index;
      result.damage = static_cast<uint16_t>(dmg);
      result.remainingHp = static_cast<uint16_t>(std::max(0, summon.hp));
      result.killed = killed;
      result.ownerFd = summon.ownerFd;
      outResults->push_back(result);
    }
    return;
  }

  // Out of range — chase toward summon
  mon.repathTimer -= dt;
  if (mon.currentPath.empty() || mon.repathTimer <= 0.0f) {
    mon.repathTimer = 0.5f;
    GridPoint start{mon.gridX, mon.gridY};
    GridPoint end{summon.gridX, summon.gridY};

    // Target adjacent cell to the summon (for melee)
    if (mon.attackRange <= 1) {
      int bestAdjDist = 999;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0)
            continue;
          int nx = (int)summon.gridX + dx;
          int ny = (int)summon.gridY + dy;
          if (nx < 0 || ny < 0 || nx >= TERRAIN_SIZE || ny >= TERRAIN_SIZE)
            continue;
          if (!IsWalkableGrid((uint8_t)nx, (uint8_t)ny))
            continue;
          int d = PathFinder::ChebyshevDist(mon.gridX, mon.gridY, (uint8_t)nx,
                                            (uint8_t)ny);
          if (d < bestAdjDist) {
            bestAdjDist = d;
            end = {(uint8_t)nx, (uint8_t)ny};
          }
        }
      }
    }

    auto path = m_pathFinder->FindPath(start, end, m_terrainAttributes.data(),
                                       16, 500, true, nullptr);
    if (!path.empty()) {
      mon.currentPath = std::move(path);
      mon.pathStep = 0;
      GridPoint pathEnd = mon.currentPath.back();
      emitMoveIfChanged(mon, pathEnd.x, pathEnd.y, true, true, outMoves);
    }
  }

  // Advance path
  if (mon.pathStep < (int)mon.currentPath.size()) {
    advancePathStep(mon, dt, outMoves, true);
  }
}

// ─── Find monster by index ───────────────────────────────────────────────────

MonsterInstance *GameWorld::FindMonster(uint16_t index) {
  for (auto &mon : m_monsterInstances) {
    if (mon.index == index)
      return &mon;
  }
  return nullptr;
}

// ─── Summon Spawn / Despawn ──────────────────────────────────────────────────

MonsterInstance *GameWorld::SpawnSummon(uint16_t type, uint8_t gridX,
                                        uint8_t gridY, int ownerFd,
                                        int ownerCharId, uint16_t ownerLevel) {
  MonsterInstance mon{};
  mon.index = m_nextSummonIndex++;
  mon.type = type;
  mon.gridX = gridX;
  mon.gridY = gridY;
  mon.spawnGridX = gridX;
  mon.spawnGridY = gridY;
  mon.dir = 0;
  mon.worldX = gridY * 100.0f;
  mon.worldZ = gridX * 100.0f;
  mon.spawnX = mon.worldX;
  mon.spawnZ = mon.worldZ;
  mon.aiState = MonsterInstance::AIState::IDLE;
  mon.stateTimer = 1.0f;
  mon.ownerFd = ownerFd;
  mon.ownerCharId = ownerCharId;
  mon.aggressive = false; // Summons don't self-aggro, AI handles targeting

  const MonsterTypeDef *def = FindMonsterTypeDef(type);
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
  } else {
    // Fallback — use the type's known HP from summon table
    mon.hp = 100;
    mon.maxHp = 100;
    mon.defense = 5;
    mon.defenseRate = 5;
    mon.attackMin = 10;
    mon.attackMax = 15;
    mon.attackRate = 20;
    mon.level = 5;
  }

  // Scale summon stats to owner's level (summon matches player power)
  if (ownerLevel > mon.level && mon.level > 0) {
    float scale = (float)ownerLevel / (float)mon.level;
    mon.hp = (int)(mon.hp * scale);
    mon.maxHp = mon.hp;
    mon.attackMin = (int)(mon.attackMin * scale);
    mon.attackMax = (int)(mon.attackMax * scale);
    mon.defense = (int)(mon.defense * scale);
    mon.defenseRate = (int)(mon.defenseRate * scale);
    mon.attackRate = (int)(mon.attackRate * scale);
    mon.level = ownerLevel;
  }

  // Summons move faster than their wild counterparts to keep up with the player
  mon.moveDelay = std::min(mon.moveDelay, 0.15f);
  // Keep the monster's natural attack cooldown (e.g. Goblin=1.8s per OpenMU)

  setOccupied(gridX, gridY, true);
  m_monsterInstances.push_back(mon);

  printf("[Summon] Spawned summon index=%d type=%d at (%d,%d) owner fd=%d\n",
         mon.index, type, gridX, gridY, ownerFd);

  return &m_monsterInstances.back();
}

void GameWorld::DespawnSummon(uint16_t summonIndex) {
  for (auto it = m_monsterInstances.begin(); it != m_monsterInstances.end();
       ++it) {
    if (it->index == summonIndex && it->isSummon()) {
      setOccupied(it->gridX, it->gridY, false);
      printf("[Summon] Despawned summon index=%d type=%d\n", it->index,
             it->type);
      m_monsterInstances.erase(it);
      return;
    }
  }
}

void GameWorld::DespawnSummonsForOwner(int ownerFd) {
  for (auto it = m_monsterInstances.begin(); it != m_monsterInstances.end();) {
    if (it->isSummon() && it->ownerFd == ownerFd) {
      setOccupied(it->gridX, it->gridY, false);
      printf("[Summon] Despawned summon index=%d (owner fd=%d disconnected)\n",
             it->index, ownerFd);
      it = m_monsterInstances.erase(it);
    } else {
      ++it;
    }
  }
}

void GameWorld::RescaleSummon(uint16_t summonIndex, uint16_t newOwnerLevel) {
  auto *summon = FindMonster(summonIndex);
  if (!summon || !summon->isSummon())
    return;

  const MonsterTypeDef *def = FindMonsterTypeDef(summon->type);
  if (!def || def->level == 0)
    return;

  float scale = (float)newOwnerLevel / (float)def->level;
  float hpRatio =
      (summon->maxHp > 0) ? (float)summon->hp / (float)summon->maxHp : 1.0f;

  summon->maxHp = (int)(def->hp * scale);
  summon->hp = (int)(summon->maxHp * hpRatio);
  summon->attackMin = (int)(def->attackMin * scale);
  summon->attackMax = (int)(def->attackMax * scale);
  summon->defense = (int)(def->defense * scale);
  summon->defenseRate = (int)(def->defenseRate * scale);
  summon->attackRate = (int)(def->attackRate * scale);
  summon->level = newOwnerLevel;
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

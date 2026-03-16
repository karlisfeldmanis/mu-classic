#ifndef MU_GAME_WORLD_HPP
#define MU_GAME_WORLD_HPP

#include "Database.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// GridPoint: also defined in PathFinder.hpp — must stay identical (ODR)
#ifndef MU_GRID_POINT_DEFINED
#define MU_GRID_POINT_DEFINED
struct GridPoint {
  uint8_t x = 0, y = 0;
  bool operator==(const GridPoint &o) const { return x == o.x && y == o.y; }
  bool operator!=(const GridPoint &o) const { return !(*this == o); }
};
#endif

class PathFinder; // Forward declaration — included by .cpp

// ─── Server Config (tunable rates) ─────────────────────────────────────
namespace ServerConfig {
static constexpr int XP_MULTIPLIER =
    100;                            // XP gain multiplier (1=normal, 100=100x)
static constexpr int DROP_RATE = 1; // Drop rate multiplier

// WoW-style progressive/degressive XP calculation
// All monsters give at least 1 XP, with smooth reduction for lower-level mobs
inline int CalculateXP(int playerLevel, int monsterLevel) {
  // Gray threshold scales with player level (8 at low, up to 20 at high)
  int grayThreshold = 8 + playerLevel / 10;
  if (grayThreshold > 20) grayThreshold = 20;

  int levelDiff = playerLevel - monsterLevel;

  // Base XP (MU Online formula)
  double baseXP = (monsterLevel + 25.0) * monsterLevel / 3.0;
  if (monsterLevel >= 65)
    baseXP += (double)(monsterLevel - 64) * ((double)monsterLevel / 4.0);

  // Progressive reduction: 100% at same level, minimum 15% beyond gray threshold
  if (levelDiff > 0) {
    double ratio = (double)levelDiff / (double)grayThreshold;
    if (ratio > 1.0) ratio = 1.0;
    double scale = 1.0 - ratio * ratio * 0.85; // Quadratic curve, floors at 15%
    baseXP *= scale;
  }

  // Bonus for higher-level monsters (up to 20%)
  if (levelDiff < 0) {
    double bonus = std::min(0.20, (double)(-levelDiff) * 0.04);
    baseXP *= (1.0 + bonus);
  }

  return std::max(1, (int)(baseXP * 1.25 * XP_MULTIPLIER));
}

} // namespace ServerConfig

struct NpcSpawn {
  uint16_t index;   // Unique object index (1001+)
  uint16_t type;    // NPC type ID (253=Amy, 250=Merchant, etc.)
  uint8_t x;        // Grid X
  uint8_t y;        // Grid Y
  uint8_t dir;      // Facing direction (0-7)
  std::string name; // Display name (for logging)

  // Guard patrol state (OpenMU: GuardIntelligence.cs)
  bool isGuard = false;         // true for type 249
  float worldX = 0, worldZ = 0; // Current world position
  float spawnX = 0, spawnZ = 0; // Original spawn point
  float wanderTimer = 0.0f;     // Countdown to next patrol move
  float wanderTargetX = 0, wanderTargetZ = 0;
  bool isWandering = false;
  uint8_t lastBroadcastX = 0, lastBroadcastY = 0;

  // Waypoint patrol: guards cycle through patrol points
  std::vector<GridPoint> patrolWaypoints;
  int patrolIndex = 0; // Current waypoint target

  // A* path for current patrol segment (grid-step movement like monsters)
  std::vector<GridPoint> guardPath;
  int guardPathStep = 0;
  float guardMoveTimer = 0.0f;
  static constexpr float GUARD_MOVE_DELAY = 0.4f; // seconds per grid step

  // Player interaction: pauses patrol when a player is talking to the guard
  int interactingFd = -1; // FD of interacting player (-1 = none)
};

// ─── Per-type monster stat definition (replaces per-type constexprs) ───
struct MonsterTypeDef {
  uint16_t type;
  int hp, defense, defenseRate, attackMin, attackMax, attackRate, level;
  float atkCooldown;   // Seconds between attacks (AtkSpeed/1000)
  float moveDelay;     // Seconds per grid step (MoveSpeed/1000)
  uint8_t moveRange;   // Wander radius in grid cells (MvRange)
  uint8_t viewRange;   // Aggro detection range in grid cells (ViewRange)
  uint8_t attackRange; // Attack range in grid cells (1=melee, 4=Lich ranged)
  bool aggressive;     // true=red (auto-aggro), false=yellow (passive)
};

// Live monster state (server-authoritative)
struct MonsterInstance {
  uint16_t index;                 // Unique ID (2001+)
  uint16_t type;                  // Monster type (3=Spider)
  uint8_t gridX, gridY;           // Authoritative grid position
  uint8_t spawnGridX, spawnGridY; // Spawn position for leash/respawn
  uint8_t dir;
  float worldX, worldZ; // Derived from grid: worldX=gridY*100, worldZ=gridX*100
  float spawnX, spawnZ; // Derived from spawn grid

  int hp, maxHp;
  int defense, defenseRate;
  int attackMin, attackMax;
  int attackRate;
  int level;

  // ── Proper AI state machine (replaces boolean flag soup) ──
  enum class AIState : uint8_t {
    IDLE,        // Standing, decrementing idle timer
    WANDERING,   // Following A* path to random wander point
    CHASING,     // Following A* path toward player
    APPROACHING, // In attack range, brief delay before first hit (WoW-style)
    ATTACKING,   // In attack range, executing attack cooldown
    RETURNING,   // Following A* path back to spawn (evading/invulnerable)
    DYING,       // Death animation (3s)
    DEAD         // Respawn wait (10s)
  };
  AIState aiState = AIState::IDLE;
  float stateTimer = 0.0f;     // Time in current state / idle timer
  float attackCooldown = 0.0f; // Cooldown between attacks
  bool justRespawned = false;  // Set true on respawn, cleared after broadcast

  // A* path following (grid-step movement)
  std::vector<GridPoint> currentPath; // A* result, consumed one step at a time
  int pathStep = 0;                   // Current step index in path
  float moveTimer = 0.0f;             // Accumulator for moveDelay timing

  // Per-type AI parameters (from MonsterTypeDef)
  float atkCooldownTime = 1.8f; // Seconds between attacks
  float moveDelay = 0.4f;       // Seconds per grid step
  uint8_t moveRange = 3;        // Grid cells for wandering
  uint8_t viewRange = 5;        // Grid cells for aggro detection
  uint8_t attackRange = 1;      // Grid cells for attack range
  bool aggressive = false;      // true=red (auto-aggro)

  // Passive HP regen timer (1% maxHP per second when idle + out of combat)
  float regenTimer = 0.0f;

  // Main 5.2: StormTime — Twister stun (pauses AI for N ticks)
  int stormTime = 0;
  float stormTickTimer = 0.0f;

  // Aggro memory
  int aggroTargetFd = -1;  // FD of player who attacked us
  float aggroTimer = 0.0f; // Duration to keep aggro (negative = respawn immune)
  float repathTimer = 0.0f; // Timer for re-pathfinding during chase
  int chaseFailCount = 0;   // Consecutive pathfinding failures

  // WoW-style approach + evade
  float approachTimer = 0.0f;   // Time spent in APPROACHING state
  float staggerDelay = 0.0f;    // Per-monster random offset for attack timing
  bool evading = false;          // True during RETURNING (invulnerable)

  // Poison DoT debuff (Main 5.2: AT_SKILL_POISON, OpenMU PoisonMagicEffect)
  bool poisoned = false;        // Currently has poison debuff
  float poisonTickTimer = 0.0f; // Accumulator for 3-second tick interval
  float poisonDuration = 0.0f;  // Remaining poison duration
  int poisonDamage = 0;         // Flat damage per tick
  int poisonAttackerFd = -1;    // FD of player who applied poison (for XP/aggro)

  // Broadcast dedup (event-driven: only emit when something changes)
  uint8_t lastBroadcastTargetX = 0;
  uint8_t lastBroadcastTargetY = 0;
  bool lastBroadcastChasing = false;
  bool lastBroadcastIsMoving = false;
};

// Server-side ground drop
struct GroundDrop {
  uint16_t index;   // Unique drop ID
  int16_t defIndex; // -1=Zen, 0-511+=item def index
  uint8_t quantity;
  uint8_t itemLevel; // Enhancement +0..+2
  float worldX, worldZ;
  float age = 0.0f; // Seconds since spawn (despawn after 30s)
};

class GameWorld {
public:
  GameWorld();
  ~GameWorld(); // Defined in .cpp where PathFinder is complete

  // Load NPCs and monsters from database
  void LoadNpcsFromDB(Database &db, uint8_t mapId = 0);
  void LoadMonstersFromDB(Database &db, uint8_t mapId = 0);

  // Player info for server-side monster AI
  struct PlayerTarget {
    int fd;
    float worldX, worldZ;
    uint8_t gridX, gridY; // Pre-computed grid position
    int defense;
    int defenseRate;
    int life;
    bool dead;
    uint16_t level;
    float petDamageReduction = 0.0f; // Guardian Angel: 0.2 (20%)
  };

  // Monster attack result to broadcast
  struct MonsterAttackResult {
    int targetFd; // Which player to send to
    uint16_t monsterIndex;
    uint16_t damage;
    uint8_t damageType;   // 0=Miss, 1=Normal, 2=Crit, 3=Exc, ...
    uint16_t remainingHp; // Player's remaining HP after damage
  };

  // Poison DoT tick result to broadcast (reuses DAMAGE packet)
  struct PoisonTickResult {
    uint16_t monsterIndex;
    uint16_t damage;
    uint16_t remainingHp;
    int attackerFd; // For XP if poison kills
  };

  // Process poison DoT ticks on all monsters. Returns tick results to broadcast.
  std::vector<PoisonTickResult> ProcessPoisonTicks(float dt);

  // Monster target update to broadcast (event-driven, not periodic)
  struct MonsterMoveUpdate {
    uint16_t monsterIndex;
    uint8_t targetX, targetY; // Grid cell the monster is heading toward
    uint8_t chasing;          // 1=chasing player, 0=returning to spawn/idle
  };

  // NPC (guard) movement update to broadcast
  struct NpcMoveUpdate {
    uint16_t npcIndex;
    uint8_t targetX, targetY; // Grid cell the guard is heading toward
  };

  // Game tick — updates monster AI, respawn timers, drop aging, guard patrol
  void Update(float dt,
              std::function<void(uint16_t)> dropExpiredCallback = nullptr,
              std::vector<MonsterMoveUpdate> *outWanderMoves = nullptr,
              std::vector<NpcMoveUpdate> *outNpcMoves = nullptr,
              std::function<void(uint16_t)> guardKillCallback = nullptr);

  // Process monster AI: aggro, pathfinding, attacks. Returns attacks to
  // broadcast. Also populates outMoves with movement updates.
  std::vector<MonsterAttackResult>
  ProcessMonsterAI(float dt, std::vector<PlayerTarget> &players,
                   std::vector<MonsterMoveUpdate> &outMoves);

  const std::vector<NpcSpawn> &GetNpcs() const { return m_npcs; }

  // Guard NPC interaction: pause/resume patrol when a player talks to a guard
  void SetGuardInteracting(uint16_t npcType, int playerFd, bool interact);
  void ClearGuardInteractionsForPlayer(int playerFd);

  const std::vector<MonsterInstance> &GetMonsterInstances() const {
    return m_monsterInstances;
  }
  std::vector<MonsterInstance> &GetMonsterInstancesMut() {
    return m_monsterInstances;
  }

  // Find monster by unique index (returns nullptr if not found)
  MonsterInstance *FindMonster(uint16_t index);

  // Build viewport packets
  std::vector<uint8_t> BuildNpcViewportPacket() const;
  std::vector<uint8_t> BuildMonsterViewportPacket() const; // Legacy 0x1F
  std::vector<uint8_t>
  BuildMonsterViewportV2Packet() const; // New 0x34 with HP/state

  // Drops
  std::vector<GroundDrop> SpawnDrops(float worldX, float worldZ,
                                     int monsterLevel, uint16_t monsterType,
                                     Database &db);
  GroundDrop *FindDrop(uint16_t dropIndex);
  bool RemoveDrop(uint16_t dropIndex);
  const std::vector<GroundDrop> &GetDrops() const { return m_drops; }
  uint16_t AllocDropIndex() { return m_nextDropIndex++; }
  void AddDrop(const GroundDrop &drop) { m_drops.push_back(drop); }

  static constexpr float DYING_DURATION = 3.0f;
  static constexpr float RESPAWN_DELAY = 10.0f;
  static constexpr float DROP_DESPAWN_TIME = 30.0f;

  // Guard patrol constants (OpenMU: GuardIntelligence.cs)
  static constexpr float GUARD_WANDER_SPEED = 150.0f;
  static constexpr int GUARD_ATTACK_RANGE =
      3; // Grid cells — guards kill nearby monsters

  // Load terrain attributes (.att file) for walkability checks
  bool LoadTerrainAttributes(const std::string &attFilePath);
  bool LoadTerrainAttributesForMap(uint8_t mapId, const std::string &attFilePath);
  void SetActiveMap(uint8_t mapId);
  void ClearWorldData(); // Clear NPCs, monsters, drops for map transition
  bool IsWalkable(float worldX, float worldZ) const;
  bool IsSafeZone(float worldX, float worldZ) const;
  bool IsWalkableGrid(uint8_t gx, uint8_t gy) const;
  bool IsSafeZoneGrid(uint8_t gx, uint8_t gy) const;

  static constexpr int TERRAIN_SIZE = 256;
  static constexpr uint8_t TW_NOMOVE = 0x04;
  static constexpr uint8_t TW_SAFEZONE = 0x01;
  static constexpr uint8_t TW_NOGROUND = 0x08;

  // Guard patrol still uses tryMove (will be refactored separately)
  bool tryMove(float &x, float &z, float sX, float sZ) const;

  // Monster type definition lookup
  static const MonsterTypeDef *FindMonsterTypeDef(uint16_t type);

  // Terrain attributes accessor (for pathfinder)
  const std::vector<uint8_t> &GetTerrainAttributes() const {
    return m_terrainAttributes;
  }

private:
  std::vector<NpcSpawn> m_npcs;
  std::vector<MonsterInstance> m_monsterInstances;
  std::vector<GroundDrop> m_drops;
  std::vector<uint8_t> m_terrainAttributes; // 256x256 attribute grid (active map)
  std::unordered_map<uint8_t, std::vector<uint8_t>> m_mapTerrainAttributes; // Per-map
  uint8_t m_activeMapId = 0;
  uint16_t m_nextMonsterIndex = 2001;
  uint16_t m_nextDropIndex = 1;

  // Monster occupancy grid: true = cell has a monster
  bool m_monsterOccupancy[TERRAIN_SIZE * TERRAIN_SIZE] = {};
  void setOccupied(uint8_t gx, uint8_t gy, bool val);
  bool isOccupied(uint8_t gx, uint8_t gy) const;
  void rebuildOccupancyGrid();

  // A* pathfinder instance (heap-allocated to allow forward decl)
  std::unique_ptr<PathFinder> m_pathFinder;

  // AI state handlers
  void processIdle(MonsterInstance &mon, float dt,
                   std::vector<PlayerTarget> &players,
                   std::vector<MonsterMoveUpdate> &outMoves);
  void processWandering(MonsterInstance &mon, float dt,
                        std::vector<PlayerTarget> &players,
                        std::vector<MonsterMoveUpdate> &outMoves);
  void processChasing(MonsterInstance &mon, float dt,
                      std::vector<PlayerTarget> &players,
                      std::vector<MonsterMoveUpdate> &outMoves,
                      std::vector<MonsterAttackResult> &attacks);
  void processApproaching(MonsterInstance &mon, float dt,
                          std::vector<PlayerTarget> &players,
                          std::vector<MonsterMoveUpdate> &outMoves,
                          std::vector<MonsterAttackResult> &attacks);
  void processAttacking(MonsterInstance &mon, float dt,
                        std::vector<PlayerTarget> &players,
                        std::vector<MonsterMoveUpdate> &outMoves,
                        std::vector<MonsterAttackResult> &attacks);
  void processReturning(MonsterInstance &mon, float dt,
                        std::vector<MonsterMoveUpdate> &outMoves);

  // Attack stagger: offset attack timers for multi-monster encounters
  float calculateStaggerDelay(int targetFd) const;

  // Grid-step path advancement: returns true if monster moved one cell
  bool advancePathStep(MonsterInstance &mon, float dt,
                       std::vector<MonsterMoveUpdate> &outMoves, bool chasing);

  // Find closest valid target within viewRange
  PlayerTarget *findBestTarget(const MonsterInstance &mon,
                               std::vector<PlayerTarget> &players) const;

  // Emit broadcast only when grid cell/state changes
  static void emitMoveIfChanged(MonsterInstance &mon, uint8_t targetX,
                                uint8_t targetY, bool chasing, bool moving,
                                std::vector<MonsterMoveUpdate> &outMoves);

  // Direction from grid delta
  static uint8_t dirFromDelta(int dx, int dy);
};

#endif // MU_GAME_WORLD_HPP

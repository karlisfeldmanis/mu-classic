// MU Online Map Analyzer — Comprehensive map intelligence tool
// Build: g++ -std=c++17 -O2 -I../client/external/glm -I../server/external/sqlite map_analyze.cpp -lsqlite3 -o map_analyze
//
// Extracts complete map information:
// - Terrain files (.map, .att, .obj, height, lightmap)
// - Monster spawns from database (types, counts, levels, density)
// - Object types with VFX/sound/lighting descriptions
// - Map atmosphere (sky, ambient sound, music, fog, luminosity)
// - Safe zones, walkable areas, void regions
// - World object types (fires, doors, gates, decorations)
// - Cross-references with Main 5.2 rendering code
//
// Usage:
//   map_analyze --map 1                    # Full analysis of map ID 1 (Dungeon)
//   map_analyze --map 4 --monsters         # Lost Tower monster spawns
//   map_analyze --map 0 --objects --vfx    # Lorencia objects with VFX
//   map_analyze --all                      # Analyze all maps

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// SQLite for database queries
#include "sqlite3.h"

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
// Configuration & Reference Data
// ═══════════════════════════════════════════════════════════════════════════

static const char *DATA_PATH = "../client/Data";
static const char *DB_PATH = "../server/build/mu_server.db";

struct MapInfo {
  int mapId;
  const char *name;
  const char *terrainFile;
  const char *ambientSound;
  const char *safeMusic;
  const char *wildMusic;
  bool hasSky, hasGrass, hasWind;
  float luminosity;
  const char *atmosphere; // description
};

static const MapInfo MAP_INFO[] = {
    {0, "Lorencia", "EncTerrain1", "SOUND_WIND01", "MuTheme.mp3", nullptr, true,
     true, true, 1.0f, "Warm grassland, gentle hills, fountain center"},
    {1, "Dungeon", "EncTerrain2", "SOUND_DUNGEON01", "Dungeon.mp3", nullptr,
     false, false, false, 0.9f,
     "Dark underground, 3 floors, torch-lit corridors"},
    {2, "Devias", "EncTerrain3", "SOUND_WIND01", "Devias.mp3", nullptr, false,
     true, true, 1.0f, "Cool desert ruins, roofed houses"},
    {3, "Noria", "EncTerrain4", "SOUND_WIND01", "Noria.mp3", nullptr, true,
     true, true, 1.0f, "Forest town, green atmosphere"},
    {4, "Lost Tower", "EncTerrain5", "SOUND_DUNGEON01", "lost_tower_a.mp3",
     nullptr, false, false, false, 0.9f,
     "Dark tower dungeon, 7 floors, metallic/stone architecture"},
};

struct ObjectTypeInfo {
  int type;
  const char *name;
  const char *model; // Object##.bmd or named
  const char *vfx;   // VFX description
  const char *sound; // Sound effect
  int blendMesh;     // -1 = none
  const char *pointLight; // "r,g,b,range" or empty
};

static const ObjectTypeInfo OBJECT_TYPES[] = {
    // Lorencia (Object1/)
    {1, "Tree01", "Object1/Tree01.bmd", "", "", -1, ""},
    {2, "Tree02", "Object1/Tree02.bmd", "", "", -1, ""},
    {3, "Tree03 (Chrome)", "Object1/Tree03.bmd",
     "Chrome/metallic scrolling UV (BlendMeshTexCoordU)", "", -1, ""},
    {4, "Tree04 (Chrome)", "Object1/Tree04.bmd",
     "Chrome/metallic scrolling UV (BlendMeshTexCoordU)", "", -1, ""},
    {5, "Tree05", "Object1/Tree05.bmd", "", "", -1, ""},
    {50, "FireLight01", "Object1/FireLight01.bmd",
     "Fire particle emitter (ambient)", "SOUND_FIRE_LOOP", 1,
     "255,150,50,400"},
    {51, "FireLight02", "Object1/FireLight02.bmd",
     "Fire particle emitter (ambient)", "SOUND_FIRE_LOOP", 1,
     "255,150,50,400"},
    {52, "Bonfire01", "Object1/Bonfire01.bmd",
     "Fire particles, wider flicker (0.4-0.9)", "SOUND_FIRE_LOOP", 1,
     "255,200,100,500"},
    {55, "DungeonGate01", "Object1/DungeonGate01.bmd",
     "Fire braziers, gate entrance", "SOUND_FIRE_LOOP", 1, "255,150,50,400"},
    {80, "Bridge01", "Object1/Bridge01.bmd", "Walkable bridge over void", "", -1,
     ""},
    {81, "Statue01", "Object1/Statue01.bmd", "", "", -1, ""},
    {82, "Statue02", "Object1/Statue02.bmd", "", "", -1, ""},
    {85, "BridgeStone01", "Object1/BridgeStone01.bmd",
     "Stone bridge over dungeon rifts", "", -1, ""},
    {90, "StreetLight01", "Object1/StreetLight01.bmd", "Constant glow (1.0)",
     "", 1, "255,255,200,300"},
    {96, "Fence01", "Object1/Fence01.bmd", "", "", -1, ""},
    {98, "Carriage01", "Object1/Carriage01.bmd", "Window light BlendMesh=2", "",
     2, ""},
    {99, "Fence03", "Object1/Fence03.bmd", "", "", -1, ""},
    {105, "Waterspout01", "Object1/Waterspout01.bmd",
     "Water smoke emitters (+180, +120)", "", 3, ""},
    {117, "House03", "Object1/House03.bmd", "Window flicker (0.4-0.7)", "", 4,
     "255,200,150,250"},
    {118, "House04", "Object1/House04.bmd",
     "Window flicker + UV scroll (StreamMesh)", "", 8, "255,200,150,250"},
    {119, "House05", "Object1/House05.bmd",
     "Window flicker + UV scroll (StreamMesh)", "", 2, "255,200,150,250"},
    {122, "HouseWall02", "Object1/HouseWall02.bmd", "Window flicker (0.4-0.7)",
     "", 4, "255,200,150,250"},
    {125, "Fountain", "Object1/Fountain.bmd", "Water animation (wave texture)",
     "SOUND_WATERFALL_LOOP", -1, ""},
    {126, "Fountain_Bottom", "Object1/Fountain_Bottom.bmd",
     "Submerged base (static)", "", -1, ""},
    {127, "HouseEtc01", "Object1/HouseEtc01.bmd", "Flag pole (static)", "", -1,
     ""},
    {130, "Light01", "Object1/Light01.bmd", "Fire emitter (ambient)", "", -1,
     "255,150,50,350"},
    {150, "Candle01", "Object1/Candle01.bmd", "Constant glow (1.0)", "", 1,
     "255,200,100,200"},

    // Dungeon (Object2/)
    {11, "CliffWall", "Object2/Object11.bmd",
     "Cliff face fade (cliffBottomFade)", "", -1, ""},
    {12, "BridgePlank01", "Object2/Object12.bmd", "Bridge support plank", "", -1,
     ""},
    {13, "BridgeSupport01", "Object2/Object13.bmd", "Vertical bridge post", "",
     -1, ""},
    {39, "LanceTrap (REMOVED)", "Object2/Object40.bmd",
     "Trap removed — objects remain in .obj files but all code/AI deleted", "", -1,
     ""},
    {40, "IronStickTrap (REMOVED)", "Object2/Object41.bmd",
     "Trap removed — objects remain in .obj files but all code/AI deleted", "", -1,
     ""},
    {41, "Torch01 (Dungeon)", "Object2/Object42.bmd",
     "Fire glow mesh (BlendMesh=1), phase-offset flicker", "", 1,
     "255,180,100,350"},
    {42, "Torch02 (Dungeon)", "Object2/Object43.bmd",
     "Fire glow mesh (BlendMesh=1), phase-offset flicker", "", 1,
     "255,180,100,350"},
    {51, "FireTrap (REMOVED)", "Object2/Object52.bmd",
     "Trap removed — objects remain in .obj files but all code/AI deleted", "", -1,
     ""},

    // Lost Tower (Object5/)
    {19, "MagicOrb", "Object5/Object20.bmd",
     "BITMAP_MAGIC+1 sprites at bones 15,19,21", "", -1, "255,150,255,300"},
    {20, "LightningOrb", "Object5/Object21.bmd",
     "BITMAP_LIGHTNING+1 sprites at bones 15,19,21", "", -1,
     "150,150,255,300"},
    {24, "FlamePillar", "Object5/Object25.bmd",
     "HiddenMesh=-2, BITMAP_FLAME particles (1/64 prob)", "", -1, ""},
    {25, "MeteoriteTrap (REMOVED)", "Object5/Object26.bmd",
     "HiddenMesh=-2, trap removed — objects remain in .obj but inactive", "", -1,
     ""},
    {38, "Skull01", "Object5/Object39.bmd",
     "CheckSkull() — tracks player, rotates, plays SOUND_BONE2", "", -1, ""},
    // NOTE: Types 39-40 in Lost Tower are trap objects (LanceTrap/IronStickTrap),
    // already defined above in Dungeon section. NOT Skull02/LightningColumn.
};

static const char *GetObjectTypeName(int type) {
  for (auto &info : OBJECT_TYPES) {
    if (info.type == type)
      return info.name;
  }
  return "Unknown";
}

static const ObjectTypeInfo *GetObjectTypeInfo(int type) {
  for (auto &info : OBJECT_TYPES) {
    if (info.type == type)
      return &info;
  }
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Encryption (same as map_inspect.cpp)
// ═══════════════════════════════════════════════════════════════════════════

static const uint8_t MAP_XOR_KEY[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                         0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                         0x37, 0xB3, 0xE7, 0xA2};

static std::vector<uint8_t> DecryptMapFile(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> decrypted(data.size());
  uint8_t map_key = 0x5E;
  for (size_t i = 0; i < data.size(); ++i) {
    uint8_t src_byte = data[i];
    uint8_t xor_byte = MAP_XOR_KEY[i % 16];
    uint8_t val = (src_byte ^ xor_byte) - map_key;
    decrypted[i] = val;
    map_key = src_byte + 0x3D;
  }
  return decrypted;
}

static std::vector<uint8_t> ApplyBuxConvert(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> result = data;
  uint8_t bux_code[3] = {0xFC, 0xCF, 0xAB};
  for (size_t i = 0; i < result.size(); ++i)
    result[i] ^= bux_code[i % 3];
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// File Parsers
// ═══════════════════════════════════════════════════════════════════════════

static const int TERRAIN_SIZE = 256;

struct AttFileData {
  std::vector<uint8_t> attributes;
  std::vector<uint8_t> symmetry;
};

struct ObjFileData {
  struct Object {
    int16_t type;
    float posX, posY, posZ;
    float angleX, angleY, angleZ;
    float scale;
  };
  std::vector<Object> objects;
};

static AttFileData ParseAttFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  AttFileData res{};
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;
  if (!file)
    return res;

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = ApplyBuxConvert(DecryptMapFile(raw));

  const size_t word_size = 4 + cells * 2;
  if (data.size() >= word_size) {
    res.attributes.resize(cells);
    res.symmetry.resize(cells);
    for (size_t i = 0; i < cells; ++i) {
      res.attributes[i] = data[4 + i * 2];
      res.symmetry[i] = data[5 + i * 2];
    }
  } else if (data.size() >= 4 + cells) {
    res.attributes.assign(data.begin() + 4, data.begin() + 4 + cells);
  }
  return res;
}

static ObjFileData ParseObjFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  ObjFileData res{};
  if (!file)
    return res;

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = DecryptMapFile(raw);
  if (data.size() < 4)
    return res;

  int16_t count;
  memcpy(&count, data.data() + 2, 2);
  if (count < 0 || count > 10000)
    return res;

  const size_t expected = 4 + count * 30;
  if (data.size() < expected)
    return res;

  res.objects.reserve(count);
  size_t ptr = 4;
  for (int i = 0; i < count; ++i) {
    ObjFileData::Object obj{};
    memcpy(&obj.type, data.data() + ptr, 2);
    ptr += 2;
    memcpy(&obj.posX, data.data() + ptr, 4);
    memcpy(&obj.posY, data.data() + ptr + 4, 4);
    memcpy(&obj.posZ, data.data() + ptr + 8, 4);
    ptr += 12;
    memcpy(&obj.angleX, data.data() + ptr, 4);
    memcpy(&obj.angleY, data.data() + ptr + 4, 4);
    memcpy(&obj.angleZ, data.data() + ptr + 8, 4);
    ptr += 12;
    memcpy(&obj.scale, data.data() + ptr, 4);
    ptr += 4;
    res.objects.push_back(obj);
  }
  return res;
}

// ═══════════════════════════════════════════════════════════════════════════
// Monster Definitions (from GameWorld.cpp)
// ═══════════════════════════════════════════════════════════════════════════

struct MonsterTypeDef {
  uint16_t type;
  const char *name;
  int hp, defense, defenseRate;
  int attackMin, attackMax, attackRate;
  int level;
  float attackCooldown, moveDelay;
  int moveRange, viewRange, attackRange;
  bool aggressive;
  float respawnTime;
};

static const MonsterTypeDef MONSTER_DEFS[] = {
    // Lorencia
    {0, "Bull Fighter", 100, 6, 6, 16, 20, 28, 6, 1.6f, 0.4f, 3, 5, 1, true, 3.0f},
    {1, "Hound", 140, 9, 9, 22, 27, 39, 9, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},
    {2, "Budge Dragon", 60, 3, 3, 10, 13, 18, 4, 2.0f, 0.4f, 3, 4, 1, true, 3.0f},
    {3, "Spider", 30, 1, 1, 4, 7, 8, 2, 1.8f, 0.6f, 2, 5, 1, true, 10.0f},
    {4, "Elite Bull Fighter", 190, 12, 12, 31, 36, 50, 12, 1.4f, 0.4f, 3, 4, 1, true, 10.0f},
    {6, "Lich", 255, 14, 14, 41, 46, 62, 14, 2.0f, 0.4f, 3, 7, 4, true, 10.0f},
    {7, "Giant", 400, 18, 18, 57, 62, 80, 17, 2.2f, 0.4f, 2, 3, 2, true, 10.0f},
    {14, "Skeleton Warrior", 525, 22, 22, 68, 74, 93, 19, 1.4f, 0.4f, 2, 4, 1, true, 10.0f},
    {15, "Skeleton Archer", 1100, 45, 41, 115, 120, 170, 34, 2.0f, 0.4f, 2, 7, 4, true, 10.0f},
    {16, "Elite Skeleton", 1800, 65, 49, 135, 140, 210, 42, 1.6f, 0.4f, 2, 4, 1, true, 10.0f},
    // Dungeon
    {5, "Hell Hound", 1400, 55, 45, 125, 130, 190, 38, 1.2f, 0.4f, 3, 4, 1, true, 10.0f},
    {8, "Poison Bull", 2500, 75, 61, 145, 150, 230, 46, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},
    {9, "Thunder Lich", 2000, 70, 55, 140, 145, 220, 44, 2.2f, 0.4f, 3, 7, 4, true, 10.0f},
    {10, "Dark Knight", 3000, 80, 70, 150, 155, 240, 48, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},
    {11, "Ghost", 1000, 40, 39, 110, 115, 160, 32, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},
    {12, "Larva", 750, 31, 31, 90, 95, 125, 25, 1.8f, 0.4f, 3, 4, 1, true, 20.0f},
    {13, "Hell Spider", 1600, 60, 47, 130, 135, 200, 40, 1.6f, 0.4f, 3, 7, 4, true, 10.0f},
    {17, "Cyclops", 850, 35, 35, 100, 105, 140, 28, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},
    {18, "Gorgon", 6000, 100, 82, 165, 175, 275, 55, 1.6f, 0.4f, 3, 7, 1, true, 10.0f},
    // Devias
    {19, "Yeti", 900, 37, 37, 105, 110, 150, 30, 2.0f, 0.4f, 2, 6, 4, true, 3.0f},
    {20, "Elite Yeti", 1200, 50, 43, 120, 125, 180, 36, 1.4f, 0.4f, 3, 6, 1, true, 3.0f},
    {21, "Assassin", 800, 33, 33, 95, 100, 130, 26, 2.0f, 0.4f, 2, 7, 1, true, 10.0f},
    {22, "Ice Monster", 650, 27, 27, 80, 85, 110, 22, 2.0f, 0.4f, 2, 5, 1, true, 10.0f},
    {23, "Hommerd", 700, 29, 29, 85, 90, 120, 24, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},
    {24, "Worm", 600, 25, 25, 75, 80, 100, 20, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},
    {25, "Ice Queen", 4000, 90, 76, 155, 165, 260, 52, 1.4f, 0.4f, 3, 7, 4, true, 50.0f},
    // Noria
    {26, "Goblin", 45, 2, 2, 7, 10, 13, 3, 1.8f, 0.4f, 2, 4, 1, true, 10.0f},
    {27, "Chain Scorpion", 80, 4, 4, 13, 17, 23, 5, 1.8f, 0.4f, 3, 4, 1, true, 10.0f},
    {28, "Beetle Monster", 165, 10, 10, 26, 31, 44, 10, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},
    {29, "Hunter", 220, 13, 13, 36, 41, 56, 13, 1.6f, 0.4f, 3, 4, 4, true, 10.0f},
    {30, "Forest Monster", 295, 15, 15, 46, 51, 68, 15, 1.6f, 0.4f, 3, 4, 1, true, 10.0f},
    {31, "Agon", 340, 16, 16, 51, 57, 74, 16, 1.4f, 0.4f, 2, 4, 1, true, 10.0f},
    {32, "Stone Golem", 465, 20, 20, 62, 68, 86, 18, 2.2f, 0.4f, 2, 3, 2, true, 10.0f},
    {33, "Elite Goblin", 120, 8, 8, 19, 23, 33, 8, 1.6f, 0.4f, 3, 5, 1, true, 10.0f},
    // Lost Tower
    {34, "Cursed Wizard", 4000, 95, 80, 160, 170, 270, 54, 2.0f, 0.4f, 3, 7, 4, true, 10.0f},
    {35, "Death Gorgon", 6000, 130, 94, 200, 210, 320, 64, 2.0f, 0.4f, 3, 7, 1, true, 10.0f},
    {36, "Shadow", 2800, 78, 67, 148, 153, 235, 47, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},
    {37, "Devil", 5000, 115, 88, 180, 195, 300, 60, 2.0f, 0.4f, 3, 7, 4, true, 10.0f},
    {38, "Balrog", 9000, 160, 99, 220, 240, 330, 66, 1.6f, 0.4f, 3, 7, 2, true, 150.0f},
    {39, "Poison Shadow", 3500, 85, 73, 155, 160, 250, 50, 1.4f, 0.4f, 3, 5, 1, true, 10.0f},
    {40, "Death Knight", 5500, 120, 91, 190, 200, 310, 62, 1.8f, 0.4f, 3, 7, 1, true, 10.0f},
    {41, "Death Cow", 4500, 110, 85, 170, 180, 285, 57, 1.6f, 0.4f, 3, 7, 1, true, 10.0f},
    // Summons
    {150, "Bali", 5000, 100, 75, 165, 170, 260, 52, 1.6f, 0.20f, 3, 7, 1, false, 10.0f},
};

static const MonsterTypeDef *FindMonsterDef(int type) {
  for (auto &def : MONSTER_DEFS) {
    if (def.type == (uint16_t)type)
      return &def;
  }
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Database Queries
// ═══════════════════════════════════════════════════════════════════════════

struct MonsterSpawn {
  int type;
  std::string name;
  int level;
  int hp;
  int gridX, gridY;
};

static std::vector<MonsterSpawn> QueryMonsterSpawns(sqlite3 *db, int mapId) {
  std::vector<MonsterSpawn> spawns;
  const char *sql = R"(
    SELECT type, pos_x, pos_y
    FROM monster_spawns
    WHERE map_id = ?
    ORDER BY type, pos_x, pos_y
  )";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQL error: " << sqlite3_errmsg(db) << std::endl;
    return spawns;
  }

  sqlite3_bind_int(stmt, 1, mapId);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MonsterSpawn spawn;
    spawn.type = sqlite3_column_int(stmt, 0);
    spawn.gridX = sqlite3_column_int(stmt, 1);
    spawn.gridY = sqlite3_column_int(stmt, 2);

    // Look up monster def
    auto *def = FindMonsterDef(spawn.type);
    if (def) {
      spawn.name = def->name;
      spawn.level = def->level;
      spawn.hp = def->hp;
    } else {
      spawn.name = "Unknown";
      spawn.level = 0;
      spawn.hp = 0;
    }

    spawns.push_back(spawn);
  }

  sqlite3_finalize(stmt);
  return spawns;
}

// ═══════════════════════════════════════════════════════════════════════════
// Analysis & Reporting
// ═══════════════════════════════════════════════════════════════════════════

static void AnalyzeMap(int mapId, bool showMonsters, bool showObjects,
                       bool showVFX, bool showTerrain) {
  // Find map info
  const MapInfo *info = nullptr;
  for (auto &m : MAP_INFO) {
    if (m.mapId == mapId) {
      info = &m;
      break;
    }
  }
  if (!info) {
    std::cerr << "Unknown map ID: " << mapId << std::endl;
    return;
  }

  std::cout << "\n";
  std::cout
      << "═══════════════════════════════════════════════════════════════\n";
  std::cout << "  MAP " << mapId << ": " << info->name << "\n";
  std::cout
      << "═══════════════════════════════════════════════════════════════\n";

  // ── Atmosphere ──
  std::cout << "\n┌─ ATMOSPHERE ─────────────────────────────────────┐\n";
  std::cout << "│ Description:   " << info->atmosphere << "\n";
  std::cout << "│ Ambient Sound: " << info->ambientSound << "\n";
  std::cout << "│ Safe Music:    " << info->safeMusic << "\n";
  if (info->wildMusic)
    std::cout << "│ Wild Music:    " << info->wildMusic << "\n";
  std::cout << "│ Features:      ";
  if (info->hasSky)
    std::cout << "Sky ";
  if (info->hasGrass)
    std::cout << "Grass ";
  if (info->hasWind)
    std::cout << "Wind3D ";
  std::cout << "\n";
  std::cout << "│ Luminosity:    " << info->luminosity << "\n";
  std::cout << "└──────────────────────────────────────────────────┘\n";

  // ── Terrain Files ──
  if (showTerrain) {
    std::string worldDir =
        std::string(DATA_PATH) + "/World" + std::to_string(mapId + 1) + "/";
    std::string attPath = worldDir + info->terrainFile + ".att";
    std::string objPath = worldDir + info->terrainFile + ".obj";

    std::cout << "\n┌─ TERRAIN DATA ───────────────────────────────────┐\n";
    std::cout << "│ Terrain File:  " << info->terrainFile << "\n";
    std::cout << "│ World Dir:     World" << (mapId + 1) << "/\n";

    if (fs::exists(attPath)) {
      AttFileData att = ParseAttFile(attPath);
      if (!att.attributes.empty()) {
        int safezone = 0, nomove = 0, noground = 0, walkable = 0;
        for (auto a : att.attributes) {
          if (a & 0x01)
            safezone++;
          if (a & 0x04)
            nomove++;
          if (a & 0x08)
            noground++;
          if ((a & 0x0C) == 0)
            walkable++;
        }
        const int cells = TERRAIN_SIZE * TERRAIN_SIZE;
        printf("│ Safe Zone:     %5d cells (%5.2f%%)\n", safezone,
               safezone * 100.0f / cells);
        printf("│ Walls/Blocked: %5d cells (%5.2f%%)\n", nomove,
               nomove * 100.0f / cells);
        printf("│ Void/NoGround: %5d cells (%5.2f%%)\n", noground,
               noground * 100.0f / cells);
        printf("│ Walkable:      %5d cells (%5.2f%%)\n", walkable,
               walkable * 100.0f / cells);
      }
    }
    std::cout << "└──────────────────────────────────────────────────┘\n";
  }

  // ── World Objects ──
  if (showObjects) {
    std::string worldDir =
        std::string(DATA_PATH) + "/World" + std::to_string(mapId + 1) + "/";
    std::string objPath = worldDir + info->terrainFile + ".obj";

    if (fs::exists(objPath)) {
      ObjFileData obj = ParseObjFile(objPath);
      if (!obj.objects.empty()) {
        std::cout << "\n┌─ WORLD OBJECTS ──────────────────────────────────┐\n";
        std::cout << "│ Total Objects: " << obj.objects.size() << "\n";

        // Group by type
        std::map<int16_t, int> typeCounts;
        for (auto &o : obj.objects)
          typeCounts[o.type]++;

        std::cout << "│\n";
        std::cout << "│ Type Distribution:\n";
        std::vector<std::pair<int16_t, int>> sorted(typeCounts.begin(),
                                                     typeCounts.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](auto &a, auto &b) { return a.second > b.second; });

        for (auto &kv : sorted) {
          const char *name = GetObjectTypeName(kv.first);
          printf("│   Type %3d: %4d × %-30s\n", kv.first, kv.second, name);
        }
        std::cout << "└──────────────────────────────────────────────────┘\n";

        // VFX/Sound details
        if (showVFX) {
          std::cout
              << "\n┌─ OBJECT VFX & EFFECTS ───────────────────────────┐\n";
          std::set<int16_t> uniqueTypes;
          for (auto &o : obj.objects)
            uniqueTypes.insert(o.type);

          for (int t : uniqueTypes) {
            auto *oti = GetObjectTypeInfo(t);
            if (!oti)
              continue;
            if (strlen(oti->vfx) == 0 && strlen(oti->sound) == 0 &&
                oti->blendMesh == -1 && strlen(oti->pointLight) == 0)
              continue;

            printf("│ Type %d: %s\n", t, oti->name);
            if (strlen(oti->model) > 0)
              printf("│   Model:       %s\n", oti->model);
            if (strlen(oti->vfx) > 0)
              printf("│   VFX:         %s\n", oti->vfx);
            if (strlen(oti->sound) > 0)
              printf("│   Sound:       %s\n", oti->sound);
            if (oti->blendMesh >= 0)
              printf("│   BlendMesh:   %d (additive glow)\n", oti->blendMesh);
            if (strlen(oti->pointLight) > 0)
              printf("│   Point Light: %s\n", oti->pointLight);
            printf("│\n");
          }
          std::cout << "└──────────────────────────────────────────────────┘\n";
        }
      }
    }
  }

  // ── Monster Spawns ──
  if (showMonsters) {
    sqlite3 *db = nullptr;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
      std::cerr << "Cannot open database: " << DB_PATH << std::endl;
    } else {
      auto spawns = QueryMonsterSpawns(db, mapId);
      sqlite3_close(db);

      if (!spawns.empty()) {
        std::cout << "\n┌─ MONSTER SPAWNS ─────────────────────────────────┐\n";
        std::cout << "│ Total Spawns:  " << spawns.size() << "\n";

        // Group by type
        std::map<int, int> typeCounts;
        std::map<int, MonsterSpawn> typeExample;
        for (auto &s : spawns) {
          typeCounts[s.type]++;
          if (typeExample.count(s.type) == 0)
            typeExample[s.type] = s;
        }

        std::cout << "│\n";
        std::cout << "│ Monster Distribution:\n";
        std::vector<std::pair<int, int>> sorted(typeCounts.begin(),
                                                 typeCounts.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](auto &a, auto &b) { return a.second > b.second; });

        for (auto &kv : sorted) {
          auto &ex = typeExample[kv.first];
          printf("│   Type %3d: %4d × %-20s (Lv%3d, HP=%d)\n", kv.first,
                 kv.second, ex.name.c_str(), ex.level, ex.hp);
        }

        // Density analysis
        const int R = 16; // 16x16 grid regions
        const int C = 16;
        std::vector<int> density(C * C, 0);
        for (auto &s : spawns) {
          int rx = s.gridX / R, ry = s.gridY / R;
          if (rx >= 0 && ry >= 0 && rx < C && ry < C)
            density[ry * C + rx]++;
        }
        int maxDensity = *std::max_element(density.begin(), density.end());

        std::cout << "│\n";
        std::cout << "│ Spawn Density Heatmap (16x16 regions, 16×16 grid "
                     "cells each):\n";
        for (int y = 0; y < C; ++y) {
          std::cout << "│   ";
          for (int x = 0; x < C; ++x) {
            int d = density[y * C + x];
            char c = '.';
            if (d == 0)
              c = ' ';
            else if (d * 4 < maxDensity)
              c = ':';
            else if (d * 2 < maxDensity)
              c = '+';
            else if (d * 4 < maxDensity * 3)
              c = 'o';
            else
              c = 'O';
            printf("%c", c);
          }
          printf("\n");
        }
        printf("│   Max: %d spawns/region\n", maxDensity);

        std::cout << "└──────────────────────────────────────────────────┘\n";
      } else {
        std::cout << "\n┌─ MONSTER SPAWNS ─────────────────────────────────┐\n";
        std::cout << "│ No monster spawns found in database\n";
        std::cout << "└──────────────────────────────────────────────────┘\n";
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

static void PrintUsage() {
  std::cout << R"(
MU Online Map Analyzer — Comprehensive map intelligence tool

Usage:
  map_analyze --map <id> [options]

Options:
  --map <id>     Map ID to analyze (0=Lorencia, 1=Dungeon, 2=Devias, 3=Noria, 4=LostTower)
  --monsters     Show monster spawn data from database
  --objects      Show world objects with counts
  --vfx          Show VFX/sound/lighting details for objects
  --terrain      Show terrain attribute statistics
  --all          Enable all analysis modes

Examples:
  map_analyze --map 0 --all                  # Full Lorencia analysis
  map_analyze --map 1 --monsters --objects   # Dungeon spawns + objects
  map_analyze --map 4 --vfx                  # Lost Tower VFX reference
)";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  int mapId = -1;
  bool showMonsters = false, showObjects = false, showVFX = false;
  bool showTerrain = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--map" && i + 1 < argc)
      mapId = std::atoi(argv[++i]);
    else if (arg == "--monsters")
      showMonsters = true;
    else if (arg == "--objects")
      showObjects = true;
    else if (arg == "--vfx")
      showVFX = true;
    else if (arg == "--terrain")
      showTerrain = true;
    else if (arg == "--all") {
      showMonsters = showObjects = showVFX = showTerrain = true;
    }
  }

  if (mapId < 0) {
    std::cerr << "Error: --map <id> is required\n";
    PrintUsage();
    return 1;
  }

  // Default: show everything if no flags
  if (!showMonsters && !showObjects && !showVFX && !showTerrain) {
    showMonsters = showObjects = showVFX = showTerrain = true;
  }

  AnalyzeMap(mapId, showMonsters, showObjects, showVFX, showTerrain);

  return 0;
}

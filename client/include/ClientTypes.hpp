#ifndef MU_CLIENT_TYPES_HPP
#define MU_CLIENT_TYPES_HPP

#include "HeroCharacter.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ── Character class codes and starting stats ──

enum ClassCode : uint8_t {
  CLASS_DW = 0,
  CLASS_DK = 16,
  CLASS_ELF = 32,
  CLASS_MG = 48,
};

struct ClassStartingStats {
  uint8_t classCode;
  const char *name;
  int str, dex, vit, ene;
  int hp, mp;
};

// OpenMU Version075 starting stats per class
inline const ClassStartingStats &GetClassStats(uint8_t classCode) {
  static const ClassStartingStats stats[] = {
      {CLASS_DW, "Dark Wizard", 18, 18, 15, 30, 60, 60},
      {CLASS_DK, "Dark Knight", 28, 20, 25, 10, 110, 20},
      {CLASS_ELF, "Fairy Elf", 22, 25, 20, 15, 80, 30},
      {CLASS_MG, "Magic Gladiator", 26, 26, 26, 26, 110, 60},
  };
  for (auto &s : stats)
    if (s.classCode == classCode)
      return s;
  static const ClassStartingStats fallback = {0, "Unknown", 20, 20, 20, 20, 60,
                                              30};
  return fallback;
}

// ── Client-side item definition (synced from server item_definitions table) ──

struct ClientItemDefinition {
  uint8_t category = 0;
  uint8_t itemIndex = 0;
  std::string name;
  std::string modelFile;
  uint16_t reqStr = 0;
  uint16_t reqDex = 0;
  uint16_t reqVit = 0;
  uint16_t reqEne = 0;
  uint16_t levelReq = 0;
  uint8_t width = 1;
  uint8_t height = 1;
  uint32_t classFlags = 0xFFFFFFFF;
  uint16_t dmgMin = 0;
  uint16_t dmgMax = 0;
  uint16_t defense = 0;
  uint8_t attackSpeed = 0;
  bool twoHanded = false;
  uint32_t buyPrice = 0;
  uint16_t magicPower = 0;  // Staff Rise base value (for tooltip display)
};

// ── Client-side inventory slot ──

static constexpr int INVENTORY_SLOTS = 64;

struct ClientInventoryItem {
  int16_t defIndex = -2;
  uint8_t quantity = 0;
  uint8_t itemLevel = 0;
  uint8_t optionFlags = 0;
  bool occupied = false;
  bool primary = false;
};

// ── Equipment display slot ──

struct ClientEquipSlot {
  uint8_t category = 0xFF;
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;
  uint8_t quantity = 0;    // Stack count (arrows/bolts)
  uint8_t optionFlags = 0;
  std::string modelFile;
  bool equipped = false;
};

// ── Ground item drops ──

struct GroundItem {
  uint16_t dropIndex;
  int16_t defIndex; // -1=Zen
  int quantity;
  uint8_t itemLevel;
  uint8_t optionFlags = 0;
  glm::vec3 position;
  float timer;
  bool active;

  // Physics state
  glm::vec3 angle;
  float gravity;
  float scale;
  bool isResting;

  // Sparkle effect timer (Main 5.2: every ~1.92s)
  float sparkleTimer = 0.0f;
};

static constexpr int MAX_GROUND_ITEMS = 64;

// ── Shop item (received from server) ──

struct ShopItem {
  int16_t defIndex = 0;
  uint8_t itemLevel = 0;
  uint32_t buyPrice = 0;
};

// ── Server equipment slot (for initial sync) ──

struct ServerEquipSlot {
  uint8_t slot = 0;
  WeaponEquipInfo info;
};

// ── Initial server data (populated during connection burst) ──

struct ServerData {
  std::vector<ServerNpcSpawn> npcs;
  std::vector<ServerMonsterSpawn> monsters;
  std::vector<ServerEquipSlot> equipment;
  bool connected = false;
  uint8_t spawnGridX = 0; // Character spawn position from CharInfo (F3:03)
  uint8_t spawnGridY = 0;
  uint8_t spawnMapId = 0; // Character map from CharInfo
  bool hasSpawnPos = false;
  uint16_t cameraZoom = 8000; // Camera zoom * 10 (from server)
};

#endif // MU_CLIENT_TYPES_HPP

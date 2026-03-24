#ifndef MU_ITEM_DATABASE_HPP
#define MU_ITEM_DATABASE_HPP

#include "ClientTypes.hpp"
#include <cstdint>
#include <map>
#include <string>

// Drop item definition from the static items[] table
struct DropDef {
  const char *name;
  const char *model;
  int dmgMin;
  int dmgMax;
  int defense;
};

namespace ItemDatabase {

// Initialize the g_itemDefs map (must be called once at startup)
void Init();

// Load item definitions from server catalog packet (replaces hardcoded data)
void LoadFromCatalog(const uint8_t *packetData, size_t packetLen);

// Access the global item definitions map
std::map<int16_t, ClientItemDefinition> &GetItemDefs();

// Lookup functions
const DropDef *GetDropInfo(int16_t defIndex);
const char *GetDropName(int16_t defIndex);
const char *GetDropModelName(int16_t defIndex);
const char *GetItemNameByDef(int16_t defIndex);
int16_t GetDefIndexFromCategory(uint8_t category, uint8_t index);
void GetItemCategoryAndIndex(int16_t defIndex, uint8_t &cat, uint8_t &idx);
std::string GetBodyPartModelFile(uint8_t category, uint8_t index);
int GetBodyPartIndex(uint8_t category);
const char *GetEquipSlotName(int slot);
const char *GetCategoryName(uint8_t category);

} // namespace ItemDatabase

#endif // MU_ITEM_DATABASE_HPP

#include "ItemDatabase.hpp"
#include "PacketDefs.hpp"
#include <cstdio>
#include <cstring>

namespace {

static std::map<int16_t, ClientItemDefinition> g_itemDefs;

// Category names for fallback item naming
static const char *kCatNames[] = {
    "Sword",      "Axe",       "Mace",         "Spear",       "Bow",    "Staff",
    "Shield",     "Helm",      "Armor",        "Pants",       "Gloves", "Boots",
    "Wings/Misc", "Accessory", "Jewel/Potion", "Scroll/Skill"};

// Fallback model per category (used when item not in g_itemDefs)
static const char *kCatFallbackModel[] = {
    "Sword01.bmd",      // 0 Swords
    "Axe01.bmd",        // 1 Axes
    "Mace01.bmd",       // 2 Maces
    "Spear01.bmd",      // 3 Spears
    "Bow01.bmd",        // 4 Bows
    "Staff01.bmd",      // 5 Staffs
    "Shield01.bmd",     // 6 Shields
    "HelmClass02.bmd",  // 7 Helms
    "ArmorClass02.bmd", // 8 Armor
    "PantClass02.bmd",  // 9 Pants
    "GloveClass02.bmd", // 10 Gloves
    "BootClass02.bmd",  // 11 Boots
    "Ring01.bmd",       // 12 Rings
    "Pendant01.bmd",    // 13 Pendants
    "Potion01.bmd",     // 14 Potions
    "Scroll01.bmd",     // 15 Scrolls
};

// Thread-local buffer for fallback name (avoids static lifetime issues)
static std::string g_fallbackNameBuf;

// Cached DropDef for GetDropInfo (single-threaded, safe for game loop)
static DropDef g_cachedDropDef;

} // anonymous namespace

namespace ItemDatabase {

void Init() {
  g_itemDefs.clear();
}

void LoadFromCatalog(const uint8_t *packetData, size_t packetLen) {
  // Static catalog — skip if already loaded this session
  if (!g_itemDefs.empty())
    return;

  // Wire format: [0]=0xC2, [1-2]=size(BE), [3]=opcode, [4-5]=count(LE), [6+]=entries
  if (packetLen < 6)
    return;

  uint16_t count =
      (uint16_t)packetData[4] | ((uint16_t)packetData[5] << 8);

  size_t entrySize = sizeof(PMSG_ITEM_DEF_ENTRY);
  size_t expectedSize = 6 + count * entrySize;
  if (packetLen < expectedSize) {
    printf("[ItemDB] Catalog packet too small: %zu < %zu (count=%d)\n",
           packetLen, expectedSize, count);
    return;
  }

  g_itemDefs.clear();

  for (uint16_t i = 0; i < count; i++) {
    const auto *entry = reinterpret_cast<const PMSG_ITEM_DEF_ENTRY *>(
        packetData + 6 + i * entrySize);

    ClientItemDefinition cd;
    cd.category = entry->category;
    cd.itemIndex = entry->itemIndex;
    cd.name = std::string(entry->name, strnlen(entry->name, 32));
    cd.modelFile = std::string(entry->modelFile, strnlen(entry->modelFile, 32));
    cd.levelReq = entry->levelReq;
    cd.dmgMin = entry->dmgMin;
    cd.dmgMax = entry->dmgMax;
    cd.defense = entry->defense;
    cd.attackSpeed = entry->attackSpeed;
    cd.twoHanded = entry->twoHanded != 0;
    cd.width = entry->width;
    cd.height = entry->height;
    cd.reqStr = entry->reqStr;
    cd.reqDex = entry->reqDex;
    cd.reqVit = entry->reqVit;
    cd.reqEne = entry->reqEne;
    cd.classFlags = entry->classFlags;
    cd.buyPrice = entry->buyPrice;
    cd.magicPower = entry->magicPower;

    int16_t defIndex = (int16_t)cd.category * 32 + cd.itemIndex;
    g_itemDefs[defIndex] = std::move(cd);
  }

  printf("[ItemDB] Loaded %d item definitions from server catalog\n", count);
}

std::map<int16_t, ClientItemDefinition> &GetItemDefs() { return g_itemDefs; }

const DropDef *GetDropInfo(int16_t defIndex) {
  if (defIndex == -1) {
    static const DropDef zen = {"Zen", "Gold01.bmd", 0, 0, 0};
    return &zen;
  }

  auto it = g_itemDefs.find(defIndex);
  if (it == g_itemDefs.end())
    return nullptr;

  g_cachedDropDef.name = it->second.name.c_str();
  g_cachedDropDef.model = it->second.modelFile.c_str();
  g_cachedDropDef.dmgMin = it->second.dmgMin;
  g_cachedDropDef.dmgMax = it->second.dmgMax;
  g_cachedDropDef.defense = it->second.defense;
  return &g_cachedDropDef;
}

const char *GetDropName(int16_t defIndex) {
  if (defIndex == -1)
    return "Zen";
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.name.c_str();
  // Generate fallback: "Bow [15]" from category*32+idx
  int cat = (defIndex >= 0) ? (defIndex / 32) : 0;
  int idx = (defIndex >= 0) ? (defIndex % 32) : 0;
  const char *catName = (cat >= 0 && cat < 16) ? kCatNames[cat] : "Item";
  char buf[32];
  snprintf(buf, sizeof(buf), "%s [%d]", catName, idx);
  g_fallbackNameBuf = buf;
  return g_fallbackNameBuf.c_str();
}

const char *GetDropModelName(int16_t defIndex) {
  if (defIndex == -1)
    return "Gold01.bmd";
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.modelFile.c_str();
  // Return category-appropriate fallback model
  int cat = (defIndex >= 0) ? (defIndex / 32) : 14;
  if (cat >= 0 && cat < 16)
    return kCatFallbackModel[cat];
  return "Potion01.bmd"; // last resort
}

const char *GetItemNameByDef(int16_t defIndex) {
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.name.c_str();
  return "Item";
}

int16_t GetDefIndexFromCategory(uint8_t category, uint8_t index) {
  for (auto const &[id, def] : g_itemDefs) {
    if (def.category == category && def.itemIndex == index) {
      return id;
    }
  }
  return -1;
}

void GetItemCategoryAndIndex(int16_t defIndex, uint8_t &cat, uint8_t &idx) {
  if (defIndex < 0) {
    cat = 0xFF;
    idx = 0;
    return;
  }
  cat = defIndex / 32;
  idx = defIndex % 32;
}

// Map equipment category+index to Player body part BMD filename
std::string GetBodyPartModelFile(uint8_t category, uint8_t index) {
  if (category < 7 || category > 11)
    return "";

  int16_t defIndex = (category * 32) + index;
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end() && !it->second.modelFile.empty())
    return it->second.modelFile;
  return "";
}

int GetBodyPartIndex(uint8_t category) {
  int idx = category - 7;
  if (idx >= 0 && idx <= 4)
    return idx;
  return -1;
}

const char *GetEquipSlotName(int slot) {
  static const char *names[] = {"R.Hand", "L.Hand",  "Helm",   "Armor",
                                "Pants",  "Gloves",  "Boots",  "Wings",
                                "Pet",    "Pendant", "Ring 1", "Ring 2"};
  if (slot >= 0 && slot < 12)
    return names[slot];
  return "???";
}

const char *GetCategoryName(uint8_t category) {
  if (category < 16)
    return kCatNames[category];
  return "";
}

} // namespace ItemDatabase

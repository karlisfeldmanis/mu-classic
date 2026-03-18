#include "ItemDatabase.hpp"
#include <cstdio>

namespace {

static std::map<int16_t, ClientItemDefinition> g_itemDefs;

static const DropDef zen = {"Zen", "Gold01.bmd", 0, 0, 0};

// MU 0.97d complete item database (Mapped to Cat * 32 + Idx)
static const DropDef items[] = {
    // Category 0: Swords (0-31)
    [0] = {"Kris", "Sword01.bmd", 6, 11, 0},
    {"Short Sword", "Sword02.bmd", 3, 7, 0},
    {"Rapier", "Sword03.bmd", 9, 13, 0},
    {"Katana", "Sword04.bmd", 12, 18, 0},
    {"Sword of Assassin", "Sword05.bmd", 15, 22, 0},
    {"Blade", "Sword06.bmd", 21, 31, 0},
    {"Gladius", "Sword07.bmd", 18, 26, 0},
    {"Falchion", "Sword08.bmd", 24, 34, 0},
    {"Serpent Sword", "Sword09.bmd", 30, 42, 0},
    {"Salamander", "Sword10.bmd", 36, 51, 0},
    {"Light Sabre", "Sword11.bmd", 42, 57, 0},
    {"Legendary Sword", "Sword12.bmd", 48, 64, 0},
    {"Heliacal Sword", "Sword13.bmd", 56, 72, 0},
    {"Double Blade", "Sword14.bmd", 44, 61, 0},
    {"Lighting Sword", "Sword15.bmd", 52, 68, 0},
    {"Giant Sword", "Sword16.bmd", 64, 82, 0},
    {"Sword of Destruction", "Sword17.bmd", 84, 108, 0},
    {"Dark Breaker", "Sword18.bmd", 96, 124, 0},
    {"Thunder Blade", "Sword19.bmd", 102, 132, 0},
    {"Divine Sword", "Sword20.bmd", 110, 140, 0},

    // Category 1: Axes (32-63)
    [32] = {"Small Axe", "Axe01.bmd", 1, 6, 0},
    {"Hand Axe", "Axe02.bmd", 4, 9, 0},
    {"Double Axe", "Axe03.bmd", 14, 24, 0},
    {"Tomahawk", "Axe04.bmd", 18, 28, 0},
    {"Elven Axe", "Axe05.bmd", 26, 38, 0},
    {"Battle Axe", "Axe06.bmd", 30, 44, 0},
    {"Nikea Axe", "Axe07.bmd", 34, 50, 0},
    {"Larkan Axe", "Axe08.bmd", 46, 67, 0},
    {"Crescent Axe", "Axe09.bmd", 54, 69, 0},

    // Category 2: Maces (64-95)
    [64] = {"Mace", "Mace01.bmd", 7, 13, 0},
    {"Morning Star", "Mace02.bmd", 13, 22, 0},
    {"Flail", "Mace03.bmd", 22, 32, 0},
    {"Great Hammer", "Mace04.bmd", 38, 56, 0},
    {"Crystal Morning Star", "Mace05.bmd", 66, 107, 0},
    {"Crystal Sword", "Mace06.bmd", 72, 120, 0},
    {"Chaos Dragon Axe", "Mace07.bmd", 75, 130, 0},
    {"Elemental Mace", "Mace08.bmd", 62, 80, 0},
    {"Mace of the King", "Mace09.bmd", 40, 51, 0},

    // Category 3: Spears (96-127)
    [96] = {"Light Spear", "Spear01.bmd", 42, 63, 0},
    {"Spear", "Spear02.bmd", 30, 41, 0},
    {"Dragon Lance", "Spear03.bmd", 21, 33, 0},
    {"Giant Trident", "Spear04.bmd", 35, 43, 0},
    {"Serpent Spear", "Spear05.bmd", 58, 80, 0},
    {"Double Poleaxe", "Spear06.bmd", 19, 31, 0},
    {"Halberd", "Spear07.bmd", 25, 35, 0},
    {"Berdysh", "Spear08.bmd", 42, 54, 0},
    {"Great Scythe", "Spear09.bmd", 71, 92, 0},
    {"Bill of Balrog", "Spear10.bmd", 76, 102, 0},
    {"Dragon Spear", "Spear11.bmd", 112, 140, 0},

    // Category 4: Bows (128-159)
    [128] = {"Short Bow", "Bow01.bmd", 3, 5, 0},
    {"Bow", "Bow02.bmd", 9, 13, 0},
    {"Elven Bow", "Bow03.bmd", 17, 24, 0},
    {"Battle Bow", "Bow04.bmd", 28, 37, 0},
    {"Tiger Bow", "Bow05.bmd", 42, 52, 0},
    {"Silver Bow", "Bow06.bmd", 59, 71, 0},
    {"Chaos Nature Bow", "Bow07.bmd", 88, 106, 0},
    [136] = {"Crossbow", "Bow09.bmd", 5, 8, 0}, // C4I8
    {"Golden Crossbow", "Bow10.bmd", 13, 19, 0},
    {"Arquebus", "Bow11.bmd", 22, 30, 0},
    {"Light Crossbow", "Bow12.bmd", 35, 44, 0},
    {"Serpent Crossbow", "Bow13.bmd", 50, 61, 0},
    {"Bluewing Crossbow", "Bow14.bmd", 68, 82, 0},
    {"Aquagold Crossbow", "Bow15.bmd", 78, 92, 0},

    // Category 5: Staffs (160-191)
    [160] = {"Skull Staff", "Staff01.bmd", 6, 11, 0},
    {"Angelic Staff", "Staff02.bmd", 18, 26, 0},
    {"Serpent Staff", "Staff03.bmd", 30, 42, 0},
    {"Thunder Staff", "Staff04.bmd", 42, 57, 0},
    {"Gorgon Staff", "Staff05.bmd", 56, 72, 0},
    {"Legendary Staff", "Staff06.bmd", 73, 98, 0},
    {"Staff of Resurrection", "Staff07.bmd", 88, 106, 0},
    {"Chaos Lightning Staff", "Staff08.bmd", 102, 132, 0},
    {"Staff of Destruction", "Staff09.bmd", 110, 140, 0},

    // Category 6: Shields (192-223)
    [192] = {"Small Shield", "Shield01.bmd", 0, 0, 3},
    {"Horn Shield", "Shield02.bmd", 0, 0, 6},
    {"Kite Shield", "Shield03.bmd", 0, 0, 10},
    {"Elven Shield", "Shield04.bmd", 0, 0, 15},
    {"Buckler", "Shield05.bmd", 0, 0, 20},
    {"Dragon Slayer Shield", "Shield06.bmd", 0, 0, 26},
    {"Skull Shield", "Shield07.bmd", 0, 0, 33},
    {"Spiked Shield", "Shield08.bmd", 0, 0, 41},
    {"Tower Shield", "Shield09.bmd", 0, 0, 50},
    {"Plate Shield", "Shield10.bmd", 0, 0, 60},
    {"Big Round Shield", "Shield11.bmd", 0, 0, 72},
    {"Serpent Shield", "Shield12.bmd", 0, 0, 85},
    {"Bronze Shield", "Shield13.bmd", 0, 0, 100},
    {"Dragon Shield", "Shield14.bmd", 0, 0, 115},
    {"Legendary Shield", "Shield15.bmd", 0, 0, 132},

    // Category 7: Helms (224-255)
    [224] = {"Bronze Helm", "HelmMale01.bmd", 0, 0, 8},
    {"Dragon Helm", "HelmMale02.bmd", 0, 0, 48},
    {"Pad Helm", "HelmMale03.bmd", 0, 0, 2},
    {"Legendary Helm", "HelmMale04.bmd", 0, 0, 28},
    {"Bone Helm", "HelmMale05.bmd", 0, 0, 14},
    {"Leather Helm", "HelmMale06.bmd", 0, 0, 4},
    {"Scale Helm", "HelmMale07.bmd", 0, 0, 12},
    {"Sphinx Mask", "HelmMale08.bmd", 0, 0, 21},
    {"Brass Helm", "HelmMale09.bmd", 0, 0, 18},
    {"Plate Helm", "HelmMale10.bmd", 0, 0, 35},

    // Category 8: Armor (256-287)
    [256] = {"Bronze Armor", "ArmorMale01.bmd", 0, 0, 15},
    {"Dragon Armor", "ArmorMale02.bmd", 0, 0, 65},
    {"Pad Armor", "ArmorMale03.bmd", 0, 0, 5},
    {"Legendary Armor", "ArmorMale04.bmd", 0, 0, 42},
    {"Bone Armor", "ArmorMale05.bmd", 0, 0, 24},
    {"Leather Armor", "ArmorMale06.bmd", 0, 0, 8},
    {"Scale Armor", "ArmorMale07.bmd", 0, 0, 20},
    {"Sphinx Armor", "ArmorMale08.bmd", 0, 0, 32},
    {"Brass Armor", "ArmorMale09.bmd", 0, 0, 28},
    {"Plate Armor", "ArmorMale10.bmd", 0, 0, 50},

    // Category 9: Pants (288-319)
    [288] = {"Bronze Pants", "PantMale01.bmd", 0, 0, 12},
    {"Dragon Pants", "PantMale02.bmd", 0, 0, 55},
    {"Pad Pants", "PantMale03.bmd", 0, 0, 4},
    {"Legendary Pants", "PantMale04.bmd", 0, 0, 35},
    {"Bone Pants", "PantMale05.bmd", 0, 0, 19},
    {"Leather Pants", "PantMale06.bmd", 0, 0, 6},
    {"Scale Pants", "PantMale07.bmd", 0, 0, 16},
    {"Sphinx Pants", "PantMale08.bmd", 0, 0, 27},
    {"Brass Pants", "PantMale09.bmd", 0, 0, 23},
    {"Plate Pants", "PantMale10.bmd", 0, 0, 43},

    // Category 10: Gloves (320-351)
    [320] = {"Bronze Gloves", "GloveMale01.bmd", 0, 0, 6},
    {"Dragon Gloves", "GloveMale02.bmd", 0, 0, 40},
    {"Pad Gloves", "GloveMale03.bmd", 0, 0, 1},
    {"Legendary Gloves", "GloveMale04.bmd", 0, 0, 22},
    {"Bone Gloves", "GloveMale05.bmd", 0, 0, 10},
    {"Leather Gloves", "GloveMale06.bmd", 0, 0, 2},
    {"Scale Gloves", "GloveMale07.bmd", 0, 0, 8},
    {"Sphinx Gloves", "GloveMale08.bmd", 0, 0, 15},
    {"Brass Gloves", "GloveMale09.bmd", 0, 0, 12},
    {"Plate Gloves", "GloveMale10.bmd", 0, 0, 28},

    // Category 11: Boots (352-383)
    [352] = {"Bronze Boots", "BootMale01.bmd", 0, 0, 6},
    {"Dragon Boots", "BootMale02.bmd", 0, 0, 40},
    {"Pad Boots", "BootMale03.bmd", 0, 0, 1},
    {"Legendary Boots", "BootMale04.bmd", 0, 0, 22},
    {"Bone Boots", "BootMale05.bmd", 0, 0, 10},
    {"Leather Boots", "BootMale06.bmd", 0, 0, 2},
    {"Scale Boots", "BootMale07.bmd", 0, 0, 8},
    {"Sphinx Boots", "BootMale08.bmd", 0, 0, 15},
    {"Brass Boots", "BootMale09.bmd", 0, 0, 12},
    {"Plate Boots", "BootMale10.bmd", 0, 0, 28},

    // Category 12: Wings/Orbs (384-415)
    [384] = {"Wings of Elf", "Wing01.bmd", 0, 0, 0},
    {"Wings of Heaven", "Wing02.bmd", 0, 0, 0},
    {"Wings of Satan", "Wing03.bmd", 0, 0, 0},
    {"Wings of Spirit", "Wing04.bmd", 0, 0, 0},
    {"Wings of Soul", "Wing05.bmd", 0, 0, 0},
    {"Wings of Dragon", "Wing06.bmd", 0, 0, 0},
    {"Wings of Darkness", "Wing07.bmd", 0, 0, 0},

    // Orbs (391-408)
    [391] = {"Orb of Twisting Slash", "Gem01.bmd", 0, 0, 0},
    [396] = {"Orb of Rageful Blow", "Gem06.bmd", 0, 0, 0},
    [399] = {"Jewel of Chaos", "Jewel04.bmd", 0, 0, 0},
    [403] = {"Orb of Death Stab", "Gem13.bmd", 0, 0, 0},
    {"Orb of Falling Slash", "Gem01.bmd", 0, 0, 0},
    {"Orb of Lunge", "Gem01.bmd", 0, 0, 0},
    {"Orb of Uppercut", "Gem01.bmd", 0, 0, 0},
    {"Orb of Cyclone", "Gem01.bmd", 0, 0, 0},
    {"Orb of Slash", "Gem01.bmd", 0, 0, 0},

    // Category 13: Rings (416-447)
    [416] = {"Ring of Ice", "Ring01.bmd", 0, 0, 0},
    {"Ring of Poison", "Ring02.bmd", 0, 0, 0},
    {"Ring of Fire", "Ring01.bmd", 0, 0, 0},  // Reusing Ring01
    {"Ring of Earth", "Ring02.bmd", 0, 0, 0}, // Reusing Ring02
    {"Ring of Wind", "Ring01.bmd", 0, 0, 0},  // Reusing Ring01
    {"Ring of Magic", "Ring02.bmd", 0, 0, 0}, // Reusing Ring02

    // Category 14: Potions (448-479)
    [448] = {"Apple", "Potion01.bmd", 0, 0, 0},
    {"Small Health Potion", "Potion02.bmd", 0, 0, 0},
    {"Medium Health Potion", "Potion03.bmd", 0, 0, 0},
    {"Large Health Potion", "Potion04.bmd", 0, 0, 0},
    {"Small Mana Potion", "Potion05.bmd", 0, 0, 0},
    {"Medium Mana Potion", "Potion06.bmd", 0, 0, 0},
    {"Large Mana Potion", "Potion07.bmd", 0, 0, 0},

    // Misc Items (Cat 13/14 overlap or special IDs in standard MU, but using
    // our logic)
    // Zen is special index -1
    // Jewels typically Cat 14 or 12 or 13 depending on version.
    // Item.txt says Jewels are Cat 14 (Index 13, 14, 16) or Cat 12.
    // 0.97k Item.txt: Jewel of Bless is 14, 13
    [461] = {"Jewel of Bless", "Jewel01.bmd", 0, 0, 0},
    {"Jewel of Soul", "Jewel02.bmd", 0, 0, 0},
    {"Jewel of Life", "Jewel03.bmd", 0, 0, 0},
    {"Jewel of Chaos", "Jewel04.bmd", 0, 0, 0},
};

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

} // anonymous namespace

namespace ItemDatabase {

void Init() {
  // Matches 0.97d server seeding
  auto addDef = [](int16_t id, uint8_t cat, uint8_t idx, const char *name,
                   const char *mod, uint8_t w, uint8_t h, uint16_t s,
                   uint16_t d, uint16_t v, uint16_t e, uint16_t l, uint32_t cf,
                   uint16_t dmgMin = 0, uint16_t dmgMax = 0,
                   uint16_t defense = 0, uint8_t attackSpeed = 0,
                   bool twoHanded = false, uint32_t buyPrice = 0,
                   uint16_t magicPower = 0) {
    ClientItemDefinition cd;
    cd.category = cat;
    cd.itemIndex = idx;
    cd.name = name;
    cd.modelFile = mod;
    cd.width = w;
    cd.height = h;
    cd.reqStr = s;
    cd.reqDex = d;
    cd.reqVit = v;
    cd.reqEne = e;
    cd.levelReq = l;
    cd.classFlags = cf;
    cd.dmgMin = dmgMin;
    cd.dmgMax = dmgMax;
    cd.defense = defense;
    cd.attackSpeed = attackSpeed;
    cd.twoHanded = twoHanded;
    cd.buyPrice = buyPrice;
    cd.magicPower = magicPower;

    // Use Standard ID (Cat*32 + Idx) as key
    // This matches what the server sends for drops and ensures consistency
    int16_t standardId = (int16_t)cat * 32 + idx;
    g_itemDefs[standardId] = cd;
  };

  // IDs are used locally as keys in g_itemDefs.
  // We'll use IDs that won't collide with the server's autoincrement range if
  // possible, but since we sync by (Cat, Idx), the actual ID value here is
  // arbitrary as long as it's unique.

  // Auto-generated from Database.cpp
  // Category 0: Swords
  //                id  cat idx  name              model         w  h  str dex
  //                vit ene lvl cf  dmgMin dmgMax def atkSpd 2H
  // Category 0: Swords (OpenMU 0.95d Weapons.cs)
  addDef(0, 0, 0, "Kris", "Sword01.bmd", 1, 2, 10, 8, 0, 0, 1, 11, 6, 11, 0, 50,
         false);
  addDef(1, 0, 1, "Short Sword", "Sword02.bmd", 1, 3, 20, 0, 0, 0, 1, 7, 3, 7,
         0, 20, false);
  addDef(2, 0, 2, "Rapier", "Sword03.bmd", 1, 3, 50, 40, 0, 0, 9, 6, 9, 15, 0,
         40, false);
  addDef(3, 0, 3, "Katana", "Sword04.bmd", 1, 3, 80, 40, 0, 0, 16, 2, 16, 26, 0,
         35, false);
  addDef(4, 0, 4, "Sword of Assassin", "Sword05.bmd", 1, 3, 60, 40, 0, 0, 12, 2,
         12, 18, 0, 30, false);
  addDef(5, 0, 5, "Blade", "Sword06.bmd", 1, 3, 80, 50, 0, 0, 36, 7, 36, 47, 0,
         30, false);
  addDef(6, 0, 6, "Gladius", "Sword07.bmd", 1, 3, 110, 0, 0, 0, 20, 6, 20, 30,
         0, 20, false);
  addDef(7, 0, 7, "Falchion", "Sword08.bmd", 1, 3, 120, 0, 0, 0, 24, 2, 24, 34,
         0, 25, false);
  addDef(8, 0, 8, "Serpent Sword", "Sword09.bmd", 1, 3, 130, 0, 0, 0, 30, 2, 30,
         40, 0, 20, false);
  addDef(9, 0, 9, "Sword of Salamander", "Sword10.bmd", 2, 3, 103, 0, 0, 0, 32,
         2, 32, 46, 0, 30, true);
  addDef(10, 0, 10, "Light Saber", "Sword11.bmd", 2, 4, 80, 60, 0, 0, 40, 6, 47,
         61, 0, 25, true);
  addDef(11, 0, 11, "Legendary Sword", "Sword12.bmd", 2, 3, 120, 0, 0, 0, 44, 2,
         56, 72, 0, 20, true);
  addDef(12, 0, 12, "Heliacal Sword", "Sword13.bmd", 2, 3, 140, 0, 0, 0, 56, 2,
         73, 98, 0, 25, true);
  addDef(13, 0, 13, "Double Blade", "Sword14.bmd", 1, 3, 70, 70, 0, 0, 48, 6,
         48, 56, 0, 30, false);
  addDef(14, 0, 14, "Lightning Sword", "Sword15.bmd", 1, 3, 90, 50, 0, 0, 59, 6,
         59, 67, 0, 30, false);
  addDef(15, 0, 15, "Giant Sword", "Sword16.bmd", 2, 3, 140, 0, 0, 0, 52, 2, 60,
         85, 0, 20, true);
  addDef(16, 0, 16, "Sword of Destruction", "Sword17.bmd", 1, 4, 160, 60, 0, 0,
         82, 10, 82, 90, 0, 35, false);
  addDef(17, 0, 17, "Dark Breaker", "Sword18.bmd", 2, 4, 180, 50, 0, 0, 104, 2,
         128, 153, 0, 40, true);
  addDef(18, 0, 18, "Thunder Blade", "Sword19.bmd", 2, 3, 180, 50, 0, 0, 105, 8,
         140, 168, 0, 40, true);
  // Category 1: Axes (OpenMU 0.95d Weapons.cs)
  addDef(32, 1, 0, "Small Axe", "Axe01.bmd", 1, 3, 20, 0, 0, 0, 1, 7, 1, 6, 0,
         20, false);
  addDef(33, 1, 1, "Hand Axe", "Axe02.bmd", 1, 3, 70, 0, 0, 0, 4, 7, 4, 9, 0,
         30, false);
  addDef(34, 1, 2, "Double Axe", "Axe03.bmd", 1, 3, 90, 0, 0, 0, 14, 2, 14, 24,
         0, 20, false);
  addDef(35, 1, 3, "Tomahawk", "Axe04.bmd", 1, 3, 100, 0, 0, 0, 18, 2, 18, 28,
         0, 30, false);
  addDef(36, 1, 4, "Elven Axe", "Axe05.bmd", 1, 3, 50, 70, 0, 0, 26, 5, 26, 38,
         0, 40, false);
  addDef(37, 1, 5, "Battle Axe", "Axe06.bmd", 2, 3, 120, 0, 0, 0, 30, 6, 36, 44,
         0, 20, true);
  addDef(38, 1, 6, "Nikkea Axe", "Axe07.bmd", 2, 3, 130, 0, 0, 0, 34, 6, 38, 50,
         0, 30, true);
  addDef(39, 1, 7, "Larkan Axe", "Axe08.bmd", 2, 3, 140, 0, 0, 0, 46, 2, 54, 67,
         0, 25, true);
  addDef(40, 1, 8, "Crescent Axe", "Axe09.bmd", 2, 3, 100, 40, 0, 0, 54, 3, 69,
         89, 0, 30, true);
  // Category 2: Maces (OpenMU 0.95d Weapons.cs)
  addDef(64, 2, 0, "Mace", "Mace01.bmd", 1, 3, 100, 0, 0, 0, 7, 2, 7, 13, 0, 15,
         false);
  addDef(65, 2, 1, "Morning Star", "Mace02.bmd", 1, 3, 100, 0, 0, 0, 13, 2, 13,
         22, 0, 15, false);
  addDef(66, 2, 2, "Flail", "Mace03.bmd", 1, 3, 80, 50, 0, 0, 22, 2, 22, 32, 0,
         15, false);
  addDef(67, 2, 3, "Great Hammer", "Mace04.bmd", 2, 3, 150, 0, 0, 0, 38, 2, 45,
         56, 0, 15, true);
  addDef(68, 2, 4, "Crystal Morning Star", "Mace05.bmd", 2, 3, 130, 0, 0, 0, 66,
         7, 78, 107, 0, 30, true);
  addDef(69, 2, 5, "Crystal Sword", "Mace06.bmd", 2, 4, 130, 70, 0, 0, 72, 7,
         89, 120, 0, 40, true);
  addDef(70, 2, 6, "Chaos Dragon Axe", "Mace07.bmd", 2, 4, 140, 50, 0, 0, 75, 2,
         102, 130, 0, 35, true);
  // Category 3: Spears (OpenMU 0.95d Weapons.cs)
  addDef(96, 3, 0, "Light Spear", "Spear01.bmd", 2, 4, 60, 70, 0, 0, 42, 6, 50,
         63, 0, 25, true);
  addDef(97, 3, 1, "Spear", "Spear02.bmd", 2, 4, 70, 50, 0, 0, 23, 6, 30, 41, 0,
         30, true);
  addDef(98, 3, 2, "Dragon Lance", "Spear03.bmd", 2, 4, 70, 50, 0, 0, 15, 6, 21,
         33, 0, 30, true);
  addDef(99, 3, 3, "Giant Trident", "Spear04.bmd", 2, 4, 90, 30, 0, 0, 29, 6,
         35, 43, 0, 25, true);
  addDef(100, 3, 4, "Serpent Spear", "Spear05.bmd", 2, 4, 90, 30, 0, 0, 46, 6,
         58, 80, 0, 20, true);
  addDef(101, 3, 5, "Double Poleaxe", "Spear06.bmd", 2, 4, 70, 50, 0, 0, 13, 6,
         19, 31, 0, 30, true);
  addDef(102, 3, 6, "Halberd", "Spear07.bmd", 2, 4, 70, 50, 0, 0, 19, 6, 25, 35,
         0, 30, true);
  addDef(103, 3, 7, "Berdysh", "Spear08.bmd", 2, 4, 80, 50, 0, 0, 37, 6, 42, 54,
         0, 30, true);
  addDef(104, 3, 8, "Great Scythe", "Spear09.bmd", 2, 4, 90, 50, 0, 0, 54, 6,
         71, 92, 0, 25, true);
  addDef(105, 3, 9, "Bill of Balrog", "Spear10.bmd", 2, 4, 80, 50, 0, 0, 63, 6,
         76, 102, 0, 25, true);
  // Category 4: Bows & Crossbows (OpenMU 0.95d Weapons.cs)
  addDef(128, 4, 0, "Short Bow", "Bow01.bmd", 2, 3, 20, 80, 0, 0, 2, 4, 3, 5, 0,
         30, false);
  addDef(129, 4, 1, "Bow", "Bow02.bmd", 2, 3, 30, 90, 0, 0, 8, 4, 9, 13, 0, 30,
         false);
  addDef(130, 4, 2, "Elven Bow", "Bow03.bmd", 2, 3, 30, 90, 0, 0, 16, 4, 17, 24,
         0, 30, false);
  addDef(131, 4, 3, "Battle Bow", "Bow04.bmd", 2, 3, 30, 90, 0, 0, 26, 4, 28,
         37, 0, 30, false);
  addDef(132, 4, 4, "Tiger Bow", "Bow05.bmd", 2, 4, 30, 100, 0, 0, 40, 4, 42,
         52, 0, 30, false);
  addDef(133, 4, 5, "Silver Bow", "Bow06.bmd", 2, 4, 30, 100, 0, 0, 56, 4, 59,
         71, 0, 40, false);
  addDef(134, 4, 6, "Chaos Nature Bow", "Bow07.bmd", 2, 4, 40, 150, 0, 0, 75, 4,
         88, 106, 0, 35, false);
  addDef(135, 4, 7, "Bolt", "Arrows01.bmd", 1, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0,
         false);
  addDef(136, 4, 8, "Crossbow", "CrossBow01.bmd", 2, 2, 20, 90, 0, 0, 4, 4, 5,
         8, 0, 40, false);
  addDef(137, 4, 9, "Golden Crossbow", "CrossBow02.bmd", 2, 2, 30, 90, 0, 0, 12,
         4, 13, 19, 0, 40, false);
  addDef(138, 4, 10, "Arquebus", "CrossBow03.bmd", 2, 2, 30, 90, 0, 0, 20, 4,
         22, 30, 0, 40, false);
  addDef(139, 4, 11, "Light Crossbow", "CrossBow04.bmd", 2, 3, 30, 90, 0, 0, 32,
         4, 35, 44, 0, 40, false);
  addDef(140, 4, 12, "Serpent Crossbow", "CrossBow05.bmd", 2, 3, 30, 100, 0, 0,
         48, 4, 50, 61, 0, 40, false);
  addDef(141, 4, 13, "Bluewing Crossbow", "CrossBow06.bmd", 2, 3, 40, 110, 0, 0,
         68, 4, 68, 82, 0, 40, false);
  addDef(142, 4, 14, "Aquagold Crossbow", "CrossBow07.bmd", 2, 3, 50, 130, 0, 0,
         72, 4, 78, 92, 0, 30, false);
  addDef(143, 4, 15, "Arrows", "Arrows02.bmd", 1, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0,
         0, false);
  addDef(144, 4, 16, "Saint Crossbow", "CrossBow08.bmd", 2, 3, 50, 130, 0, 0,
         83, 4, 90, 108, 0, 35, false);
  // Category 5: Staves (OpenMU 0.95d Weapons.cs) — last arg = magicPower
  addDef(160, 5, 0, "Skull Staff", "Staff01.bmd", 1, 3, 40, 0, 0, 0, 6, 1, 3, 4,
         0, 20, false, 0, 6);
  addDef(161, 5, 1, "Angelic Staff", "Staff02.bmd", 2, 3, 50, 0, 0, 0, 18, 1,
         10, 12, 0, 25, false, 0, 20);
  addDef(162, 5, 2, "Serpent Staff", "Staff03.bmd", 2, 3, 50, 0, 0, 0, 30, 1,
         17, 18, 0, 25, false, 0, 34);
  addDef(163, 5, 3, "Thunder Staff", "Staff04.bmd", 2, 4, 40, 10, 0, 0, 42, 1,
         23, 25, 0, 25, false, 0, 46);
  addDef(164, 5, 4, "Gorgon Staff", "Staff05.bmd", 2, 4, 60, 0, 0, 0, 52, 1, 29,
         32, 0, 25, false, 0, 58);
  addDef(165, 5, 5, "Legendary Staff", "Staff06.bmd", 1, 4, 50, 0, 0, 0, 59, 1,
         29, 31, 0, 25, false, 0, 59);
  addDef(166, 5, 6, "Staff of Resurrection", "Staff07.bmd", 1, 4, 60, 10, 0, 0,
         70, 1, 35, 39, 0, 25, false, 0, 70);
  addDef(167, 5, 7, "Chaos Lightning Staff", "Staff08.bmd", 2, 4, 60, 10, 0, 0,
         75, 1, 47, 48, 0, 30, false, 0, 94);
  addDef(168, 5, 8, "Staff of Destruction", "Staff09.bmd", 2, 4, 60, 10, 0, 0,
         90, 9, 55, 60, 0, 35, false, 0, 115);
  // Category 6: Shields (OpenMU v0.75)
  addDef(192, 6, 0, "Small Shield", "Shield01.bmd", 2, 2, 70, 0, 0, 0, 3, 15, 0,
         0, 3, 0, false);
  addDef(193, 6, 1, "Horn Shield", "Shield02.bmd", 2, 2, 100, 0, 0, 0, 9, 2, 0,
         0, 9, 0, false);
  addDef(194, 6, 2, "Kite Shield", "Shield03.bmd", 2, 2, 110, 0, 0, 0, 12, 2, 0,
         0, 12, 0, false);
  addDef(195, 6, 3, "Elven Shield", "Shield04.bmd", 2, 2, 30, 100, 0, 0, 21, 4,
         0, 0, 21, 0, false);
  addDef(196, 6, 4, "Buckler", "Shield05.bmd", 2, 2, 80, 0, 0, 0, 6, 15, 0, 0,
         6, 0, false);
  addDef(197, 6, 5, "Dragon Slayer Shield", "Shield06.bmd", 2, 2, 100, 40, 0, 0,
         35, 2, 0, 0, 36, 0, false);
  addDef(198, 6, 6, "Skull Shield", "Shield07.bmd", 2, 2, 110, 0, 0, 0, 15, 15,
         0, 0, 15, 0, false);
  addDef(199, 6, 7, "Spiked Shield", "Shield08.bmd", 2, 2, 130, 0, 0, 0, 30, 2,
         0, 0, 30, 0, false);
  addDef(200, 6, 8, "Tower Shield", "Shield09.bmd", 2, 2, 130, 0, 0, 0, 40, 11,
         0, 0, 40, 0, false);
  addDef(201, 6, 9, "Plate Shield", "Shield10.bmd", 2, 2, 120, 0, 0, 0, 25, 2,
         0, 0, 25, 0, false);
  addDef(202, 6, 10, "Big Round Shield", "Shield11.bmd", 2, 2, 120, 0, 0, 0, 18,
         2, 0, 0, 18, 0, false);
  addDef(203, 6, 11, "Serpent Shield", "Shield12.bmd", 2, 2, 130, 0, 0, 0, 45,
         11, 0, 0, 45, 0, false);
  addDef(204, 6, 12, "Bronze Shield", "Shield13.bmd", 2, 2, 140, 0, 0, 0, 54, 2,
         0, 0, 54, 0, false);
  addDef(205, 6, 13, "Dragon Shield", "Shield14.bmd", 2, 2, 120, 40, 0, 0, 60,
         2, 0, 0, 60, 0, false);
  addDef(206, 6, 14, "Legendary Shield", "Shield15.bmd", 2, 3, 90, 25, 0, 0, 48,
         5, 0, 0, 48, 0, false);
  // Category 7-11: Armors (OpenMU v0.75 - Pad, Leather, Bronze, etc.)
  // Helmets (7)
  addDef(224, 7, 0, "Bronze Helm", "HelmMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(225, 7, 1, "Dragon Helm", "HelmMale02.bmd", 2, 2, 120, 30, 0, 0, 57, 2,
         0, 0, 68, 0, false);
  addDef(226, 7, 2, "Pad Helm", "HelmMale03.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(227, 7, 3, "Legendary Helm", "HelmMale04.bmd", 2, 2, 30, 0, 0, 0, 50,
         1, 0, 0, 42, 0, false);
  addDef(228, 7, 4, "Bone Helm", "HelmMale05.bmd", 2, 2, 30, 0, 0, 0, 18, 1, 0,
         0, 30, 0, false);
  addDef(229, 7, 5, "Leather Helm", "HelmMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(230, 7, 6, "Scale Helm", "HelmMale07.bmd", 2, 2, 110, 0, 0, 0, 26, 2,
         0, 0, 40, 0, false);
  addDef(231, 7, 7, "Sphinx Mask", "HelmMale08.bmd", 2, 2, 30, 0, 0, 0, 32, 1,
         0, 0, 36, 0, false);
  addDef(232, 7, 8, "Brass Helm", "HelmMale09.bmd", 2, 2, 100, 30, 0, 0, 36, 2,
         0, 0, 44, 0, false);
  addDef(233, 7, 9, "Plate Helm", "HelmMale10.bmd", 2, 2, 130, 0, 0, 0, 46, 2,
         0, 0, 50, 0, false);
  addDef(234, 7, 10, "Vine Helm", "HelmElf01.bmd", 2, 2, 30, 60, 0, 0, 6, 4,
         0, 0, 22, 0, false);
  addDef(235, 7, 11, "Silk Helm", "HelmElf02.bmd", 2, 2, 0, 0, 0, 20, 1, 4, 0,
         0, 26, 0, false);
  addDef(236, 7, 12, "Wind Helm", "HelmElf03.bmd", 2, 2, 30, 80, 0, 0, 28, 4,
         0, 0, 32, 0, false);
  addDef(237, 7, 13, "Spirit Helm", "HelmElf04.bmd", 2, 2, 40, 80, 0, 0, 40,
         4, 0, 0, 38, 0, false);
  addDef(238, 7, 14, "Guardian Helm", "HelmElf05.bmd", 2, 2, 40, 80, 0, 0, 53,
         4, 0, 0, 45, 0, false);
  // Armors (8)
  addDef(256, 8, 0, "Bronze Armor", "ArmorMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(257, 8, 1, "Dragon Armor", "ArmorMale02.bmd", 2, 3, 120, 30, 0, 0, 59,
         2, 0, 0, 68, 0, false);
  addDef(258, 8, 2, "Pad Armor", "ArmorMale03.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(259, 8, 3, "Legendary Armor", "ArmorMale04.bmd", 2, 2, 40, 0, 0, 0,
         56, 1, 0, 0, 42, 0, false);
  addDef(260, 8, 4, "Bone Armor", "ArmorMale05.bmd", 2, 2, 40, 0, 0, 0, 22, 1,
         0, 0, 30, 0, false);
  addDef(261, 8, 5, "Leather Armor", "ArmorMale06.bmd", 2, 3, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(262, 8, 6, "Scale Armor", "ArmorMale07.bmd", 2, 2, 110, 0, 0, 0, 28, 2,
         0, 0, 40, 0, false);
  addDef(263, 8, 7, "Sphinx Armor", "ArmorMale08.bmd", 2, 3, 40, 0, 0, 0, 38,
         1, 0, 0, 36, 0, false);
  addDef(264, 8, 8, "Brass Armor", "ArmorMale09.bmd", 2, 2, 100, 30, 0, 0, 38,
         2, 0, 0, 44, 0, false);
  addDef(265, 8, 9, "Plate Armor", "ArmorMale10.bmd", 2, 2, 130, 0, 0, 0, 48, 2,
         0, 0, 50, 0, false);
  addDef(266, 8, 10, "Vine Armor", "ArmorElf01.bmd", 2, 2, 30, 60, 0, 0, 10,
         4, 0, 0, 22, 0, false);
  addDef(267, 8, 11, "Silk Armor", "ArmorElf02.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(268, 8, 12, "Wind Armor", "ArmorElf03.bmd", 2, 2, 30, 80, 0, 0, 32,
         4, 0, 0, 32, 0, false);
  addDef(269, 8, 13, "Spirit Armor", "ArmorElf04.bmd", 2, 2, 40, 80, 0, 0, 44,
         4, 0, 0, 38, 0, false);
  addDef(270, 8, 14, "Guardian Armor", "ArmorElf05.bmd", 2, 2, 40, 80, 0, 0,
         57, 4, 0, 0, 45, 0, false);
  // Pants (9)
  addDef(288, 9, 0, "Bronze Pants", "PantMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(289, 9, 1, "Dragon Pants", "PantMale02.bmd", 2, 2, 120, 30, 0, 0, 55,
         2, 0, 0, 68, 0, false);
  addDef(290, 9, 2, "Pad Pants", "PantMale03.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(291, 9, 3, "Legendary Pants", "PantMale04.bmd", 2, 2, 40, 0, 0, 0, 53,
         1, 0, 0, 42, 0, false);
  addDef(292, 9, 4, "Bone Pants", "PantMale05.bmd", 2, 2, 40, 0, 0, 0, 20, 1,
         0, 0, 30, 0, false);
  addDef(293, 9, 5, "Leather Pants", "PantMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(294, 9, 6, "Scale Pants", "PantMale07.bmd", 2, 2, 110, 0, 0, 0, 25, 2,
         0, 0, 40, 0, false);
  addDef(295, 9, 7, "Sphinx Pants", "PantMale08.bmd", 2, 2, 40, 0, 0, 0, 34, 1,
         0, 0, 36, 0, false);
  addDef(296, 9, 8, "Brass Pants", "PantMale09.bmd", 2, 2, 100, 30, 0, 0, 35, 2,
         0, 0, 44, 0, false);
  addDef(297, 9, 9, "Plate Pants", "PantMale10.bmd", 2, 2, 130, 0, 0, 0, 45, 2,
         0, 0, 50, 0, false);
  addDef(298, 9, 10, "Vine Pants", "PantElf01.bmd", 2, 2, 30, 60, 0, 0, 8, 4,
         0, 0, 22, 0, false);
  addDef(299, 9, 11, "Silk Pants", "PantElf02.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(300, 9, 12, "Wind Pants", "PantElf03.bmd", 2, 2, 30, 80, 0, 0, 30, 4,
         0, 0, 32, 0, false);
  addDef(301, 9, 13, "Spirit Pants", "PantElf04.bmd", 2, 2, 40, 80, 0, 0, 42,
         4, 0, 0, 38, 0, false);
  addDef(302, 9, 14, "Guardian Pants", "PantElf05.bmd", 2, 2, 40, 80, 0, 0,
         54, 4, 0, 0, 45, 0, false);
  // Gloves (10)
  addDef(320, 10, 0, "Bronze Gloves", "GloveMale01.bmd", 2, 2, 25, 20, 0, 0, 1,
         2, 0, 0, 34, 0, false);
  addDef(321, 10, 1, "Dragon Gloves", "GloveMale02.bmd", 2, 2, 120, 30, 0, 0,
         52, 2, 0, 0, 68, 0, false);
  addDef(322, 10, 2, "Pad Gloves", "GloveMale03.bmd", 2, 2, 0, 0, 0, 20, 1, 1,
         0, 0, 28, 0, false);
  addDef(323, 10, 3, "Legendary Gloves", "GloveMale04.bmd", 2, 2, 20, 0, 0, 0,
         44, 1, 0, 0, 42, 0, false);
  addDef(324, 10, 4, "Bone Gloves", "GloveMale05.bmd", 2, 2, 20, 0, 0, 0, 14,
         1, 0, 0, 30, 0, false);
  addDef(325, 10, 5, "Leather Gloves", "GloveMale06.bmd", 2, 2, 20, 0, 0, 0, 1,
         2, 0, 0, 30, 0, false);
  addDef(326, 10, 6, "Scale Gloves", "GloveMale07.bmd", 2, 2, 110, 0, 0, 0, 22,
         2, 0, 0, 40, 0, false);
  addDef(327, 10, 7, "Sphinx Gloves", "GloveMale08.bmd", 2, 2, 20, 0, 0, 0, 28,
         1, 0, 0, 36, 0, false);
  addDef(328, 10, 8, "Brass Gloves", "GloveMale09.bmd", 2, 2, 100, 30, 0, 0, 32,
         2, 0, 0, 44, 0, false);
  addDef(329, 10, 9, "Plate Gloves", "GloveMale10.bmd", 2, 2, 130, 0, 0, 0, 42,
         2, 0, 0, 50, 0, false);
  addDef(330, 10, 10, "Vine Gloves", "GloveElf01.bmd", 2, 2, 30, 60, 0, 0, 4,
         4, 0, 0, 22, 0, false);
  addDef(331, 10, 11, "Silk Gloves", "GloveElf02.bmd", 2, 2, 0, 0, 0, 20, 1,
         4, 0, 0, 26, 0, false);
  addDef(332, 10, 12, "Wind Gloves", "GloveElf03.bmd", 2, 2, 30, 80, 0, 0, 26,
         4, 0, 0, 32, 0, false);
  addDef(333, 10, 13, "Spirit Gloves", "GloveElf04.bmd", 2, 2, 40, 80, 0, 0,
         38, 4, 0, 0, 38, 0, false);
  addDef(334, 10, 14, "Guardian Gloves", "GloveElf05.bmd", 2, 2, 40, 80, 0, 0,
         50, 4, 0, 0, 45, 0, false);
  // Boots (11)
  addDef(352, 11, 0, "Bronze Boots", "BootMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(353, 11, 1, "Dragon Boots", "BootMale02.bmd", 2, 2, 120, 30, 0, 0, 54,
         2, 0, 0, 68, 0, false);
  addDef(354, 11, 2, "Pad Boots", "BootMale03.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(355, 11, 3, "Legendary Boots", "BootMale04.bmd", 2, 2, 30, 0, 0, 0,
         46, 1, 0, 0, 42, 0, false);
  addDef(356, 11, 4, "Bone Boots", "BootMale05.bmd", 2, 2, 30, 0, 0, 0, 16, 1,
         0, 0, 30, 0, false);
  addDef(357, 11, 5, "Leather Boots", "BootMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(358, 11, 6, "Scale Boots", "BootMale07.bmd", 2, 2, 110, 0, 0, 0, 22, 2,
         0, 0, 40, 0, false);
  addDef(359, 11, 7, "Sphinx Boots", "BootMale08.bmd", 2, 2, 30, 0, 0, 0, 30,
         1, 0, 0, 36, 0, false);
  addDef(360, 11, 8, "Brass Boots", "BootMale09.bmd", 2, 2, 100, 30, 0, 0, 32,
         2, 0, 0, 44, 0, false);
  addDef(361, 11, 9, "Plate Boots", "BootMale10.bmd", 2, 2, 130, 0, 0, 0, 42, 2,
         0, 0, 50, 0, false);
  addDef(362, 11, 10, "Vine Boots", "BootElf01.bmd", 2, 2, 30, 60, 0, 0, 5, 4,
         0, 0, 22, 0, false);
  addDef(363, 11, 11, "Silk Boots", "BootElf02.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(364, 11, 12, "Wind Boots", "BootElf03.bmd", 2, 2, 30, 80, 0, 0, 27,
         4, 0, 0, 32, 0, false);
  addDef(365, 11, 13, "Spirit Boots", "BootElf04.bmd", 2, 2, 40, 80, 0, 0, 40,
         4, 0, 0, 38, 0, false);
  addDef(366, 11, 14, "Guardian Boots", "BootElf05.bmd", 2, 2, 40, 80, 0, 0,
         52, 4, 0, 0, 45, 0, false);

  // Category 12: Wings (IDs 700+)
  addDef(700, 12, 0, "Wings of Elf", "Wing01.bmd", 3, 2, 0, 0, 0, 0, 100, 4);
  addDef(701, 12, 1, "Wings of Heaven", "Wing02.bmd", 3, 2, 0, 0, 0, 0, 100, 1);
  addDef(702, 12, 2, "Wings of Satan", "Wing03.bmd", 3, 2, 0, 0, 0, 0, 100, 2);
  addDef(703, 12, 3, "Wings of Spirits", "Wing04.bmd", 4, 3, 0, 0, 0, 0, 150,
         4);
  addDef(704, 12, 4, "Wings of Soul", "Wing05.bmd", 4, 3, 0, 0, 0, 0, 150, 1);
  addDef(705, 12, 5, "Wings of Dragon", "Wing06.bmd", 4, 3, 0, 0, 0, 0, 150, 2);
  addDef(706, 12, 6, "Wings of Darkness", "Wing07.bmd", 4, 3, 0, 0, 0, 0, 150,
         8);

  // Category 12: Orbs (IDs 750+)
  addDef(757, 12, 7, "Orb of Twisting Slash", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 47,
         2);
  addDef(758, 12, 8, "Orb of Healing", "Gem02.bmd", 1, 1, 0, 0, 0, 100, 8, 4);
  addDef(759, 12, 9, "Orb of Greater Defense", "Gem03.bmd", 1, 1, 0, 0, 0, 100,
         13, 4);
  addDef(760, 12, 10, "Orb of Greater Damage", "Gem04.bmd", 1, 1, 0, 0, 0, 100,
         18, 4);
  addDef(761, 12, 11, "Orb of Goblin", "Gem05.bmd", 1, 1, 0, 0, 0, 0, 3, 4);
  addDef(762, 12, 12, "Orb of Rageful Blow", "Gem06.bmd", 1, 1, 170, 0, 0, 0,
         78, 2);
  addDef(763, 12, 13, "Orb of Impale", "Gem07.bmd", 1, 1, 28, 0, 0, 0, 20, 2);
  addDef(764, 12, 14, "Orb of Greater Fortitude", "Gem08.bmd", 1, 1, 120, 0, 0,
         0, 60, 2);
  addDef(766, 12, 16, "Orb of Fire Slash", "Gem10.bmd", 1, 1, 320, 0, 0, 0, 60,
         8);
  addDef(767, 12, 17, "Orb of Penetration", "Gem11.bmd", 1, 1, 130, 0, 0, 0, 64,
         4);
  addDef(768, 12, 18, "Orb of Ice Arrow", "Gem12.bmd", 1, 1, 0, 258, 0, 0, 81,
         4);
  addDef(769, 12, 19, "Orb of Death Stab", "Gem13.bmd", 1, 1, 160, 0, 0, 0, 72,
         2);

  // Basic DK skill orbs (indices 20-24)
  addDef(770, 12, 20, "Orb of Falling Slash", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 1,
         2);
  addDef(771, 12, 21, "Orb of Lunge", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 1, 2);
  addDef(772, 12, 22, "Orb of Uppercut", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 1, 2);
  addDef(773, 12, 23, "Orb of Cyclone", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 1, 2);
  addDef(774, 12, 24, "Orb of Slash", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 1, 2);

  // Elf summon orbs (indices 25-29)
  addDef(775, 12, 25, "Orb of Golem Summoning", "Gem05.bmd", 1, 1, 0, 0, 0,
         60, 18, 4);
  addDef(776, 12, 26, "Orb of Assassin Summoning", "Gem05.bmd", 1, 1, 0, 0, 0,
         90, 26, 4);
  addDef(777, 12, 27, "Orb of Yeti Summoning", "Gem05.bmd", 1, 1, 0, 0, 0,
         130, 36, 4);
  addDef(778, 12, 28, "Orb of Knight Summoning", "Gem05.bmd", 1, 1, 0, 0, 0,
         170, 48, 4);
  addDef(779, 12, 29, "Orb of Bali Summoning", "Gem05.bmd", 1, 1, 0, 0, 0,
         210, 52, 4);

  // Category 12 (Jewels mix) & Category 13 (Jewelry/Pets) (IDs 800+)
  addDef(815, 12, 15, "Jewel of Chaos", "Jewel15.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(800, 13, 0, "Guardian Angel", "Helper01.bmd", 1, 1, 0, 0, 0, 0, 23,
         15);
  addDef(801, 13, 1, "Imp", "Helper02.bmd", 1, 1, 0, 0, 0, 0, 28, 15);
  addDef(802, 13, 2, "Horn of Uniria", "Helper03.bmd", 1, 1, 0, 0, 0, 0, 25,
         15);
  addDef(803, 13, 3, "Horn of Dinorant", "Helper04.bmd", 1, 1, 0, 0, 0, 0, 110,
         15);
  addDef(808, 13, 8, "Ring of Ice", "Ring01.bmd", 1, 1, 0, 0, 0, 0, 20, 15);
  addDef(809, 13, 9, "Ring of Poison", "Ring02.bmd", 1, 1, 0, 0, 0, 0, 17, 15);
  addDef(810, 13, 10, "Transformation Ring", "Ring01.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(812, 13, 12, "Pendant of Lighting", "Necklace01.bmd", 1, 1, 0, 0, 0, 0,
         21, 15);
  addDef(813, 13, 13, "Pendant of Fire", "Necklace02.bmd", 1, 1, 0, 0, 0, 0, 13,
         15);

  // Category 14: Consumables (IDs 850+)
  addDef(850, 14, 0, "Apple", "Potion01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(851, 14, 1, "Small HP Potion", "Potion02.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(852, 14, 2, "Medium HP Potion", "Potion03.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(853, 14, 3, "Large HP Potion", "Potion04.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(854, 14, 4, "Small Mana Potion", "Potion05.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(855, 14, 5, "Medium Mana Potion", "Potion06.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(856, 14, 6, "Large Mana Potion", "Potion07.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(858, 14, 8, "Antidote", "Antidote01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(859, 14, 9, "Ale", "Beer01.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(860, 14, 10, "Town Portal", "Scroll01.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(863, 14, 13, "Jewel of Bless", "Jewel01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(864, 14, 14, "Jewel of Soul", "Jewel02.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(866, 14, 16, "Jewel of Life", "Jewel03.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(872, 14, 22, "Jewel of Creation", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);

  // Category 15: Scrolls (IDs 900+) — Version075 requirements (level, energy)
  //             id  cat idx  name                    model       w h  s  d  v   e    l  cf
  addDef(900, 15, 0, "Scroll of Poison", "Book01.bmd", 1, 2, 0, 0, 0, 140, 30, 1);
  addDef(901, 15, 1, "Scroll of Meteorite", "Book02.bmd", 1, 2, 0, 0, 0, 104, 21, 1);
  addDef(902, 15, 2, "Scroll of Lightning", "Book03.bmd", 1, 2, 0, 0, 0, 72, 13, 1);
  addDef(903, 15, 3, "Scroll of Fire Ball", "Book04.bmd", 1, 2, 0, 0, 0, 40, 5, 1);
  addDef(904, 15, 4, "Scroll of Flame", "Book05.bmd", 1, 2, 0, 0, 0, 160, 35, 1);
  addDef(905, 15, 5, "Scroll of Teleport", "Book06.bmd", 1, 2, 0, 0, 0, 88, 17, 1);
  addDef(906, 15, 6, "Scroll of Ice", "Book07.bmd", 1, 2, 0, 0, 0, 120, 25, 1);
  addDef(907, 15, 7, "Scroll of Twister", "Book08.bmd", 1, 2, 0, 0, 0, 180, 40, 1);
  addDef(908, 15, 8, "Scroll of Evil Spirit", "Book09.bmd", 1, 2, 0, 0, 0, 220, 50, 1);
  addDef(909, 15, 9, "Scroll of Hellfire", "Book10.bmd", 1, 2, 0, 0, 0, 260, 60, 1);
  addDef(911, 15, 11, "Scroll of Aqua Beam", "Book12.bmd", 1, 2, 0, 0, 0, 345, 74, 1);
  addDef(912, 15, 12, "Scroll of Cometfall", "Book13.bmd", 1, 2, 0, 0, 0, 436, 80, 1);
  addDef(913, 15, 13, "Scroll of Inferno", "Book14.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);

  // ── Additional 0.97d items (Main 5.2 deep dive) ──

  // Missing Swords (0)
  addDef(0, 0, 19, "Sword of Destruction", "Sword20.bmd", 1, 4, 124, 44, 0, 0,
         76, 8, 68, 93);
  addDef(0, 0, 20, "Spirit Sword", "Sword21.bmd", 1, 4, 140, 48, 0, 0, 88, 2,
         92, 112);
  addDef(0, 0, 21, "Dark Master Sword", "Sword22.bmd", 1, 4, 154, 50, 0, 0, 98,
         8, 108, 132);

  // Missing Maces (2)
  addDef(0, 2, 7, "Battle Scepter", "Mace08.bmd", 2, 4, 132, 32, 0, 0, 80, 2,
         85, 110);
  addDef(0, 2, 8, "Master Scepter", "Mace09.bmd", 2, 4, 142, 38, 0, 0, 86, 2,
         92, 126);
  addDef(0, 2, 9, "Great Scepter", "Mace10.bmd", 2, 4, 152, 42, 0, 0, 92, 2,
         105, 140);
  addDef(0, 2, 10, "Lord Scepter", "Mace11.bmd", 2, 4, 158, 44, 0, 0, 96, 2,
         110, 148);
  addDef(0, 2, 11, "Great Lord Scepter", "Mace12.bmd", 2, 4, 164, 48, 0, 0, 100,
         2, 118, 156);
  addDef(0, 2, 12, "Divine Scepter", "Mace13.bmd", 2, 4, 170, 50, 0, 0, 104, 2,
         125, 168);
  addDef(0, 2, 13, "Saint Scepter", "Saint.bmd", 1, 3, 72, 18, 0, 0, 96, 1, 106,
         144);

  // Missing Spears (3)
  addDef(0, 3, 10, "Dragon Spear", "Spear11.bmd", 2, 4, 170, 60, 0, 0, 92, 2,
         112, 140);

  // Missing Bows (4)
  addDef(0, 4, 17, "Celestial Bow", "Bow18.bmd", 2, 4, 54, 198, 0, 0, 92, 4,
         127, 155);
  addDef(0, 4, 18, "Divine CB of Archangel", "CrossBow17.bmd", 2, 3, 40, 110, 0,
         0, 100, 4, 144, 166);

  // Missing Staffs (5)
  addDef(0, 5, 9, "Dragon Soul Staff", "Staff10.bmd", 1, 4, 52, 16, 0, 0, 100,
         1, 46, 48);
  addDef(0, 5, 10, "Staff of Imperial", "Staff11.bmd", 2, 4, 36, 4, 0, 0, 104,
         1, 50, 53);
  addDef(0, 5, 11, "Divine Staff of Archangel", "Staff12.bmd", 2, 4, 36, 4, 0,
         0, 104, 1, 53, 55);

  // Missing Shields (6)
  addDef(0, 6, 15, "Grand Soul Shield", "Shield16.bmd", 2, 3, 70, 23, 0, 0, 74,
         1, 0, 0, 55);
  addDef(0, 6, 16, "Elemental Shield", "Shield17.bmd", 2, 3, 50, 110, 0, 0, 78,
         4, 0, 0, 58);

  // Missing Helms (7) — indices 15-21
  // Note: Storm Crow set (index 15) had no helm in 0.97k, HelmMale16.bmd may not exist
  addDef(0, 7, 15, "Storm Crow Helm", "HelmMale16.bmd", 2, 2, 150, 70, 0, 0, 72,
         8, 0, 0, 50);
  addDef(0, 7, 16, "Black Dragon Helm", "HelmMale17.bmd", 2, 2, 170, 60, 0, 0,
         82, 2, 0, 0, 55);
  addDef(0, 7, 17, "Dark Phoenix Helm", "HelmMale18.bmd", 2, 2, 205, 62, 0, 0,
         92, 10, 0, 0, 60);
  addDef(0, 7, 18, "Grand Soul Helm", "HelmMale19.bmd", 2, 2, 59, 20, 0, 0, 81,
         1, 0, 0, 48);
  addDef(0, 7, 19, "Divine Helm", "HelmMale20.bmd", 2, 2, 50, 110, 0, 0, 85, 4,
         0, 0, 52);
  addDef(0, 7, 20, "Thunder Hawk Helm", "HelmMale21.bmd", 2, 2, 150, 70, 0, 0,
         88, 8, 0, 0, 54);
  addDef(0, 7, 21, "Great Dragon Helm", "HelmMale22.bmd", 2, 2, 200, 58, 0, 0,
         104, 10, 0, 0, 66);

  // Missing Armors (8) — indices 15-21
  addDef(0, 8, 15, "Storm Crow Armor", "ArmorMale11.bmd", 2, 3, 150, 70, 0, 0,
         80, 8, 0, 0, 58);
  addDef(0, 8, 16, "Black Dragon Armor", "ArmorMale12.bmd", 2, 3, 170, 60, 0, 0,
         90, 2, 0, 0, 63);
  addDef(0, 8, 17, "Dark Phoenix Armor", "ArmorMale13.bmd", 2, 3, 214, 65, 0, 0,
         100, 10, 0, 0, 70);
  addDef(0, 8, 18, "Grand Soul Armor", "ArmorClass10.bmd", 2, 3, 59, 20, 0, 0,
         91, 1, 0, 0, 52);
  addDef(0, 8, 19, "Divine Armor", "ArmorClass11.bmd", 2, 2, 50, 110, 0, 0, 92,
         4, 0, 0, 56);
  addDef(0, 8, 20, "Thunder Hawk Armor", "ArmorMale14.bmd", 2, 3, 170, 70, 0, 0,
         107, 8, 0, 0, 68);
  addDef(0, 8, 21, "Great Dragon Armor", "ArmorMale15.bmd", 2, 3, 200, 58, 0, 0,
         126, 10, 0, 0, 80);

  // Missing Pants (9) — indices 15-21
  addDef(0, 9, 15, "Storm Crow Pants", "PantMale11.bmd", 2, 2, 150, 70, 0, 0,
         74, 8, 0, 0, 50);
  addDef(0, 9, 16, "Black Dragon Pants", "PantMale12.bmd", 2, 2, 170, 60, 0, 0,
         84, 2, 0, 0, 55);
  addDef(0, 9, 17, "Dark Phoenix Pants", "PantMale13.bmd", 2, 2, 207, 63, 0, 0,
         96, 10, 0, 0, 62);
  addDef(0, 9, 18, "Grand Soul Pants", "PantClass10.bmd", 2, 2, 59, 20, 0, 0,
         86, 1, 0, 0, 48);
  addDef(0, 9, 19, "Divine Pants", "PantClass11.bmd", 2, 2, 50, 110, 0, 0, 88,
         4, 0, 0, 52);
  addDef(0, 9, 20, "Thunder Hawk Pants", "PantMale14.bmd", 2, 2, 150, 70, 0, 0,
         99, 8, 0, 0, 60);
  addDef(0, 9, 21, "Great Dragon Pants", "PantMale15.bmd", 2, 2, 200, 58, 0, 0,
         113, 10, 0, 0, 72);

  // Missing Gloves (10) — indices 15-21
  addDef(0, 10, 15, "Storm Crow Gloves", "GloveMale11.bmd", 2, 2, 150, 70, 0, 0,
         70, 8, 0, 0, 46);
  addDef(0, 10, 16, "Black Dragon Gloves", "GloveMale12.bmd", 2, 2, 170, 60, 0,
         0, 76, 2, 0, 0, 50);
  addDef(0, 10, 17, "Dark Phoenix Gloves", "GloveMale13.bmd", 2, 2, 205, 63, 0,
         0, 86, 10, 0, 0, 56);
  addDef(0, 10, 18, "Grand Soul Gloves", "GloveClass10.bmd", 2, 2, 49, 10, 0, 0,
         70, 1, 0, 0, 44);
  addDef(0, 10, 19, "Divine Gloves", "GloveClass11.bmd", 2, 2, 50, 110, 0, 0,
         72, 4, 0, 0, 48);
  addDef(0, 10, 20, "Thunder Hawk Gloves", "GloveMale14.bmd", 2, 2, 150, 70, 0,
         0, 88, 8, 0, 0, 54);
  addDef(0, 10, 21, "Great Dragon Gloves", "GloveMale15.bmd", 2, 2, 200, 58, 0,
         0, 94, 10, 0, 0, 64);

  // Missing Boots (11) — indices 15-21
  addDef(0, 11, 15, "Storm Crow Boots", "BootMale11.bmd", 2, 2, 150, 70, 0, 0,
         72, 8, 0, 0, 48);
  addDef(0, 11, 16, "Black Dragon Boots", "BootMale12.bmd", 2, 2, 170, 60, 0, 0,
         78, 2, 0, 0, 52);
  addDef(0, 11, 17, "Dark Phoenix Boots", "BootMale13.bmd", 2, 2, 198, 60, 0, 0,
         93, 10, 0, 0, 58);
  addDef(0, 11, 18, "Grand Soul Boots", "BootClass10.bmd", 2, 2, 59, 10, 0, 0,
         76, 1, 0, 0, 44);
  addDef(0, 11, 19, "Divine Boots", "BootClass11.bmd", 2, 2, 50, 110, 0, 0, 81,
         4, 0, 0, 50);
  addDef(0, 11, 20, "Thunder Hawk Boots", "BootMale14.bmd", 2, 2, 150, 70, 0, 0,
         92, 8, 0, 0, 56);
  addDef(0, 11, 21, "Great Dragon Boots", "BootMale15.bmd", 2, 2, 200, 58, 0, 0,
         98, 10, 0, 0, 68);

  // Missing Helpers/Jewelry (13)
  addDef(0, 13, 4, "Dark Horse Horn", "DarkHorseHorn.bmd", 1, 1, 0, 0, 0, 0,
         110, 15);
  addDef(0, 13, 5, "Spirit Bill", "SpiritBill.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 7, "Covenant", "Covenant.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 11, "Summon Book", "SummonBook.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 21, "Fire Ring", "FireRing.bmd", 1, 1, 0, 0, 0, 0, 68, 15);
  addDef(0, 13, 22, "Ground Ring", "GroundRing.bmd", 1, 1, 0, 0, 0, 0, 76, 15);
  addDef(0, 13, 23, "Wind Ring", "WindRing.bmd", 1, 1, 0, 0, 0, 0, 84, 15);
  addDef(0, 13, 24, "Mana Ring", "ManaRing.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 25, "Ice Necklace", "IceNecklace.bmd", 1, 1, 0, 0, 0, 0, 68,
         15);
  addDef(0, 13, 26, "Wind Necklace", "WindNecklace.bmd", 1, 1, 0, 0, 0, 0, 76,
         15);
  addDef(0, 13, 27, "Water Necklace", "WaterNecklace.bmd", 1, 1, 0, 0, 0, 0, 84,
         15);
  addDef(0, 13, 28, "AG Necklace", "AgNecklace.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 29, "Chaos Castle Invitation", "EventChaosCastle.bmd", 1, 1, 0,
         0, 0, 0, 0, 15);

  // Missing Potions/Consumables (14)
  addDef(0, 14, 7, "Special Healing Potion", "SpecialPotion.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 11, "Box of Luck", "MagicBox01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 12, "Heart of Love", "Event01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 15, "Zen", "Gold01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 17, "Devil Square Key (Bronze)", "Devil00.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 18, "Devil Square Key (Silver)", "Devil01.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 19, "Devil Square Key (Gold)", "Devil02.bmd", 1, 1, 0, 0, 0, 0,
         0, 15);
  addDef(0, 14, 20, "Remedy of Love", "Drink00.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 31, "Guardian Angel Scroll", "Suho.bmd", 1, 2, 0, 0, 0, 0, 0,
         15);

  // Compute buy prices (matching server formula)
  for (auto &[id, def] : g_itemDefs) {
    if (def.buyPrice > 0)
      continue; // already set
    uint8_t cat = def.category;
    uint8_t idx = def.itemIndex;
    if (cat <= 5) {
      // Weapons: levelReq * 100 + dmgMax * 20
      def.buyPrice = def.levelReq * 100 + def.dmgMax * 20;
      // Ammo overrides
      if ((cat == 4 && idx == 7) || (cat == 4 && idx == 15))
        def.buyPrice = 100;
    } else if (cat == 6) {
      // Shields
      def.buyPrice = def.levelReq * 80 + def.defense * 30;
    } else if (cat >= 7 && cat <= 11) {
      // Armor
      def.buyPrice = def.levelReq * 80 + def.defense * 30;
    } else if (cat == 12) {
      if (idx <= 6)
        def.buyPrice = 50000; // Wings
      else if (idx == 15)
        def.buyPrice = 810000; // Jewel of Chaos
      else
        def.buyPrice = def.levelReq * 200; // Orbs
    } else if (cat == 13) {
      def.buyPrice = def.levelReq * 300;
    } else if (cat == 14) {
      // Potions — specific prices
      switch (idx) {
      case 0:
        def.buyPrice = 20;
        break;
      case 1:
        def.buyPrice = 80;
        break;
      case 2:
        def.buyPrice = 300;
        break;
      case 3:
        def.buyPrice = 1000;
        break;
      case 4:
        def.buyPrice = 120;
        break;
      case 5:
        def.buyPrice = 450;
        break;
      case 6:
        def.buyPrice = 1500;
        break;
      case 7:
        def.buyPrice = 3500;
        break;
      case 8:
        def.buyPrice = 100;
        break;
      case 9:
        def.buyPrice = 1000;
        break;
      case 10:
        def.buyPrice = 2000;
        break;
      case 13:
        def.buyPrice = 9000000;
        break;
      case 14:
        def.buyPrice = 6000000;
        break;
      case 16:
        def.buyPrice = 45000000;
        break;
      case 20:
        def.buyPrice = 900;
        break;
      case 22:
        def.buyPrice = 36000000;
        break;
      default:
        def.buyPrice = 500;
        break;
      }
    } else if (cat == 15) {
      // Scrolls
      switch (idx) {
      case 0:
        def.buyPrice = 3800;
        break;
      case 1:
        def.buyPrice = 3100;
        break;
      case 2:
        def.buyPrice = 2400;
        break;
      case 3:
        def.buyPrice = 1500;
        break;
      case 4:
        def.buyPrice = 4400;
        break;
      case 5:
        def.buyPrice = 2800;
        break;
      case 6:
        def.buyPrice = 3500;
        break;
      case 7:
        def.buyPrice = 5000;
        break;
      case 8:
        def.buyPrice = 6200;
        break;
      case 9:
        def.buyPrice = 7500;
        break;
      case 10:
        def.buyPrice = 500;
        break;
      case 11:
        def.buyPrice = 12000;
        break;
      case 12:
        def.buyPrice = 18000;
        break;
      case 13:
        def.buyPrice = 30000;
        break;
      default:
        def.buyPrice = 1000;
        break;
      }
    }
  }
}

std::map<int16_t, ClientItemDefinition> &GetItemDefs() { return g_itemDefs; }

const DropDef *GetDropInfo(int16_t defIndex) {
  if (defIndex == -1)
    return &zen;

  if (defIndex >= 0 && defIndex < (int)(sizeof(items) / sizeof(items[0])))
    return &items[defIndex];

  return nullptr;
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
// Returns empty string if not a body part (e.g. weapons/potions)
std::string GetBodyPartModelFile(uint8_t category, uint8_t index) {
  // Category 7=Helm...11=Boot
  const char *prefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  int partCat = category - 7; // 0=Helm...4=Boot
  if (partCat < 0 || partCat > 4)
    return "";

  // Class specific prefixes
  // Simplification: We only have Male/Class01/Class02/Class03/Class04 mapped in
  // code We need to map exact filenames. For now, let's assume a simple mapping
  // or use the filename from GetDropInfo? Actually, GetDropInfo already has the
  // model filename! We can just reverse look up? No, that's slow. Or we can
  // rely on standard naming conventions if possible. The current function uses
  // a simple mapping. Let's keep it but ideally use the DropDef.

  // BUT: GetDropInfo stores "Drop Model".
  // Armor/Helm in Drop is usually same BMD as equipped?
  // Drop: ArmorMale01.bmd. Equipped: ArmorMale01.bmd. YES.
  // So we can use GetDropInfo to find the model file!

  int16_t defIndex = (category * 32) + index;
  auto *def = GetDropInfo(defIndex);
  if (def && def->model) {
    return def->model;
  }
  // Fallback: check g_itemDefs (covers elf helms, DW armor, etc.
  // that are beyond the static items[] array bounds)
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end() && !it->second.modelFile.empty()) {
    return it->second.modelFile;
  }
  return "";
}

// Map category to body part index (0=Helm, 1=Armor, 2=Pants, 3=Gloves, 4=Boots)
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

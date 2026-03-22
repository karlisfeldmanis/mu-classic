#include "handlers/QuestHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include <cstdio>
#include <cstring>

// ═══════════════════════════════════════════════════════
// Quest definitions — 20 kill quests, per-class rewards
// ═══════════════════════════════════════════════════════

struct QuestKillTarget {
  uint8_t monsterType;
  uint8_t killsRequired;
};

struct QuestItemReward {
  int16_t defIndex;  // -1 = no item
  uint8_t itemLevel;
};

struct QuestDef {
  uint16_t guardNpcType;
  uint8_t targetCount;
  QuestKillTarget targets[3];
  const char *targetNames[3];
  const char *questName;
  const char *location;
  uint8_t recommendedLevel;
  uint32_t zenReward;
  uint32_t xpReward;
  // Per-class rewards: [class][0=weapon, 1=skill item]
  // Class index: DW=0, DK=1, ELF=2, MG=3 (classCode/16)
  QuestItemReward classReward[4][2];
  const char *loreText;
};

static constexpr int QUEST_COUNT = 34;

static const QuestDef g_quests[QUEST_COUNT] = {
    // ════════════════════════════════════════════════════════
    // Lorencia quests (Q0-Q7) — 1 monster type per quest
    // ════════════════════════════════════════════════════════
    // Q0: Brynn — Bull Fighter x10
    {246, 1, {{0, 10}, {0, 0}, {0, 0}},
     {"Bull Fighter", "", ""},
     "The Road's First Threat", "Lorencia", 1, 3000, 25000,
     {{{160, 0}, {483, 0}},  // DW: Skull Staff + Scroll of Fire Ball
      {{0, 0}, {404, 0}},    // DK: Kris + Orb of Falling Slash
      {{128, 0}, {395, 0}},  // ELF: Short Bow + Orb of Goblin
      {{1, 0}, {192, 0}}},   // MG: Short Sword + Small Shield
     "The roads around Lorencia grow\n"
     "dangerous. Bull Fighters charge at\n"
     "travelers without warning. Slay 10\n"
     "near the eastern fields to secure\n"
     "the path."},
    // Q1: Brynn — Hound x10
    {246, 1, {{1, 10}, {0, 0}, {0, 0}},
     {"Hound", "", ""},
     "Hounds of Lorencia", "Lorencia", 5, 5000, 40000,
     {{{226, 0}, {196, 0}},  // DW: Pad Helm + Buckler
      {{224, 0}, {405, 0}},  // DK: Bronze Helm + Orb of Lunge
      {{234, 0}, {330, 0}},  // ELF: Vine Helm + Vine Gloves
      {{2, 0}, {196, 0}}},   // MG: Rapier + Buckler
     "Feral Hounds roam the plains at night,\n"
     "picking off livestock and stragglers.\n"
     "The farmers plead for help. Hunt down\n"
     "10 Hounds before they grow bolder."},
    // Q2: Kael — Budge Dragon x10
    {248, 1, {{2, 10}, {0, 0}, {0, 0}},
     {"Budge Dragon", "", ""},
     "Dragon Whelps", "Lorencia", 8, 8000, 60000,
     {{{228, 0}, {482, 0}},  // DW: Bone Helm + Scroll of Lightning
      {{64, 0}, {193, 0}},   // DK: Mace + Horn Shield
      {{136, 0}, {362, 0}},  // ELF: Crossbow + Vine Boots
      {{33, 1}, {198, 0}}},  // MG: Hand Axe+1 + Skull Shield
     "Budge Dragons nest in the rocky\n"
     "outcrops south of town. They are small\n"
     "but their fire breath is deadly to the\n"
     "unprepared. Destroy 10 of them."},
    // Q3: Kael — Spider x10
    {248, 1, {{3, 10}, {0, 0}, {0, 0}},
     {"Spider", "", ""},
     "Vermin in the Shadows", "Lorencia", 10, 10000, 80000,
     {{{258, 1}, {481, 0}},  // DW: Pad Armor+1 + Scroll of Meteorite
      {{65, 0}, {230, 0}},   // DK: Morning Star + Scale Helm
      {{266, 1}, {392, 0}},  // ELF: Vine Armor+1 + Orb of Healing
      {{101, 0}, {192, 1}}}, // MG: Double Poleaxe + Small Shield+1
     "Giant Spiders infest the sewers\n"
     "beneath the city. Their webs block the\n"
     "tunnels and their venom sickens all who\n"
     "venture below. Clear out 10 Spiders."},
    // Q4: Aldric — Elite Bull Fighter x10
    {245, 1, {{4, 10}, {0, 0}, {0, 0}},
     {"Elite Bull Fighter", "", ""},
     "The Elite Vanguard", "Lorencia", 14, 15000, 100000,
     {{{292, 0}, {485, 0}},  // DW: Bone Pants + Scroll of Teleport
      {{4, 0}, {194, 0}},    // DK: Sword of Assassin + Kite Shield
      {{130, 0}, {298, 1}},  // ELF: Elven Bow + Vine Pants+1
      {{6, 0}, {198, 1}}},   // MG: Gladius + Skull Shield+1
     "Elite Bull Fighters are far more\n"
     "dangerous than their common kin. They\n"
     "coordinate attacks on supply wagons.\n"
     "Dispatch 10 of these armored brutes."},
    // Q5: Dorian — Lich x8
    {247, 1, {{6, 8}, {0, 0}, {0, 0}},
     {"Lich", "", ""},
     "Whispers from the Ruins", "Lorencia", 17, 20000, 130000,
     {{{161, 0}, {486, 0}},  // DW: Angelic Staff + Scroll of Ice
      {{3, 0}, {358, 0}},    // DK: Katana + Scale Boots
      {{137, 0}, {332, 0}},  // ELF: Golden Crossbow + Wind Gloves
      {{98, 0}, {196, 2}}},  // MG: Dragon Lance + Buckler+2
     "Liches linger in the ruined chapel\n"
     "west of town. Their dark magic corrupts\n"
     "the land itself. Destroy 8 Liches\n"
     "before their influence spreads."},
    // Q6: Aldric — Giant x7
    {245, 1, {{7, 7}, {0, 0}, {0, 0}},
     {"Giant", "", ""},
     "The Gatekeepers", "Lorencia", 20, 30000, 180000,
     {{{260, 0}, {480, 0}},  // DW: Bone Armor + Scroll of Poison
      {{7, 0}, {294, 0}},    // DK: Falchion + Scale Pants
      {{138, 0}, {236, 0}},  // ELF: Arquebus + Wind Helm
      {{102, 0}, {200, 0}}}, // MG: Halberd + Tower Shield
     "Giants have come down from the\n"
     "mountains. Their massive clubs crush\n"
     "men in full plate. Strike down 7 of\n"
     "these behemoths before they breach\n"
     "the walls."},
    // Q7: Dorian — Skeleton Warrior x7
    {247, 1, {{14, 7}, {0, 0}, {0, 0}},
     {"Skeleton Warrior", "", ""},
     "The Undead March", "Lorencia", 24, 40000, 250000,
     {{{231, 0}, {484, 0}},  // DW: Sphinx Mask + Scroll of Flame
      {{8, 0}, {326, 0}},    // DK: Serpent Sword + Scale Gloves
      {{131, 0}, {300, 0}},  // ELF: Battle Bow + Wind Pants
      {{97, 0}, {203, 0}}},  // MG: Spear + Serpent Shield
     "Skeleton Warriors march from the\n"
     "ancient cemetery at nightfall. Once\n"
     "noble soldiers, now bound in undeath.\n"
     "Put 7 of them to final rest."},
    // ════════════════════════════════════════════════════════
    // Dungeon quests (Q8-Q19) — all from Captain Marcus
    // ════════════════════════════════════════════════════════
    // Q8: Marcus — Skeleton Warrior (dungeon) x15
    {249, 1, {{14, 15}, {0, 0}, {0, 0}},
     {"Skeleton Warrior", "", ""},
     "Descent Into Darkness", "Dungeon", 28, 40000, 300000,
     {{{359, 0}, {487, 0}},  // DW: Sphinx Boots + Scroll of Twister
      {{66, 0}, {232, 0}},   // DK: Flail + Brass Helm
      {{364, 0}, {393, 0}},  // ELF: Wind Boots + Orb of Greater Defense
      {{99, 0}, {200, 1}}},  // MG: Giant Trident + Tower Shield+1
     "The dungeon entrance is overrun with\n"
     "Skeleton Warriors. They guard the\n"
     "passages to deeper chambers. Cut\n"
     "through 15 to secure our foothold."},
    // Q9: Marcus — Larva x12
    {249, 1, {{12, 12}, {0, 0}, {0, 0}},
     {"Larva", "", ""},
     "The Larvae Nests", "Dungeon", 30, 45000, 350000,
     {{{162, 0}, {356, 1}},  // DW: Serpent Staff + Bone Boots+1
      {{262, 1}, {391, 0}},  // DK: Scale Armor+1 + Orb of Twisting Slash
      {{268, 0}, {394, 0}},  // ELF: Wind Armor + Orb of Greater Damage
      {{5, 0}, {198, 2}}},   // MG: Blade + Skull Shield+2
     "Larvae crawl through the dungeon's\n"
     "narrow tunnels, leaving trails of\n"
     "caustic slime. Their acid dissolves\n"
     "stone and flesh. Exterminate 12."},
    // Q10: Marcus — Hell Hound x10
    {249, 1, {{5, 10}, {0, 0}, {0, 0}},
     {"Hell Hound", "", ""},
     "Infernal Hounds", "Dungeon", 32, 50000, 380000,
     {{{263, 0}, {488, 0}},  // DW: Sphinx Armor + Scroll of Evil Spirit
      {{9, 0}, {360, 0}},    // DK: Sword of Salamander + Brass Boots
      {{139, 0}, {237, 0}},  // ELF: Light Crossbow + Spirit Helm
      {{103, 0}, {200, 2}}}, // MG: Berdysh + Tower Shield+2
     "Hell Hounds prowl the second level,\n"
     "their hellfire breath lighting corridors\n"
     "with infernal glow. They must be\n"
     "destroyed -- slay 10 Hell Hounds."},
    // Q11: Marcus — Poison Bull x12
    {249, 1, {{8, 12}, {0, 0}, {0, 0}},
     {"Poison Bull", "", ""},
     "The Venomous Herd", "Dungeon", 34, 55000, 400000,
     {{{327, 1}, {489, 0}},  // DW: Sphinx Gloves+1 + Scroll of Hellfire
      {{67, 0}, {264, 0}},   // DK: Great Hammer + Brass Armor
      {{333, 0}, {410, 0}},  // ELF: Spirit Gloves + Orb of Assassin Summoning
      {{96, 0}, {203, 1}}},  // MG: Light Spear + Serpent Shield+1
     "Poison Bulls fill the corridors with\n"
     "toxic fumes. Their very breath is\n"
     "lethal. Destroy 12 of these creatures\n"
     "to clear the air."},
    // Q12: Marcus — Skeleton Archer x12
    {249, 1, {{15, 12}, {0, 0}, {0, 0}},
     {"Skeleton Archer", "", ""},
     "Arrows from the Dark", "Dungeon", 36, 60000, 420000,
     {{{227, 0}, {260, 2}},  // DW: Legendary Helm + Bone Armor+2
      {{34, 2}, {296, 0}},   // DK: Double Axe+2 + Brass Pants
      {{301, 0}, {268, 1}},  // ELF: Spirit Pants + Wind Armor+1
      {{100, 0}, {203, 2}}}, // MG: Serpent Spear + Serpent Shield+2
     "Skeleton Archers line the corridors,\n"
     "raining arrows on anyone who dares\n"
     "enter. Their aim is deadly even in\n"
     "death. Destroy 12 of them."},
    // Q13: Marcus — Thunder Lich x12
    {249, 1, {{9, 12}, {0, 0}, {0, 0}},
     {"Thunder Lich", "", ""},
     "Storm Beneath the Earth", "Dungeon", 38, 65000, 450000,
     {{{295, 1}, {355, 0}},  // DW: Sphinx Pants+1 + Legendary Boots
      {{35, 2}, {328, 0}},   // DK: Tomahawk+2 + Brass Gloves
      {{132, 0}, {365, 0}},  // ELF: Tiger Bow + Spirit Boots
      {{13, 0}, {200, 3}}},  // MG: Double Blade + Tower Shield+3
     "Thunder Liches unleash lightning\n"
     "through the dungeon halls. Their storms\n"
     "shatter stone and scatter expeditions.\n"
     "Destroy 12 of these sorcerers."},
    // Q14: Marcus — Hell Spider x7
    {249, 1, {{13, 7}, {0, 0}, {0, 0}},
     {"Hell Spider", "", ""},
     "Web of Nightmares", "Dungeon", 40, 70000, 500000,
     {{{163, 0}, {291, 0}},  // DW: Thunder Staff + Legendary Pants
      {{39, 0}, {233, 0}},   // DK: Larkan Axe + Plate Helm
      {{269, 0}, {411, 0}},  // ELF: Spirit Armor + Orb of Yeti Summoning
      {{10, 0}, {203, 3}}},  // MG: Light Saber + Serpent Shield+3
     "Hell Spiders weave webs of shadow and\n"
     "fire in the deep tunnels. Their venom\n"
     "burns like acid. Only 7 remain -- but\n"
     "each is a deadly challenge."},
    // Q15: Marcus — Ghost x20
    {249, 1, {{11, 20}, {0, 0}, {0, 0}},
     {"Ghost", "", ""},
     "The Restless Dead", "Dungeon", 42, 75000, 550000,
     {{{323, 0}, {259, 0}},  // DW: Legendary Gloves + Legendary Armor
      {{297, 0}, {361, 0}},  // DK: Plate Pants + Plate Boots
      {{195, 0}, {301, 1}},  // ELF: Elven Shield + Spirit Pants+1
      {{38, 0}, {200, 4}}},  // MG: Nikkea Axe + Tower Shield+4
     "Ghosts drift through the dungeon in\n"
     "countless numbers. They drain the life\n"
     "from the living with a single touch.\n"
     "Banish 20 of these apparitions."},
    // Q16: Marcus — Elite Skeleton x15
    {249, 1, {{16, 15}, {0, 0}, {0, 0}},
     {"Elite Skeleton", "", ""},
     "The Bone Commanders", "Dungeon", 44, 80000, 600000,
     {{{164, 0}, {355, 1}},  // DW: Gorgon Staff + Legendary Boots+1
      {{11, 0}, {265, 0}},   // DK: Legendary Sword + Plate Armor
      {{238, 0}, {412, 0}},  // ELF: Guardian Helm + Orb of Knight Summoning
      {{104, 0}, {203, 4}}}, // MG: Great Scythe + Serpent Shield+4
     "Elite Skeletons command the undead\n"
     "legions. They are ancient warriors of\n"
     "terrible skill, far deadlier than their\n"
     "lesser kin. Slay 15 of them."},
    // Q17: Marcus — Cyclops x15
    {249, 1, {{17, 15}, {0, 0}, {0, 0}},
     {"Cyclops", "", ""},
     "The One-Eyed Terror", "Dungeon", 48, 90000, 700000,
     {{{291, 1}, {491, 0}},  // DW: Legendary Pants+1 + Scroll of Aqua Beam
      {{329, 0}, {199, 0}},  // DK: Plate Gloves + Spiked Shield
      {{133, 0}, {334, 0}},  // ELF: Silver Bow + Guardian Gloves
      {{105, 0}, {200, 5}}}, // MG: Bill of Balrog + Tower Shield+5
     "Cyclops roam the dungeon's great\n"
     "caverns, crushing everything in their\n"
     "path. Their single eye sees in perfect\n"
     "darkness. Fell 15 of these titans."},
    // Q18: Marcus — Dark Knight x5
    {249, 1, {{10, 5}, {0, 0}, {0, 0}},
     {"Dark Knight", "", ""},
     "Fallen Champions", "Dungeon", 52, 100000, 800000,
     {{{165, 0}, {227, 1}},  // DW: Legendary Staff + Legendary Helm+1
      {{15, 0}, {225, 0}},   // DK: Giant Sword + Dragon Helm
      {{302, 0}, {413, 0}},  // ELF: Guardian Pants + Orb of Bali Summoning
      {{14, 0}, {203, 5}}},  // MG: Lightning Sword + Serpent Shield+5
     "Dark Knights were once heroes who fell\n"
     "to corruption. Their swordplay is\n"
     "flawless, their armor impenetrable.\n"
     "Only 5 remain -- destroy them all."},
    // Q19: Marcus — Gorgon x3
    {249, 1, {{18, 3}, {0, 0}, {0, 0}},
     {"Gorgon", "", ""},
     "Heart of Darkness", "Dungeon", 55, 120000, 1000000,
     {{{166, 0}, {492, 0}},  // DW: Staff of Resurrection + Scroll of Cometfall
      {{12, 0}, {353, 0}},   // DK: Heliacal Sword + Dragon Boots
      {{270, 0}, {206, 0}},  // ELF: Guardian Armor + Legendary Shield
      {{19, 0}, {399, 0}}},  // MG: Sword of Destruction + Jewel of Chaos
     "At the dungeon's heart lurks the\n"
     "Gorgon -- a creature of terrible power.\n"
     "Only 3 exist, each guarding ancient\n"
     "treasures. Slay them and claim\n"
     "your reward."},
    // ════════════════════════════════════════════════════════
    // Devias quests (Q20-Q25)
    // ════════════════════════════════════════════════════════
    // Q20: Elise — Worm x20
    {310, 1, {{24, 20}, {0, 0}, {0, 0}},
     {"Worm", "", ""},
     "Beneath the Snow", "Devias", 22, 25000, 200000,
     {{{356, 2}, {485, 0}},  // DW: Bone Boots+2 + Scroll of Teleport
      {{326, 1}, {34, 0}},   // DK: Scale Gloves+1 + Double Axe
      {{332, 1}, {392, 0}},  // ELF: Wind Gloves+1 + Orb of Healing
      {{98, 1}, {198, 1}}},  // MG: Dragon Lance+1 + Skull Shield+1
     "Worms burrow beneath the frozen\n"
     "ground of Devias, emerging without\n"
     "warning to drag victims below. Slay\n"
     "20 to make the roads safe again."},
    // Q21: Elise — Assassin x15
    {310, 1, {{21, 15}, {0, 0}, {0, 0}},
     {"Assassin", "", ""},
     "Shadow Stalkers", "Devias", 26, 35000, 280000,
     {{{231, 1}, {486, 0}},  // DW: Sphinx Mask+1 + Scroll of Ice
      {{8, 1}, {230, 1}},    // DK: Serpent Sword+1 + Scale Helm+1
      {{131, 1}, {236, 1}},  // ELF: Battle Bow+1 + Wind Helm+1
      {{97, 1}, {200, 0}}},  // MG: Spear+1 + Tower Shield
     "Assassins lurk in the frozen forests,\n"
     "striking from the shadows. Their blades\n"
     "are swift and silent. Eliminate 15 of\n"
     "these deadly killers."},
    // Q22: Nolan — Ice Monster x20
    {311, 1, {{22, 20}, {0, 0}, {0, 0}},
     {"Ice Monster", "", ""},
     "Frozen Sentinels", "Devias", 30, 50000, 380000,
     {{{162, 1}, {359, 1}},  // DW: Serpent Staff+1 + Sphinx Boots+1
      {{66, 1}, {232, 1}},   // DK: Flail+1 + Brass Helm+1
      {{139, 1}, {268, 1}},  // ELF: Light Crossbow+1 + Wind Armor+1
      {{5, 1}, {203, 1}}},   // MG: Blade+1 + Serpent Shield+1
     "Ice Monsters guard the mountain\n"
     "passes of Devias. Born of ancient\n"
     "winter magic, they freeze all who\n"
     "approach. Destroy 20 of them."},
    // Q23: Nolan — Hommerd x20
    {311, 1, {{23, 20}, {0, 0}, {0, 0}},
     {"Hommerd", "", ""},
     "The Iron Brutes", "Devias", 35, 60000, 450000,
     {{{263, 1}, {487, 0}},  // DW: Sphinx Armor+1 + Scroll of Twister
      {{264, 1}, {360, 1}},  // DK: Brass Armor+1 + Brass Boots+1
      {{132, 0}, {237, 1}},  // ELF: Tiger Bow + Spirit Helm+1
      {{103, 1}, {200, 3}}}, // MG: Berdysh+1 + Tower Shield+3
     "Hommerds are armored beasts of\n"
     "immense strength. They patrol the\n"
     "central plains, crushing anything\n"
     "in their path. Slay 20 of them."},
    // Q24: Hale — Elite Yeti x25
    {312, 1, {{20, 25}, {0, 0}, {0, 0}},
     {"Elite Yeti", "", ""},
     "The Yeti Hordes", "Devias", 40, 80000, 600000,
     {{{163, 0}, {227, 0}},  // DW: Thunder Staff + Legendary Helm
      {{39, 0}, {233, 0}},   // DK: Larkan Axe + Plate Helm
      {{269, 1}, {411, 0}},  // ELF: Spirit Armor+1 + Orb of Yeti Summoning
      {{10, 0}, {203, 3}}},  // MG: Light Saber + Serpent Shield+3
     "Elite Yetis have overrun the southern\n"
     "reaches of Devias. Their numbers seem\n"
     "endless. Push them back -- slay 25 of\n"
     "these savage beasts."},
    // Q25: Hale — Ice Queen x20
    {312, 1, {{25, 20}, {0, 0}, {0, 0}},
     {"Ice Queen", "", ""},
     "The Frozen Throne", "Devias", 50, 120000, 900000,
     {{{164, 0}, {259, 0}},  // DW: Gorgon Staff + Legendary Armor
      {{15, 0}, {265, 0}},   // DK: Giant Sword + Plate Armor
      {{238, 0}, {334, 0}},  // ELF: Guardian Helm + Guardian Gloves
      {{104, 0}, {200, 5}}}, // MG: Great Scythe + Tower Shield+5
     "The Ice Queen commands all creatures\n"
     "of Devias from her frozen throne. She\n"
     "is the source of the endless winter.\n"
     "Destroy 20 and break her hold."},
    // ════════════════════════════════════════════════════════
    // Noria quests (Q26-Q33) — from Sentinel Arwen
    // ════════════════════════════════════════════════════════
    // Q26: Goblin x10
    {256, 1, {{26, 10}, {0, 0}, {0, 0}},
     {"Goblin", "", ""},
     "Goblin Menace", "Noria", 3, 2000, 30000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{363, 0}, {392, 0}},
      {{-1, 0}, {-1, 0}}},
     "Greetings, young archer. The Goblins\n"
     "near the village entrance grow bolder\n"
     "each day. Thin their numbers -- slay 10\n"
     "Goblins and I shall reward you."},
    // Q27: Chain Scorpion x8
    {256, 1, {{27, 8}, {0, 0}, {0, 0}},
     {"Chain Scorpion", "", ""},
     "Scorpion Sting", "Noria", 5, 3000, 50000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{331, 0}, {393, 0}},
      {{-1, 0}, {-1, 0}}},
     "Well struck. Now the Chain Scorpions\n"
     "threaten our eastern paths. Their poison\n"
     "is deadly to the young elves. Eliminate\n"
     "8 of them."},
    // Q28: Elite Goblin x8
    {256, 1, {{33, 8}, {0, 0}, {0, 0}},
     {"Elite Goblin", "", ""},
     "Elite Threat", "Noria", 8, 5000, 70000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{299, 1}, {-1, 0}},
      {{-1, 0}, {-1, 0}}},
     "The Elite Goblins are smarter and\n"
     "stronger than their lesser kin. They\n"
     "coordinate raids on our supply lines.\n"
     "Destroy 8 of them."},
    // Q29: Beetle Monster x10
    {256, 1, {{28, 10}, {0, 0}, {0, 0}},
     {"Beetle Monster", "", ""},
     "Root Rot", "Noria", 10, 8000, 100000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{267, 2}, {394, 0}},
      {{-1, 0}, {-1, 0}}},
     "Beetle Monsters burrow through the\n"
     "forest floor, destroying the ancient\n"
     "roots of our sacred trees. Slay 10\n"
     "to protect Noria's heart."},
    // Q30: Hunter x8
    {256, 1, {{29, 8}, {0, 0}, {0, 0}},
     {"Hunter", "", ""},
     "The Poachers", "Noria", 13, 12000, 130000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{235, 2}, {395, 0}},
      {{-1, 0}, {-1, 0}}},
     "Poachers -- Hunters who stalk our\n"
     "forest creatures without mercy. They\n"
     "encroach deeper each season. Put an\n"
     "end to 8 of them."},
    // Q31: Forest Monster x8
    {256, 1, {{30, 8}, {0, 0}, {0, 0}},
     {"Forest Monster", "", ""},
     "Corrupted Guardians", "Noria", 15, 15000, 160000,
     {{{128, 2}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{362, 3}, {-1, 0}},
      {{-1, 0}, {-1, 0}}},
     "The Forest Monsters were once peaceful\n"
     "guardians, now corrupted by dark magic.\n"
     "Free their tortured spirits -- slay 8\n"
     "in the deep woods."},
    // Q32: Agon x6
    {256, 1, {{31, 6}, {0, 0}, {0, 0}},
     {"Agon", "", ""},
     "Beast Territory", "Noria", 16, 20000, 200000,
     {{{-1, 0}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{298, 3}, {-1, 0}},
      {{-1, 0}, {-1, 0}}},
     "The Agons are fierce beasts that even\n"
     "seasoned warriors fear. Their territory\n"
     "blocks the southern passage. Defeat 6\n"
     "to clear the way."},
    // Q33: Stone Golem x5
    {256, 1, {{32, 5}, {0, 0}, {0, 0}},
     {"Stone Golem", "", ""},
     "Ancient Constructs", "Noria", 18, 30000, 300000,
     {{{130, 3}, {-1, 0}},
      {{-1, 0}, {-1, 0}},
      {{266, 4}, {-1, 0}},
      {{-1, 0}, {-1, 0}}},
     "The Stone Golems are ancient constructs\n"
     "awakened by forbidden magic. They are\n"
     "the greatest threat Noria faces. Destroy\n"
     "5 -- and the forest will know peace again."},
};

// ═══════════════════════════════════════════════════════
// Helper: insert item reward into player inventory
// ═══════════════════════════════════════════════════════

static void GiveItemReward(Session &session, Database &db,
                           const QuestItemReward &reward) {
  if (reward.defIndex < 0)
    return;

  uint8_t cat = reward.defIndex / 32;
  uint8_t idx = reward.defIndex % 32;

  auto def = db.GetItemDefinition(cat, idx);
  if (def.name.empty()) {
    printf("[Quest] Item reward defIndex=%d not found in DB\n", reward.defIndex);
    return;
  }

  uint8_t outSlot = 0;
  if (!InventoryHandler::FindEmptySpace(session, def.width, def.height,
                                        outSlot)) {
    printf("[Quest] fd=%d inventory full, skipping item reward %s\n",
           session.GetFd(), def.name.c_str());
    return;
  }

  for (int y = 0; y < def.height; ++y) {
    for (int x = 0; x < def.width; ++x) {
      int slot = (outSlot / 8 + y) * 8 + (outSlot % 8 + x);
      if (slot < 64) {
        session.bag[slot].occupied = true;
        session.bag[slot].defIndex = reward.defIndex;
        session.bag[slot].category = cat;
        session.bag[slot].itemIndex = idx;
        session.bag[slot].quantity = 1;
        session.bag[slot].itemLevel = reward.itemLevel;
        session.bag[slot].primary = (slot == outSlot);
      }
    }
  }

  db.SaveCharacterInventory(session.characterId, reward.defIndex, 1,
                            reward.itemLevel, outSlot);

  printf("[Quest] fd=%d received item: %s +%d (slot %d)\n", session.GetFd(),
         def.name.c_str(), reward.itemLevel, outSlot);
}

// ═══════════════════════════════════════════════════════
// Helper: level-up check after XP award
// ═══════════════════════════════════════════════════════

static bool CheckLevelUp(Session &session) {
  bool leveledUp = false;
  while (true) {
    uint64_t nextXP = Database::GetXPForLevel(session.level);
    if (session.experience >= nextXP && session.level < 400) {
      session.level++;
      CharacterClass charCls =
          static_cast<CharacterClass>(session.classCode);
      session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls);
      session.maxHp = StatCalculator::CalculateMaxHP(
                          charCls, session.level, session.vitality) +
                      session.petBonusMaxHp;
      session.maxMana = StatCalculator::CalculateMaxMP(charCls, session.level,
                                                      session.energy);
      session.maxAg = StatCalculator::CalculateMaxAG(
          session.strength, session.dexterity, session.vitality,
          session.energy);
      session.hp = session.maxHp;
      session.mana = session.maxMana;
      session.ag = session.maxAg;
      leveledUp = true;
    } else {
      break;
    }
  }
  return leveledUp;
}

namespace QuestHandler {

// ═══════════════════════════════════════════════════════
// Send quest catalog — all quest definitions (C2 packet)
// ═══════════════════════════════════════════════════════

void SendQuestCatalog(Session &session) {
  uint16_t totalSize = (uint16_t)(sizeof(PWMSG_HEAD) + 1 +
                                   QUEST_COUNT * sizeof(PMSG_QUEST_CATALOG_ENTRY));
  std::vector<uint8_t> buf(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(buf.data());
  *head = MakeC2Header(totalSize, Opcode::QUEST_CATALOG);
  buf[sizeof(PWMSG_HEAD)] = (uint8_t)QUEST_COUNT;

  for (int i = 0; i < QUEST_COUNT; i++) {
    auto *e = reinterpret_cast<PMSG_QUEST_CATALOG_ENTRY *>(
        buf.data() + sizeof(PWMSG_HEAD) + 1 +
        i * sizeof(PMSG_QUEST_CATALOG_ENTRY));
    const auto &q = g_quests[i];
    e->questId = (uint8_t)i;
    e->guardNpcType = q.guardNpcType;
    e->targetCount = q.targetCount;
    for (int t = 0; t < 3; t++) {
      e->targets[t].monsterType = q.targets[t].monsterType;
      e->targets[t].killsRequired = q.targets[t].killsRequired;
      strncpy(e->targets[t].name, q.targetNames[t] ? q.targetNames[t] : "", 23);
      e->targets[t].name[23] = '\0';
    }
    strncpy(e->questName, q.questName ? q.questName : "", 31);
    e->questName[31] = '\0';
    strncpy(e->location, q.location ? q.location : "", 15);
    e->location[15] = '\0';
    e->recommendedLevel = q.recommendedLevel;
    e->zenReward = q.zenReward;
    e->xpReward = q.xpReward;
    for (int c = 0; c < 4; c++)
      for (int s = 0; s < 2; s++) {
        e->classReward[c][s].defIndex = q.classReward[c][s].defIndex;
        e->classReward[c][s].itemLevel = q.classReward[c][s].itemLevel;
      }
    strncpy(e->loreText, q.loreText ? q.loreText : "", 255);
    e->loreText[255] = '\0';
  }

  session.Send(buf.data(), buf.size());
  printf("[Quest] Sent catalog (%d quests, %d bytes) to fd=%d\n",
         QUEST_COUNT, (int)totalSize, session.GetFd());
}

// ═══════════════════════════════════════════════════════
// Send quest state — variable-length C1 packet
// ═══════════════════════════════════════════════════════

void SendQuestState(Session &session) {
  uint8_t buf[256];
  size_t off = sizeof(PSBMSG_HEAD); // 4 bytes

  // completedMask (8 bytes)
  memcpy(buf + off, &session.completedQuestMask, 8);
  off += 8;

  // activeCount (1 byte)
  uint8_t count = (uint8_t)session.activeQuests.size();
  buf[off++] = count;

  // entries (4 bytes each)
  for (auto &aq : session.activeQuests) {
    buf[off++] = (uint8_t)aq.questId;
    buf[off++] = (uint8_t)aq.killCount[0];
    buf[off++] = (uint8_t)aq.killCount[1];
    buf[off++] = (uint8_t)aq.killCount[2];
  }

  auto *head = reinterpret_cast<PSBMSG_HEAD *>(buf);
  *head = MakeC1SubHeader((uint8_t)off, Opcode::QUEST, Opcode::SUB_QUEST_STATE);
  session.Send(buf, off);
}

// ═══════════════════════════════════════════════════════
// Accept quest
// ═══════════════════════════════════════════════════════

void HandleQuestAccept(Session &session, const std::vector<uint8_t> &packet,
                       Database &db) {
  if (packet.size() < sizeof(PMSG_QUEST_ACCEPT_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_QUEST_ACCEPT_RECV *>(packet.data());
  int questId = recv->questId;

  if (questId < 0 || questId >= QUEST_COUNT) {
    printf("[Quest] fd=%d invalid questId=%d\n", session.GetFd(), questId);
    return;
  }

  // Check if already completed
  if (session.completedQuestMask & (1ULL << questId)) {
    printf("[Quest] fd=%d quest %d already completed\n", session.GetFd(), questId);
    return;
  }

  // Check if already active
  for (auto &aq : session.activeQuests) {
    if (aq.questId == questId) {
      printf("[Quest] fd=%d quest %d already active\n", session.GetFd(), questId);
      return;
    }
  }

  // Accept: add to active quests
  Session::ActiveQuest aq;
  aq.questId = questId;
  aq.killCount[0] = aq.killCount[1] = aq.killCount[2] = 0;
  session.activeQuests.push_back(aq);

  db.SaveQuestProgress(session.characterId, questId, 0, 0, 0, false);
  SendQuestState(session);

  printf("[Quest] fd=%d accepted quest %d\n", session.GetFd(), questId);
}

// ═══════════════════════════════════════════════════════
// Complete quest
// ═══════════════════════════════════════════════════════

void HandleQuestComplete(Session &session, const std::vector<uint8_t> &packet,
                         Database &db, Server &server) {
  if (packet.size() < sizeof(PMSG_QUEST_COMPLETE_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_QUEST_COMPLETE_RECV *>(packet.data());
  int questId = recv->questId;

  if (questId < 0 || questId >= QUEST_COUNT) {
    printf("[Quest] fd=%d invalid questId=%d for complete\n", session.GetFd(), questId);
    return;
  }

  // Find in active quests
  int activeIdx = -1;
  for (int i = 0; i < (int)session.activeQuests.size(); i++) {
    if (session.activeQuests[i].questId == questId) {
      activeIdx = i;
      break;
    }
  }
  if (activeIdx < 0) {
    printf("[Quest] fd=%d quest %d not active\n", session.GetFd(), questId);
    return;
  }

  const auto &q = g_quests[questId];
  auto &aq = session.activeQuests[activeIdx];

  // Verify all targets met
  for (int i = 0; i < q.targetCount; i++) {
    if (aq.killCount[i] < q.targets[i].killsRequired) {
      printf("[Quest] fd=%d quest %d target %d not done (%d/%d)\n",
             session.GetFd(), questId, i, aq.killCount[i],
             q.targets[i].killsRequired);
      return;
    }
  }

  // Award zen + XP
  session.zen += q.zenReward;
  session.experience += q.xpReward;

  // Give class-specific rewards only
  int classIdx = session.classCode / 16; // DW=0, DK=1, ELF=2, MG=3
  if (classIdx >= 0 && classIdx < 4) {
    GiveItemReward(session, db, q.classReward[classIdx][0]);
    GiveItemReward(session, db, q.classReward[classIdx][1]);
  }

  bool leveledUp = CheckLevelUp(session);

  // Mark completed
  session.completedQuestMask |= (1ULL << questId);
  session.activeQuests.erase(session.activeQuests.begin() + activeIdx);

  db.SaveQuestProgress(session.characterId, questId, 0, 0, 0, true);
  db.UpdateCharacterMoney(session.characterId, session.zen);

  // Send reward notification
  PMSG_QUEST_REWARD_SEND reward{};
  reward.h = MakeC1SubHeader(sizeof(reward), Opcode::QUEST, Opcode::SUB_QUEST_REWARD);
  reward.zenReward = q.zenReward;
  reward.xpReward = q.xpReward;
  reward.questId = (uint8_t)questId;
  session.Send(&reward, sizeof(reward));

  SendQuestState(session);
  InventoryHandler::SendInventorySync(session);
  CharacterHandler::SendCharStats(session);

  char buf[128];
  snprintf(buf, sizeof(buf), "Quest complete! +%u Zen, +%u Experience",
           q.zenReward, q.xpReward);
  server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
  if (leveledUp) {
    snprintf(buf, sizeof(buf), "Congratulations! Level %d reached!",
             (int)session.level);
    server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
    // Rescale active summon to match new owner level
    if (session.activeSummonIndex > 0)
      server.GetWorld().RescaleSummon(session.activeSummonIndex, session.level);
  }

  printf("[Quest] fd=%d completed quest %d (zen+%u xp+%u)\n",
         session.GetFd(), questId, q.zenReward, q.xpReward);
}

// ═══════════════════════════════════════════════════════
// Abandon quest
// ═══════════════════════════════════════════════════════

void HandleQuestAbandon(Session &session, const std::vector<uint8_t> &packet,
                        Database &db) {
  if (packet.size() < sizeof(PMSG_QUEST_ABANDON_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_QUEST_ABANDON_RECV *>(packet.data());
  int questId = recv->questId;

  if (questId < 0 || questId >= QUEST_COUNT) {
    printf("[Quest] fd=%d invalid questId=%d for abandon\n", session.GetFd(), questId);
    return;
  }

  // Find and remove from active quests
  for (auto it = session.activeQuests.begin(); it != session.activeQuests.end(); ++it) {
    if (it->questId == questId) {
      printf("[Quest] fd=%d abandoned quest %d (was %d/%d/%d)\n",
             session.GetFd(), questId, it->killCount[0], it->killCount[1], it->killCount[2]);
      session.activeQuests.erase(it);
      db.DeleteQuestProgress(session.characterId, questId);
      SendQuestState(session);
      return;
    }
  }

  printf("[Quest] fd=%d quest %d not found for abandon\n", session.GetFd(), questId);
}

// ═══════════════════════════════════════════════════════
// Monster kill — check all active quests
// ═══════════════════════════════════════════════════════

void OnMonsterKill(Session &session, uint16_t monsterType, bool isSummon, Database &db) {
  if (isSummon) return; // Killing a summon doesn't count for quests
  for (auto &aq : session.activeQuests) {
    if (aq.questId < 0 || aq.questId >= QUEST_COUNT)
      continue;
    const auto &q = g_quests[aq.questId];
    for (int i = 0; i < q.targetCount; i++) {
      if (q.targets[i].monsterType == monsterType &&
          aq.killCount[i] < q.targets[i].killsRequired) {
        aq.killCount[i]++;
        printf("[Quest] fd=%d quest %d target %d: kill %d/%d (monType=%d)\n",
               session.GetFd(), aq.questId, i, aq.killCount[i],
               q.targets[i].killsRequired, monsterType);
        db.SaveQuestProgress(session.characterId, aq.questId,
                             aq.killCount[0], aq.killCount[1], aq.killCount[2], false);
        SendQuestState(session);
        return; // Only count once per kill
      }
    }
  }
}

} // namespace QuestHandler

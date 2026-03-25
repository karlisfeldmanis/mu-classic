#include "InventoryUI_Internal.hpp"
#include "HeroCharacter.hpp"
#include <algorithm>
#include <cstdio>

// OpenMU Version075: Staff Rise % = magicPower/2 + level bonus table
static int CalculateStaffRise(int magicPower, int itemLevel) {
  if (magicPower <= 0)
    return 0;
  int base = magicPower / 2;
  static const int evenTable[16] = {0,  3,  7,  10, 14, 17, 21, 24,
                                    28, 31, 35, 40, 45, 50, 56, 63};
  static const int oddTable[16] = {0,  4,  7,  11, 14, 18, 21, 25,
                                   28, 32, 36, 40, 45, 51, 57, 63};
  int lvl = std::min(itemLevel, 15);
  int levelBonus = (magicPower % 2 == 0) ? evenTable[lvl] : oddTable[lvl];
  return base + levelBonus;
}

// ─── WoW-style quality colors ──────────────────────────────────────────────
static const ImU32 TT_WHITE      = IM_COL32(255, 255, 255, 255);
static const ImU32 TT_GRAY       = IM_COL32(160, 160, 160, 255);
static const ImU32 TT_GREEN      = IM_COL32(30, 255, 30, 255);
static const ImU32 TT_RED        = IM_COL32(255, 50, 50, 255);
static const ImU32 TT_GOLD       = IM_COL32(255, 215, 0, 255);
static const ImU32 TT_BLUE       = IM_COL32(100, 180, 255, 255);
static const ImU32 TT_PURPLE     = IM_COL32(180, 100, 255, 255);
static const ImU32 TT_ORANGE     = IM_COL32(255, 128, 0, 255);
static const ImU32 TT_LIGHT_BLUE = IM_COL32(150, 200, 255, 255);
static const ImU32 TT_DIM_GREEN  = IM_COL32(180, 220, 180, 255);

static ImU32 GetQualityColor(int itemLevel, uint8_t optionFlags = 0) {
  if (itemLevel >= 9)  return TT_ORANGE;
  if (itemLevel >= 7)  return TT_PURPLE;
  if (itemLevel >= 4)  return TT_BLUE;
  if (optionFlags != 0) return TT_BLUE;  // Any option → blue minimum
  if (itemLevel >= 1)  return TT_GREEN;
  return TT_WHITE;
}

static const char *GetSlotText(const ClientItemDefinition *def) {
  if (def->category <= 5) return def->twoHanded ? "Two-Hand" : "Main Hand";
  if (def->category == 6) return "Off Hand";
  if (def->category == 7) return "Head";
  if (def->category == 8) return "Chest";
  if (def->category == 9) return "Legs";
  if (def->category == 10) return "Hands";
  if (def->category == 11) return "Feet";
  return nullptr;
}

static const char *GetTypeText(uint8_t category) {
  switch (category) {
    case 0: return "Sword";
    case 1: return "Axe";
    case 2: return "Mace";
    case 3: return "Spear";
    case 4: return "Bow";
    case 5: return "Staff";
    case 6: return "Shield";
    case 7: return "Helm";
    case 8: return "Armor";
    case 9: return "Pants";
    case 10: return "Gloves";
    case 11: return "Boots";
    default: return nullptr;
  }
}

namespace InventoryUI {

void AddPendingItemTooltip(int16_t defIndex, int itemLevel,
                           uint32_t shopBuyPrice, uint8_t optionFlags) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIndex);
  const ClientItemDefinition *def = nullptr;
  ClientItemDefinition fallback;

  if (it != g_itemDefs.end()) {
    def = &it->second;
  } else {
    fallback.name = ItemDatabase::GetDropName(defIndex);
    fallback.category = (uint8_t)(defIndex / 32);
    fallback.width = 1;
    fallback.height = 1;
    def = &fallback;
  }

  // Find equipped item for comparison (use ClientItemDefinition for accurate stats)
  int equipSlot = GetEquipSlotForCategory(def->category);
  const ClientItemDefinition *equippedCID = nullptr;
  int equippedLevel = 0;
  if (equipSlot >= 0 && equipSlot < 12 && s_ctx->equipSlots[equipSlot].equipped) {
    equippedLevel = s_ctx->equipSlots[equipSlot].itemLevel;
    int16_t eqDi = ItemDatabase::GetDefIndexFromCategory(
        s_ctx->equipSlots[equipSlot].category,
        s_ctx->equipSlots[equipSlot].itemIndex);
    // Don't compare item against itself
    if (eqDi != defIndex ||
        s_ctx->equipSlots[equipSlot].itemLevel != (uint8_t)itemLevel) {
      auto &defs = ItemDatabase::GetItemDefs();
      auto eit = defs.find(eqDi);
      if (eit != defs.end())
        equippedCID = &eit->second;
    }
  }

  // ─── Pre-calculate data ─────────────────────────────────────────────────

  // OpenMU Version075: Enhancement bonus table for weapons and armor
  // Shields use +1/level instead (separate rule)
  static const int kEnhanceTable[16] = {0,  3,  6,  9,  12, 15, 18, 21,
                                         24, 27, 31, 36, 42, 49, 57, 66};

  int levelDmgBonus = 0, levelDefBonus = 0;
  if (itemLevel > 0) {
    int lvl = std::min(itemLevel, 15);
    if (def->category <= 5)
      levelDmgBonus = kEnhanceTable[lvl]; // Weapon damage bonus
    if (def->category >= 7 && def->category <= 11)
      levelDefBonus = kEnhanceTable[lvl]; // Armor defense bonus (same table)
    if (def->category == 6)
      levelDefBonus = lvl; // Shield: +1 defense per level
  }

  int staffRise = 0;
  if (def->category == 5 && def->magicPower > 0)
    staffRise = CalculateStaffRise(def->magicPower, itemLevel);

  int potionHeal = 0, potionMana = 0;
  const char *potionEffect = nullptr;
  if (def->category == 14) {
    uint8_t pidx = (uint8_t)(defIndex % 32);
    if (pidx == 0)      potionHeal = 10;
    else if (pidx == 1) potionHeal = 20;
    else if (pidx == 2) potionHeal = 50;
    else if (pidx == 3) potionHeal = 100;
    else if (pidx == 4) potionMana = 20;
    else if (pidx == 5) potionMana = 50;
    else if (pidx == 6) potionMana = 100;
    else if (pidx == 8) potionEffect = "Cures Poison";
    else if (pidx == 9) potionEffect = "Restores 10 HP";
    else if (pidx == 10) potionEffect = "Teleport to Town";
  }

  const char *accessoryEffect = nullptr;
  const char *accessoryEffect2 = nullptr;
  const char *accessoryEffect3 = nullptr;
  const char *accessorySlot = nullptr;
  if (def->category == 13) {
    uint8_t aidx = (uint8_t)(defIndex % 32);
    if (aidx == 0) {
      accessoryEffect = "+50 Max HP";
      accessoryEffect2 = "20% Damage Reduction";
      accessorySlot = "Pet";
    } else if (aidx == 1) {
      accessoryEffect = "30% Attack Damage Increase";
      accessorySlot = "Pet";
    } else if (aidx == 2) {
      accessoryEffect = "Rideable Mount";
      accessorySlot = "Pet";
    } else if (aidx == 3) {
      accessoryEffect = "Rideable Mount";
      accessoryEffect2 = "Enables Flying";
      accessorySlot = "Pet";
    } else if (aidx == 8) {
      accessoryEffect = "+50 Ice Resistance";
      accessorySlot = "Ring";
    } else if (aidx == 9) {
      accessoryEffect = "+50 Poison Resistance";
      accessorySlot = "Ring";
    } else if (aidx == 10) {
      accessoryEffect = "Transform into Monsters";
      accessorySlot = "Ring";
    } else if (aidx == 12) {
      accessoryEffect = "+50 Lightning Resistance";
      accessorySlot = "Pendant";
    } else if (aidx == 13) {
      accessoryEffect = "+50 Fire Resistance";
      accessorySlot = "Pendant";
    }
  }

  uint8_t scrollSkillId = 0;
  bool scrollAlreadyLearned = false;
  if (def->category == 15) {
    static const uint8_t scrollMap[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7},
        {7, 8}, {8, 9}, {9, 10}, {11, 12}, {12, 13}, {13, 14},
    };
    uint8_t idx = (uint8_t)(defIndex % 32);
    for (auto &m : scrollMap)
      if (m[0] == idx) { scrollSkillId = m[1]; break; }
  } else if (def->category == 12) {
    static const uint8_t orbMap[][2] = {
        {20, 19}, {21, 20}, {22, 21}, {23, 22},
        {24, 23}, {7, 41}, {12, 42}, {19, 43},
        // Elf orbs (OpenMU Version075)
        {8, 26}, {9, 27}, {10, 28}, {11, 30},
        {25, 31}, {26, 32}, {27, 33}, {28, 34}, {29, 35},
    };
    uint8_t idx = (uint8_t)(defIndex % 32);
    for (auto &m : orbMap)
      if (m[0] == idx) { scrollSkillId = m[1]; break; }
  }
  const SkillDef *skillDef = nullptr;
  if (scrollSkillId > 0) {
    if (s_ctx->learnedSkills) {
      for (auto s : *s_ctx->learnedSkills)
        if (s == scrollSkillId) { scrollAlreadyLearned = true; break; }
    }
    for (int i = 0; i < NUM_DK_SKILLS; i++)
      if (g_dkSkills[i].skillId == scrollSkillId) { skillDef = &g_dkSkills[i]; break; }
    if (!skillDef)
      for (int i = 0; i < NUM_DW_SPELLS; i++)
        if (g_dwSpells[i].skillId == scrollSkillId) { skillDef = &g_dwSpells[i]; break; }
    if (!skillDef)
      for (int i = 0; i < NUM_ELF_SKILLS; i++)
        if (g_elfSkills[i].skillId == scrollSkillId) { skillDef = &g_elfSkills[i]; break; }
  }

  // ─── Calculate height ──────────────────────────────────────────────────

  float lineH = 18.0f;
  float sepH = 10.0f;
  float th = 12.0f; // top pad

  th += lineH; // Name

  bool hasSlotLine = (def->category <= 11);
  if (hasSlotLine) th += lineH;
  if (accessorySlot) th += lineH;

  th += sepH; // separator after header

  bool hasStats = false;
  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0)) {
    th += lineH; hasStats = true;
  }
  if (def->category <= 5 && def->attackSpeed > 0) {
    th += lineH; hasStats = true;
  }
  // DPS line for weapons with both damage and speed
  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0) && def->attackSpeed > 0) {
    th += lineH;
  }
  if (staffRise > 0) {
    th += lineH; hasStats = true;
  }
  if ((def->category == 6 || (def->category >= 7 && def->category <= 11)) &&
      def->defense > 0) {
    th += lineH; hasStats = true;
  }

  if (equippedCID) {
    if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0))
      th += lineH;
    if ((def->category == 6 || (def->category >= 7 && def->category <= 11)) &&
        def->defense > 0)
      th += lineH;
  }

  // Item options height
  bool hasOptions = (optionFlags != 0 && def->category <= 11);
  if (hasOptions) {
    if (hasStats) th += sepH;
    if (optionFlags & 0x80) th += lineH; // Skill
    if (optionFlags & 0x40) th += lineH; // Luck
    if (optionFlags & 0x07) th += lineH; // Additional
  }

  bool hasEffects = false;
  if (potionHeal > 0 || potionMana > 0 || potionEffect) {
    if (hasStats || hasOptions) th += sepH;
    th += lineH;
    hasEffects = true;
  }
  if (accessoryEffect) {
    if (hasStats && !hasEffects) th += sepH;
    th += lineH;
    hasEffects = true;
  }
  if (accessoryEffect2) th += lineH;
  if (accessoryEffect3) th += lineH;
  // Mounts are unique items
  bool isMount = (def->category == 13 && (defIndex % 32 == 2 || defIndex % 32 == 3));
  if (isMount) th += lineH;

  if (skillDef) {
    if (hasStats || hasEffects) th += sepH;
    th += lineH; // Teaches line
    if (skillDef->damageBonus > 0) th += lineH;
    if (skillDef->desc && skillDef->desc[0]) th += lineH;
    th += lineH; // Learned status
    if (!scrollAlreadyLearned) th += lineH; // "Right-click to learn" hint
  }

  // Requirements come from item definition (DB-driven via catalog packet)
  bool hasReqs = (def->levelReq > 0 || def->reqStr > 0 ||
                  def->reqDex > 0 || def->reqVit > 0 ||
                  def->reqEne > 0);
  bool hasClassReqs = (def->classFlags > 0 && (def->classFlags & 0x0F) != 0x0F);
  if (hasReqs || hasClassReqs) {
    th += sepH;
    if (def->levelReq > 0) th += lineH;
    if (def->reqStr > 0) th += lineH;
    if (def->reqDex > 0) th += lineH;
    if (def->reqVit > 0) th += lineH;
    if (def->reqEne > 0) th += lineH;
    if (hasClassReqs) {
      if (def->classFlags & 1) th += lineH;
      if (def->classFlags & 2) th += lineH;
      if (def->classFlags & 4) th += lineH;
      if (def->classFlags & 8) th += lineH;
    }
  }

  if (def->buyPrice > 0) {
    th += sepH;
    th += lineH;
  }

  th += 8; // bottom pad

  // ─── Build lines ────────────────────────────────────────────────────────

  float tooltipW = 240.0f;
  BeginPendingTooltip(tooltipW, th);

  ImU32 qualityColor = GetQualityColor(itemLevel, optionFlags);
  g_pendingTooltip.borderColor = qualityColor;

  // Name (centered, quality color)
  char nameBuf[64];
  if (itemLevel > 0)
    snprintf(nameBuf, sizeof(nameBuf), "%s +%d", def->name.c_str(), itemLevel);
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", def->name.c_str());
  AddPendingTooltipLine(qualityColor, nameBuf, 1); // centered

  // Slot + Type (split left|right)
  if (hasSlotLine) {
    const char *slotText = GetSlotText(def);
    const char *typeText = GetTypeText(def->category);
    char slotBuf[64];
    if (slotText && typeText)
      snprintf(slotBuf, sizeof(slotBuf), "%s|%s", slotText, typeText);
    else if (slotText)
      snprintf(slotBuf, sizeof(slotBuf), "%s|", slotText);
    else if (typeText)
      snprintf(slotBuf, sizeof(slotBuf), "|%s", typeText);
    else
      slotBuf[0] = '\0';
    if (slotBuf[0])
      AddPendingTooltipLine(TT_WHITE, slotBuf, 8); // split left|right
  }

  if (accessorySlot)
    AddPendingTooltipLine(TT_WHITE, accessorySlot);

  AddTooltipSeparator();

  // ─── Stats ──────────────────────────────────────────────────────────────

  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0)) {
    int dMin = def->dmgMin + levelDmgBonus;
    int dMax = def->dmgMax + levelDmgBonus;
    char buf[64];
    if (levelDmgBonus > 0)
      snprintf(buf, sizeof(buf), "%d - %d Damage  (+%d)", dMin, dMax, levelDmgBonus);
    else
      snprintf(buf, sizeof(buf), "%d - %d Damage", dMin, dMax);
    AddPendingTooltipLine(TT_WHITE, buf);

    if (equippedCID) {
      // Skip damage comparison between physical and magic weapons
      bool newIsMagic = (def->category == 5 && def->magicPower > 0);
      bool eqIsMagic = (equippedCID->category == 5 && equippedCID->magicPower > 0);
      if (newIsMagic == eqIsMagic) {
        int eqDmgBonus = kEnhanceTable[std::min(equippedLevel, 15)];
        int avgNew = (dMin + dMax) / 2;
        int avgOld = ((int)equippedCID->dmgMin + eqDmgBonus + (int)equippedCID->dmgMax + eqDmgBonus) / 2;
        int diff = avgNew - avgOld;
        char cmpBuf[48];
        if (diff > 0) {
          snprintf(cmpBuf, sizeof(cmpBuf), "  (+%d vs equipped)", diff);
          AddPendingTooltipLine(TT_GREEN, cmpBuf);
        } else if (diff < 0) {
          snprintf(cmpBuf, sizeof(cmpBuf), "  (%d vs equipped)", diff);
          AddPendingTooltipLine(TT_RED, cmpBuf);
        } else {
          AddPendingTooltipLine(TT_GRAY, "  (same as equipped)");
        }
      }
    }
  }

  if (def->category <= 5 && def->attackSpeed > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Speed %d", def->attackSpeed);
    AddPendingTooltipLine(TT_WHITE, buf);
  }

  // DPS = average damage * attacks per second
  // Attack speed multiplier: 1.0 + weaponSpeed * 0.015 (from HeroCharacter)
  // Base cooldown: 0.6s → attacks/sec = (1 + speed*0.015) / 0.6
  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0) && def->attackSpeed > 0) {
    int dpsDmgMin = def->dmgMin + levelDmgBonus;
    int dpsDmgMax = def->dmgMax + levelDmgBonus;
    float avgDmg = (float)(dpsDmgMin + dpsDmgMax) / 2.0f;
    float aps = (1.0f + def->attackSpeed * 0.015f) / 0.6f;
    float dps = avgDmg * aps;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f DPS", dps);
    AddPendingTooltipLine(TT_GOLD, buf);
  }

  if (staffRise > 0) {
    char buf[48];
    snprintf(buf, sizeof(buf), "+%d%% Magic Damage", staffRise);
    AddPendingTooltipLine(TT_GREEN, buf);

    // Compare magic damage % vs equipped staff
    if (equippedCID && equippedCID->category == 5 && equippedCID->magicPower > 0) {
      int eqStaffRise = CalculateStaffRise(equippedCID->magicPower, equippedLevel);
      int diff = staffRise - eqStaffRise;
      char cmpBuf[48];
      if (diff > 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  (+%d%% vs equipped)", diff);
        AddPendingTooltipLine(TT_GREEN, cmpBuf);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  (%d%% vs equipped)", diff);
        AddPendingTooltipLine(TT_RED, cmpBuf);
      } else {
        AddPendingTooltipLine(TT_GRAY, "  (same as equipped)");
      }
    }
  }

  if ((def->category == 6 || (def->category >= 7 && def->category <= 11)) &&
      def->defense > 0) {
    int totalDef = def->defense + levelDefBonus;
    char buf[64];
    if (levelDefBonus > 0)
      snprintf(buf, sizeof(buf), "%d Defense  (+%d)", totalDef, levelDefBonus);
    else
      snprintf(buf, sizeof(buf), "%d Defense", totalDef);
    AddPendingTooltipLine(TT_WHITE, buf);

    if (equippedCID) {
      int eqDefBonus = (equippedCID->category >= 7 && equippedCID->category <= 11)
          ? kEnhanceTable[std::min(equippedLevel, 15)]  // Armor: enhancement table
          : std::min(equippedLevel, 15);                 // Shield: +1/level
      int diff = totalDef - ((int)equippedCID->defense + eqDefBonus);
      char cmpBuf[48];
      if (diff > 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  (+%d vs equipped)", diff);
        AddPendingTooltipLine(TT_GREEN, cmpBuf);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  (%d vs equipped)", diff);
        AddPendingTooltipLine(TT_RED, cmpBuf);
      } else {
        AddPendingTooltipLine(TT_GRAY, "  (same as equipped)");
      }
    }
  }

  // ─── Item Options ──────────────────────────────────────────────────────

  if (hasOptions) {
    if (hasStats) AddTooltipSeparator();
    if (optionFlags & 0x80) {
      if (def->category == 4)
        AddPendingTooltipLine(TT_BLUE, "+Skill (Multi-Shot)");
      else
        AddPendingTooltipLine(TT_BLUE, "+Skill");
    }
    if (optionFlags & 0x40) {
      AddPendingTooltipLine(TT_BLUE, "+Luck (+5% Critical Rate)");
    }
    int addLevel = optionFlags & 0x07;
    if (addLevel > 0) {
      char buf[48];
      if (def->category <= 5)
        snprintf(buf, sizeof(buf), "+%d Additional Damage", addLevel * 4);
      else
        snprintf(buf, sizeof(buf), "+%d Additional Defense", addLevel * 4);
      AddPendingTooltipLine(TT_BLUE, buf);
    }
  }

  // ─── Use/Equip effects ──────────────────────────────────────────────────

  if (potionHeal > 0 || potionMana > 0 || potionEffect) {
    if (hasStats || hasOptions) AddTooltipSeparator();
    if (potionHeal > 0) {
      char buf[48];
      snprintf(buf, sizeof(buf), "Use: Restores %d Health", potionHeal);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
    if (potionMana > 0) {
      char buf[48];
      snprintf(buf, sizeof(buf), "Use: Restores %d Mana", potionMana);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
    if (potionEffect) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Use: %s", potionEffect);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
  }

  if (accessoryEffect) {
    if (hasStats && !(potionHeal > 0 || potionMana > 0 || potionEffect))
      AddTooltipSeparator();
    char buf[64];
    snprintf(buf, sizeof(buf), "Equip: %s", accessoryEffect);
    AddPendingTooltipLine(TT_GREEN, buf);
  }
  if (accessoryEffect2) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Equip: %s", accessoryEffect2);
    AddPendingTooltipLine(TT_GREEN, buf);
  }
  if (accessoryEffect3) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Equip: %s", accessoryEffect3);
    AddPendingTooltipLine(TT_GREEN, buf);
  }
  if (isMount) {
    AddPendingTooltipLine(TT_GOLD, "Unique");
  }

  // ─── Skill info ─────────────────────────────────────────────────────────

  if (skillDef) {
    if (hasStats || hasEffects) AddTooltipSeparator();

    bool isDW = (def->category == 15);
    char skillBuf[64];
    snprintf(skillBuf, sizeof(skillBuf), "Teaches: %s (%d %s)", skillDef->name,
             skillDef->resourceCost, isDW ? "Mana" : "AG");
    AddPendingTooltipLine(TT_LIGHT_BLUE, skillBuf);

    if (skillDef->damageBonus > 0) {
      char dmgBuf[48];
      snprintf(dmgBuf, sizeof(dmgBuf), "+%d Bonus Damage", skillDef->damageBonus);
      AddPendingTooltipLine(TT_GREEN, dmgBuf);
    }

    if (skillDef->desc && skillDef->desc[0])
      AddPendingTooltipLine(TT_GOLD, skillDef->desc);

    if (scrollAlreadyLearned)
      AddPendingTooltipLine(TT_ORANGE, "Already Known");
    else {
      AddPendingTooltipLine(TT_GREEN, "Not Yet Learned");
      AddPendingTooltipLine(TT_GRAY, "Right-click in inventory to learn");
    }
  }

  // ─── Requirements ───────────────────────────────────────────────────────

  if (hasReqs || hasClassReqs) {
    AddTooltipSeparator();

    auto addReq = [&](const char *label, int current, int req) {
      char rBuf[48];
      snprintf(rBuf, sizeof(rBuf), "Requires %s %d", label, req);
      AddPendingTooltipLine((current >= req) ? TT_DIM_GREEN : TT_RED, rBuf);
    };

    if (def->levelReq > 0) addReq("Level", *s_ctx->serverLevel, def->levelReq);
    if (def->reqStr > 0) addReq("Strength", *s_ctx->serverStr, def->reqStr);
    if (def->reqDex > 0) addReq("Agility", *s_ctx->serverDex, def->reqDex);
    if (def->reqVit > 0) addReq("Vitality", *s_ctx->serverVit, def->reqVit);
    if (def->reqEne > 0) addReq("Energy", *s_ctx->serverEne, def->reqEne);

    if (hasClassReqs) {
      uint32_t myFlag = (1 << (s_ctx->hero->GetClass() / 16));
      bool canUse = (def->classFlags & myFlag) != 0;
      const char *classNames[] = {"Dark Wizard", "Dark Knight", "Fairy Elf",
                                  "Magic Gladiator"};
      for (int i = 0; i < 4; i++) {
        if (def->classFlags & (1 << i))
          AddPendingTooltipLine(canUse ? TT_DIM_GREEN : TT_RED, classNames[i]);
      }
    }
  }

  // ─── Price ──────────────────────────────────────────────────────────────

  if (shopBuyPrice > 0) {
    // Shop item: show buy price (already includes stack multiplier from server)
    AddTooltipSeparator();
    std::string bStr = std::to_string(shopBuyPrice);
    int bn = (int)bStr.length() - 3;
    while (bn > 0) { bStr.insert(bn, ","); bn -= 3; }
    char buf[64];
    snprintf(buf, sizeof(buf), "Buy Price: %s Zen", bStr.c_str());
    AddPendingTooltipLine(TT_GOLD, buf);
  } else if (def->buyPrice > 0) {
    // Inventory item: show sell price (accounts for stack size)
    AddTooltipSeparator();
    bool isAmmo = (def->category == 4 && (def->itemIndex == 7 || def->itemIndex == 15));
    bool isPotion = (def->category == 14 && def->width == 1 && def->height == 1);
    uint32_t stackQty = isAmmo ? 255 : (isPotion ? 20 : 1);
    uint32_t sellTotal = (def->buyPrice * stackQty) / 3;
    std::string sStr = std::to_string(sellTotal);
    int n = (int)sStr.length() - 3;
    while (n > 0) { sStr.insert(n, ","); n -= 3; }
    char buf[64];
    snprintf(buf, sizeof(buf), "Sell Price: %s Zen", sStr.c_str());
    AddPendingTooltipLine(TT_GOLD, buf);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// WoW-style tooltip renderer
// ═══════════════════════════════════════════════════════════════════════════

void FlushPendingTooltip() {
  if (!g_pendingTooltip.active)
    return;
  g_pendingTooltip.active = false;
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  float tw = g_pendingTooltip.w;

  // Measure actual height and width from lines (scaled for fullscreen)
  float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;
  float lineH = 18.0f * uiScale;
  float sepH = 10.0f * uiScale;
  float padX = 10.0f * uiScale;
  float th = 12.0f * uiScale;

  // Push font early so CalcTextSize uses the correct font metrics
  if (s_ctx->fontDefault)
    ImGui::PushFont(s_ctx->fontDefault);

  float maxTextW = 0.0f;
  for (auto &line : g_pendingTooltip.lines) {
    if (line.flags & 2) {
      th += sepH;
    } else {
      th += lineH;
      // Measure text width to auto-size tooltip
      const char *measureText = line.text.c_str();
      // For split lines, measure both halves with gap
      if (line.flags & 8) {
        size_t sep = line.text.find('|');
        if (sep != std::string::npos) {
          std::string left = line.text.substr(0, sep);
          std::string right = line.text.substr(sep + 1);
          float splitW = ImGui::CalcTextSize(left.c_str()).x +
                         ImGui::CalcTextSize(right.c_str()).x + padX;
          if (splitW > maxTextW) maxTextW = splitW;
          continue;
        }
      }
      float textW = ImGui::CalcTextSize(measureText).x;
      if (textW > maxTextW) maxTextW = textW;
    }
  }
  th += 8.0f * uiScale;

  // Expand tooltip width to fit longest line (with padding on both sides)
  float minW = maxTextW + padX * 2.0f;
  if (minW > tw) tw = minW;

  // Position near cursor, clamp to screen
  ImVec2 mp = ImGui::GetIO().MousePos;
  ImVec2 tPos(mp.x + 16, mp.y + 16);
  ImVec2 dispSize = ImGui::GetIO().DisplaySize;
  if (tPos.x + tw > dispSize.x - 10)
    tPos.x = dispSize.x - tw - 10;
  if (tPos.y + th > dispSize.y - 10)
    tPos.y = dispSize.y - th - 10;
  if (tPos.x < 5) tPos.x = 5;
  if (tPos.y < 5) tPos.y = 5;

  ImVec2 br(tPos.x + tw, tPos.y + th);

  // Background: warm dark gradient
  dl->AddRectFilledMultiColor(
      tPos, br,
      IM_COL32(15, 12, 8, 245),
      IM_COL32(15, 12, 8, 245),
      IM_COL32(8, 6, 4, 250),
      IM_COL32(8, 6, 4, 250));

  // Thin dark border
  dl->AddRect(tPos, br, IM_COL32(40, 35, 25, 200), 0.0f, 0, 1.0f);

  // Quality-colored top edge accent
  ImU32 qCol = g_pendingTooltip.borderColor;
  uint8_t qr = (qCol >> 0) & 0xFF;
  uint8_t qg = (qCol >> 8) & 0xFF;
  uint8_t qb = (qCol >> 16) & 0xFF;
  ImU32 qDim = IM_COL32(qr * 3 / 4, qg * 3 / 4, qb * 3 / 4, 160);
  dl->AddLine(ImVec2(tPos.x + 1, tPos.y), ImVec2(br.x - 1, tPos.y), qDim,
              2.0f);

  // Render lines (font already pushed above for measurement)
  float curY = tPos.y + 8.0f * uiScale;

  for (auto &line : g_pendingTooltip.lines) {
    if (line.flags & 2) {
      // Separator
      float sy = curY + 4.0f * uiScale;
      dl->AddLine(ImVec2(tPos.x + 6, sy), ImVec2(br.x - 6, sy),
                  IM_COL32(60, 55, 40, 120), 1.0f);
      curY += sepH;
    } else if (line.flags & 8) {
      // Split line: "Left|Right"
      size_t sep = line.text.find('|');
      if (sep != std::string::npos) {
        std::string left = line.text.substr(0, sep);
        std::string right = line.text.substr(sep + 1);
        DrawShadowText(dl, ImVec2(tPos.x + padX, curY), line.color,
                       left.c_str());
        if (!right.empty()) {
          ImVec2 rSize = ImGui::CalcTextSize(right.c_str());
          DrawShadowText(dl, ImVec2(br.x - padX - rSize.x, curY), line.color,
                         right.c_str());
        }
      }
      curY += lineH;
    } else if (line.flags & 1) {
      // Centered
      ImVec2 textSize = ImGui::CalcTextSize(line.text.c_str());
      float cx = tPos.x + (tw - textSize.x) * 0.5f;
      DrawShadowText(dl, ImVec2(cx, curY), line.color, line.text.c_str());
      curY += lineH;
    } else {
      // Normal left-aligned with shadow
      DrawShadowText(dl, ImVec2(tPos.x + padX, curY), line.color,
                     line.text.c_str());
      curY += lineH;
    }
  }

  if (s_ctx->fontDefault)
    ImGui::PopFont();
}

} // namespace InventoryUI

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

// ─── Tooltip color palette (matched to HTML design) ───────────────────────
static const ImU32 TT_WHITE      = IM_COL32(229, 226, 225, 255); // #e5e2e1 on-surface
static const ImU32 TT_GRAY       = IM_COL32(140, 135, 130, 255); // on-surface-variant (muted)
static const ImU32 TT_GREEN      = IM_COL32(74, 222, 128, 255);  // standard green (effects, met reqs)
static const ImU32 TT_RED        = IM_COL32(255, 100, 100, 255); // standard red (unmet reqs, wrong class)
static const ImU32 TT_BRIGHT_GREEN = IM_COL32(50, 255, 100, 255); // bright green (compare: better)
static const ImU32 TT_BRIGHT_RED   = IM_COL32(255, 60, 60, 255);  // bright red (compare: worse)
static const ImU32 TT_GOLD       = IM_COL32(233, 195, 73, 255);  // #e9c349 secondary (gold)
static const ImU32 TT_BLUE       = IM_COL32(96, 165, 250, 255);  // #60a5fa option bonuses
static const ImU32 TT_PURPLE     = IM_COL32(216, 180, 254, 255); // #d8b4fe purple quality
static const ImU32 TT_ORANGE     = IM_COL32(251, 146, 60, 255);  // #fb923c orange quality
static const ImU32 TT_LIGHT_BLUE = IM_COL32(147, 197, 253, 255); // #93c5fd skill info
static const ImU32 TT_DIM_GREEN  = IM_COL32(74, 222, 128, 255);  // standard green for met reqs

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

  // Check if requirements are not met — adds warning line at bottom
  {
    bool meetsReqs = true;
    if (def->levelReq > 0 && *s_ctx->serverLevel < (int)def->levelReq) meetsReqs = false;
    if (def->reqStr > 0 && *s_ctx->serverStr < (int)def->reqStr) meetsReqs = false;
    if (def->reqDex > 0 && *s_ctx->serverDex < (int)def->reqDex) meetsReqs = false;
    if (def->reqVit > 0 && *s_ctx->serverVit < (int)def->reqVit) meetsReqs = false;
    if (def->reqEne > 0 && *s_ctx->serverEne < (int)def->reqEne) meetsReqs = false;
    if (!meetsReqs) {
      th += sepH;
      th += lineH;
    }
  }

  th += 8; // bottom pad

  // ─── Build lines ────────────────────────────────────────────────────────

  float tooltipW = 350.0f; // Fixed width for all tooltips
  BeginPendingTooltip(tooltipW, th);

  ImU32 qualityColor = GetQualityColor(itemLevel, optionFlags);
  g_pendingTooltip.borderColor = qualityColor;

  // Name (centered, quality color)
  char nameBuf[64];
  if (itemLevel > 0)
    snprintf(nameBuf, sizeof(nameBuf), "%s +%d", def->name.c_str(), itemLevel);
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", def->name.c_str());
  AddPendingTooltipLine(qualityColor, nameBuf, 1 | 32); // centered + headline (Newsreader)

  // Quality label + Type (split left|right) — D4-style subheader
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
      AddPendingTooltipLine(TT_GRAY, slotBuf, 8); // split left|right, dimmer
  }

  if (accessorySlot)
    AddPendingTooltipLine(TT_GRAY, accessorySlot);

  AddTooltipSeparator();

  // ─── Stats ──────────────────────────────────────────────────────────────

  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0)) {
    int dMin = def->dmgMin + levelDmgBonus;
    int dMax = def->dmgMax + levelDmgBonus;
    char buf[64];
    // Split: label left (gray), value right (white bold)
    if (levelDmgBonus > 0)
      snprintf(buf, sizeof(buf), "Attack Damage|%d ~ %d  (+%d)", dMin, dMax, levelDmgBonus);
    else
      snprintf(buf, sizeof(buf), "Attack Damage|%d ~ %d", dMin, dMax);
    AddPendingTooltipLine(TT_WHITE, buf, 8); // split L|R

    if (equippedCID) {
      bool newIsMagic = (def->category == 5 && def->magicPower > 0);
      bool eqIsMagic = (equippedCID->category == 5 && equippedCID->magicPower > 0);
      if (newIsMagic == eqIsMagic) {
        int eqDmgBonus = kEnhanceTable[std::min(equippedLevel, 15)];
        int avgNew = (dMin + dMax) / 2;
        int avgOld = ((int)equippedCID->dmgMin + eqDmgBonus + (int)equippedCID->dmgMax + eqDmgBonus) / 2;
        int diff = avgNew - avgOld;
        char cmpBuf[48];
        if (diff > 0) {
          snprintf(cmpBuf, sizeof(cmpBuf), "|+%d vs equipped", diff);
          AddPendingTooltipLine(TT_BRIGHT_GREEN, cmpBuf, 8);
        } else if (diff < 0) {
          snprintf(cmpBuf, sizeof(cmpBuf), "|%d vs equipped", diff);
          AddPendingTooltipLine(TT_BRIGHT_RED, cmpBuf, 8);
        }
      }
    }
  }

  if (def->category <= 5 && def->attackSpeed > 0) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Attack Speed|%d", def->attackSpeed);
    AddPendingTooltipLine(TT_GRAY, buf, 8); // split L|R, gray
  }

  // DPS: bold gold, split format
  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0) && def->attackSpeed > 0) {
    int dpsDmgMin = def->dmgMin + levelDmgBonus;
    int dpsDmgMax = def->dmgMax + levelDmgBonus;
    float avgDmg = (float)(dpsDmgMin + dpsDmgMax) / 2.0f;
    float aps = (1.0f + def->attackSpeed * 0.015f) / 0.6f;
    float dps = avgDmg * aps;
    char buf[48];
    snprintf(buf, sizeof(buf), "DPS|%.0f", dps);
    AddPendingTooltipLine(TT_GOLD, buf, 8 | 16); // split + bold
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
        AddPendingTooltipLine(TT_BRIGHT_GREEN, cmpBuf);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  (%d%% vs equipped)", diff);
        AddPendingTooltipLine(TT_BRIGHT_RED, cmpBuf);
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
      snprintf(buf, sizeof(buf), "Defense|%d  (+%d)", totalDef, levelDefBonus);
    else
      snprintf(buf, sizeof(buf), "Defense|%d", totalDef);
    AddPendingTooltipLine(TT_WHITE, buf, 8); // split L|R

    if (equippedCID) {
      int eqDefBonus = (equippedCID->category >= 7 && equippedCID->category <= 11)
          ? kEnhanceTable[std::min(equippedLevel, 15)]
          : std::min(equippedLevel, 15);
      int diff = totalDef - ((int)equippedCID->defense + eqDefBonus);
      char cmpBuf[48];
      if (diff > 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "|+%d vs equipped", diff);
        AddPendingTooltipLine(TT_BRIGHT_GREEN, cmpBuf, 8);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "|%d vs equipped", diff);
        AddPendingTooltipLine(TT_BRIGHT_RED, cmpBuf, 8);
      }
    }
  }

  // ─── Item Options ──────────────────────────────────────────────────────

  if (hasOptions) {
    if (hasStats) AddTooltipSeparator();
    if (optionFlags & 0x80) {
      if (def->category == 4)
        AddPendingTooltipLine(TT_BLUE, "Skill (Multi-Shot)");
      else
        AddPendingTooltipLine(TT_BLUE, "Skill");
    }
    if (optionFlags & 0x40) {
      AddPendingTooltipLine(TT_BLUE, "Luck (+5% Critical Rate)");
    }
    int addLevel = optionFlags & 0x07;
    if (addLevel > 0) {
      char buf[48];
      if (def->category <= 5)
        snprintf(buf, sizeof(buf), "Increased Damage +%d", addLevel * 4);
      else
        snprintf(buf, sizeof(buf), "Increased Defense +%d", addLevel * 4);
      AddPendingTooltipLine(TT_BLUE, buf);
    }
  }

  // ─── Use/Equip effects ──────────────────────────────────────────────────

  if (potionHeal > 0 || potionMana > 0 || potionEffect) {
    if (hasStats || hasOptions) AddTooltipSeparator();
    if (potionHeal > 0) {
      char buf[48];
      snprintf(buf, sizeof(buf), "+ Restores %d Health", potionHeal);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
    if (potionMana > 0) {
      char buf[48];
      snprintf(buf, sizeof(buf), "+ Restores %d Mana", potionMana);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
    if (potionEffect) {
      char buf[64];
      snprintf(buf, sizeof(buf), "+ %s", potionEffect);
      AddPendingTooltipLine(TT_GREEN, buf);
    }
  }

  if (accessoryEffect) {
    if (hasStats && !(potionHeal > 0 || potionMana > 0 || potionEffect))
      AddTooltipSeparator();
    char buf[64];
    snprintf(buf, sizeof(buf), "+ %s", accessoryEffect);
    AddPendingTooltipLine(TT_GREEN, buf);
  }
  if (accessoryEffect2) {
    char buf[64];
    snprintf(buf, sizeof(buf), "+ %s", accessoryEffect2);
    AddPendingTooltipLine(TT_GREEN, buf);
  }
  if (accessoryEffect3) {
    char buf[64];
    snprintf(buf, sizeof(buf), "+ %s", accessoryEffect3);
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

    // Split format: "Level Required|80" — label gray left, value green/red right
    auto addReq = [&](const char *label, int current, int req) {
      char rBuf[48];
      snprintf(rBuf, sizeof(rBuf), "%s Required|%d", label, req);
      AddPendingTooltipLine((current >= req) ? TT_WHITE : TT_BRIGHT_RED, rBuf, 8);
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
      // Build class list string
      std::string classList;
      for (int i = 0; i < 4; i++) {
        if (def->classFlags & (1 << i)) {
          if (!classList.empty()) classList += ", ";
          classList += classNames[i];
        }
      }
      if (!classList.empty()) {
        std::string classBuf = "Class: " + classList;
        AddPendingTooltipLine(canUse ? TT_WHITE : TT_BRIGHT_RED, classBuf, 16); // bold
      }
    }
  }

  // ─── Price ──────────────────────────────────────────────────────────────

  if (shopBuyPrice > 0) {
    AddTooltipSeparator();
    std::string bStr = std::to_string(shopBuyPrice);
    int bn = (int)bStr.length() - 3;
    while (bn > 0) { bStr.insert(bn, ","); bn -= 3; }
    char buf[64];
    snprintf(buf, sizeof(buf), "Zen Value|%s", bStr.c_str());
    AddPendingTooltipLine(TT_GOLD, buf, 8 | 4 | 16); // split + footer bg + bold
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
    snprintf(buf, sizeof(buf), "Zen Value|%s", sStr.c_str());
    AddPendingTooltipLine(TT_GOLD, buf, 8 | 4 | 16); // split + footer bg + bold
  }

  // Red warning at the very bottom if character doesn't meet requirements
  {
    bool meetsReqs = true;
    if (def->levelReq > 0 && *s_ctx->serverLevel < (int)def->levelReq) meetsReqs = false;
    if (def->reqStr > 0 && *s_ctx->serverStr < (int)def->reqStr) meetsReqs = false;
    if (def->reqDex > 0 && *s_ctx->serverDex < (int)def->reqDex) meetsReqs = false;
    if (def->reqVit > 0 && *s_ctx->serverVit < (int)def->reqVit) meetsReqs = false;
    if (def->reqEne > 0 && *s_ctx->serverEne < (int)def->reqEne) meetsReqs = false;
    if (!meetsReqs) {
      AddTooltipSeparator();
      AddPendingTooltipLine(TT_BRIGHT_RED, "Requirements not met", 0);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// WoW-style tooltip renderer
// ═══════════════════════════════════════════════════════════════════════════

// Word-wrap text to fit within maxW pixels. Returns number of lines.
static int CalcWrappedLines(const char *text, float maxW) {
  if (!text || !text[0]) return 1;
  float w = ImGui::CalcTextSize(text).x;
  if (w <= maxW) return 1;
  // Rough estimate: ceil(textWidth / maxWidth)
  int lines = (int)(w / maxW) + 1;
  return lines > 1 ? lines : 1;
}

// Draw word-wrapped text, returns Y advance
static float DrawWrappedText(ImDrawList *dl, ImVec2 pos, ImU32 color,
                             const char *text, float maxW, float lineH) {
  float textW = ImGui::CalcTextSize(text).x;
  if (textW <= maxW || maxW < 20.0f) {
    DrawShadowText(dl, pos, color, text);
    return lineH;
  }
  // Simple word wrap
  std::string str(text);
  float curY = 0.0f;
  size_t start = 0;
  while (start < str.size()) {
    // Find how many chars fit on this line
    size_t bestBreak = str.size();
    for (size_t i = start + 1; i <= str.size(); i++) {
      std::string sub = str.substr(start, i - start);
      if (ImGui::CalcTextSize(sub.c_str()).x > maxW) {
        // Back up to last space
        size_t spacePos = str.rfind(' ', i - 1);
        if (spacePos != std::string::npos && spacePos > start)
          bestBreak = spacePos;
        else
          bestBreak = i - 1; // force break mid-word
        break;
      }
    }
    std::string line = str.substr(start, bestBreak - start);
    DrawShadowText(dl, ImVec2(pos.x, pos.y + curY), color, line.c_str());
    curY += lineH;
    start = bestBreak;
    // Skip space at break point
    if (start < str.size() && str[start] == ' ') start++;
  }
  return curY;
}

void FlushPendingTooltip() {
  if (!g_pendingTooltip.active)
    return;
  g_pendingTooltip.active = false;
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  float tw = g_pendingTooltip.w;

  // Measure actual height and width from lines (scaled for fullscreen)
  float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;
  float lineH = 16.0f * uiScale;   // tighter line spacing
  float sepH = 10.0f * uiScale;    // compact separator
  float padX = 12.0f * uiScale;    // side padding
  float th = 12.0f * uiScale;      // top padding

  // Push tooltip body font (Work Sans 13px) — HTML font-label
  ImFont *ttBody = s_ctx->fontLabel ? s_ctx->fontLabel : s_ctx->fontDefault;
  ImFont *ttHeadline = s_ctx->fontBold ? s_ctx->fontBold : ttBody; // Cinzel (same as damage numbers)
  ImFont *ttBold = s_ctx->fontLabelBold ? s_ctx->fontLabelBold : ttBody; // Work Sans SemiBold
  if (ttBody)
    ImGui::PushFont(ttBody);

  float fixedW = 350.0f; // Consistent width for all tooltips
  float maxTextW = 0.0f;
  for (auto &line : g_pendingTooltip.lines) {
    // Font selection: 32=headline (Newsreader), 16=bold (Cinzel), default=label (Work Sans)
    bool useHeadline = (line.flags & 32) && ttHeadline != ttBody;
    bool useBold = !useHeadline && (line.flags & 16) && ttBold != ttBody;
    if (useHeadline) { ImGui::PopFont(); ImGui::PushFont(ttHeadline); }
    else if (useBold) { ImGui::PopFont(); ImGui::PushFont(ttBold); }

    if (line.flags & 2) {
      th += sepH;
    } else {
      float lh = (useHeadline || useBold) ? ImGui::GetFontSize() + 4.0f * uiScale : lineH;
      th += lh;
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
          if (useHeadline || useBold) { ImGui::PopFont(); ImGui::PushFont(ttBody); }
          continue;
        }
      }
      float textW = ImGui::CalcTextSize(measureText).x;
      if (textW > maxTextW) maxTextW = textW;
      // Account for word-wrap: add extra lines if text exceeds fixed width
      float wrapMaxW = fixedW - padX * 2.0f;
      int wrapLines = CalcWrappedLines(measureText, wrapMaxW);
      if (wrapLines > 1) th += lh * (wrapLines - 1);
    }

    if (useHeadline || useBold) { ImGui::PopFont(); ImGui::PushFont(ttBody); }
  }
  th += 8.0f * uiScale;

  // Fixed tooltip width
  tw = fixedW;

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

  // Background: semi-transparent dark
  dl->AddRectFilled(tPos, br, IM_COL32(10, 10, 10, 210));

  // Subtle white border
  dl->AddRect(tPos, br, IM_COL32(255, 255, 255, 25), 0.0f, 0, 1.0f);

  // Drop shadow (2-layer for depth, like shadow-2xl)
  dl->AddRect(ImVec2(tPos.x + 2, tPos.y + 2), ImVec2(br.x + 2, br.y + 2),
              IM_COL32(0, 0, 0, 80), 0.0f, 0, 1.0f);

  // Quality-colored top border (3px) + inset glow
  ImU32 qCol = g_pendingTooltip.borderColor;
  uint8_t qr = (qCol >> 0) & 0xFF;
  uint8_t qg = (qCol >> 8) & 0xFF;
  uint8_t qb = (qCol >> 16) & 0xFF;
  // Solid 3px top border
  dl->AddRectFilled(ImVec2(tPos.x, tPos.y), ImVec2(br.x, tPos.y + 3.0f),
                    IM_COL32(qr, qg, qb, 220));
  // Inset glow: box-shadow inset 0 20px 20px -20px rgba(color, 0.3)
  dl->AddRectFilledMultiColor(
      ImVec2(tPos.x + 1, tPos.y + 3),
      ImVec2(br.x - 1, tPos.y + 24.0f * uiScale),
      IM_COL32(qr, qg, qb, 76), IM_COL32(qr, qg, qb, 76),  // 0.3 * 255 ≈ 76
      IM_COL32(qr, qg, qb, 0), IM_COL32(qr, qg, qb, 0));

  float padTop = 10.0f * uiScale;
  float curY = tPos.y + padTop;

  for (auto &line : g_pendingTooltip.lines) {
    // Font selection: 32=headline (Newsreader), 16=bold (Cinzel), default=label (Work Sans)
    bool useHeadline = (line.flags & 32) && ttHeadline != ttBody;
    bool useBold = !useHeadline && (line.flags & 16) && ttBold != ttBody;
    if (useHeadline) { ImGui::PopFont(); ImGui::PushFont(ttHeadline); }
    else if (useBold) { ImGui::PopFont(); ImGui::PushFont(ttBold); }
    float lh = (useHeadline || useBold) ? ImGui::GetFontSize() + 4.0f * uiScale : lineH;

    // Footer background (flag 4): surface-container-low #1c1b1b (HTML)
    if (line.flags & 4) {
      float footerPad = 8.0f * uiScale; // p-4 padding
      // Fill from current position to bottom of tooltip
      dl->AddRectFilled(
          ImVec2(tPos.x + 1, curY - footerPad),
          ImVec2(br.x - 1, br.y - 1),
          IM_COL32(20, 20, 20, 200));
      // Subtle top border: border-t border-outline-variant/10
      dl->AddLine(ImVec2(tPos.x + 1, curY - footerPad),
                  ImVec2(br.x - 1, curY - footerPad),
                  IM_COL32(90, 64, 60, 25), 1.0f);
    }

    if (line.flags & 2) {
      // Separator: h-[1px] bg-outline-variant/20 mx-4 (HTML)
      float sy = curY + 5.0f * uiScale;
      dl->AddLine(ImVec2(tPos.x + padX, sy), ImVec2(br.x - padX, sy),
                  IM_COL32(90, 64, 60, 50), 1.0f); // outline-variant/20
      curY += sepH;
    } else if (line.flags & 8) {
      // Split line: "Left|Right"
      // Left label: Work Sans Regular, gray (on-surface-variant)
      // Right value: Work Sans SemiBold, line color (HTML font-bold on values)
      size_t sep = line.text.find('|');
      if (sep != std::string::npos) {
        std::string left = line.text.substr(0, sep);
        std::string right = line.text.substr(sep + 1);
        if (!left.empty())
          DrawShadowText(dl, ImVec2(tPos.x + padX, curY), TT_GRAY,
                         left.c_str());
        if (!right.empty()) {
          // Switch to SemiBold for value (HTML: values are font-bold)
          bool switchedForValue = (ttBold != ttBody) && !useHeadline && !useBold;
          if (switchedForValue) { ImGui::PopFont(); ImGui::PushFont(ttBold); }
          ImVec2 rSize = ImGui::CalcTextSize(right.c_str());
          DrawShadowText(dl, ImVec2(br.x - padX - rSize.x, curY), line.color,
                         right.c_str());
          if (switchedForValue) { ImGui::PopFont(); ImGui::PushFont(ttBody); }
        }
      }
      curY += lh;
    } else if (line.flags & 1) {
      // Centered
      ImVec2 textSize = ImGui::CalcTextSize(line.text.c_str());
      float cx = tPos.x + (tw - textSize.x) * 0.5f;
      DrawShadowText(dl, ImVec2(cx, curY), line.color, line.text.c_str());
      curY += lh;
    } else {
      // Normal left-aligned with word wrap
      float wrapMaxW = tw - padX * 2.0f;
      float adv = DrawWrappedText(dl, ImVec2(tPos.x + padX, curY), line.color,
                                  line.text.c_str(), wrapMaxW, lh);
      curY += adv;
    }

    // Restore to tooltip body font (Work Sans)
    if (useHeadline || useBold) {
      ImGui::PopFont();
      ImGui::PushFont(ttBody);
    }
  }

  if (ttBody)
    ImGui::PopFont();
}

} // namespace InventoryUI

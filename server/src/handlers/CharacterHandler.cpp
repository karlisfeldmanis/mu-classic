#include "handlers/CharacterHandler.hpp"
#include "PacketDefs.hpp"
#include "StatCalculator.hpp"
#include "handlers/InventoryHandler.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace CharacterHandler {

// Forward declarations for level bonus helpers (defined below SendEquipment)
static int GetDefenseLevelBonus(int category, int itemLevel);

void SendCharStats(Session &session, Database &db, int characterId) {
  CharacterData c = db.GetCharacterById(characterId);
  if (c.id == 0) {
    printf("[Character] Character %d not found for stats send\n", characterId);
    return;
  }

  // Pre-calculate equipment defense using same formula as RefreshCombatStats
  auto equip = db.GetCharacterEquipment(characterId);
  session.totalDefense = 0;
  for (auto &slot : equip) {
    auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0) {
      if (slot.category >= 6 && slot.category <= 11) {
        session.totalDefense +=
            itemDef.defense + GetDefenseLevelBonus(slot.category, slot.itemLevel);
        session.totalDefense += (slot.optionFlags & 0x07) * 4;
      } else {
        session.totalDefense += itemDef.defense;
      }
    }
  }

  // Sync session state from DB data
  session.characterId = c.id;
  session.characterName = c.name;
  session.charClass = c.charClass;
  session.classCode = c.charClass;
  session.levelUpPoints = c.levelUpPoints;
  session.level = c.level;
  session.strength = c.strength;
  session.dexterity = c.dexterity;
  session.vitality = c.vitality;
  session.energy = c.energy;

  CharacterClass charCls = static_cast<CharacterClass>(c.charClass);
  session.classCode = c.charClass;

  session.maxHp = StatCalculator::CalculateMaxHP(charCls, c.level, c.vitality) +
                  session.petBonusMaxHp;
  session.hp = std::min(static_cast<int>(c.life), session.maxHp);
  if (session.hp <= 0)
    session.hp = session.maxHp;
  session.maxMana = StatCalculator::CalculateMaxMP(charCls, c.level, c.energy);
  session.mana = std::min(static_cast<int>(c.mana), session.maxMana);
  if (session.mana <= 0)
    session.mana = session.maxMana;

  session.maxAg = StatCalculator::CalculateMaxAG(c.strength, c.dexterity,
                                                 c.vitality, c.energy);
  session.ag = std::min(static_cast<int>(c.ag), session.maxAg);

  session.experience = c.experience;
  memcpy(session.skillBar, c.skillBar, 10);
  memcpy(session.potionBar, c.potionBar, 8);
  session.rmcSkillId = c.rmcSkillId;

  PMSG_CHARSTATS_SEND pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CHARSTATS);
  pkt.characterId = static_cast<uint16_t>(c.id);
  strncpy(pkt.name, c.name.c_str(), sizeof(pkt.name));
  pkt.name[sizeof(pkt.name) - 1] = '\0';
  pkt.level = c.level;
  printf(
      "[Character] Sending stats for '%s': Level=%d STR=%d HP=%d/%d AG=%d/%d\n",
      c.name.c_str(), (int)pkt.level, (int)pkt.strength, (int)pkt.life,
      (int)pkt.maxLife, (int)pkt.ag, (int)pkt.maxAg);
  pkt.strength = c.strength;
  pkt.dexterity = c.dexterity;
  pkt.vitality = c.vitality;
  pkt.energy = c.energy;
  pkt.life = static_cast<uint16_t>(session.hp);
  pkt.maxLife = static_cast<uint16_t>(session.maxHp);
  pkt.mana = static_cast<uint16_t>(session.mana);
  pkt.maxMana = static_cast<uint16_t>(session.maxMana);
  pkt.ag = static_cast<uint16_t>(session.ag);
  pkt.maxAg = static_cast<uint16_t>(session.maxAg);
  pkt.levelUpPoints = c.levelUpPoints;
  pkt.experienceLo = static_cast<uint32_t>(c.experience & 0xFFFFFFFF);
  pkt.experienceHi = static_cast<uint32_t>(c.experience >> 32);
  pkt.charClass = c.charClass;
  memcpy(pkt.skillBar, c.skillBar, 10);
  memcpy(pkt.potionBar, c.potionBar, 8);
  pkt.rmcSkillId = c.rmcSkillId; // New: Populate rmcSkillId

  session.hasBow = false;
  for (auto &slot : equip) {
    auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0 && slot.category == 4) {
      session.hasBow = true;
      break;
    }
  }

  bool hasBowLocal = session.hasBow;
  pkt.attackSpeed = static_cast<uint16_t>(StatCalculator::CalculateAttackSpeed(
      charCls, session.dexterity, hasBowLocal));
  pkt.magicSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateMagicSpeed(charCls, session.dexterity));
  pkt.defense = static_cast<uint16_t>(
      StatCalculator::CalculateDefense(charCls, session.dexterity) +
      session.totalDefense);

  session.Send(&pkt, sizeof(pkt));
  printf("[Character] Sent char stats to fd=%d: Lv%d STR=%d DEX=%d VIT=%d "
         "ENE=%d HP=%d/%d XP=%llu pts=%d RMC=%d\n",
         session.GetFd(), c.level, c.strength, c.dexterity, c.vitality,
         c.energy, session.hp, session.maxHp, (unsigned long long)c.experience,
         c.levelUpPoints, pkt.defense, pkt.attackSpeed, c.rmcSkillId);
}

void SendCharStats(Session &session) {
  PMSG_CHARSTATS_SEND pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CHARSTATS);
  pkt.characterId = static_cast<uint16_t>(session.characterId);
  strncpy(pkt.name, session.characterName.c_str(), sizeof(pkt.name));
  pkt.name[sizeof(pkt.name) - 1] = '\0';
  pkt.level = session.level;
  pkt.strength = session.strength;
  pkt.dexterity = session.dexterity;
  pkt.vitality = session.vitality;
  pkt.energy = session.energy;
  pkt.life = static_cast<uint16_t>(session.hp);
  pkt.maxLife = static_cast<uint16_t>(session.maxHp);
  pkt.mana = static_cast<uint16_t>(session.mana);
  pkt.maxMana = static_cast<uint16_t>(session.maxMana);
  pkt.ag = static_cast<uint16_t>(session.ag);
  pkt.maxAg = static_cast<uint16_t>(session.maxAg);
  pkt.levelUpPoints = session.levelUpPoints;
  CharacterClass charCls = static_cast<CharacterClass>(session.charClass);
  pkt.attackSpeed = static_cast<uint16_t>(StatCalculator::CalculateAttackSpeed(
      charCls, session.dexterity, session.hasBow));
  pkt.magicSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateMagicSpeed(charCls, session.dexterity));
  pkt.defense = static_cast<uint16_t>(
      StatCalculator::CalculateDefense(charCls, session.dexterity) +
      session.totalDefense);
  pkt.experienceLo = static_cast<uint32_t>(session.experience & 0xFFFFFFFF);
  pkt.experienceHi = static_cast<uint32_t>(session.experience >> 32);
  pkt.charClass = session.charClass;
  memcpy(pkt.skillBar, session.skillBar, 10);
  memcpy(pkt.potionBar, session.potionBar, 8);
  pkt.rmcSkillId = session.rmcSkillId; // New: Populate rmcSkillId

  session.Send(&pkt, sizeof(pkt));
}

void SendEquipment(Session &session, Database &db, int characterId) {
  auto equip = db.GetCharacterEquipment(characterId);

  // Match PMSG_EQUIPMENT_SLOT: slot(1) + cat(1) + idx(1) + lvl(1) + qty(1) + model(32) = 37
  size_t entrySize = sizeof(PMSG_EQUIPMENT_SLOT);
  size_t totalSize = 5 + equip.size() * entrySize;
  std::vector<uint8_t> packet(totalSize, 0);

  packet[0] = 0xC2;
  packet[1] = static_cast<uint8_t>(totalSize >> 8);
  packet[2] = static_cast<uint8_t>(totalSize & 0xFF);
  packet[3] = Opcode::EQUIPMENT;
  packet[4] = static_cast<uint8_t>(equip.size());

  for (size_t i = 0; i < equip.size(); i++) {
    size_t off = 5 + i * entrySize;
    packet[off + 0] = equip[i].slot;
    packet[off + 1] = equip[i].category;
    packet[off + 2] = equip[i].itemIndex;
    packet[off + 3] = equip[i].itemLevel;
    packet[off + 4] = equip[i].quantity;
    packet[off + 5] = equip[i].optionFlags;

    auto itemDef = db.GetItemDefinition(equip[i].category, equip[i].itemIndex);
    strncpy(reinterpret_cast<char *>(&packet[off + 6]),
            itemDef.modelFile.c_str(), 31);
  }

  // Also update session equipment cache
  for (auto &e : equip) {
    if (e.slot < Session::NUM_EQUIP_SLOTS) {
      session.equipment[e.slot].quantity = e.quantity;
      session.equipment[e.slot].optionFlags = e.optionFlags;
    }
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent %zu equipment slots to fd=%d. Size=%zu\n",
         equip.size(), session.GetFd(), totalSize);
}

// OpenMU weapon damage bonus per item level (+0 to +15)
static const int kWeaponDmgBonus[] = {0,  3,  6,  9,  12, 15, 18, 21,
                                      24, 27, 31, 36, 42, 49, 57, 66};
// OpenMU armor defense bonus per item level
static const int kArmorDefBonus[] = {0,  3,  6,  9,  12, 15, 18, 21,
                                     24, 27, 31, 36, 42, 49, 57, 66};
// Shields: +1 per level
static const int kShieldDefBonus[] = {0, 1, 2,  3,  4,  5,  6,  7,
                                      8, 9, 10, 11, 12, 13, 14, 15};

static int GetWeaponLevelBonus(int itemLevel) {
  if (itemLevel < 0)
    return 0;
  if (itemLevel < 16)
    return kWeaponDmgBonus[itemLevel];
  return 66 + (itemLevel - 15) * 8;
}
static int GetDefenseLevelBonus(int category, int itemLevel) {
  if (itemLevel < 0)
    return 0;
  if (category == 6) { // Shield
    return itemLevel < 16 ? kShieldDefBonus[itemLevel] : itemLevel;
  }
  return itemLevel < 16 ? kArmorDefBonus[itemLevel] : 66 + (itemLevel - 15) * 8;
}

void RefreshCombatStats(Session &session, Database &db, int characterId) {
  auto equip = db.GetCharacterEquipment(characterId);
  session.weaponDamageMin = 0;
  session.weaponDamageMax = 0;
  session.totalDefense = 0;
  session.hasBow = false;
  session.hasTwoHandedWeapon = false;

  // Track per-hand weapon damage for dual-wield calculation
  int rightDmgMin = 0, rightDmgMax = 0;
  int leftDmgMin = 0, leftDmgMax = 0;
  bool hasRightWeapon = false, hasLeftWeapon = false;

  for (auto &slot : equip) {
    auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0) {
      if (itemDef.twoHanded) {
        session.hasTwoHandedWeapon = true;
      }
      if (slot.category == 4) { // 4=Bow (OpenMU category 4)
        session.hasBow = true;
      }

      int wpnBonus = GetWeaponLevelBonus(slot.itemLevel);
      if (slot.slot == 0 && slot.category <= 5) { // R.Hand weapon
        rightDmgMin = itemDef.damageMin + wpnBonus;
        rightDmgMax = itemDef.damageMax + wpnBonus;
        hasRightWeapon = true;
      }
      if (slot.slot == 1 && slot.category <= 5) { // L.Hand weapon (dual-wield)
        leftDmgMin = itemDef.damageMin + wpnBonus;
        leftDmgMax = itemDef.damageMax + wpnBonus;
        hasLeftWeapon = true;
      }
      // Only armor (6=shield, 7-11=helm/armor/pants/gloves/boots) get defense
      // level bonus. Weapons (0-5) contribute base defense only (usually 0).
      if (slot.category >= 6 && slot.category <= 11) {
        session.totalDefense +=
            itemDef.defense + GetDefenseLevelBonus(slot.category, slot.itemLevel);
        session.totalDefense += (slot.optionFlags & 0x07) * 4;
      } else {
        session.totalDefense += itemDef.defense;
      }

      if (slot.slot == 7) { // Wings
        rightDmgMin += 15;
        rightDmgMax += 25;
      }
    }
  }

  // Main 5.2 dual-wield: DK/MG with weapons in both hands get 55% per hand
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  if (hasRightWeapon && hasLeftWeapon &&
      (charCls == CharacterClass::CLASS_DK ||
       charCls == CharacterClass::CLASS_MG)) {
    session.weaponDamageMin =
        (rightDmgMin * 55) / 100 + (leftDmgMin * 55) / 100;
    session.weaponDamageMax =
        (rightDmgMax * 55) / 100 + (leftDmgMax * 55) / 100;
    printf("[Character] Dual-wield: R(%d-%d)*55%% + L(%d-%d)*55%% = %d-%d\n",
           rightDmgMin, rightDmgMax, leftDmgMin, leftDmgMax,
           session.weaponDamageMin, session.weaponDamageMax);
  } else {
    session.weaponDamageMin = rightDmgMin + leftDmgMin;
    session.weaponDamageMax = rightDmgMax + leftDmgMax;
  }

  // Staff Rise calculation (OpenMU Version075)
  session.staffRisePercent = 0;
  for (auto &slot : equip) {
    if (slot.slot == 0 && slot.category == 5) { // Right hand = staff
      auto staffDef = db.GetItemDefinition(slot.category, slot.itemIndex);
      if (staffDef.magicPower > 0) {
        int base = staffDef.magicPower / 2;
        static const int evenTable[16] = {0,  3,  7,  10, 14, 17, 21, 24,
                                          28, 31, 35, 40, 45, 50, 56, 63};
        static const int oddTable[16] = {0,  4,  7,  11, 14, 18, 21, 25,
                                         28, 32, 36, 40, 45, 51, 57, 63};
        int lvl = std::min((int)slot.itemLevel, 15);
        int levelBonus =
            (staffDef.magicPower % 2 == 0) ? evenTable[lvl] : oddTable[lvl];
        session.staffRisePercent = base + levelBonus;
        printf("[Character] Staff Rise: %s +%d -> %d%%\n",
               staffDef.name.c_str(), slot.itemLevel,
               session.staffRisePercent);
      }
      break;
    }
  }

  // Pet bonuses (slot 8, category 13)
  // Also check if mount replaced pet in slot 8 — scan inventory for owned pets.
  // This allows Imp/Guardian Angel buffs to persist while mounted.
  session.petBonusMaxHp = 0;
  session.petDamageReduction = 0.0f;
  session.petAttackMultiplier = 1.0f;
  bool hasPetInSlot8 = false;
  for (auto &slot : equip) {
    if (slot.slot == 8 && slot.category == 13) {
      if (slot.itemIndex == 0) { // Guardian Angel: +50 HP, 20% dmg reduction
        session.petBonusMaxHp = 50;
        session.petDamageReduction = 0.2f;
        hasPetInSlot8 = true;
      } else if (slot.itemIndex == 1) { // Imp: 30% attack increase
        session.petAttackMultiplier = 1.3f;
        hasPetInSlot8 = true;
      } else if (slot.itemIndex == 3) { // Dinorant: 15% atk, 10% dmg reduction
        session.petAttackMultiplier = 1.15f;
        session.petDamageReduction = 0.1f;
      }
    }
  }
  // If slot 8 has a mount (not a pet), check inventory for owned pet items
  if (!hasPetInSlot8) {
    auto inv = db.GetCharacterInventory(characterId);
    for (auto &item : inv) {
      int cat = item.defIndex / 32;
      int idx = item.defIndex % 32;
      if (cat == 13 && idx == 0) { // Guardian Angel in bag
        session.petBonusMaxHp = 50;
        session.petDamageReduction = 0.2f;
        break;
      } else if (cat == 13 && idx == 1) { // Imp in bag
        session.petAttackMultiplier = 1.3f;
        break;
      }
    }
  }
}

void HandleCharSave(Session &session, const std::vector<uint8_t> &packet,
                    Database &db) {
  if (packet.size() < sizeof(PMSG_CHARSAVE_RECV))
    return;
  const auto *save =
      reinterpret_cast<const PMSG_CHARSAVE_RECV *>(packet.data());

  int charId = save->characterId;
  if (charId <= 0)
    charId = session.characterId;

  memcpy(session.skillBar, save->skillBar, 10);
  memcpy(session.potionBar, save->potionBar, 8);
  session.rmcSkillId = save->rmcSkillId; // New: Save rmcSkillId to session

  uint8_t posX = static_cast<uint8_t>(session.worldZ / 100.0f);
  uint8_t posY = static_cast<uint8_t>(session.worldX / 100.0f);
  db.SaveCharacterFull(
      charId, session.level, session.strength, session.dexterity,
      session.vitality, session.energy, save->life, save->maxLife,
      static_cast<uint16_t>(std::max(session.mana, 0)),
      static_cast<uint16_t>(session.maxMana),
      static_cast<uint16_t>(std::max(session.ag, 0)),
      static_cast<uint16_t>(session.maxAg), session.levelUpPoints,
      save->experienceLo | (static_cast<uint64_t>(save->experienceHi) << 32),
      session.zen, posX, posY, session.mapId, session.skillBar,
      session.potionBar, save->rmcSkillId, -1, &session);

  if (session.dead && save->life > 0) {
    // Respawn: enforce full HP and AG/mana server-side
    session.dead = false;
    session.hp = session.maxHp;
    session.mana = session.maxMana;
    printf("[Character] Player fd=%d respawned (HP=%d/%d)\n", session.GetFd(),
           session.hp, session.maxHp);
    // Notify client of restored HP/mana so HUD updates from 0
    SendCharStats(session);
  } else {
    session.hp = save->life;
  }
  if (save->maxLife >= save->life) {
    session.maxHp = save->maxLife;
  }

  printf("[Character] Character %d saved from fd=%d (lvl=%d pts=%d)\n", charId,
         session.GetFd(), save->level, save->levelUpPoints);
}

void HandleEquip(Session &session, const std::vector<uint8_t> &packet,
                 Database &db) {
  if (packet.size() < sizeof(PMSG_EQUIP_RECV))
    return;
  const auto *eq = reinterpret_cast<const PMSG_EQUIP_RECV *>(packet.data());

  int charId = eq->characterId;
  if (charId <= 0)
    charId = 1;

  // Authoritative requirement check (if not un-equipping)
  if (eq->category != 0xFF) {
    auto itemDef = db.GetItemDefinition(eq->category, eq->itemIndex);
    if (itemDef.id > 0) {
      if (session.level < itemDef.level ||
          session.strength < itemDef.reqStrength ||
          session.dexterity < itemDef.reqDexterity) {
        printf(
            "[Character] Rejecting equip char=%d cat=%d idx=%d: reqs not met "
            "(Lv:%d/%d Str:%d/%d Dex:%d/%d)\n",
            charId, eq->category, eq->itemIndex, (int)session.level,
            itemDef.level, (int)session.strength, itemDef.reqStrength,
            (int)session.dexterity, itemDef.reqDexterity);
        SendEquipment(session, db, charId);
        InventoryHandler::SendInventorySync(session);
        return;
      }
      int bitIndex = session.charClass >> 4;
      if (!(itemDef.classFlags & (1 << bitIndex))) {
        printf("[Character] Rejecting equip char=%d cat=%d idx=%d: class "
               "mismatch (Class:%d Bit:%d Flags:0x%X)\n",
               charId, eq->category, eq->itemIndex, (int)session.charClass,
               bitIndex, itemDef.classFlags);
        SendEquipment(session, db, charId);
        InventoryHandler::SendInventorySync(session);
        return;
      }

      // Slot 8: reject mounts (idx 2-3), only allow pets (idx 0-1)
      if (eq->slot == 8 && eq->category == 13 &&
          (eq->itemIndex == 2 || eq->itemIndex == 3)) {
        printf("[Character] Rejecting equip char=%d: mounts must use quickslots\n",
               charId);
        SendEquipment(session, db, charId);
        InventoryHandler::SendInventorySync(session);
        return;
      }

      // Slot 1 (left hand) constraint logic
      if (eq->slot == 1) {
        // Ammo (arrows/bolts) is allowed in slot 1 alongside matching bow/crossbow
        bool isAmmoItem = (eq->category == 4 &&
                           (eq->itemIndex == 7 || eq->itemIndex == 15));
        // Validate ammo type matches weapon: bolts(7) for crossbows, arrows(15) for bows
        if (isAmmoItem) {
          auto equip0 = db.GetCharacterEquipment(charId);
          for (auto &e0 : equip0) {
            if (e0.slot == 0 && e0.category == 4) {
              bool weapIsCrossbow =
                  (e0.itemIndex >= 8 && e0.itemIndex <= 14) ||
                  e0.itemIndex == 16 || e0.itemIndex == 18;
              bool ammoIsBolt = (eq->itemIndex == 7);
              if (weapIsCrossbow != ammoIsBolt) {
                printf("[Character] Rejecting equip char=%d: %s requires %s\n",
                       charId, weapIsCrossbow ? "crossbow" : "bow",
                       weapIsCrossbow ? "bolts" : "arrows");
                SendEquipment(session, db, charId);
                InventoryHandler::SendInventorySync(session);
                return;
              }
            }
          }
        }
        auto equip = db.GetCharacterEquipment(charId);
        for (auto &e : equip) {
          if (e.slot == 0 && e.category != 0xFF) {
            auto s0Def = db.GetItemDefinition(e.category, e.itemIndex);
            if (s0Def.twoHanded && !isAmmoItem) {
              printf("[Character] Rejecting equip char=%d slot=1: cannot equip "
                     "with 2-handed weapon in slot 0\n",
                     charId);
              SendEquipment(session, db, charId);
              InventoryHandler::SendInventorySync(session);
              return;
            }
          }
        }

        if (eq->category <= 5) { // Weapon
          // Allow ammo (arrows idx 15, bolts idx 7) in off-hand for any class
          bool isAmmo = (eq->category == 4 &&
                         (eq->itemIndex == 7 || eq->itemIndex == 15));
          if (!isAmmo) {
            if (itemDef.twoHanded) {
              printf("[Character] Rejecting equip char=%d cat=%d: cannot dual "
                     "wield two-handed weapon\n",
                     charId, eq->category);
              SendEquipment(session, db, charId);
              InventoryHandler::SendInventorySync(session);
              return;
            }
            CharacterClass charCls =
                static_cast<CharacterClass>(session.classCode);
            if (charCls != CharacterClass::CLASS_DK &&
                charCls != CharacterClass::CLASS_MG) {
              printf(
                  "[Character] Rejecting equip char=%d cat=%d: only DK/MG can "
                  "dual wield\n",
                  charId, eq->category);
              SendEquipment(session, db, charId);
              InventoryHandler::SendInventorySync(session);
              return;
            }
          }
        } else if (eq->category != 6) { // Not a shield either
          printf("[Character] Rejecting equip char=%d cat=%d: invalid item for "
                 "left hand\n",
                 charId, eq->category);
          SendEquipment(session, db, charId);
          InventoryHandler::SendInventorySync(session);
          return;
        }
      }

      // Two-handed weapon logic (Slot 0 only): auto-unequip left hand
      if (eq->slot == 0 && itemDef.twoHanded) {
        auto equip = db.GetCharacterEquipment(charId);
        for (auto &slot : equip) {
          if (slot.slot == 1 && slot.category != 0xFF) {
            int16_t lhDef =
                (int16_t)((int)slot.category * 32 + (int)slot.itemIndex);
            auto lhItemDef = db.GetItemDefinition(lhDef);
            int lhW = lhItemDef.width > 0 ? lhItemDef.width : 1;
            int lhH = lhItemDef.height > 0 ? lhItemDef.height : 1;
            bool hasSpace = false;
            for (int r = 0; r <= 8 - lhH && !hasSpace; r++) {
              for (int c2 = 0; c2 <= 8 - lhW && !hasSpace; c2++) {
                bool fits = true;
                for (int hh = 0; hh < lhH && fits; hh++)
                  for (int ww = 0; ww < lhW && fits; ww++)
                    if (session.bag[(r + hh) * 8 + (c2 + ww)].occupied)
                      fits = false;
                if (fits)
                  hasSpace = true;
              }
            }
            if (!hasSpace) {
              printf("[Character] Rejecting equip char=%d cat=%d: no space in "
                     "bag for left-hand item\n",
                     charId, eq->category);
              SendEquipment(session, db, charId);
              InventoryHandler::SendInventorySync(session);
              return;
            }
          }
        }
      }
    }
  }

  // Save old equipment in this slot for swap/unequip
  auto oldEquip = db.GetCharacterEquipment(charId);
  uint8_t oldCat = 0xFF, oldIdx = 0, oldLvl = 0, oldQty = 0, oldOpt = 0;
  for (auto &oe : oldEquip) {
    if (oe.slot == eq->slot && oe.category != 0xFF) {
      oldCat = oe.category;
      oldIdx = oe.itemIndex;
      oldLvl = oe.itemLevel;
      oldQty = oe.quantity;
      oldOpt = oe.optionFlags;
      break;
    }
  }

  // PRE-CHECK BAG SPACE for unequip/swap case
  if (oldCat != 0xFF) {
    int16_t oldDef = (int16_t)((int)oldCat * 32 + (int)oldIdx);
    auto itemDef = db.GetItemDefinition(oldDef);
    int w = itemDef.width > 0 ? itemDef.width : 1;
    int h = itemDef.height > 0 ? itemDef.height : 1;
    bool hasSpace = false;
    int bagSlotOfNewItem = -1;
    if (eq->category != 0xFF) {
      int16_t newDef = (int16_t)((int)eq->category * 32 + (int)eq->itemIndex);
      for (int i = 0; i < 64; i++) {
        if (session.bag[i].occupied && session.bag[i].primary &&
            session.bag[i].defIndex == newDef) {
          bagSlotOfNewItem = i;
          break;
        }
      }
    }
    for (int r = 0; r <= 8 - h && !hasSpace; r++) {
      for (int c2 = 0; c2 <= 8 - w && !hasSpace; c2++) {
        bool fits = true;
        for (int hh = 0; hh < h && fits; hh++) {
          for (int ww = 0; ww < w && fits; ww++) {
            int s = (r + hh) * 8 + (c2 + ww);
            if (s >= 64) {
              fits = false;
              continue;
            }
            if (session.bag[s].occupied) {
              if (bagSlotOfNewItem >= 0) {
                int16_t nDef =
                    (int16_t)((int)eq->category * 32 + (int)eq->itemIndex);
                auto nDefI = db.GetItemDefinition(nDef);
                int niW = nDefI.width > 0 ? nDefI.width : 1;
                int niH = nDefI.height > 0 ? nDefI.height : 1;
                int niR = bagSlotOfNewItem / 8, niC = bagSlotOfNewItem % 8;
                int cR = s / 8, cC = s % 8;
                if (cR >= niR && cR < niR + niH && cC >= niC &&
                    cC < niC + niW) {
                } else
                  fits = false;
              } else
                fits = false;
            }
          }
        }
        if (fits)
          hasSpace = true;
      }
    }
    if (!hasSpace) {
      printf("[Character] Rejecting unequip/swap char=%d slot=%d: no space in "
             "bag\n",
             charId, eq->slot);
      SendEquipment(session, db, charId);
      InventoryHandler::SendInventorySync(session);
      return;
    }
  }

  // NOW we can safely modify state because we know it will fit.
  // 1. Remove left-hand item if equipping 2H to slot 0 (pre-verified space
  // above)
  ItemDefinition tempDef;
  const ItemDefinition *idPtr = nullptr;
  if (eq->category != 0xFF) {
    tempDef = db.GetItemDefinition(eq->category, eq->itemIndex);
    idPtr = &tempDef;
  }

  if (eq->slot == 0 && idPtr && idPtr->twoHanded) {
    for (auto &slot : oldEquip) {
      if (slot.slot == 1 && slot.category != 0xFF) {
        // Move left-hand item to bag (same logic as before but now we know it
        // fits)
        int16_t lhDef =
            (int16_t)((int)slot.category * 32 + (int)slot.itemIndex);
        auto lDefI = db.GetItemDefinition(lhDef);
        int lhW = lDefI.width > 0 ? lDefI.width : 1;
        int lhH = lDefI.height > 0 ? lDefI.height : 1;
        bool lhPlaced = false;
        for (int r = 0; r <= 8 - lhH && !lhPlaced; r++) {
          for (int c2 = 0; c2 <= 8 - lhW && !lhPlaced; c2++) {
            bool fits = true;
            for (int hh = 0; hh < lhH && fits; hh++)
              for (int ww = 0; ww < lhW && fits; ww++)
                if (session.bag[(r + hh) * 8 + (c2 + ww)].occupied)
                  fits = false;
            if (fits) {
              for (int hh = 0; hh < lhH; hh++) {
                for (int ww = 0; ww < lhW; ww++) {
                  int s = (r + hh) * 8 + (c2 + ww);
                  session.bag[s].occupied = true;
                  session.bag[s].primary = (hh == 0 && ww == 0);
                  session.bag[s].defIndex = lhDef;
                  session.bag[s].category = slot.category;
                  session.bag[s].itemIndex = slot.itemIndex;
                  if (hh == 0 && ww == 0) {
                    session.bag[s].quantity = 1;
                    session.bag[s].itemLevel = slot.itemLevel;
                    session.bag[s].optionFlags = slot.optionFlags;
                  }
                }
              }
              db.SaveCharacterInventory(charId, lhDef, 1, slot.itemLevel,
                                        (uint8_t)(r * 8 + c2), slot.optionFlags);
              lhPlaced = true;
            }
          }
        }
        if (!lhPlaced) {
          // CRITICAL: left-hand item would be lost! Reject entire 2H equip.
          // At this point bag hasn't been modified yet, so just return.
          printf("[Character] REJECTED 2H equip: no bag space for left-hand "
                 "item def=%d\n", lhDef);
          SendEquipment(session, db, charId);
          InventoryHandler::SendInventorySync(session);
          return;
        }
        db.UpdateEquipment(charId, 1, 0xFF, 0, 0);
        session.equipment[1].category = 0xFF;
        printf("[Character] Auto-unequipped left hand (def=%d) for 2H weapon\n",
               lhDef);
        break;
      }
    }
  }

  // 1b. Auto-unequip incompatible ammo when equipping bow/crossbow to slot 0
  if (eq->slot == 0 && eq->category == 4) {
    bool newIsCrossbow =
        (eq->itemIndex >= 8 && eq->itemIndex <= 14) ||
        eq->itemIndex == 16 || eq->itemIndex == 18;
    // Re-read equipment (may have been modified by 2H unequip above)
    auto equip1 = db.GetCharacterEquipment(charId);
    for (auto &e1 : equip1) {
      if (e1.slot == 1 && e1.category == 4 &&
          (e1.itemIndex == 7 || e1.itemIndex == 15)) {
        bool ammoIsBolt = (e1.itemIndex == 7);
        if (newIsCrossbow != ammoIsBolt) {
          // Wrong ammo type — auto-unequip it back to bag
          int16_t ammoDef = (int16_t)(4 * 32 + e1.itemIndex);
          uint8_t ammoSlot;
          if (InventoryHandler::FindEmptySpace(session, 1, 1, ammoSlot)) {
            session.bag[ammoSlot].occupied = true;
            session.bag[ammoSlot].primary = true;
            session.bag[ammoSlot].defIndex = ammoDef;
            session.bag[ammoSlot].category = e1.category;
            session.bag[ammoSlot].itemIndex = e1.itemIndex;
            session.bag[ammoSlot].quantity = e1.quantity;
            session.bag[ammoSlot].itemLevel = e1.itemLevel;
            session.bag[ammoSlot].optionFlags = e1.optionFlags;
            db.SaveCharacterInventory(charId, ammoDef, e1.quantity,
                                      e1.itemLevel, ammoSlot, e1.optionFlags);
          }
          db.UpdateEquipment(charId, 1, 0xFF, 0, 0);
          session.equipment[1].category = 0xFF;
          // Notify player
          const char *msg = newIsCrossbow
              ? "Bolts required for crossbow! Arrows unequipped."
              : "Arrows required for bow! Bolts unequipped.";
          db.SaveChatMessage(session.characterId, 1, 0xFF64FFFF, msg);
          printf("[Character] Auto-unequipped incompatible ammo for %s\n",
                 newIsCrossbow ? "crossbow" : "bow");
          break;
        }
      }
    }
  }

  // When equipping (category != 0xFF), remove item from inventory bag
  // Track which slot was freed so we can prefer it for the old weapon
  int freedSlot = -1;
  uint8_t newItemQty = 0; // Carry quantity from bag (for arrows/bolts)
  uint8_t newItemOpt = 0; // Carry optionFlags from bag
  if (eq->category != 0xFF) {
    int16_t dIdx = (int16_t)((int)eq->category * 32 + (int)eq->itemIndex);
    // First pass: match by defIndex AND itemLevel for exact match
    for (int i = 0; i < 64; i++) {
      if (session.bag[i].occupied && session.bag[i].primary &&
          session.bag[i].defIndex == dIdx &&
          session.bag[i].itemLevel == eq->itemLevel) {
        freedSlot = i;
        break;
      }
    }
    // Fallback: match by defIndex only (for level-0 items or mismatches)
    if (freedSlot < 0) {
      for (int i = 0; i < 64; i++) {
        if (session.bag[i].occupied && session.bag[i].primary &&
            session.bag[i].defIndex == dIdx) {
          freedSlot = i;
          break;
        }
      }
    }
    if (freedSlot < 0) {
      printf("[Character] REJECTED equip: item def=%d +%d not found in bag!\n",
             dIdx, eq->itemLevel);
      SendEquipment(session, db, charId);
      InventoryHandler::SendInventorySync(session);
      return;
    }
    newItemQty = session.bag[freedSlot].quantity;
    newItemOpt = session.bag[freedSlot].optionFlags;
    // Clear bag slot (including multi-cell)
    auto dI = db.GetItemDefinition(dIdx);
    int w = dI.width > 0 ? dI.width : 1;
    int h = dI.height > 0 ? dI.height : 1;
    int r = freedSlot / 8, c2 = freedSlot % 8;
    for (int hh = 0; hh < h; hh++) {
      for (int ww = 0; ww < w; ww++) {
        int s = (r + hh) * 8 + (c2 + ww);
        if (s < 64)
          session.bag[s] = {};
      }
    }
    db.DeleteCharacterInventoryItem(charId, freedSlot);
    printf("[Character] Removed equipped item from bag slot %d (def=%d +%d qty=%d)\n",
           freedSlot, dIdx, eq->itemLevel, newItemQty);
  }

  // Move old equipped item back to inventory (both unequip AND swap cases)
  // Always place old item back — even if same type/level (player may have
  // bought a second copy from shop)
  if (oldCat != 0xFF) {
    int16_t oDef = (int16_t)((int)oldCat * 32 + (int)oldIdx);
    auto oDefI = db.GetItemDefinition(oDef);
    int w = oDefI.width > 0 ? oDefI.width : 1;
    int h = oDefI.height > 0 ? oDefI.height : 1;

    // Restore quantity for arrows/bolts being unequipped back to bag
    uint8_t restoreQty = (oldQty > 0) ? oldQty : 1;

    // Helper lambda: try to place old weapon at a specific slot
    auto tryPlace = [&](int startSlot) -> bool {
      int r = startSlot / 8, c = startSlot % 8;
      if (c + w > 8 || r + h > 8)
        return false;
      for (int hh = 0; hh < h; hh++)
        for (int ww = 0; ww < w; ww++)
          if (session.bag[(r + hh) * 8 + (c + ww)].occupied)
            return false;
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (r + hh) * 8 + (c + ww);
          session.bag[s].occupied = true;
          session.bag[s].primary = (hh == 0 && ww == 0);
          session.bag[s].defIndex = oDef;
          session.bag[s].category = oldCat;
          session.bag[s].itemIndex = oldIdx;
          if (hh == 0 && ww == 0) {
            session.bag[s].quantity = restoreQty;
            session.bag[s].itemLevel = oldLvl;
            session.bag[s].optionFlags = oldOpt;
          }
        }
      }
      db.SaveCharacterInventory(charId, oDef, restoreQty, oldLvl, (uint8_t)startSlot, oldOpt);
      printf("[Character] Unequipped item saved to bag slot %d (def=%d qty=%d)\n",
             startSlot, oDef, restoreQty);
      return true;
    };

    bool placed = false;

    // First: try the slot that was just freed (most natural swap position)
    if (freedSlot >= 0)
      placed = tryPlace(freedSlot);

    // Second: scan entire bag for any empty space
    if (!placed) {
      for (int r = 0; r <= 8 - h && !placed; r++) {
        for (int c2 = 0; c2 <= 8 - w && !placed; c2++) {
          placed = tryPlace(r * 8 + c2);
        }
      }
    }

    if (!placed) {
      // CRITICAL: item would be lost! Reject entire operation — restore old equipment
      printf("[Character] REJECTED equip: no bag space for unequipped item "
             "def=%d, restoring equipment\n", oDef);
      // Restore old equipment in DB
      db.UpdateEquipment(charId, eq->slot, oldCat, oldIdx, oldLvl, oldQty, oldOpt);
      // Restore bag: put the new item back (if it was taken from bag)
      if (eq->category != 0xFF && freedSlot >= 0) {
        int16_t nDef = (int16_t)((int)eq->category * 32 + (int)eq->itemIndex);
        auto nDefI = db.GetItemDefinition(nDef);
        int nw = nDefI.width > 0 ? nDefI.width : 1;
        int nh = nDefI.height > 0 ? nDefI.height : 1;
        int fr = freedSlot / 8, fc = freedSlot % 8;
        for (int hh = 0; hh < nh; hh++) {
          for (int ww = 0; ww < nw; ww++) {
            int s = (fr + hh) * 8 + (fc + ww);
            if (s < 64) {
              session.bag[s].occupied = true;
              session.bag[s].primary = (hh == 0 && ww == 0);
              session.bag[s].defIndex = nDef;
              if (hh == 0 && ww == 0) {
                session.bag[s].quantity = newItemQty;
                session.bag[s].itemLevel = eq->itemLevel;
                session.bag[s].optionFlags = newItemOpt;
              }
            }
          }
        }
        db.SaveCharacterInventory(charId, nDef, newItemQty, eq->itemLevel,
                                  (uint8_t)freedSlot, newItemOpt);
      }
      SendEquipment(session, db, charId);
      InventoryHandler::SendInventorySync(session);
      return;
    }
  }

  db.UpdateEquipment(charId, eq->slot, eq->category, eq->itemIndex,
                     eq->itemLevel, newItemQty, newItemOpt);

  // Update session equipment cache
  if (eq->slot < Session::NUM_EQUIP_SLOTS) {
    session.equipment[eq->slot].category = eq->category;
    session.equipment[eq->slot].itemIndex = eq->itemIndex;
    session.equipment[eq->slot].itemLevel = eq->itemLevel;
    session.equipment[eq->slot].quantity = newItemQty;
    session.equipment[eq->slot].optionFlags = newItemOpt;
  }

  RefreshCombatStats(session, db, charId);

  printf("[Character] Equipment change fd=%d: char=%d slot=%d cat=%d idx=%d "
         "+%d (weapon=%d-%d def=%d)\n",
         session.GetFd(), charId, eq->slot, eq->category, eq->itemIndex,
         eq->itemLevel, session.weaponDamageMin, session.weaponDamageMax,
         session.totalDefense);

  SendEquipment(session, db, charId);
  InventoryHandler::SendInventorySync(session);
  SendCharStats(session, db, charId);
}

void HandleStatAlloc(Session &session, const std::vector<uint8_t> &packet,
                     Database &db) {
  if (packet.size() < sizeof(PMSG_STAT_ALLOC_RECV))
    return;
  auto *req = reinterpret_cast<const PMSG_STAT_ALLOC_RECV *>(packet.data());

  PMSG_STAT_ALLOC_SEND resp{};
  resp.h = MakeC1Header(sizeof(resp), Opcode::STAT_ALLOC_RESULT);
  resp.statType = req->statType;

  if (session.levelUpPoints == 0 || req->statType > 3) {
    resp.result = 0;
    session.Send(&resp, sizeof(resp));
    return;
  }

  session.levelUpPoints--;
  switch (req->statType) {
  case 0:
    session.strength++;
    resp.newValue = session.strength;
    break;
  case 1:
    session.dexterity++;
    resp.newValue = session.dexterity;
    break;
  case 2:
    session.vitality++;
    resp.newValue = session.vitality;
    break;
  case 3:
    session.energy++;
    resp.newValue = session.energy;
    break;
  }

  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  session.maxHp =
      StatCalculator::CalculateMaxHP(charCls, session.level, session.vitality) +
      session.petBonusMaxHp;
  session.maxMana = StatCalculator::CalculateMaxManaOrAG(
      charCls, session.level, session.strength, session.dexterity,
      session.vitality, session.energy);

  resp.result = 1;
  resp.levelUpPoints = session.levelUpPoints;
  resp.maxLife = static_cast<uint16_t>(session.maxHp);
  resp.ag = static_cast<uint16_t>(std::max(session.ag, 0));
  resp.maxAg = static_cast<uint16_t>(session.maxAg);
  session.Send(&resp, sizeof(resp));

  SendCharStats(session);

  {
    uint8_t posX = static_cast<uint8_t>(session.worldZ / 100.0f);
    uint8_t posY = static_cast<uint8_t>(session.worldX / 100.0f);
    db.SaveCharacterFull(
        session.characterId, session.level, session.strength, session.dexterity,
        session.vitality, session.energy, static_cast<uint16_t>(session.hp),
        static_cast<uint16_t>(session.maxHp),
        static_cast<uint16_t>(std::max(session.mana, 0)),
        static_cast<uint16_t>(session.maxMana),
        static_cast<uint16_t>(std::max(session.ag, 0)),
        static_cast<uint16_t>(session.maxAg), session.levelUpPoints,
        session.experience, session.zen, posX, posY, session.mapId,
        session.skillBar, session.potionBar, session.rmcSkillId, -1, &session);
  }

  printf("[Character] Stat alloc: type=%d newVal=%d pts=%d maxHP=%d\n",
         req->statType, resp.newValue, session.levelUpPoints, session.maxHp);
}

void SendSkillList(Session &session) {
  uint8_t count = static_cast<uint8_t>(session.learnedSkills.size());
  size_t totalSize = 4 + 1 + count; // C2 header(4) + count(1) + skillIds
  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::SKILL_LIST);
  packet[4] = count;

  for (int i = 0; i < count; i++) {
    packet[5 + i] = session.learnedSkills[i];
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent %d skills to fd=%d\n", count, session.GetFd());
}

void SendItemCatalog(Session &session, Database &db) {
  auto items = db.GetAllItemDefinitions();
  uint16_t count = static_cast<uint16_t>(items.size());
  size_t entrySize = sizeof(PMSG_ITEM_DEF_ENTRY);
  size_t totalSize = 4 + 2 + count * entrySize; // C2 header(4) + count(2) + entries

  std::vector<uint8_t> packet(totalSize, 0);
  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::ITEM_CATALOG);

  // Little-endian uint16_t count at offset [4-5]
  packet[4] = static_cast<uint8_t>(count & 0xFF);
  packet[5] = static_cast<uint8_t>((count >> 8) & 0xFF);

  for (uint16_t i = 0; i < count; i++) {
    auto *entry = reinterpret_cast<PMSG_ITEM_DEF_ENTRY *>(
        packet.data() + 6 + i * entrySize);
    const auto &item = items[i];
    entry->category = item.category;
    entry->itemIndex = item.itemIndex;
    std::memset(entry->name, 0, 32);
    std::strncpy(entry->name, item.name.c_str(), 31);
    std::memset(entry->modelFile, 0, 32);
    std::strncpy(entry->modelFile, item.modelFile.c_str(), 31);
    entry->levelReq = item.level;
    entry->dmgMin = item.damageMin;
    entry->dmgMax = item.damageMax;
    entry->defense = item.defense;
    entry->attackSpeed = item.attackSpeed;
    entry->twoHanded = item.twoHanded;
    entry->width = item.width;
    entry->height = item.height;
    entry->reqStr = item.reqStrength;
    entry->reqDex = item.reqDexterity;
    entry->reqVit = item.reqVitality;
    entry->reqEne = item.reqEnergy;
    entry->classFlags = item.classFlags;
    entry->buyPrice = item.buyPrice;
    entry->magicPower = item.magicPower;
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent item catalog (%d items, %zu bytes) to fd=%d\n",
         count, totalSize, session.GetFd());
}

void SendClassDefinitions(Session &session, Database &db) {
  auto classes = db.GetClassDefinitions();
  uint8_t count = static_cast<uint8_t>(classes.size());
  size_t entrySize = sizeof(PMSG_CLASS_DEF_ENTRY);
  size_t totalSize = 4 + 1 + count * entrySize; // C2 header(4) + count(1) + entries

  std::vector<uint8_t> packet(totalSize, 0);
  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::CLASS_DEFINITIONS);
  packet[4] = count;

  for (uint8_t i = 0; i < count; i++) {
    auto *entry = reinterpret_cast<PMSG_CLASS_DEF_ENTRY *>(
        packet.data() + 5 + i * entrySize);
    *entry = classes[i];
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent class definitions (%d classes) to fd=%d\n",
         count, session.GetFd());
}

} // namespace CharacterHandler

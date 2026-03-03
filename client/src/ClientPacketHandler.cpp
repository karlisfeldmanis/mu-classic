#include "ClientPacketHandler.hpp"
#include "CharacterSelect.hpp"
#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "PacketDefs.hpp"
#include "SoundManager.hpp"
#include <cstring>
#include <iostream>

namespace ClientPacketHandler {

static ClientGameState *g_state = nullptr;

void Init(ClientGameState *state) { g_state = state; }

// ── Equipment helpers ──

static void ApplyEquipToHero(uint8_t slot, const WeaponEquipInfo &weapon) {
  if (slot == 0) {
    g_state->hero->EquipWeapon(weapon);
  } else if (slot == 1) {
    g_state->hero->EquipShield(weapon);
  } else if (weapon.category == 0xFF) {
    // Unequipped: revert to default naked body part (slots 2-6 → parts 0-4)
    int bodyPart = (int)slot - 2;
    if (bodyPart >= 0 && bodyPart <= 4)
      g_state->hero->EquipBodyPart(bodyPart, ""); // empty = default DK Class02
  } else if (g_state->getBodyPartIndex) {
    int bodyPart = g_state->getBodyPartIndex(weapon.category);
    if (bodyPart >= 0 && g_state->getBodyPartModelFile) {
      std::string partModel =
          g_state->getBodyPartModelFile(weapon.category, weapon.itemIndex);
      if (!partModel.empty())
        g_state->hero->EquipBodyPart(bodyPart, partModel);
    }
  }
}

static void ApplyEquipToUI(uint8_t slot, const WeaponEquipInfo &weapon) {
  if (slot < 12) {
    g_state->equipSlots[slot].category = weapon.category;
    g_state->equipSlots[slot].itemIndex = weapon.itemIndex;
    g_state->equipSlots[slot].itemLevel = weapon.itemLevel;
    // Use client-side model file lookup to ensure consistency with
    // ItemModelManager cache (server may have different naming)
    int16_t defIdx =
        (int16_t)weapon.category * 32 + (int16_t)weapon.itemIndex;
    const char *clientModel = ItemDatabase::GetDropModelName(defIdx);
    g_state->equipSlots[slot].modelFile =
        (clientModel && clientModel[0]) ? clientModel : weapon.modelFile;
    g_state->equipSlots[slot].equipped = (weapon.category != 0xFF);
  }
}

// ── Stats sync helper ──

static bool s_initialStatsReceived = false;

static void SyncCharStats(const PMSG_CHARSTATS_SEND *stats) {
  if (g_state->characterName) {
    strncpy(g_state->characterName, stats->name, 31);
    g_state->characterName[31] = '\0';
  }
  *g_state->serverLevel = stats->level;
  *g_state->serverStr = stats->strength;
  *g_state->serverDex = stats->dexterity;
  *g_state->serverVit = stats->vitality;
  *g_state->serverEne = stats->energy;
  *g_state->serverHP = stats->life;
  *g_state->serverMaxHP = stats->maxLife;
  *g_state->serverMP = stats->mana;
  *g_state->serverMaxMP = stats->maxMana;
  if (g_state->serverAG)
    *g_state->serverAG = stats->ag;
  if (g_state->serverMaxAG)
    *g_state->serverMaxAG = stats->maxAg;
  *g_state->serverLevelUpPoints = stats->levelUpPoints;
  // Only apply server quickSlotDefIndex on initial load — client manages it
  // locally during gameplay (sent to server via CharSave)
  if (!s_initialStatsReceived) {
    if (g_state->potionBar)
      memcpy(g_state->potionBar, stats->potionBar, 8); // 4 × int16_t
    if (g_state->skillBar)
      memcpy(g_state->skillBar, stats->skillBar, 10);
    *g_state->rmcSkillId = stats->rmcSkillId;
    s_initialStatsReceived = true;
  }
  *g_state->serverXP =
      ((int64_t)stats->experienceHi << 32) | stats->experienceLo;
  *g_state->serverDefense = stats->defense;
  *g_state->serverAttackSpeed = stats->attackSpeed;
  *g_state->serverMagicSpeed = stats->magicSpeed;
  // Pass attack speed to hero for agility-based animation scaling
  if (g_state->hero)
    g_state->hero->SetAttackSpeed(stats->attackSpeed);
  if (g_state->heroCharacterId) {
    *g_state->heroCharacterId = stats->characterId;
  }
}

// ── Equipment packet parsing (shared between initial and ongoing) ──

static void ParseEquipmentPacket(const uint8_t *pkt, int pktSize,
                                 int countOffset, int dataOffset,
                                 ServerData *serverData) {
  uint8_t count = pkt[countOffset];
  int entrySize = 4 + 32; // slot(1)+cat(1)+idx(1)+lvl(1)+model(32)
  for (int i = 0; i < count; i++) {
    int off = dataOffset + i * entrySize;
    if (off + entrySize > pktSize)
      break;

    uint8_t slot = pkt[off + 0];
    WeaponEquipInfo weapon;
    weapon.category = pkt[off + 1];
    weapon.itemIndex = pkt[off + 2];
    weapon.itemLevel = pkt[off + 3];
    char modelBuf[33] = {};
    std::memcpy(modelBuf, &pkt[off + 4], 32);
    weapon.modelFile = modelBuf;

    // Resolve twoHanded flag from ItemDatabase
    int16_t defIdx = (int16_t)weapon.category * 32 + (int16_t)weapon.itemIndex;
    auto &defs = ItemDatabase::GetItemDefs();
    auto it = defs.find(defIdx);
    if (it != defs.end())
      weapon.twoHanded = it->second.twoHanded;

    if (serverData)
      serverData->equipment.push_back({slot, weapon});

    ApplyEquipToHero(slot, weapon);
    ApplyEquipToUI(slot, weapon);
  }
}

// ── Inventory sync parsing (shared between initial and ongoing) ──

static void ParseInventorySync(const uint8_t *pkt, int pktSize) {
  if (pktSize < 9)
    return;

  uint32_t zen;
  std::memcpy(&zen, pkt + 4, 4);
  *g_state->zen = zen;
  uint8_t count = pkt[8];

  // Clear client grid
  for (int i = 0; i < INVENTORY_SLOTS; i++)
    g_state->inventory[i] = {};

  const int itemSize = 5; // slot(1)+cat(1)+idx(1)+qty(1)+lvl(1)
  for (int i = 0; i < count; i++) {
    int off = 9 + i * itemSize;
    if (off + itemSize > pktSize)
      break;

    uint8_t slot = pkt[off];
    uint8_t cat = pkt[off + 1];
    uint8_t idx = pkt[off + 2];
    uint8_t qty = pkt[off + 3];
    uint8_t lvl = pkt[off + 4];

    if (slot < INVENTORY_SLOTS) {
      int16_t defIdx = ItemDatabase::GetDefIndexFromCategory(cat, idx);
      if (defIdx != -1) {
        InventoryUI::SetBagItem(slot, defIdx, qty, lvl);
      } else {
        g_state->inventory[slot].occupied = true;
        g_state->inventory[slot].primary = true;
        g_state->inventory[slot].quantity = qty;
        g_state->inventory[slot].itemLevel = lvl;
      }
    }
  }
  *g_state->syncDone = true;
}

// ═══════════════════════════════════════════════════════════════════
// Initial packet handler (connection burst: NPCs, monsters, equipment, stats)
// ═══════════════════════════════════════════════════════════════════

void HandleInitialPacket(const uint8_t *pkt, int pktSize, ServerData &result) {
  if (pktSize < 3)
    return;
  uint8_t type = pkt[0];

  // C2 packets (4-byte header)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // NPC viewport
    if (headcode == Opcode::NPC_VIEWPORT) {
      uint8_t count = pkt[4];
      int entryStart = 5;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * 9;
        if (off + 9 > pktSize)
          break;
        ServerNpcSpawn npc;
        // Extract server index from high bit-masked bytes
        npc.serverIndex =
            (uint16_t)(((pkt[off + 0] & 0x7F) << 8) | pkt[off + 1]);
        npc.type = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        npc.gridX = pkt[off + 4];
        npc.gridY = pkt[off + 5];
        npc.dir = pkt[off + 8] >> 4;
        result.npcs.push_back(npc);
      }
      std::cout << "[Net] NPC viewport: " << (int)count << " NPCs" << std::endl;
    }

    // Monster viewport V2
    if (headcode == Opcode::MON_VIEWPORT_V2) {
      uint8_t count = pkt[4];
      int entrySize = 12;
      for (int i = 0; i < count; i++) {
        int off = 5 + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        ServerMonsterSpawn mon;
        mon.serverIndex = (uint16_t)((pkt[off + 0] << 8) | pkt[off + 1]);
        mon.monsterType = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        mon.gridX = pkt[off + 4];
        mon.gridY = pkt[off + 5];
        mon.dir = pkt[off + 6];
        // hp/maxHp are packed uint16_t (little-endian on macOS)
        mon.hp = (uint16_t)(pkt[off + 7] | (pkt[off + 8] << 8));
        mon.maxHp = (uint16_t)(pkt[off + 9] | (pkt[off + 10] << 8));
        mon.state = pkt[off + 11];
        result.monsters.push_back(mon);
      }
      std::cout << "[Net] Monster viewport V2: " << (int)count << " monsters"
                << std::endl;
    }

    // Inventory sync
    if (headcode == Opcode::INV_SYNC && pktSize >= 9) {
      // Initial sync uses slightly different format (category*32+index
      // encoding)
      uint32_t zen;
      std::memcpy(&zen, pkt + 4, 4);
      *g_state->zen = zen;
      uint8_t count = pkt[8];
      for (int i = 0; i < INVENTORY_SLOTS; i++)
        g_state->inventory[i] = {};
      for (int i = 0; i < count; i++) {
        int off = 9 + i * sizeof(PMSG_INVENTORY_ITEM);
        if (off + (int)sizeof(PMSG_INVENTORY_ITEM) > pktSize)
          break;
        const auto *item =
            reinterpret_cast<const PMSG_INVENTORY_ITEM *>(pkt + off);
        uint8_t slot = item->slot;
        if (slot < INVENTORY_SLOTS) {
          int16_t defIdx =
              (int16_t)((int)item->category * 32 + (int)item->itemIndex);
          g_state->inventory[slot].defIndex = defIdx;
          g_state->inventory[slot].quantity = item->quantity;
          g_state->inventory[slot].itemLevel = item->itemLevel;
          g_state->inventory[slot].occupied = true;
          g_state->inventory[slot].primary = true;

          auto it = g_state->itemDefs->find(defIdx);
          if (it != g_state->itemDefs->end()) {
            int w = it->second.width;
            int h = it->second.height;
            int row = slot / 8;
            int col = slot % 8;
            for (int hh = 0; hh < h; hh++) {
              for (int ww = 0; ww < w; ww++) {
                if (hh == 0 && ww == 0)
                  continue;
                int s = (row + hh) * 8 + (col + ww);
                if (s < INVENTORY_SLOTS && (col + ww) < 8) {
                  g_state->inventory[s].occupied = true;
                  g_state->inventory[s].primary = false;
                  g_state->inventory[s].defIndex = defIdx;
                }
              }
            }
          }
        }
      }
      std::cout << "[Net] Inventory sync: " << (int)count
                << " items, zen=" << *g_state->zen << std::endl;
    }

    // Equipment (C2)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 5) {
      ParseEquipmentPacket(pkt, pktSize, 4, 5, &result);
      std::cout << "[Net] Equipment (C2): " << (int)pkt[4] << " slots"
                << std::endl;
    }

    // Skill List (C2) — initial sync
    if (headcode == Opcode::SKILL_LIST && pktSize >= 5) {
      uint8_t count = pkt[4];
      if (g_state->learnedSkills) {
        g_state->learnedSkills->clear();
        for (int i = 0; i < count; i++) {
          if (5 + i >= pktSize)
            break;
          g_state->learnedSkills->push_back(pkt[5 + i]);
        }
        std::cout << "[Net] Initial skill list: " << (int)count << " skills\n";
      }
    }
  }

  // C1 packets (2-byte header)
  if (type == 0xC1 && pktSize >= 3) {
    uint8_t headcode = pkt[2];

    // CharInfo (F3:03) — extract spawn position
    if (headcode == Opcode::CHARSELECT && pktSize >= 4 &&
        pkt[3] == Opcode::SUB_CHARSELECT &&
        pktSize >= (int)sizeof(PMSG_CHARINFO_SEND)) {
      auto *info = reinterpret_cast<const PMSG_CHARINFO_SEND *>(pkt);
      result.spawnGridX = info->x;
      result.spawnGridY = info->y;
      result.hasSpawnPos = true;
      std::cout << "[Net] CharInfo spawn position: grid (" << (int)info->x
                << "," << (int)info->y << ")" << std::endl;
    }

    // Monster viewport V1
    if (headcode == 0x1F && pktSize >= 4) {
      uint8_t count = pkt[3];
      int entrySize = 5;
      for (int i = 0; i < count; i++) {
        int off = 4 + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        ServerMonsterSpawn mon;
        mon.monsterType = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
        mon.gridX = pkt[off + 2];
        mon.gridY = pkt[off + 3];
        mon.dir = pkt[off + 4];
        result.monsters.push_back(mon);
      }
    }

    // Character stats
    if (headcode == Opcode::CHARSTATS &&
        pktSize >= (int)sizeof(PMSG_CHARSTATS_SEND)) {
      auto *stats = reinterpret_cast<const PMSG_CHARSTATS_SEND *>(pkt);
      SyncCharStats(stats);

      g_state->hero->LoadStats(
          *g_state->serverLevel, *g_state->serverStr, *g_state->serverDex,
          *g_state->serverVit, *g_state->serverEne,
          (uint64_t)*g_state->serverXP, *g_state->serverLevelUpPoints,
          *g_state->serverHP, *g_state->serverMaxHP, *g_state->serverMP,
          *g_state->serverMaxMP, stats->ag, stats->maxAg, stats->charClass);

      std::cout << "[Net] Character stats: Lv." << *g_state->serverLevel
                << " HP=" << *g_state->serverHP << "/" << *g_state->serverMaxHP
                << " STR=" << *g_state->serverStr
                << " XP=" << *g_state->serverXP
                << " Pts=" << *g_state->serverLevelUpPoints << std::endl;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Ongoing game packet handler (monster AI, combat, drops, stats sync)
// ═══════════════════════════════════════════════════════════════════

void HandleGamePacket(const uint8_t *pkt, int pktSize) {
  if (pktSize < 3)
    return;
  uint8_t type = pkt[0];

  if (type == 0xC1) {
    uint8_t headcode = pkt[2];

    // NPC (guard) movement
    if (headcode == Opcode::NPC_MOVE &&
        pktSize >= (int)sizeof(PMSG_NPC_MOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_NPC_MOVE_SEND *>(pkt);
      float worldX = ((float)p->targetY + 0.5f) * 100.0f;
      float worldZ = ((float)p->targetX + 0.5f) * 100.0f;
      if (g_state->npcManager)
        g_state->npcManager->SetNpcMoveTarget(p->npcIndex, worldX, worldZ);
    }

    // Monster move/chase
    if (headcode == Opcode::MON_MOVE &&
        pktSize >= (int)sizeof(PMSG_MONSTER_MOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_MOVE_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        float worldX = (float)p->targetY * 100.0f;
        float worldZ = (float)p->targetX * 100.0f;
        g_state->monsterManager->SetMonsterServerPosition(idx, worldX, worldZ,
                                                          p->chasing != 0);
      }
    }

    // Damage result (player hits monster)
    if (headcode == Opcode::DAMAGE &&
        pktSize >= (int)sizeof(PMSG_DAMAGE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DAMAGE_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        MonsterInfo mi = g_state->monsterManager->GetMonsterInfo(idx);
        g_state->monsterManager->SetMonsterHP(idx, p->remainingHp, mi.maxHp);

        glm::vec3 monPos =
            g_state->monsterManager->GetMonsterInfo(idx).position;
        glm::vec3 hitPos = monPos + glm::vec3(0, 50, 0);

        // Poison DoT ticks: no hit animation or blood, just green damage number
        if (p->damageType == 4) {
          // Spawn small green poison particles on tick
          g_state->vfxManager->SpawnBurst(ParticleType::SPELL_POISON, hitPos, 5);
        } else {
          // Normal/skill hits: trigger hit animation + VFX
          g_state->monsterManager->TriggerHitAnimation(idx);
          SoundManager::Play(SOUND_HIT1 + rand() % 5);

          // Skill attacks: spawn skill-specific impact VFX + reduced blood
          // Normal attacks: standard blood burst (Main 5.2: 10x BITMAP_BLOOD+1)
          uint8_t heroSkill = g_state->hero->GetActiveSkillId();
          if (heroSkill > 0 &&
              p->attackerCharId == (uint16_t)*g_state->heroCharacterId) {
            g_state->vfxManager->SpawnSkillImpact(heroSkill, monPos);
            // Note: Twister StormTime spin is applied by proximity check
            // in main loop when tornado VFX reaches the monster, not here
            if (mi.type != 7)
              g_state->vfxManager->SpawnBurst(ParticleType::BLOOD, hitPos, 5);
          } else {
            // Giant (type 7) excluded from blood
            if (mi.type != 7)
              g_state->vfxManager->SpawnBurst(ParticleType::BLOOD, hitPos, 10);
          }
        }

        uint8_t dmgType = 0;
        if (p->damageType == 0)
          dmgType = 7; // Miss
        else if (p->damageType == 2)
          dmgType = 2; // Critical
        else if (p->damageType == 3)
          dmgType = 3; // Excellent
        else if (p->damageType == 4)
          dmgType = 4; // Poison DoT (green)

        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(monPos + glm::vec3(0, 80, 0), p->damage,
                                     dmgType);
      }
    }

    // Monster death
    if (headcode == Opcode::MON_DEATH &&
        pktSize >= (int)sizeof(PMSG_MONSTER_DEATH_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_DEATH_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->SetMonsterDying(idx);

      uint32_t xp = p->xpReward;
      if (xp > 0) {
        g_state->hero->GainExperience(xp);
        if (g_state->hero->LeveledUpThisFrame())
          SoundManager::Play(SOUND_LEVEL_UP);
        *g_state->serverXP = (int64_t)g_state->hero->GetExperience();
        *g_state->serverLevel = g_state->hero->GetLevel();
        *g_state->serverLevelUpPoints = g_state->hero->GetLevelUpPoints();
        *g_state->serverMaxHP = g_state->hero->GetMaxHP();
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), (int)xp, 9);
      }
    }

    // Monster attack player
    if (headcode == Opcode::MON_ATTACK &&
        pktSize >= (int)sizeof(PMSG_MONSTER_ATTACK_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_ATTACK_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->TriggerAttackAnimation(idx);

      if (g_state->hero->IsInSafeZone())
        return;

      *g_state->serverHP = (int)p->remainingHp;
      g_state->hero->SetHP(*g_state->serverHP);

      if (p->remainingHp <= 0) {
        *g_state->serverHP = 0;
        g_state->hero->ForceDie();
      }

      if (p->damage == 0) {
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), 0, 7);
      } else {
        g_state->hero->ApplyHitReaction();
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), p->damage,
                                     8);
      }
    }

    // Monster respawn
    if (headcode == Opcode::MON_RESPAWN &&
        pktSize >= (int)sizeof(PMSG_MONSTER_RESPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_RESPAWN_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->RespawnMonster(idx, p->x, p->y, p->hp);
    }

    // Stat allocation response
    if (headcode == Opcode::STAT_ALLOC_RESULT &&
        pktSize >= (int)sizeof(PMSG_STAT_ALLOC_SEND)) {
      auto *resp = reinterpret_cast<const PMSG_STAT_ALLOC_SEND *>(pkt);
      if (resp->result) {
        switch (resp->statType) {
        case 0:
          *g_state->serverStr = resp->newValue;
          break;
        case 1:
          *g_state->serverDex = resp->newValue;
          break;
        case 2:
          *g_state->serverVit = resp->newValue;
          break;
        case 3:
          *g_state->serverEne = resp->newValue;
          break;
        }
        *g_state->serverLevelUpPoints = resp->levelUpPoints;
        *g_state->serverMaxHP = resp->maxLife;
        if (g_state->serverAG)
          *g_state->serverAG = resp->ag;
        if (g_state->serverMaxAG)
          *g_state->serverMaxAG = resp->maxAg;

        g_state->hero->LoadStats(
            *g_state->serverLevel, *g_state->serverStr, *g_state->serverDex,
            *g_state->serverVit, *g_state->serverEne,
            (uint64_t)*g_state->serverXP, *g_state->serverLevelUpPoints,
            *g_state->serverHP, *g_state->serverMaxHP, *g_state->serverMP,
            *g_state->serverMaxMP, resp->ag, resp->maxAg,
            g_state->hero->GetClass());

        std::cout << "[Net] Stat alloc OK: type=" << (int)resp->statType
                  << " val=" << resp->newValue << " pts=" << resp->levelUpPoints
                  << std::endl;
      }
    }

    // Ground drop spawned
    if (headcode == Opcode::DROP_SPAWN &&
        pktSize >= (int)sizeof(PMSG_DROP_SPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_SPAWN_SEND *>(pkt);
      for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
        auto &gi = g_state->groundItems[i];
        if (!gi.active) {
          gi.dropIndex = p->dropIndex;
          gi.defIndex = p->defIndex;
          gi.quantity = p->quantity;
          gi.itemLevel = p->itemLevel;

          float h = g_state->terrain->GetHeight(p->worldX, p->worldZ);
          gi.position = glm::vec3(p->worldX, h + 100.0f, p->worldZ);
          gi.timer = 0.0f;
          gi.gravity = 15.0f;
          gi.scale = 1.0f;
          gi.isResting = false;

          if (g_state->getItemRestingAngle)
            g_state->getItemRestingAngle(gi.defIndex, gi.angle, gi.scale);
          gi.angle.y += (float)(rand() % 360);

          gi.active = true;
          break;
        }
      }
    }

    // Pickup result
    if (headcode == Opcode::PICKUP_RESULT &&
        pktSize >= (int)sizeof(PMSG_PICKUP_RESULT_SEND)) {
      auto *p = reinterpret_cast<const PMSG_PICKUP_RESULT_SEND *>(pkt);
      if (p->success) {
        // Play appropriate pickup sound based on item type
        if (p->defIndex == -1)
          SoundManager::Play(SOUND_DROP_GOLD01); // Zen pickup
        else
          SoundManager::Play(SOUND_GET_ITEM01); // Item pickup
        for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
          if (g_state->groundItems[i].active &&
              g_state->groundItems[i].dropIndex == p->dropIndex) {
            g_state->groundItems[i].active = false;
            break;
          }
        }
      }
    }

    // Drop removed
    if (headcode == Opcode::DROP_REMOVE &&
        pktSize >= (int)sizeof(PMSG_DROP_REMOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_REMOVE_SEND *>(pkt);
      for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
        if (g_state->groundItems[i].active &&
            g_state->groundItems[i].dropIndex == p->dropIndex) {
          g_state->groundItems[i].active = false;
          break;
        }
      }
    }

    // Equipment sync (C1 during gameplay)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 4) {
      ParseEquipmentPacket(pkt, pktSize, 3, 4, nullptr);
    }

    // Character stats sync
    if (headcode == Opcode::CHARSTATS &&
        pktSize >= (int)sizeof(PMSG_CHARSTATS_SEND)) {
      auto *stats = reinterpret_cast<const PMSG_CHARSTATS_SEND *>(pkt);
      int oldHP = *g_state->serverHP;
      SyncCharStats(stats);

      g_state->hero->LoadStats(
          stats->level, stats->strength, stats->dexterity, stats->vitality,
          stats->energy, (uint64_t)*g_state->serverXP,
          *g_state->serverLevelUpPoints, *g_state->serverHP,
          *g_state->serverMaxHP, *g_state->serverMP, *g_state->serverMaxMP,
          stats->ag, stats->maxAg, stats->charClass);

      // Floating heal text if HP increased
      if (*g_state->serverHP > oldHP && oldHP > 0) {
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(),
                                     *g_state->serverHP - oldHP, 10);
      }
    }

    // Shop Buy Result
    if (headcode == Opcode::SHOP_BUY_RESULT &&
        pktSize >= (int)sizeof(PMSG_SHOP_BUY_RESULT_SEND)) {
      auto *p = reinterpret_cast<const PMSG_SHOP_BUY_RESULT_SEND *>(pkt);
      if (p->result) {
        SoundManager::Play(SOUND_DROP_GOLD01);
        std::cout << "[Shop] Bought item defIndex=" << p->defIndex
                  << " qty=" << (int)p->quantity << "\n";
      } else {
        SoundManager::Play(SOUND_ERROR01);
        std::cout << "[Shop] Failed to buy item\n";
      }
    }

    // Shop Sell Result
    if (headcode == Opcode::SHOP_SELL_RESULT &&
        pktSize >= (int)sizeof(PMSG_SHOP_SELL_RESULT_SEND)) {
      auto *p = reinterpret_cast<const PMSG_SHOP_SELL_RESULT_SEND *>(pkt);
      if (p->result) {
        SoundManager::Play(SOUND_DROP_GOLD01);
        std::cout << "[Shop] Sold item bagSlot=" << (int)p->bagSlot
                  << " gained " << p->zenGained << " zen\n";
      } else {
        SoundManager::Play(SOUND_ERROR01);
        std::cout << "[Shop] Failed to sell item\n";
      }
    }
  }

  // C2 packets (ongoing)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // Inventory sync
    if (headcode == Opcode::INV_SYNC) {
      ParseInventorySync(pkt, pktSize);
    }

    // Equipment sync (C2)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 5) {
      ParseEquipmentPacket(pkt, pktSize, 4, 5, nullptr);
    }

    // Skill List
    if (headcode == Opcode::SKILL_LIST && pktSize >= 5) {
      uint8_t count = pkt[4];
      if (g_state->learnedSkills) {
        g_state->learnedSkills->clear();
        for (int i = 0; i < count; i++) {
          if (5 + i >= pktSize)
            break;
          g_state->learnedSkills->push_back(pkt[5 + i]);
        }
        std::cout << "[Net] Received " << (int)count << " learned skills\n";
      }
    }

    // Shop List
    if (headcode == Opcode::SHOP_LIST) {
      if (g_state->shopOpen && g_state->shopItems) {
        g_state->shopItems->clear();
        uint8_t count = pkt[4];
        for (int i = 0; i < count; i++) {
          int off = 5 + i * sizeof(PMSG_SHOP_ITEM);
          if (off + (int)sizeof(PMSG_SHOP_ITEM) > pktSize)
            break;
          auto *si = reinterpret_cast<const PMSG_SHOP_ITEM *>(pkt + off);
          ShopItem item;
          item.defIndex = si->defIndex;
          item.itemLevel = si->itemLevel;
          item.buyPrice = si->buyPrice;
          g_state->shopItems->push_back(item);
        }
        *g_state->shopOpen = true;
        SoundManager::Play(SOUND_INTERFACE01);
        std::cout << "[Shop] Received list with " << (int)count << " items\n";
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Character select packet handler (F3 sub-codes: charlist, create, delete)
// ═══════════════════════════════════════════════════════════════════

void HandleCharSelectPacket(const uint8_t *pkt, int pktSize) {
  if (pktSize < 4)
    return;
  uint8_t type = pkt[0];
  if (type != 0xC1)
    return;

  uint8_t headcode = pkt[2];
  if (headcode != Opcode::CHARSELECT)
    return;

  uint8_t subcode = pkt[3];

  // F3:00 — Character List
  if (subcode == Opcode::SUB_CHARLIST) {
    if (pktSize < (int)sizeof(PMSG_CHARLIST_HEAD))
      return;
    auto *head = reinterpret_cast<const PMSG_CHARLIST_HEAD *>(pkt);
    int count = head->count;
    int entryOff = sizeof(PMSG_CHARLIST_HEAD);

    CharacterSelect::CharSlot slots[CharacterSelect::MAX_SLOTS] = {};
    int parsed = 0;

    for (int i = 0; i < count; i++) {
      if (entryOff + (int)sizeof(PMSG_CHARLIST_ENTRY) > pktSize)
        break;
      auto *entry =
          reinterpret_cast<const PMSG_CHARLIST_ENTRY *>(pkt + entryOff);
      int slot = entry->slot;
      if (slot >= 0 && slot < CharacterSelect::MAX_SLOTS) {
        slots[slot].occupied = true;
        std::memcpy(slots[slot].name, entry->name, 10);
        slots[slot].name[10] = '\0';
        slots[slot].classCode = entry->classCode;
        slots[slot].level = GetWordBE(reinterpret_cast<const uint8_t *>(&entry->level));
        // Parse equipment appearance from charSet[1..14]
        for (int e = 0; e < 7; e++) {
          slots[slot].equip[e].category = entry->charSet[1 + e * 2];
          slots[slot].equip[e].itemIndex = entry->charSet[2 + e * 2];
        }
        parsed++;
      }
      entryOff += sizeof(PMSG_CHARLIST_ENTRY);
    }

    CharacterSelect::SetCharacterList(slots, CharacterSelect::MAX_SLOTS);
    std::cout << "[Net] Character list: " << parsed << " characters"
              << std::endl;
  }

  // F3:01 — Character Create Result
  if (subcode == Opcode::SUB_CHARCREATE) {
    if (pktSize < (int)sizeof(PMSG_CHARCREATE_RESULT))
      return;
    auto *res = reinterpret_cast<const PMSG_CHARCREATE_RESULT *>(pkt);
    CharacterSelect::OnCreateResult(res->result, res->name, res->slot,
                                    res->classCode);
    std::cout << "[Net] Create result: " << (int)res->result << " name='"
              << res->name << "'" << std::endl;
  }

  // F3:02 — Character Delete Result
  if (subcode == Opcode::SUB_CHARDELETE) {
    if (pktSize < (int)sizeof(PMSG_CHARDELETE_RESULT))
      return;
    auto *res = reinterpret_cast<const PMSG_CHARDELETE_RESULT *>(pkt);
    CharacterSelect::OnDeleteResult(res->result);
    std::cout << "[Net] Delete result: " << (int)res->result << std::endl;
  }
}

} // namespace ClientPacketHandler

#include "handlers/CombatHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "PathFinder.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/QuestHandler.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace CombatHandler {

// Skill definitions for all classes
// resourceCost: AG for DK, Mana for DW/ELF/MG
// aoeRange: world units (0 = single target, 200 = 2 grid cells, etc.)
// isMagic: true = uses magic damage formula instead of physical
struct SkillDef {
  uint8_t skillId;
  int resourceCost;
  int damageBonus;
  float aoeRange; // 0 = single target
  bool isMagic;   // true = wizardry damage
};

static const SkillDef g_skillDefs[] = {
    // DK skills (AG cost)
    {19, 9, 15, 0, false},     // Falling Slash (single target)
    {20, 9, 15, 0, false},     // Lunge (single target)
    {21, 8, 15, 0, false},     // Uppercut (single target)
    {22, 9, 18, 0, false},     // Cyclone (single target)
    {23, 10, 20, 0, false},    // Slash (single target)
    {41, 10, 25, 200, false},  // Twisting Slash (AoE range 2 grid)
    {42, 20, 60, 300, false},  // Rageful Blow (AoE range 3 grid)
    {43, 12, 70, 100, false},  // Death Stab (splash range 1 grid around target)
    // DW spells (Mana cost) — OpenMU Version075 skill definitions
    {11, 5, 14, 0, true},       // Power Wave (ranged, OpenMU: damage=14, mana=5)
    {17, 1, 8, 0, true},        // Energy Ball (basic ranged)
    {1, 42, 20, 0, true},       // Poison (DoT effect, single target)
    {2, 12, 40, 0, true},       // Meteorite (single target ranged)
    {3, 15, 30, 0, true},       // Lightning (single target)
    {4, 3, 22, 0, true},        // Fire Ball (basic ranged)
    {5, 50, 50, 0, true},       // Flame (single target)
    {6, 30, 0, 0, true},        // Teleport (no damage, utility)
    {7, 38, 35, 0, true},       // Ice (single target)
    {8, 60, 55, 200, true},     // Twister (AoE)
    {9, 90, 80, 400, true},     // Evil Spirit (AoE, Main 5.2 range)
    {10, 160, 100, 300, true},   // Hellfire (large AoE)
    {12, 140, 90, 90, true},    // Aqua Beam (beam, line AoE — width matches visible beam)
    {13, 90, 120, 150, true},   // Cometfall (AoE sky-strike)
    {14, 200, 150, 400, true},  // Inferno (ring of explosions AoE)
    // Elf skills (Mana cost) — OpenMU Version075
    {26, 20, 0, 0, true},       // Heal (buff, no damage)
    {27, 30, 0, 0, true},       // Greater Defense (buff)
    {28, 40, 0, 0, true},       // Greater Damage (buff)
    {30, 40, 0, 0, true},       // Summon Goblin (summon)
    {31, 70, 0, 0, true},       // Summon Stone Golem
    {32, 110, 0, 0, true},      // Summon Assassin
    {33, 160, 0, 0, true},      // Summon Elite Yeti
    {34, 200, 0, 0, true},      // Summon Dark Knight
    {35, 250, 0, 0, true},      // Summon Bali
};

static const SkillDef *FindSkillDef(uint8_t skillId) {
  for (auto &s : g_skillDefs)
    if (s.skillId == skillId)
      return &s;
  return nullptr;
}

// Shared combat logic: calculate damage, apply to monster, handle aggro/kill/XP
static void ApplyDamageToMonster(Session &session, MonsterInstance *mon,
                                 int bonusDamage, GameWorld &world,
                                 Server &server, bool isMagic = false) {
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool hasBow = session.hasBow;

  int baseMin, baseMax;
  if (isMagic) {
    // Wizardry damage: ENE-based + staff magic damage
    baseMin = StatCalculator::CalculateMinMagicDamage(charCls, session.energy) +
              session.weaponDamageMin;
    baseMax = StatCalculator::CalculateMaxMagicDamage(charCls, session.energy) +
              session.weaponDamageMax;
    // Staff Rise percentage bonus (OpenMU Version075)
    if (session.staffRisePercent > 0) {
      baseMin = baseMin * (100 + session.staffRisePercent) / 100;
      baseMax = baseMax * (100 + session.staffRisePercent) / 100;
    }
  } else {
    baseMin = StatCalculator::CalculateMinDamage(charCls, session.strength,
                                                 session.dexterity,
                                                 session.energy, hasBow) +
              session.weaponDamageMin;
    baseMax = StatCalculator::CalculateMaxDamage(charCls, session.strength,
                                                 session.dexterity,
                                                 session.energy, hasBow) +
              session.weaponDamageMax;
  }

  if (baseMax < baseMin)
    baseMax = baseMin;

  uint8_t damageType = 1; // normal
  int damage = 0;
  bool missed = false;

  // Level-based auto-miss: monster 10+ levels above player always dodges
  if (mon->level >= session.level + 10) {
    missed = true;
    damageType = 0;
  }

  int attackRate = StatCalculator::CalculateAttackRate(
      session.level, session.dexterity, session.strength);
  int defRate = mon->defenseRate;

  // OpenMU hit chance: hitChance = 1 - defRate/atkRate (min 3%)
  if (!missed) {
    int hitChance;
    if (attackRate > 0 && defRate < attackRate) {
      hitChance = 100 - (defRate * 100) / attackRate;
    } else {
      hitChance = 3;
    }
    if (hitChance < 3)
      hitChance = 3;
    if (hitChance > 100)
      hitChance = 100;

    if (rand() % 100 >= hitChance) {
      missed = true;
      damageType = 0; // miss
    }
  }

  if (!missed) {
    damage =
        baseMin + (baseMax > baseMin ? rand() % (baseMax - baseMin + 1) : 0);

    int critRoll = rand() % 100;
    if (critRoll < 1) {
      damage = (baseMax * 120) / 100; // Excellent: 1.2x max
      damageType = 3;
    } else if (critRoll < 6) {
      damage = baseMax; // Critical: max damage
      damageType = 2;
    }

    // Two-handed weapon bonus: 120%
    if (session.hasTwoHandedWeapon) {
      damage = (damage * 120) / 100;
    }

    // Imp pet attack bonus (30% increase)
    if (session.petAttackMultiplier > 1.0f) {
      damage = (int)(damage * session.petAttackMultiplier);
    }

    // Skill damage bonus (flat addition before defense)
    damage += bonusDamage;

    // Greater Damage buff (Elf aura)
    if (session.buffs[1].active)
      damage += session.buffs[1].value;

    damage = std::max(1, damage - mon->defense);

    // Evading monsters are invulnerable (WoW leash behavior)
    // Cancel evade on re-aggro — this hit does no damage but re-engages
    if (mon->evading) {
      damage = 0;
      damageType = 0; // Show as miss
      mon->evading = false;
    } else {
      mon->hp -= damage;
    }
    // Track player threat for aggro system (summon threat comparison)
    mon->playerThreat += static_cast<float>(damage);
  }

  // Aggro logic — always triggers regardless of hit/miss
  // (attacking a monster should provoke it even if the attack misses)
  {
    if (mon->aggroTargetFd != session.GetFd()) {
      mon->aggroTargetFd = session.GetFd();
    }
    mon->aggroTimer = 15.0f;
    // Don't reset AI if already engaged with the same target — resetting
    // CHASING clears the path (preventing the monster from reaching the player),
    // and resetting APPROACHING/ATTACKING restarts the attack cycle
    bool alreadyEngaged =
        (mon->aggroTargetFd == session.GetFd()) &&
        (mon->aiState == MonsterInstance::AIState::CHASING ||
         mon->aiState == MonsterInstance::AIState::APPROACHING ||
         mon->aiState == MonsterInstance::AIState::ATTACKING);
    if (!alreadyEngaged) {
      mon->aiState = MonsterInstance::AIState::CHASING;
      mon->currentPath.clear();
      mon->pathStep = 0;
      mon->repathTimer = 0.0f;
      mon->moveTimer = mon->moveDelay;
    }

    // Pack assist (same-type monsters within 3 cells join aggro)
    if (mon->aggressive) {
      for (auto &ally : world.GetMonsterInstancesMut()) {
        if (ally.index == mon->index)
          continue;
        if (ally.isSummon())
          continue; // Summons follow their owner's AI, not pack aggro
        if (ally.type != mon->type)
          continue;
        if (ally.aiState == MonsterInstance::AIState::DYING ||
            ally.aiState == MonsterInstance::AIState::DEAD)
          continue;
        if (ally.aiState == MonsterInstance::AIState::CHASING ||
            ally.aiState == MonsterInstance::AIState::ATTACKING ||
            ally.aiState == MonsterInstance::AIState::APPROACHING ||
            ally.aiState == MonsterInstance::AIState::RETURNING)
          continue;
        if (ally.aggroTimer < 0.0f)
          continue;
        if (ally.chaseFailCount >= 10)
          continue;
        int dist = PathFinder::ChebyshevDist(ally.gridX, ally.gridY, mon->gridX,
                                             mon->gridY);
        if (dist <= 3) { // 3 grid cells (was: ally.viewRange)
          ally.aggroTargetFd = session.GetFd();
          ally.aggroTimer = 15.0f;
          ally.aiState = MonsterInstance::AIState::CHASING;
          ally.currentPath.clear();
          ally.pathStep = 0;
          ally.repathTimer = 0.0f;
          ally.moveTimer = ally.moveDelay;
          ally.attackCooldown = 0.0f;
        }
      }
    }

    bool killed = mon->hp <= 0;
    if (killed)
      mon->hp = 0;

    if (killed) {
      mon->aiState = MonsterInstance::AIState::DYING;
      mon->stateTimer = 0.0f;

      // Clear attack target so summon stops chasing the dead monster
      if (session.attackTargetMonsterIdx == mon->index)
        session.attackTargetMonsterIdx = 0;

      int xp = ServerConfig::CalculateXP(session.level, mon->level);

      PMSG_MONSTER_DEATH_SEND deathPkt{};
      deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
      deathPkt.monsterIndex = mon->index;
      deathPkt.killerCharId = static_cast<uint16_t>(session.characterId);
      deathPkt.xpReward = static_cast<uint32_t>(xp);
      server.Broadcast(&deathPkt, sizeof(deathPkt));

      session.experience += xp;
      bool leveledUp = false;
      while (true) {
        uint64_t nextXP = Database::GetXPForLevel(session.level);
        if (session.experience >= nextXP && session.level < 400) {
          session.level++;

          CharacterClass charCls2 =
              static_cast<CharacterClass>(session.classCode);
          session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls2);

          session.maxHp = StatCalculator::CalculateMaxHP(
              charCls2, session.level, session.vitality) +
              session.petBonusMaxHp;
          session.maxMana = StatCalculator::CalculateMaxMP(
              charCls2, session.level, session.energy);
          session.maxAg = StatCalculator::CalculateMaxAG(
              session.strength, session.dexterity, session.vitality,
              session.energy);

          session.hp = session.maxHp;
          session.mana = session.maxMana;
          session.ag = session.maxAg;
          leveledUp = true;
          printf("[Combat] Char %d leveled up to %d! Total XP: %llu\n",
                 session.characterId, (int)session.level,
                 (unsigned long long)session.experience);
        } else {
          break;
        }
      }

      // Save XP/level messages to chat log
      if (xp > 0) {
        char xpBuf[64];
        snprintf(xpBuf, sizeof(xpBuf), "+%d Experience", xp);
        // purple: IM_COL32(180, 120, 255, 255) = 0xFFFF78B4
        server.GetDB().SaveChatMessage(session.characterId, 1, 0xFFFF78B4, xpBuf);
      }
      if (leveledUp) {
        char lvlBuf[64];
        snprintf(lvlBuf, sizeof(lvlBuf), "Congratulations! Level %d reached!",
                 (int)session.level);
        // yellow: IM_COL32(255, 255, 100, 255) = 0xFF64FFFF
        server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, lvlBuf);
        // Rescale active summon to match new owner level
        if (session.activeSummonIndex > 0)
          world.RescaleSummon(session.activeSummonIndex, session.level);
      }

      if (leveledUp || xp > 0) {
        CharacterHandler::SendCharStats(session);
      }

      auto drops = world.SpawnDrops(mon->worldX, mon->worldZ, mon->level,
                                    mon->type, server.GetDB());
      for (auto &drop : drops) {
        PMSG_DROP_SPAWN_SEND dropPkt{};
        dropPkt.h = MakeC1Header(sizeof(dropPkt), Opcode::DROP_SPAWN);
        dropPkt.dropIndex = drop.index;
        dropPkt.defIndex = drop.defIndex;
        dropPkt.quantity = drop.quantity;
        dropPkt.itemLevel = drop.itemLevel;
        dropPkt.worldX = drop.worldX;
        dropPkt.worldZ = drop.worldZ;
        server.Broadcast(&dropPkt, sizeof(dropPkt));
      }

      // Quest kill tracking
      QuestHandler::OnMonsterKill(session, mon->type, server.GetDB());

    }
  }

  // Broadcast damage result
  PMSG_DAMAGE_SEND dmgPkt{};
  dmgPkt.h = MakeC1Header(sizeof(dmgPkt), Opcode::DAMAGE);
  dmgPkt.monsterIndex = mon->index;
  dmgPkt.damage = static_cast<uint16_t>(damage);
  dmgPkt.damageType = damageType;
  dmgPkt.remainingHp = static_cast<uint16_t>(std::max(0, mon->hp));
  dmgPkt.attackerCharId = static_cast<uint16_t>(session.characterId);
  server.Broadcast(&dmgPkt, sizeof(dmgPkt));
}

void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server) {
  if (session.dead)
    return;
  // Combat resets idle HP regen
  session.idleTimer = 0.0f;
  session.idleHpRemainder = 0.0f;
  // Server-side attack rate limiting (prevents speed hack / GCD bypass)
  if (session.attackCooldown > 0.0f)
    return;
  if (packet.size() < sizeof(PMSG_ATTACK_RECV))
    return;
  const auto *atk = reinterpret_cast<const PMSG_ATTACK_RECV *>(packet.data());

  // Block attacks from players standing in safe zones
  if (world.IsSafeZone(session.worldX, session.worldZ))
    return;

  auto *mon = world.FindMonster(atk->monsterIndex);
  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD)
    return;

  // Bow/crossbow requires correct ammo type in left hand (slot 1)
  // Bows (idx 0-6, 17) need Arrows (idx 15), Crossbows (idx 8-14, 16, 18) need Bolts (idx 7)
  if (session.hasBow) {
    auto &ammo = session.equipment[1];
    auto &weapon = session.equipment[0];
    bool isCrossbow = (weapon.category == 4 &&
        ((weapon.itemIndex >= 8 && weapon.itemIndex <= 14) ||
         weapon.itemIndex == 16 || weapon.itemIndex == 18));
    bool correctAmmo = isCrossbow
        ? (ammo.category == 4 && ammo.itemIndex == 7)   // Bolts for crossbows
        : (ammo.category == 4 && ammo.itemIndex == 15);  // Arrows for bows
    if (!correctAmmo || ammo.quantity == 0)
      return;
  }

  ApplyDamageToMonster(session, mon, 0, world, server);
  session.attackCooldown = 0.4f; // Minimum 0.4s between melee attacks
  session.attackTargetMonsterIdx = mon->index; // Track for summon assist

  // Consume one arrow/bolt after successful ranged attack
  if (session.hasBow) {
    auto &ammo = session.equipment[1];
    if (ammo.quantity > 0) {
      ammo.quantity--;
      Database &db = server.GetDB();
      if (ammo.quantity == 0) {
        // Arrows depleted — unequip slot 1
        db.UpdateEquipment(session.characterId, 1, 0xFF, 0, 0, 0);
        ammo.category = 0xFF;
        ammo.itemIndex = 0;
        ammo.itemLevel = 0;
        CharacterHandler::SendEquipment(session, db, session.characterId);
        CharacterHandler::RefreshCombatStats(session, db, session.characterId);
      } else {
        // Update quantity in DB
        db.UpdateEquipment(session.characterId, 1, ammo.category,
                           ammo.itemIndex, ammo.itemLevel, ammo.quantity);
        CharacterHandler::SendEquipment(session, db, session.characterId);
      }
    }
  }

  // AG recovery on auto-attack hit (DK only)
  if (session.classCode == 16 && session.ag < session.maxAg) {
    int agGain = std::max(1, session.maxAg / 50); // 2% of maxAG
    session.ag = std::min(session.ag + agGain, session.maxAg);
    CharacterHandler::SendCharStats(session);
  }
}

void HandleSkillAttack(Session &session, const std::vector<uint8_t> &packet,
                       GameWorld &world, Server &server) {
  if (session.dead)
    return;
  // Block attacks from players standing in safe zones
  if (world.IsSafeZone(session.worldX, session.worldZ))
    return;

  if (packet.size() < sizeof(PMSG_SKILL_ATTACK_RECV))
    return;
  const auto *atk =
      reinterpret_cast<const PMSG_SKILL_ATTACK_RECV *>(packet.data());

  // Utility skills (buffs 26-28, summons 30-35) bypass combat GCD
  bool isUtility = (atk->skillId >= 26 && atk->skillId <= 28) ||
                   (atk->skillId >= 30 && atk->skillId <= 35);
  // Server-side attack rate limiting (prevents speed hack / GCD bypass)
  if (!isUtility && session.attackCooldown > 0.0f)
    return;

  // Validate skill is learned
  bool hasSkill = false;
  for (auto s : session.learnedSkills) {
    if (s == atk->skillId) {
      hasSkill = true;
      break;
    }
  }
  if (!hasSkill)
    return;

  // Look up skill definition
  const SkillDef *skillDef = FindSkillDef(atk->skillId);
  if (!skillDef)
    return;

  // Check resource (DK uses AG, others use Mana)
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool isDK = (charCls == CharacterClass::CLASS_DK);
  int currentResource = isDK ? session.ag : session.mana;

  if (currentResource < skillDef->resourceCost) {
    CharacterHandler::SendCharStats(session);
    return;
  }

  // Deduct resource
  if (isDK) {
    session.ag -= skillDef->resourceCost;
    session.lastAgUseTime = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
  } else {
    session.mana -= skillDef->resourceCost;
  }

  // Teleport (skill 6) — no damage, just utility
  if (skillDef->skillId == 6) {
    CharacterHandler::SendCharStats(session);
    return;
  }

  // Summon skills (30-35) — spawn a summon pet
  if (atk->skillId >= 30 && atk->skillId <= 35) {
    // Block summon casting in safe zones
    uint8_t pgx = static_cast<uint8_t>(session.worldZ / 100.0f);
    uint8_t pgy = static_cast<uint8_t>(session.worldX / 100.0f);
    if (world.IsSafeZoneGrid(pgx, pgy))
      return;

    // Map skill→monster type
    static const uint16_t summonTypeMap[] = {
        26,  // skill 30: Goblin
        32,  // skill 31: Stone Golem
        21,  // skill 32: Assassin
        20,  // skill 33: Elite Yeti
        10,  // skill 34: Dark Knight
        150, // skill 35: Bali
    };
    uint16_t monsterType = summonTypeMap[atk->skillId - 30];

    // If already have the same summon type active, despawn it (toggle off)
    if (session.activeSummonIndex > 0 &&
        session.activeSummonType == static_cast<int16_t>(monsterType)) {
      PMSG_SUMMON_DESPAWN_SEND dpkt{};
      dpkt.h = MakeC1Header(sizeof(dpkt), Opcode::SUMMON_DESPAWN);
      dpkt.monsterIndex = session.activeSummonIndex;
      server.Broadcast(&dpkt, sizeof(dpkt));
      world.DespawnSummon(session.activeSummonIndex);
      session.activeSummonIndex = 0;
      session.activeSummonType = -1;
      return;
    }

    // Despawn old summon before creating a different one
    if (session.activeSummonIndex > 0) {
      PMSG_SUMMON_DESPAWN_SEND dpkt{};
      dpkt.h = MakeC1Header(sizeof(dpkt), Opcode::SUMMON_DESPAWN);
      dpkt.monsterIndex = session.activeSummonIndex;
      server.Broadcast(&dpkt, sizeof(dpkt));
      world.DespawnSummon(session.activeSummonIndex);
      session.activeSummonIndex = 0;
    }

    // Find walkable cell adjacent to player
    uint8_t playerGX = static_cast<uint8_t>(session.worldZ / 100.0f);
    uint8_t playerGY = static_cast<uint8_t>(session.worldX / 100.0f);
    uint8_t summonGX = playerGX, summonGY = playerGY;
    bool found = false;
    for (int r = 1; r <= 3 && !found; r++) {
      for (int dy = -r; dy <= r && !found; dy++) {
        for (int dx = -r; dx <= r && !found; dx++) {
          if (std::abs(dx) != r && std::abs(dy) != r)
            continue;
          int nx = (int)playerGX + dx;
          int ny = (int)playerGY + dy;
          if (nx < 0 || ny < 0 || nx >= 256 || ny >= 256)
            continue;
          if (world.IsWalkableGrid((uint8_t)nx, (uint8_t)ny)) {
            summonGX = (uint8_t)nx;
            summonGY = (uint8_t)ny;
            found = true;
          }
        }
      }
    }

    auto *summon =
        world.SpawnSummon(monsterType, summonGX, summonGY, session.GetFd(),
                          session.characterId, session.level);
    if (summon) {
      session.activeSummonIndex = summon->index;
      session.activeSummonType = static_cast<int16_t>(monsterType);

      // Broadcast single-summon viewport so client creates the monster instance
      // (Don't use BuildMonsterViewportV2Packet — that resends ALL monsters and
      // causes duplicates on the client)
      {
        size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY_V2);
        size_t pktSize = 5 + entrySize;
        uint8_t buf[5 + sizeof(PMSG_MONSTER_VIEWPORT_ENTRY_V2)] = {};
        auto *head = reinterpret_cast<PWMSG_HEAD *>(buf);
        *head = MakeC2Header(static_cast<uint16_t>(pktSize), 0x34);
        buf[4] = 1; // count = 1
        auto *e = reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY_V2 *>(buf + 5);
        e->indexH = static_cast<uint8_t>(summon->index >> 8);
        e->indexL = static_cast<uint8_t>(summon->index & 0xFF);
        e->typeH = static_cast<uint8_t>(summon->type >> 8);
        e->typeL = static_cast<uint8_t>(summon->type & 0xFF);
        e->x = summon->gridX;
        e->y = summon->gridY;
        e->dir = summon->dir;
        e->hp = static_cast<uint16_t>(summon->hp);
        e->maxHp = static_cast<uint16_t>(summon->maxHp);
        e->state = 0; // alive
        server.Broadcast(buf, pktSize);
      }

      // Broadcast summon spawn packet
      PMSG_SUMMON_SPAWN_SEND spawnPkt{};
      spawnPkt.h = MakeC1Header(sizeof(spawnPkt), Opcode::SUMMON_SPAWN);
      spawnPkt.monsterIndex = summon->index;
      spawnPkt.ownerCharId = session.characterId;
      spawnPkt.level = summon->level;
      server.Broadcast(&spawnPkt, sizeof(spawnPkt));
    }

    CharacterHandler::SendCharStats(session);
    session.attackCooldown = 1.0f; // Summon cast GCD
    return;
  }

  // Elf buff skills (26=Heal, 27=Greater Defense, 28=Greater Damage) — self-cast
  if (atk->skillId >= 26 && atk->skillId <= 28) {
    if (atk->skillId == 26) {
      // Heal: instant, 5 + (energy / 5) — skip if already at full HP
      if (session.hp >= session.maxHp) return;
      int healAmount = 5 + session.energy / 5;
      session.hp = std::min(session.hp + healAmount, session.maxHp);
      PMSG_BUFF_EFFECT_SEND pkt{};
      pkt.h = MakeC1Header(sizeof(pkt), Opcode::BUFF_EFFECT);
      pkt.buffType = 0;
      pkt.active = 1;
      pkt.value = (uint16_t)healAmount;
      pkt.duration = 0;
      session.Send(&pkt, sizeof(pkt));
      session.attackCooldown = 2.0f; // Heal cooldown (2 seconds)
    } else if (atk->skillId == 27) {
      // Greater Defense: 2 + (energy / 8), 30 minutes
      int bonus = 2 + session.energy / 8;
      session.buffs[0] = {1, 1800.0f, bonus, true};
      PMSG_BUFF_EFFECT_SEND pkt{};
      pkt.h = MakeC1Header(sizeof(pkt), Opcode::BUFF_EFFECT);
      pkt.buffType = 1;
      pkt.active = 1;
      pkt.value = (uint16_t)bonus;
      pkt.duration = 1800.0f;
      session.Send(&pkt, sizeof(pkt));
    } else {
      // Greater Damage: 3 + (energy / 7), 30 minutes
      int bonus = 3 + session.energy / 7;
      session.buffs[1] = {2, 1800.0f, bonus, true};
      PMSG_BUFF_EFFECT_SEND pkt{};
      pkt.h = MakeC1Header(sizeof(pkt), Opcode::BUFF_EFFECT);
      pkt.buffType = 2;
      pkt.active = 1;
      pkt.value = (uint16_t)bonus;
      pkt.duration = 1800.0f;
      session.Send(&pkt, sizeof(pkt));
    }
    CharacterHandler::SendCharStats(session);
    session.attackCooldown = 1.0f;
    return;
  }

  // Find target monster
  auto *mon = world.FindMonster(atk->monsterIndex);

  // Ground-target AoE (monsterIndex=0xFFFF)
  if (!mon && skillDef->aoeRange > 0) {
    float px = session.worldX, pz = session.worldZ;
    float tx = (atk->targetX != 0) ? atk->targetX : px;
    float tz = (atk->targetZ != 0) ? atk->targetZ : pz;
    float r2 = skillDef->aoeRange * skillDef->aoeRange;
    int aoeHits = 0;

    // Evil Spirit / Hellfire are caster-centered AoE (radiate from caster, not click)
    if (skillDef->skillId == 9 || skillDef->skillId == 10 || skillDef->skillId == 14) {
      tx = px;
      tz = pz;
    }

    // Twister/Flash: check monsters along the LINE from caster toward target direction
    // Twister travels 472 units, Flash beam extends 1000 units (20 segments × 50)
    bool isLinePath = (skillDef->skillId == 8 || skillDef->skillId == 12) && (tx != px || tz != pz);
    float ldx = tx - px, ldz = tz - pz;
    float lenSq = ldx * ldx + ldz * ldz;
    if (isLinePath && lenSq > 0.01f) {
      float len = sqrtf(lenSq);
      float travelDist = (skillDef->skillId == 12) ? 1000.0f : 472.0f;
      if (len < travelDist) {
        ldx = ldx / len * travelDist;
        ldz = ldz / len * travelDist;
        tx = px + ldx;
        tz = pz + ldz;
        lenSq = ldx * ldx + ldz * ldz;
      }
    }

    for (auto &other : world.GetMonsterInstancesMut()) {
      if (other.aiState == MonsterInstance::AIState::DYING ||
          other.aiState == MonsterInstance::AIState::DEAD)
        continue;

      bool inRange = false;
      if (isLinePath && lenSq > 0.01f) {
        // Distance from monster to line segment (caster → target)
        float mx = other.worldX - px, mz = other.worldZ - pz;
        float t = (mx * ldx + mz * ldz) / lenSq;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float nx = px + ldx * t - other.worldX;
        float nz = pz + ldz * t - other.worldZ;
        inRange = (nx * nx + nz * nz <= r2);
      } else {
        // Circle check at target position (other AoE skills)
        float dx = other.worldX - tx;
        float dz = other.worldZ - tz;
        inRange = (dx * dx + dz * dz <= r2);
      }

      if (inRange) {
        ApplyDamageToMonster(session, &other, skillDef->damageBonus, world,
                             server, skillDef->isMagic);
        // Twisting Slash (skill 41): double hit (spin start + spin end)
        if (skillDef->skillId == 41 && other.hp > 0) {
          ApplyDamageToMonster(session, &other, skillDef->damageBonus, world,
                               server, skillDef->isMagic);
        }
        if ((skillDef->skillId == 8 || skillDef->skillId == 9) && other.hp > 0)
          other.stormTime = 10;
        aoeHits++;
      }
    }
    CharacterHandler::SendCharStats(session);
    return;
  }

  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD)
    return;

  // Apply damage with skill bonus to primary target
  ApplyDamageToMonster(session, mon, skillDef->damageBonus, world, server,
                       skillDef->isMagic);
  session.attackTargetMonsterIdx = mon->index; // Track for summon assist

  // Meteorite (skill 2): two fireballs = two damage hits
  if (skillDef->skillId == 2 && mon->hp > 0) {
    ApplyDamageToMonster(session, mon, skillDef->damageBonus, world, server,
                         skillDef->isMagic);
  }

  // Twisting Slash (skill 41): double hit (spin start + spin end)
  if (skillDef->skillId == 41 && mon->hp > 0) {
    ApplyDamageToMonster(session, mon, skillDef->damageBonus, world, server,
                         skillDef->isMagic);
  }

  // Main 5.2: Twister/Evil Spirit applies StormTime=10 AI stun (only if alive)
  if ((skillDef->skillId == 8 || skillDef->skillId == 9) && mon->hp > 0)
    mon->stormTime = 10;

  // Poison (skill 1): apply DoT debuff — OpenMU PoisonMagicEffect
  // Duration 10s, tick every 3s, tick damage = 30% of initial hit
  if (skillDef->skillId == 1 && mon->hp > 0) {
    int tickDmg = std::max(1, (skillDef->damageBonus + session.maxMagicDamage) / 3);
    if (!mon->poisoned) {
      // Fresh poison: start tick timer from 0
      mon->poisonTickTimer = 0.0f;
    }
    // Refresh duration and update damage (don't reset tick timer on re-apply)
    mon->poisoned = true;
    mon->poisonDuration = 10.0f;
    mon->poisonDamage = std::max(mon->poisonDamage, tickDmg);
    mon->poisonAttackerFd = session.GetFd();
  }

  // AoE: hit all nearby monsters within skill range (OpenMU: AreaSkillAutomaticHits)
  int aoeHits = 0;
  if (skillDef->aoeRange > 0) {
    float cx = mon->worldX, cz = mon->worldZ;
    float r2 = skillDef->aoeRange * skillDef->aoeRange;
    for (auto &other : world.GetMonsterInstancesMut()) {
      if (other.index == mon->index)
        continue;
      if (other.aiState == MonsterInstance::AIState::DYING ||
          other.aiState == MonsterInstance::AIState::DEAD)
        continue;
      float dx = other.worldX - cx;
      float dz = other.worldZ - cz;
      if (dx * dx + dz * dz <= r2) {
        ApplyDamageToMonster(session, &other, skillDef->damageBonus, world,
                             server, skillDef->isMagic);
        // Main 5.2: Twister/Evil Spirit stuns AoE targets too (only if alive)
        if ((skillDef->skillId == 8 || skillDef->skillId == 9) && other.hp > 0)
          other.stormTime = 10;
        aoeHits++;
      }
    }
  }

  // Send updated stats (so client sees AG decrease)
  CharacterHandler::SendCharStats(session);

  // Server-side GCD: minimum time between skill casts
  session.attackCooldown = 0.3f; // 0.3s minimum between skill attacks
}

void HandleTeleport(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world, Server &server) {
  if (session.dead)
    return;
  if (packet.size() < sizeof(PMSG_SKILL_TELEPORT_RECV))
    return;
  const auto *tp =
      reinterpret_cast<const PMSG_SKILL_TELEPORT_RECV *>(packet.data());

  // Validate skill 6 (Teleport) is learned
  bool hasSkill = false;
  for (auto s : session.learnedSkills) {
    if (s == 6) {
      hasSkill = true;
      break;
    }
  }
  if (!hasSkill)
    return;

  // Look up teleport skill def for mana cost
  const SkillDef *skillDef = FindSkillDef(6);
  if (!skillDef)
    return;

  // Check mana (DW uses mana, not AG)
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool isDK = (charCls == CharacterClass::CLASS_DK);
  int currentResource = isDK ? session.ag : session.mana;
  if (currentResource < skillDef->resourceCost) {
    CharacterHandler::SendCharStats(session);
    return;
  }

  // Block teleport from safe zone
  if (world.IsSafeZone(session.worldX, session.worldZ))
    return;

  // Validate target position is walkable
  uint8_t gx = tp->targetGridX;
  uint8_t gy = tp->targetGridY;
  if (!world.IsWalkableGrid(gx, gy))
    return;

  // Deduct mana
  if (isDK) {
    session.ag -= skillDef->resourceCost;
    session.lastAgUseTime = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
  } else {
    session.mana -= skillDef->resourceCost;
  }

  // Update session position (grid -> world coords)
  session.worldX = (float)gy * 100.0f;
  session.worldZ = (float)gx * 100.0f;

  // Move summon to near teleport destination and broadcast new position
  if (session.activeSummonIndex > 0) {
    auto *summon = world.FindMonster(session.activeSummonIndex);
    if (summon) {
      // Find walkable cell adjacent to teleport destination
      for (int r = 1; r <= 3; r++) {
        bool found = false;
        for (int dy = -r; dy <= r && !found; dy++) {
          for (int dx = -r; dx <= r && !found; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = (int)gx + dx, ny = (int)gy + dy;
            if (nx >= 0 && ny >= 0 && nx < 256 && ny < 256 &&
                world.IsWalkableGrid((uint8_t)nx, (uint8_t)ny)) {
              summon->gridX = (uint8_t)nx;
              summon->gridY = (uint8_t)ny;
              summon->worldX = summon->gridY * 100.0f;
              summon->worldZ = summon->gridX * 100.0f;
              summon->currentPath.clear();
              summon->pathStep = 0;
              summon->lastBroadcastTargetX = (uint8_t)nx;
              summon->lastBroadcastTargetY = (uint8_t)ny;
              found = true;
            }
          }
        }
        if (found) break;
      }

      // Broadcast summon's new position immediately so client snaps it
      PMSG_MONSTER_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::MON_MOVE);
      movePkt.monsterIndex = summon->index;
      movePkt.targetX = summon->gridX;
      movePkt.targetY = summon->gridY;
      movePkt.chasing = 0;
      server.Broadcast(&movePkt, sizeof(movePkt));
    }
  }

  // Send updated stats (mana deducted)
  CharacterHandler::SendCharStats(session);
}

} // namespace CombatHandler

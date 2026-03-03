#include "handlers/CombatHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "PathFinder.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
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

    // Skill damage bonus (flat addition before defense)
    damage += bonusDamage;

    damage = std::max(1, damage - mon->defense);

    // Evading monsters are invulnerable (WoW leash behavior)
    // Cancel evade on re-aggro — this hit does no damage but re-engages
    if (mon->evading) {
      damage = 0;
      damageType = 0; // Show as miss
      mon->evading = false;
      printf("[AI] Mon %d: EVADE cancelled by attacker fd=%d\n", mon->index,
             session.GetFd());
    } else {
      mon->hp -= damage;
    }

    // Aggro logic
    if (mon->aggroTargetFd != session.GetFd()) {
      mon->aggroTargetFd = session.GetFd();
      printf("[AI] Mon %d (type %d): NEW AGGRO on attacker fd=%d (dmg=%d) "
             "monGrid=(%d,%d) playerGrid=(%d,%d)\n",
             mon->index, mon->type, session.GetFd(), damage,
             mon->gridX, mon->gridY,
             (int)(session.worldZ / 100.0f), (int)(session.worldX / 100.0f));
    }
    mon->aggroTimer = 15.0f;
    mon->aiState = MonsterInstance::AIState::CHASING;
    mon->currentPath.clear();
    mon->pathStep = 0;
    mon->repathTimer = 0.0f;
    mon->moveTimer = mon->moveDelay;
    mon->attackCooldown = 0.0f;

    // Pack assist (same-type monsters within 3 cells join aggro)
    if (mon->aggressive) {
      for (auto &ally : world.GetMonsterInstancesMut()) {
        if (ally.index == mon->index)
          continue;
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
        if (ally.chaseFailCount >= 5)
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
          printf("[AI] Mon %d (type %d): PACK ASSIST on attacker fd=%d "
                 "(dist=%d cells from attacked)\n",
                 ally.index, ally.type, session.GetFd(), dist);
        }
      }
    }

    bool killed = mon->hp <= 0;
    if (killed)
      mon->hp = 0;

    if (killed) {
      mon->aiState = MonsterInstance::AIState::DYING;
      mon->stateTimer = 0.0f;

      double baseXP = (mon->level + 25.0) * mon->level / 3.0;
      if (session.level > mon->level + 10) {
        baseXP *= (double)(mon->level + 10) / session.level;
      }
      if (mon->level >= 65) {
        baseXP += (mon->level - 64) * (mon->level / 4);
      }
      int xp = std::max(1, (int)(baseXP * 1.25 * ServerConfig::XP_MULTIPLIER));

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
              charCls2, session.level, session.vitality);
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

      printf("[Combat] Monster %d killed by char %d (dmg=%d, xp=%d, "
             "drops=%zu)\n",
             mon->index, session.characterId, damage, xp, drops.size());
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
  if (packet.size() < sizeof(PMSG_ATTACK_RECV))
    return;
  const auto *atk = reinterpret_cast<const PMSG_ATTACK_RECV *>(packet.data());

  auto *mon = world.FindMonster(atk->monsterIndex);
  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD)
    return;

  ApplyDamageToMonster(session, mon, 0, world, server);
}

// File-based debug log for skill attacks (stdout goes to /dev/null)
static void SkillLog(const char *fmt, ...) {
  FILE *f = fopen("skill_debug.log", "a");
  if (!f)
    return;
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fclose(f);
}

void HandleSkillAttack(Session &session, const std::vector<uint8_t> &packet,
                       GameWorld &world, Server &server) {
  SkillLog("[SkillAttack] ENTER: pktSize=%zu learnedSkills=%zu mana=%d\n",
           packet.size(), session.learnedSkills.size(), session.mana);

  if (packet.size() < sizeof(PMSG_SKILL_ATTACK_RECV)) {
    SkillLog("[SkillAttack] FAIL: packet too small (%zu < %zu)\n",
             packet.size(), sizeof(PMSG_SKILL_ATTACK_RECV));
    return;
  }
  const auto *atk =
      reinterpret_cast<const PMSG_SKILL_ATTACK_RECV *>(packet.data());

  SkillLog("[SkillAttack] monsterIndex=%d skillId=%d\n", atk->monsterIndex,
           atk->skillId);

  // Validate skill is learned
  bool hasSkill = false;
  for (auto s : session.learnedSkills) {
    if (s == atk->skillId) {
      hasSkill = true;
      break;
    }
  }
  if (!hasSkill) {
    SkillLog("[SkillAttack] FAIL: skill %d not learned (have %zu skills)\n",
             atk->skillId, session.learnedSkills.size());
    printf("[Combat] fd=%d tried skill %d but hasn't learned it\n",
           session.GetFd(), atk->skillId);
    return;
  }

  // Look up skill definition
  const SkillDef *skillDef = FindSkillDef(atk->skillId);
  if (!skillDef) {
    SkillLog("[SkillAttack] FAIL: unknown skill %d\n", atk->skillId);
    printf("[Combat] fd=%d unknown skill %d\n", session.GetFd(), atk->skillId);
    return;
  }

  // Check resource (DK uses AG, others use Mana)
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool isDK = (charCls == CharacterClass::CLASS_DK);
  int currentResource = isDK ? session.ag : session.mana;

  if (currentResource < skillDef->resourceCost) {
    SkillLog("[SkillAttack] FAIL: not enough resource (%d < %d) isDK=%d\n",
             currentResource, skillDef->resourceCost, (int)isDK);
    printf("[Combat] fd=%d not enough %s (%d/%d) for skill %d\n",
           session.GetFd(), isDK ? "AG" : "Mana", currentResource,
           skillDef->resourceCost, atk->skillId);
    // Re-sync stats so client has accurate AG value
    CharacterHandler::SendCharStats(session);
    return;
  }

  // Deduct resource
  if (isDK) {
    session.ag -= skillDef->resourceCost;
  } else {
    session.mana -= skillDef->resourceCost;
  }

  SkillLog("[SkillAttack] Resource deducted: %d -> %d\n", currentResource,
           isDK ? session.ag : session.mana);

  // Teleport (skill 6) — no damage, just utility
  if (skillDef->skillId == 6) {
    CharacterHandler::SendCharStats(session);
    printf("[Combat] fd=%d used Teleport (mana cost=%d)\n",
           session.GetFd(), skillDef->resourceCost);
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
        if ((skillDef->skillId == 8 || skillDef->skillId == 9) && other.hp > 0) {
          other.stormTime = 10;
          printf("[Storm] Ground AoE: stunned mon %d HP=%d at (%.0f,%.0f) skill=%d\n",
                 other.index, other.hp, other.worldX, other.worldZ, skillDef->skillId);
        }
        aoeHits++;
      }
    }
    CharacterHandler::SendCharStats(session);
    printf("[Skill] fd=%d ground-cast AoE skill %d player(%.0f,%.0f) target(%.0f,%.0f) hit %d\n",
           session.GetFd(), skillDef->skillId, px, pz, tx, tz, aoeHits);
    return;
  }

  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD) {
    SkillLog("[SkillAttack] FAIL: monster %d not found or dead\n",
             atk->monsterIndex);
    return;
  }

  SkillLog("[SkillAttack] SUCCESS: applying damage bonus=%d to mon %d\n",
           skillDef->damageBonus, atk->monsterIndex);

  // Apply damage with skill bonus to primary target
  ApplyDamageToMonster(session, mon, skillDef->damageBonus, world, server,
                       skillDef->isMagic);

  // Main 5.2: Twister/Evil Spirit applies StormTime=10 AI stun (only if alive)
  if ((skillDef->skillId == 8 || skillDef->skillId == 9) && mon->hp > 0) {
    mon->stormTime = 10;
    printf("[Storm] Stunned primary target mon %d HP=%d at (%.0f,%.0f) skill=%d\n",
           mon->index, mon->hp, mon->worldX, mon->worldZ, skillDef->skillId);
  }

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
    printf("[Combat] Poison applied to mon %d (tick=%d, dur=10s) by fd=%d\n",
           mon->index, mon->poisonDamage, session.GetFd());
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
        if ((skillDef->skillId == 8 || skillDef->skillId == 9) && other.hp > 0) {
          other.stormTime = 10;
          printf("[Storm] AoE stunned mon %d HP=%d at (%.0f,%.0f) skill=%d\n",
                 other.index, other.hp, other.worldX, other.worldZ, skillDef->skillId);
        }
        aoeHits++;
      }
    }
  }

  // Send updated stats (so client sees AG decrease)
  CharacterHandler::SendCharStats(session);

  printf("[Combat] fd=%d used skill %d (AG cost=%d, bonus=%d) on mon %d "
         "(+%d AoE)\n",
         session.GetFd(), atk->skillId, skillDef->resourceCost, skillDef->damageBonus,
         atk->monsterIndex, aoeHits);
}

void HandleTeleport(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world) {
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
  if (!hasSkill) {
    printf("[Teleport] fd=%d hasn't learned Teleport\n", session.GetFd());
    return;
  }

  // Look up teleport skill def for mana cost
  const SkillDef *skillDef = FindSkillDef(6);
  if (!skillDef)
    return;

  // Check mana (DW uses mana, not AG)
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool isDK = (charCls == CharacterClass::CLASS_DK);
  int currentResource = isDK ? session.ag : session.mana;
  if (currentResource < skillDef->resourceCost) {
    printf("[Teleport] fd=%d not enough mana (%d/%d)\n", session.GetFd(),
           currentResource, skillDef->resourceCost);
    CharacterHandler::SendCharStats(session);
    return;
  }

  // Block teleport from safe zone
  if (world.IsSafeZone(session.worldX, session.worldZ)) {
    printf("[Teleport] fd=%d blocked — in safe zone\n", session.GetFd());
    return;
  }

  // Validate target position is walkable
  uint8_t gx = tp->targetGridX;
  uint8_t gy = tp->targetGridY;
  if (!world.IsWalkableGrid(gx, gy)) {
    printf("[Teleport] fd=%d target (%d,%d) not walkable\n", session.GetFd(),
           gx, gy);
    return;
  }

  // Deduct mana
  if (isDK) {
    session.ag -= skillDef->resourceCost;
  } else {
    session.mana -= skillDef->resourceCost;
  }

  // Update session position (grid -> world coords)
  session.worldX = (float)gy * 100.0f;
  session.worldZ = (float)gx * 100.0f;

  printf("[Teleport] fd=%d teleported to grid (%d,%d) world (%.0f,%.0f)\n",
         session.GetFd(), gx, gy, session.worldX, session.worldZ);

  // Send updated stats (mana deducted)
  CharacterHandler::SendCharStats(session);
}

} // namespace CombatHandler

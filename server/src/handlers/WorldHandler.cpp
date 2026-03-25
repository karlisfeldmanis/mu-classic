#include "handlers/WorldHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace WorldHandler {

void SendWelcome(Session &session) {
  PMSG_WELCOME_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::AUTH, Opcode::SUB_WELCOME);
  pkt.result = 0x01;
  session.Send(&pkt, sizeof(pkt));
  printf("[World] Sent welcome to fd=%d\n", session.GetFd());
}

void SendNpcViewport(Session &session, const GameWorld &world) {
  auto packet = world.BuildNpcViewportPacket();
  if (packet.empty())
    return;
  session.Send(packet.data(), packet.size());
  printf("[World] Sent %zu NPC viewport entries to fd=%d\n",
         world.GetNpcs().size(), session.GetFd());
}

void SendMonsterViewport(Session &session, const GameWorld &world) {
  auto packet = world.BuildMonsterViewportPacket();
  if (packet.empty())
    return;
  session.Send(packet.data(), packet.size());
  printf("[World] Sent %zu monster viewport entries to fd=%d\n",
         world.GetMonsterInstances().size(), session.GetFd());
}

void HandleMove(Session &session, const std::vector<uint8_t> &packet,
                Database &db) {
  if (packet.size() < sizeof(PMSG_MOVE_RECV))
    return;
  const auto *move = reinterpret_cast<const PMSG_MOVE_RECV *>(packet.data());

  // Don't update worldX/worldZ here — D7 PrecisePosition is the authoritative
  // source for player position. D4 only provides grid-quantized coordinates and
  // updating from it causes the server to briefly use a stale/wrong position
  // (race with D7), breaking monster AI distance checks and safe zone validation.

  // Player started walking — reset idle regen timer
  session.idleTimer = 0.0f;
  session.idleHpRemainder = 0.0f;

  // Cancel summon attack target — summon should follow instead of fighting
  session.attackTargetMonsterIdx = 0;

  if (session.characterId > 0) {
    db.UpdatePosition(session.characterId, move->x, move->y);
  }
}

void HandlePrecisePosition(Session &session,
                           const std::vector<uint8_t> &packet,
                           GameWorld &world) {
  if (packet.size() < sizeof(PMSG_PRECISE_POS_RECV))
    return;
  const auto *pos =
      reinterpret_cast<const PMSG_PRECISE_POS_RECV *>(packet.data());
  // Reset idle timer if position actually changed
  if (std::abs(session.worldX - pos->worldX) > 1.0f ||
      std::abs(session.worldZ - pos->worldZ) > 1.0f) {
    session.idleTimer = 0.0f;
    session.idleHpRemainder = 0.0f;
  }
  session.worldX = pos->worldX;
  session.worldZ = pos->worldZ;
  // PrecisePos logging suppressed (fires ~20/s, causes log spam)

  // If client just loaded a new map (pending viewport), send NPCs/monsters now.
  // Guard: only trigger if at least 0.5s has passed since TransitionMap
  // (pendingViewportDelay starts at 5.0f). This prevents stale PrecisePosition
  // packets (sent before client received MAP_CHANGE) from triggering viewport.
  if (session.pendingViewportDelay > 0.0f && session.pendingViewportDelay < 4.5f) {
    session.pendingViewportDelay = 0.0f;
    SendNpcViewport(session, world);
    auto v2pkt = world.BuildMonsterViewportV2Packet();
    if (!v2pkt.empty())
      session.Send(v2pkt.data(), v2pkt.size());
    printf("[PrecisePos] Viewport sent on client ready: %zu NPCs, %zu monsters\n",
           world.GetNpcs().size(), world.GetMonsterInstances().size());

    // Re-send SUMMON_SPAWN after viewport so client can mark the summon.
    // The original SUMMON_SPAWN arrived before ClearMonsters() wiped tracking.
    if (session.activeSummonIndex > 0 && session.activeSummonType >= 0) {
      PMSG_SUMMON_SPAWN_SEND spkt{};
      spkt.h = MakeC1Header(sizeof(spkt), Opcode::SUMMON_SPAWN);
      spkt.monsterIndex = session.activeSummonIndex;
      spkt.ownerCharId = session.characterId;
      spkt.level = session.level;
      session.Send(&spkt, sizeof(spkt));
      printf("[PrecisePos] Re-sent SUMMON_SPAWN index=%d for fd=%d\n",
             session.activeSummonIndex, session.GetFd());
    }
  }
}

void HandleLogin(Session &session, const std::vector<uint8_t> &packet,
                 Database &db) {
  if (packet.size() < sizeof(PMSG_LOGIN_RECV))
    return;

  PMSG_LOGIN_RECV login;
  std::memcpy(&login, packet.data(), sizeof(login));

  BuxDecode(login.account, 10);
  BuxDecode(login.password, 20);

  char account[11] = {};
  char password[21] = {};
  std::memcpy(account, login.account, 10);
  std::memcpy(password, login.password, 20);

  printf("[World] Login attempt: account='%s' from fd=%d\n", account,
         session.GetFd());

  int accountId = db.ValidateLogin(account, password);

  PMSG_LOGIN_RESULT_SEND result{};
  result.h = MakeC1SubHeader(sizeof(result), Opcode::AUTH, Opcode::SUB_LOGIN);

  if (accountId > 0) {
    result.result = 0x01;
    session.accountId = accountId;
    printf("[World] Login success: accountId=%d\n", accountId);
  } else {
    result.result = 0x00;
    printf("[World] Login failed for '%s'\n", account);
  }

  session.Send(&result, sizeof(result));
}

void HandleCharListRequest(Session &session, Database &db) {
  auto chars = db.GetCharacterList(session.accountId);

  size_t totalSize =
      sizeof(PMSG_CHARLIST_HEAD) + chars.size() * sizeof(PMSG_CHARLIST_ENTRY);
  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PMSG_CHARLIST_HEAD *>(packet.data());
  head->h = MakeC1SubHeader(static_cast<uint8_t>(totalSize), Opcode::CHARSELECT,
                            Opcode::SUB_CHARLIST);
  head->classCode = 0;
  head->moveCnt = 0;
  head->count = static_cast<uint8_t>(chars.size());

  auto *entries = reinterpret_cast<PMSG_CHARLIST_ENTRY *>(
      packet.data() + sizeof(PMSG_CHARLIST_HEAD));
  for (size_t i = 0; i < chars.size(); i++) {
    auto &e = entries[i];
    e.slot = static_cast<uint8_t>(chars[i].slot);
    std::memset(e.name, 0, 10);
    std::strncpy(e.name, chars[i].name.c_str(), 10);
    SetWordBE(reinterpret_cast<uint8_t *>(&e.level), chars[i].level);
    e.ctlCode = 0;
    std::memset(e.charSet, 0, 18);
    e.charSet[0] = static_cast<uint8_t>((chars[i].charClass >> 4) << 4);
    e.guildStatus = 0xFF;
  }

  session.Send(packet.data(), packet.size());
  printf("[World] Sent char list (%zu chars) to fd=%d\n", chars.size(),
         session.GetFd());
}

void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world) {
  if (packet.size() < sizeof(PMSG_CHARSELECT_RECV))
    return;
  const auto *sel =
      reinterpret_cast<const PMSG_CHARSELECT_RECV *>(packet.data());

  char name[11] = {};
  std::memcpy(name, sel->name, 10);
  printf("[World] Char select: '%s' from fd=%d\n", name, session.GetFd());

  CharacterData c = db.GetCharacter(name);
  if (c.id == 0) {
    printf("[World] Character '%s' not found\n", name);
    return;
  }

  session.characterId = c.id;
  session.characterName = c.name;
  session.charClass = c.charClass;
  session.classCode = c.charClass;

  // Send character info
  PMSG_CHARINFO_SEND info{};
  info.h =
      MakeC1SubHeader(sizeof(info), Opcode::CHARSELECT, Opcode::SUB_CHARSELECT);
  info.x = c.posX;
  info.y = c.posY;
  info.map = c.mapId;
  info.dir = c.direction;
  info.level = c.level;
  SetDwordBE(info.experience, static_cast<uint32_t>(c.experience >> 32));
  SetDwordBE(info.experience + 4,
             static_cast<uint32_t>(c.experience & 0xFFFFFFFF));
  SetDwordBE(info.nextExperience, 0);
  SetDwordBE(info.nextExperience + 4, 1000);
  info.levelUpPoint = c.levelUpPoints;
  info.strength = c.strength;
  info.dexterity = c.dexterity;
  info.vitality = c.vitality;
  info.energy = c.energy;
  info.life = c.life;
  info.maxLife = c.maxLife;
  info.mana = c.mana;
  info.maxMana = c.maxMana;
  info.shield = 0;
  info.maxShield = 0;
  info.bp = c.ag;
  info.maxBP = c.maxAg;
  info.money = c.money;
  info.pkLevel = 3;
  info.ctlCode = 0;
  info.fruitAddPoint = 0;
  info.maxFruitAddPoint = 0;
  info.leadership = 0;
  info.fruitSubPoint = 0;
  info.maxFruitSubPoint = 0;
  info.rmcSkillId = c.rmcSkillId;

  session.Send(&info, sizeof(info));
  session.inWorld = true;

  // Cache stats
  session.mapId = c.mapId;
  session.strength = c.strength;
  session.dexterity = c.dexterity;
  session.vitality = c.vitality;
  session.energy = c.energy;
  session.level = c.level;
  session.levelUpPoints = c.levelUpPoints;
  session.experience = c.experience;
  session.worldX = c.posY * 100.0f;
  session.worldZ = c.posX * 100.0f;
  session.wasInSafeZone = world.IsSafeZoneGrid(c.posX, c.posY);

  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  session.maxHp =
      StatCalculator::CalculateMaxHP(charCls, session.level, session.vitality) +
      session.petBonusMaxHp;
  session.hp = std::min(static_cast<int>(c.life), session.maxHp);
  if (session.hp <= 0)
    session.hp = session.maxHp;
  session.maxMana = StatCalculator::CalculateMaxManaOrAG(
      charCls, session.level, session.strength, session.dexterity,
      session.vitality, session.energy);
  session.mana = std::min(static_cast<int>(c.mana), session.maxMana);
  if (session.mana <= 0)
    session.mana = session.maxMana;

  if (charCls == CharacterClass::CLASS_DK) {
    session.maxAg = StatCalculator::CalculateMaxAG(c.strength, c.dexterity,
                                                   c.vitality, c.energy);
  } else {
    session.maxAg = StatCalculator::CalculateMaxManaOrAG(
        charCls, session.level, session.strength, session.dexterity,
        session.vitality, session.energy);
  }
  session.ag = std::min(static_cast<int>(c.ag), session.maxAg);

  // Use shared RefreshCombatStats instead of duplicated code
  CharacterHandler::RefreshCombatStats(session, db, c.id);

  // Load inventory from DB
  session.zen = c.money;
  InventoryHandler::LoadInventory(session, db, c.id);

  // Load learned skills from DB
  session.learnedSkills = db.GetCharacterSkills(c.id);

  // If character is on a non-default map, reload world data for that map
  if (c.mapId != 0) {
    world.ClearWorldData();
    world.SetActiveMap(c.mapId);
    world.LoadNpcsFromDB(db, c.mapId);
    world.LoadMonstersFromDB(db, c.mapId);

    // Tell client to load the correct map
    PMSG_MAP_CHANGE_SEND mapPkt{};
    mapPkt.h = MakeC1Header(sizeof(mapPkt), Opcode::MAP_CHANGE);
    mapPkt.mapId = c.mapId;
    mapPkt.spawnX = c.posX;
    mapPkt.spawnY = c.posY;
    session.Send(&mapPkt, sizeof(mapPkt));
  }

  SendNpcViewport(session, world);
  InventoryHandler::SendInventorySync(session);
  CharacterHandler::SendCharStats(session, db, session.characterId);

  // Send skill list to client
  CharacterHandler::SendSkillList(session);

  // Re-send active buffs (elf auras) so client restores visual effects
  for (int b = 0; b < 2; b++) {
    if (session.buffs[b].active && session.buffs[b].remaining > 0.0f) {
      PMSG_BUFF_EFFECT_SEND bpkt{};
      bpkt.h = MakeC1Header(sizeof(bpkt), Opcode::BUFF_EFFECT);
      bpkt.buffType = session.buffs[b].type;
      bpkt.active = 1;
      bpkt.value = static_cast<uint16_t>(session.buffs[b].value);
      bpkt.duration = session.buffs[b].remaining;
      session.Send(&bpkt, sizeof(bpkt));
    }
  }

  printf("[World] Character '%s' entered Lorencia at (%d,%d) STR=%d "
         "weapon=%d-%d def=%d zen=%u skills=%zu\n",
         name, c.posX, c.posY, session.strength, session.weaponDamageMin,
         session.weaponDamageMax, session.totalDefense, session.zen,
         session.learnedSkills.size());
}

} // namespace WorldHandler

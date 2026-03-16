#include "handlers/CharacterSelectHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include "handlers/QuestHandler.hpp"
#include "handlers/WorldHandler.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace CharacterSelectHandler {

void SendCharList(Session &session, Database &db) {
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
    e.classCode = chars[i].charClass;
    e.ctlCode = 0;
    std::memset(e.charSet, 0xFF, 20);
    e.charSet[0] = static_cast<uint8_t>((chars[i].charClass >> 4) << 4);
    e.charSet[15] = 0xFF; // Slot 8 category (pet/mount) — 0xFF = empty
    e.charSet[16] = 0;    // Slot 8 itemIndex
    e.charSet[17] = 0xFF; // Slot 7 wing category — 0xFF = empty
    e.charSet[18] = 0;    // Slot 7 wing itemIndex
    e.charSet[19] = 0;
    std::memset(e.equipLevels, 0, 8);

    // Encode equipment appearance into charSet[1..14]
    // Layout: [1..2]=rightHand, [3..4]=leftHand, [5..6]=helm,
    //         [7..8]=armor, [9..10]=pants, [11..12]=gloves, [13..14]=boots
    // charSet[15..16] = slot 8 (pet/mount): category, itemIndex
    auto equip = db.GetCharacterEquipment(chars[i].id);
    for (auto &eq : equip) {
      int offset = -1;
      int lvlIdx = -1;
      switch (eq.slot) {
      case 0: offset = 1;  lvlIdx = 0; break;  // Right hand
      case 1: offset = 3;  lvlIdx = 1; break;  // Left hand
      case 2: offset = 5;  lvlIdx = 2; break;  // Helm
      case 3: offset = 7;  lvlIdx = 3; break;  // Armor
      case 4: offset = 9;  lvlIdx = 4; break;  // Pants
      case 5: offset = 11; lvlIdx = 5; break;  // Gloves
      case 6: offset = 13; lvlIdx = 6; break;  // Boots
      case 7:                                    // Wings (slot 7)
        e.charSet[17] = eq.category;
        e.charSet[18] = eq.itemIndex;
        e.equipLevels[7] = eq.itemLevel;
        break;
      case 8:                                    // Pet/mount (slot 8)
        e.charSet[15] = eq.category;
        e.charSet[16] = eq.itemIndex;
        break;
      }
      if (offset >= 0) {
        e.charSet[offset] = eq.category;
        e.charSet[offset + 1] = eq.itemIndex;
      }
      if (lvlIdx >= 0)
        e.equipLevels[lvlIdx] = eq.itemLevel;
    }
    e.guildStatus = 0xFF;
  }

  session.Send(packet.data(), packet.size());
  printf("[CharSelect] Sent char list (%zu chars) to fd=%d\n", chars.size(),
         session.GetFd());
}

void HandleCharCreate(Session &session, const std::vector<uint8_t> &packet,
                      Database &db) {
  if (packet.size() < sizeof(PMSG_CHARCREATE_RECV))
    return;
  const auto *req =
      reinterpret_cast<const PMSG_CHARCREATE_RECV *>(packet.data());

  char name[11] = {};
  std::memcpy(name, req->name, 10);
  uint8_t classCode = req->classCode;

  printf("[CharSelect] Create request: name='%s' class=%d from fd=%d\n", name,
         classCode, session.GetFd());

  PMSG_CHARCREATE_RESULT result{};
  result.h = MakeC1SubHeader(sizeof(result), Opcode::CHARSELECT,
                             Opcode::SUB_CHARCREATE);
  std::memcpy(result.name, name, 10);
  result.classCode = classCode;
  result.level = 1;

  // Validate name length (4-10 chars)
  int nameLen = static_cast<int>(strlen(name));
  if (nameLen < 4 || nameLen > 10) {
    result.result = 0; // fail
    session.Send(&result, sizeof(result));
    printf("[CharSelect] Name too short/long: '%s'\n", name);
    return;
  }

  // Check if name taken
  if (db.CharacterNameExists(name)) {
    result.result = 2; // name taken
    session.Send(&result, sizeof(result));
    printf("[CharSelect] Name already taken: '%s'\n", name);
    return;
  }

  // Validate class code
  if (classCode != 0 && classCode != 16 && classCode != 32 &&
      classCode != 48) {
    result.result = 0;
    session.Send(&result, sizeof(result));
    return;
  }

  int charId = db.CreateCharacter(session.accountId, name, classCode);
  if (charId < 0) {
    result.result = 0;
    session.Send(&result, sizeof(result));
    return;
  }

  // DW (class 0) auto-learns Energy Ball (skill 17) on creation and equips it
  if (classCode == 0) {
    db.LearnSkill(charId, 17);
    db.SetRmcSkillId(charId, 17);
    printf("[CharSelect] DW '%s' auto-learned & equipped Energy Ball (skill 17)\n", name);
  }

  // DK (class 16) starts with Small Axe (cat=1,idx=0) + Small Shield (cat=6,idx=0)
  if (classCode == 16) {
    db.UpdateEquipment(charId, 0, 1, 0, 0); // Right hand: Small Axe
    db.UpdateEquipment(charId, 1, 6, 0, 0); // Left hand: Small Shield
    printf("[CharSelect] DK '%s' equipped Small Axe + Small Shield\n", name);
  }

  // Find the slot that was assigned
  auto chars = db.GetCharacterList(session.accountId);
  for (auto &c : chars) {
    if (c.id == charId) {
      result.slot = static_cast<uint8_t>(c.slot);
      break;
    }
  }

  result.result = 1; // success
  session.Send(&result, sizeof(result));

  // Send updated character list
  SendCharList(session, db);
}

void HandleCharDelete(Session &session, const std::vector<uint8_t> &packet,
                      Database &db) {
  if (packet.size() < sizeof(PMSG_CHARDELETE_RECV))
    return;
  const auto *req =
      reinterpret_cast<const PMSG_CHARDELETE_RECV *>(packet.data());

  char name[11] = {};
  std::memcpy(name, req->name, 10);
  uint8_t slot = req->slot;

  printf("[CharSelect] Delete request: slot=%d name='%s' from fd=%d\n", slot,
         name, session.GetFd());

  PMSG_CHARDELETE_RESULT result{};
  result.h = MakeC1SubHeader(sizeof(result), Opcode::CHARSELECT,
                             Opcode::SUB_CHARDELETE);

  // Find character by slot for this account
  auto chars = db.GetCharacterList(session.accountId);
  int charId = -1;
  for (auto &c : chars) {
    if (c.slot == slot && c.name == name) {
      charId = c.id;
      break;
    }
  }

  if (charId < 0 || !db.DeleteCharacter(session.accountId, charId)) {
    result.result = 0;
    session.Send(&result, sizeof(result));
    return;
  }

  result.result = 1;
  session.Send(&result, sizeof(result));

  // Send updated character list
  SendCharList(session, db);
}

void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world, Server &server) {
  if (packet.size() < sizeof(PMSG_CHARSELECT_RECV))
    return;
  const auto *sel =
      reinterpret_cast<const PMSG_CHARSELECT_RECV *>(packet.data());

  char name[11] = {};
  std::memcpy(name, sel->name, 10);
  printf("[CharSelect] Select: '%s' from fd=%d\n", name, session.GetFd());

  CharacterData c = db.GetCharacter(name);
  if (c.id == 0) {
    printf("[CharSelect] Character '%s' not found\n", name);
    return;
  }

  // Verify character belongs to this account
  if (c.accountId != session.accountId) {
    printf("[CharSelect] Character '%s' doesn't belong to account %d\n", name,
           session.accountId);
    return;
  }

  // Save old character before switching (prevent stat contamination)
  if (session.characterId > 0 && session.characterId != c.id && session.inWorld) {
    server.SaveSession(session);
    printf("[CharSelect] Saved old character %d before switching to %d\n",
           session.characterId, c.id);
  }

  // Clear old equipment cache to prevent bleed-through
  for (int i = 0; i < Session::NUM_EQUIP_SLOTS; i++) {
    session.equipment[i] = {};
    session.equipment[i].category = 0xFF;
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
  info.rmcSkillId = c.rmcSkillId;

  session.Send(&info, sizeof(info));
  session.inWorld = true;

  // Cache stats
  session.strength = c.strength;
  session.dexterity = c.dexterity;
  session.vitality = c.vitality;
  session.energy = c.energy;
  session.level = c.level;
  session.levelUpPoints = c.levelUpPoints;
  session.experience = c.experience;
  session.worldX = c.posY * 100.0f;
  session.worldZ = c.posX * 100.0f;
  session.mapId = c.mapId;

  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  session.maxHp =
      StatCalculator::CalculateMaxHP(charCls, session.level, session.vitality);
  session.hp = std::min(static_cast<int>(c.life), session.maxHp);
  // Prevent loading with 0 HP (dead state from previous session)
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
  session.dead = false;
  memcpy(session.skillBar, c.skillBar, 10);
  memcpy(session.potionBar, c.potionBar, 8);
  session.rmcSkillId = c.rmcSkillId;

  // Load equipment into session cache
  {
    auto equip = db.GetCharacterEquipment(c.id);
    for (auto &e : equip) {
      if (e.slot < Session::NUM_EQUIP_SLOTS) {
        session.equipment[e.slot].category = e.category;
        session.equipment[e.slot].itemIndex = e.itemIndex;
        session.equipment[e.slot].itemLevel = e.itemLevel;
      }
    }
  }

  CharacterHandler::RefreshCombatStats(session, db, c.id);

  // Load inventory from DB
  session.zen = c.money;
  InventoryHandler::LoadInventory(session, db, c.id);

  // Load learned skills from DB
  session.learnedSkills = db.GetCharacterSkills(c.id);

  // Always reload world data for the character's current map
  world.ClearWorldData();
  world.SetActiveMap(c.mapId);
  world.LoadNpcsFromDB(db, c.mapId);
  world.LoadMonstersFromDB(db, c.mapId);

  // Notify client of non-default map (client starts in Lorencia by default)
  if (c.mapId != 0) {
    PMSG_MAP_CHANGE_SEND mapPkt{};
    mapPkt.h = MakeC1Header(sizeof(mapPkt), Opcode::MAP_CHANGE);
    mapPkt.mapId = c.mapId;
    mapPkt.spawnX = c.posX;
    mapPkt.spawnY = c.posY;
    session.Send(&mapPkt, sizeof(mapPkt));
  }

  // Send world data
  WorldHandler::SendNpcViewport(session, world);

  // Send v2 monster viewport
  auto v2pkt = world.BuildMonsterViewportV2Packet();
  if (!v2pkt.empty())
    session.Send(v2pkt.data(), v2pkt.size());

  InventoryHandler::SendInventorySync(session);
  // IMPORTANT: Send CharStats BEFORE Equipment so the client knows the class
  // before body parts are equipped (LoadStats class-change resets body parts)
  CharacterHandler::SendCharStats(session, db, session.characterId);
  CharacterHandler::SendSkillList(session);
  CharacterHandler::SendEquipment(session, db, c.id);

  // Send existing ground drops
  for (auto &drop : world.GetDrops()) {
    PMSG_DROP_SPAWN_SEND dpkt{};
    dpkt.h = MakeC1Header(sizeof(dpkt), Opcode::DROP_SPAWN);
    dpkt.dropIndex = drop.index;
    dpkt.defIndex = drop.defIndex;
    dpkt.quantity = drop.quantity;
    dpkt.itemLevel = drop.itemLevel;
    dpkt.worldX = drop.worldX;
    dpkt.worldZ = drop.worldZ;
    session.Send(&dpkt, sizeof(dpkt));
  }

  // Send chat log history
  {
    auto history = db.GetChatHistory(c.id, 200);
    if (!history.empty()) {
      // Build C2 variable-length packet:
      // C2 header (4 bytes) + count(uint16_t) + entries
      // Each entry: category(1) + color(4) + msgLen(1) + msg[msgLen]
      std::vector<uint8_t> buf;
      buf.resize(6); // header(4) + count(2)
      uint16_t count = (uint16_t)history.size();
      buf[4] = (uint8_t)(count >> 8);
      buf[5] = (uint8_t)(count & 0xFF);
      for (auto &e : history) {
        buf.push_back(e.category);
        buf.push_back((uint8_t)(e.color & 0xFF));
        buf.push_back((uint8_t)((e.color >> 8) & 0xFF));
        buf.push_back((uint8_t)((e.color >> 16) & 0xFF));
        buf.push_back((uint8_t)((e.color >> 24) & 0xFF));
        uint8_t len = (uint8_t)std::min((int)e.message.size(), 200);
        buf.push_back(len);
        for (int i = 0; i < len; i++)
          buf.push_back((uint8_t)e.message[i]);
      }
      // Fill C2 header
      uint16_t pktSize = (uint16_t)buf.size();
      buf[0] = 0xC2;
      buf[1] = (uint8_t)(pktSize >> 8);
      buf[2] = (uint8_t)(pktSize & 0xFF);
      buf[3] = Opcode::CHAT_LOG_HISTORY;
      session.Send(buf.data(), buf.size());
      printf("[ChatLog] Sent %d history entries to char %d\n", (int)count, c.id);
    }
  }

  // Load quest state (both Lorencia + Devias chains) and send to client
  {
    auto qs = db.LoadQuestState(c.id);
    session.questIndex = qs.questIndex;
    session.questKillCount0 = qs.killCount0;
    session.questKillCount1 = qs.killCount1;
    session.questKillCount2 = qs.killCount2;
    session.questAccepted = qs.accepted;
    session.deviasQuestIndex = qs.deviasQuestIndex;
    session.deviasKillCount0 = qs.deviasKc0;
    session.deviasKillCount1 = qs.deviasKc1;
    session.deviasKillCount2 = qs.deviasKc2;
    session.deviasQuestAccepted = qs.deviasAccepted;
    QuestHandler::SendQuestState(session);
  }

  printf("[CharSelect] Character '%s' entered world at (%d,%d) class=%d\n",
         name, c.posX, c.posY, c.charClass);
}

} // namespace CharacterSelectHandler

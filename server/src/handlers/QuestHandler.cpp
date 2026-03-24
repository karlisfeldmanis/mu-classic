#include "handlers/QuestHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include <cstdio>
#include <cstring>

// ═══════════════════════════════════════════════════════
// Quest definitions — loaded from DB at startup
// ═══════════════════════════════════════════════════════

static std::vector<Database::QuestDefData> g_quests;

// Compute recommended level from the highest-level target monster.
// Always reflects actual MonsterTypeDef data — no manual sync needed.
static uint8_t GetQuestRecommendedLevel(const Database::QuestDefData &q) {
  int maxLevel = 1;
  for (int t = 0; t < q.targetCount; t++) {
    const auto *def =
        GameWorld::FindMonsterTypeDef(q.targets[t].monsterType);
    if (def && def->level > maxLevel)
      maxLevel = def->level;
  }
  return (uint8_t)maxLevel;
}

// Compute zen reward from monster stats.
// Formula: sum of (monster_hp * kills * 2) per target, minimum 500.
static uint32_t GetQuestZenReward(const Database::QuestDefData &q) {
  uint32_t zen = 0;
  for (int t = 0; t < q.targetCount; t++) {
    const auto *def =
        GameWorld::FindMonsterTypeDef(q.targets[t].monsterType);
    if (def)
      zen += (uint32_t)def->hp * q.targets[t].killsRequired * 2;
  }
  return std::max(zen, (uint32_t)500);
}

// Compute XP reward from monster stats.
// Formula: sum of (monster_level^2 * kills * 300) per target, minimum 5000.
static uint32_t GetQuestXpReward(const Database::QuestDefData &q) {
  uint32_t xp = 0;
  for (int t = 0; t < q.targetCount; t++) {
    const auto *def =
        GameWorld::FindMonsterTypeDef(q.targets[t].monsterType);
    if (def)
      xp += (uint32_t)(def->level * def->level) * q.targets[t].killsRequired *
            300;
  }
  return std::max(xp, (uint32_t)5000);
}

// ═══════════════════════════════════════════════════════
// Helper: insert item reward into player inventory
// ═══════════════════════════════════════════════════════

// Check if a reward item can fit in inventory (without placing it)
static bool CanFitItemReward(Session &session, Database &db,
                             const Database::QuestRewardData &reward) {
  if (reward.defIndex < 0)
    return true; // No item = always fits

  uint8_t cat = reward.defIndex / 32;
  uint8_t idx = reward.defIndex % 32;
  auto def = db.GetItemDefinition(cat, idx);
  if (def.name.empty())
    return true; // Unknown item, skip silently

  uint8_t outSlot = 0;
  return InventoryHandler::FindEmptySpace(session, def.width, def.height,
                                          outSlot);
}

static void GiveItemReward(Session &session, Database &db,
                           const Database::QuestRewardData &reward) {
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
// Initialize — load quest definitions from DB
// ═══════════════════════════════════════════════════════

void Init(Database &db) {
  g_quests = db.LoadAllQuests();
  printf("[Quest] Loaded %d quest definitions from DB\n", (int)g_quests.size());
}

// ═══════════════════════════════════════════════════════
// Send quest catalog — all quest definitions (C2 packet)
// ═══════════════════════════════════════════════════════

void SendQuestCatalog(Session &session) {
  int questCount = (int)g_quests.size();
  uint16_t totalSize = (uint16_t)(sizeof(PWMSG_HEAD) + 1 +
                                   questCount * sizeof(PMSG_QUEST_CATALOG_ENTRY));
  std::vector<uint8_t> buf(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(buf.data());
  *head = MakeC2Header(totalSize, Opcode::QUEST_CATALOG);
  buf[sizeof(PWMSG_HEAD)] = (uint8_t)questCount;

  for (int i = 0; i < questCount; i++) {
    auto *e = reinterpret_cast<PMSG_QUEST_CATALOG_ENTRY *>(
        buf.data() + sizeof(PWMSG_HEAD) + 1 +
        i * sizeof(PMSG_QUEST_CATALOG_ENTRY));
    const auto &q = g_quests[i];
    e->questId = (uint8_t)q.questId;
    e->guardNpcType = q.guardNpcType;
    e->targetCount = (uint8_t)q.targetCount;
    for (int t = 0; t < 3; t++) {
      e->targets[t].monsterType = q.targets[t].monsterType;
      e->targets[t].killsRequired = q.targets[t].killsRequired;
      // Derive target name from MonsterTypeDef
      const char *monName = "";
      if (t < q.targetCount) {
        const auto *mdef =
            GameWorld::FindMonsterTypeDef(q.targets[t].monsterType);
        if (mdef)
          monName = mdef->name;
      }
      strncpy(e->targets[t].name, monName, 23);
      e->targets[t].name[23] = '\0';
    }
    strncpy(e->questName, q.questName.c_str(), 31);
    e->questName[31] = '\0';
    strncpy(e->location, q.location.c_str(), 15);
    e->location[15] = '\0';
    e->recommendedLevel = GetQuestRecommendedLevel(q);
    e->zenReward = GetQuestZenReward(q);
    e->xpReward = GetQuestXpReward(q);
    for (int c = 0; c < 4; c++)
      for (int s = 0; s < 2; s++) {
        e->classReward[c][s].defIndex = q.classReward[c][s].defIndex;
        e->classReward[c][s].itemLevel = q.classReward[c][s].itemLevel;
      }
    strncpy(e->loreText, q.loreText.c_str(), 255);
    e->loreText[255] = '\0';
  }

  session.Send(buf.data(), buf.size());
  printf("[Quest] Sent catalog (%d quests, %d bytes) to fd=%d\n",
         questCount, (int)totalSize, session.GetFd());
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
  int questCount = (int)g_quests.size();

  if (questId < 0 || questId >= questCount) {
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
  int questCount = (int)g_quests.size();

  if (questId < 0 || questId >= questCount) {
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

  // Pre-check inventory space for ALL item rewards before awarding anything
  int classIdx = session.classCode / 16; // DW=0, DK=1, ELF=2, MG=3
  if (classIdx >= 0 && classIdx < 4) {
    if (!CanFitItemReward(session, db, q.classReward[classIdx][0]) ||
        !CanFitItemReward(session, db, q.classReward[classIdx][1])) {
      printf("[Quest] fd=%d quest %d REJECTED: inventory full for rewards\n",
             session.GetFd(), questId);
      server.GetDB().SaveChatMessage(session.characterId, 1, 0xFFFF6464,
          "Inventory full! Make space before turning in quest.");
      return;
    }
  }

  // Compute rewards from monster data
  uint32_t zenAward = GetQuestZenReward(q);
  uint32_t xpAward = GetQuestXpReward(q);

  // Award zen + XP
  session.zen += zenAward;
  session.experience += xpAward;

  // Give class-specific rewards (space already verified)
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
  reward.zenReward = zenAward;
  reward.xpReward = xpAward;
  reward.questId = (uint8_t)questId;
  session.Send(&reward, sizeof(reward));

  SendQuestState(session);
  InventoryHandler::SendInventorySync(session);
  CharacterHandler::SendCharStats(session);

  char buf[128];
  snprintf(buf, sizeof(buf), "Quest complete! +%u Zen, +%u Experience",
           zenAward, xpAward);
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
         session.GetFd(), questId, zenAward, xpAward);
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
  int questCount = (int)g_quests.size();

  if (questId < 0 || questId >= questCount) {
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
  int questCount = (int)g_quests.size();
  for (auto &aq : session.activeQuests) {
    if (aq.questId < 0 || aq.questId >= questCount)
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

#include "handlers/QuestHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include <cstdio>

// ═══════════════════════════════════════════════════════
// Quest Chain — 9 quests: 5 kill + 4 travel, linear chain across 5 guards
// ═══════════════════════════════════════════════════════

struct QuestKillTarget {
  uint8_t monsterType;
  uint8_t killsRequired;
};

struct QuestItemReward {
  int16_t defIndex;  // -1 = no item
  uint8_t itemLevel; // Enhancement level (+0, +1, etc.)
};

struct QuestDef {
  uint16_t guardNpcType;  // Kill: quest giver. Travel: destination guard
  uint8_t questType;      // 0=kill, 1=travel
  uint8_t targetCount;    // Number of kill targets (0 for travel)
  QuestKillTarget targets[3];
  uint32_t zenReward;
  uint32_t xpReward;
  QuestItemReward dkReward; // {-1,0} = no item
  QuestItemReward dwReward; // {-1,0} = no item
  QuestItemReward orbReward;    // DK skill orb
  QuestItemReward scrollReward; // DW spell scroll
};

static constexpr int QUEST_COUNT = 18;

static const QuestDef g_quests[QUEST_COUNT] = {
    // ── Lorencia quest chain (0-8) ──
    // Quest 0 (Kill): Kael — Spider + Budge Dragon
    {248, 0, 2, {{3, 10}, {2, 5}, {0, 0}}, 5000, 60000, {0, 0}, {160, 0}, {404, 0}, {483, 0}},
    // Quest 1 (Travel): → Corporal Brynn
    {246, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 3000, 30000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 2 (Kill): Brynn — Bull Fighter + Hound
    {246, 0, 2, {{0, 8}, {1, 6}, {0, 0}}, 10000, 100000, {1, 2}, {160, 2}, {405, 0}, {482, 0}},
    // Quest 3 (Travel): → Sergeant Dorian
    {247, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 5000, 50000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 4 (Kill): Dorian — Elite Bull Fighter + Lich
    {247, 0, 2, {{4, 6}, {6, 5}, {0, 0}}, 15000, 130000, {2, 0}, {160, 4}, {406, 0}, {485, 0}},
    // Quest 5 (Travel): → Warden Aldric
    {245, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 8000, 80000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 6 (Kill): Aldric — Giant
    {245, 0, 1, {{7, 5}, {0, 0}, {0, 0}}, 25000, 200000, {3, 0}, {161, 0}, {407, 0}, {481, 0}},
    // Quest 7 (Travel): → Captain Marcus
    {249, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 10000, 100000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 8 (Kill): Marcus — Skeleton Warrior + Lich
    {249, 0, 2, {{14, 5}, {6, 4}, {0, 0}}, 50000, 350000, {6, 0}, {161, 3}, {408, 0}, {486, 0}},
    // ── Dungeon quest chain (9-11) ──
    // Quest 9 (Kill): Marcus — Skeleton Warrior + Larva (Dungeon 1 entrance)
    {249, 0, 2, {{14, 10}, {12, 8}, {0, 0}}, 60000, 400000, {5, 0}, {162, 0}, {391, 0}, {484, 0}},
    // Quest 10 (Kill): Marcus — Elite Skeleton + Cyclops (Dungeon 2)
    {249, 0, 2, {{16, 8}, {17, 6}, {0, 0}}, 80000, 500000, {7, 2}, {163, 0}, {391, 0}, {487, 0}},
    // Quest 11 (Kill): Marcus — Ghost + Gorgon (Dungeon 3)
    {249, 0, 2, {{11, 10}, {18, 1}, {0, 0}}, 100000, 700000, {8, 3}, {164, 0}, {391, 0}, {488, 0}},
    // ── Devias quest chain (12-17) ──
    // Quest 12 (Kill): Ranger Elise — Worm + Assassin
    {310, 0, 2, {{24, 10}, {21, 8}, {0, 0}}, 30000, 200000, {3, 2}, {161, 2}, {397, 0}, {480, 0}},
    // Quest 13 (Travel): → Tracker Nolan
    {311, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 15000, 100000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 14 (Kill): Tracker Nolan — Hommerd + Assassin
    {311, 0, 2, {{23, 8}, {21, 6}, {0, 0}}, 50000, 300000, {6, 2}, {162, 1}, {391, 0}, {485, 0}},
    // Quest 15 (Travel): → Warden Hale
    {312, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 20000, 120000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 16 (Kill): Warden Hale — Elite Yeti
    {312, 0, 1, {{20, 16}, {0, 0}, {0, 0}}, 70000, 450000, {8, 1}, {163, 0}, {391, 0}, {486, 0}},
    // Quest 17 (Kill): Warden Hale — Ice Queen (boss)
    {312, 0, 1, {{25, 1}, {0, 0}, {0, 0}}, 120000, 800000, {10, 0}, {164, 0}, {398, 0}, {487, 0}},
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

  // Populate bag grid cells
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
// Quest chain helpers — Lorencia (0-11) vs Devias (12-17)
// ═══════════════════════════════════════════════════════

static constexpr int DEVIAS_QUEST_START = 12;

static bool IsDeviasGuard(uint16_t npcType) {
  return npcType == 310 || npcType == 311 || npcType == 312;
}

// Get the active quest index, kill counts, and accepted flag for a chain
static void GetChainState(Session &session, bool devias,
                           int &questIdx, int &kc0, int &kc1, int &kc2,
                           bool &accepted) {
  if (devias) {
    questIdx = session.deviasQuestIndex;
    kc0 = session.deviasKillCount0;
    kc1 = session.deviasKillCount1;
    kc2 = session.deviasKillCount2;
    accepted = session.deviasQuestAccepted;
  } else {
    questIdx = session.questIndex;
    kc0 = session.questKillCount0;
    kc1 = session.questKillCount1;
    kc2 = session.questKillCount2;
    accepted = session.questAccepted;
  }
}

static void SetChainState(Session &session, bool devias,
                           int questIdx, int kc0, int kc1, int kc2,
                           bool accepted) {
  if (devias) {
    session.deviasQuestIndex = questIdx;
    session.deviasKillCount0 = kc0;
    session.deviasKillCount1 = kc1;
    session.deviasKillCount2 = kc2;
    session.deviasQuestAccepted = accepted;
  } else {
    session.questIndex = questIdx;
    session.questKillCount0 = kc0;
    session.questKillCount1 = kc1;
    session.questKillCount2 = kc2;
    session.questAccepted = accepted;
  }
}

static void SaveChainState(Session &session, Database &db, bool devias) {
  if (devias) {
    db.SaveDeviasQuestState(session.characterId, session.deviasQuestIndex,
                             session.deviasKillCount0, session.deviasKillCount1,
                             session.deviasKillCount2, session.deviasQuestAccepted);
  } else {
    db.SaveQuestState(session.characterId, session.questIndex,
                       session.questKillCount0, session.questKillCount1,
                       session.questKillCount2, session.questAccepted);
  }
}

namespace QuestHandler {

void SendQuestState(Session &session) {
  PMSG_QUEST_STATE_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::QUEST, Opcode::SUB_QUEST_STATE);

  // Always send BOTH chain indices so client knows progress on both
  pkt.questIndex = (uint8_t)session.questIndex;           // Lorencia chain (0-11)
  pkt.deviasQuestIndex = (uint8_t)session.deviasQuestIndex; // Devias chain (12-17)

  // Send kill targets for whichever chain has an accepted quest.
  // Prefer current map's chain, fall back to the other if not accepted.
  bool devias = (session.mapId == 2);
  int activeIdx;
  bool activeAccepted;
  bool useDevias;

  if (devias && session.deviasQuestAccepted) {
    activeIdx = session.deviasQuestIndex;
    activeAccepted = true;
    useDevias = true;
  } else if (!devias && session.questAccepted) {
    activeIdx = session.questIndex;
    activeAccepted = true;
    useDevias = false;
  } else if (session.questAccepted) {
    // Fallback: Lorencia chain accepted but on different map
    activeIdx = session.questIndex;
    activeAccepted = true;
    useDevias = false;
  } else if (session.deviasQuestAccepted) {
    // Fallback: Devias chain accepted but on different map
    activeIdx = session.deviasQuestIndex;
    activeAccepted = true;
    useDevias = true;
  } else {
    activeIdx = 0;
    activeAccepted = false;
    useDevias = false;
  }

  if (activeIdx < QUEST_COUNT && activeAccepted) {
    const auto &q = g_quests[activeIdx];
    if (q.questType == 0) { // Kill quest
      pkt.targetCount = q.targetCount;
      int kc[3];
      if (useDevias) {
        kc[0] = session.deviasKillCount0;
        kc[1] = session.deviasKillCount1;
        kc[2] = session.deviasKillCount2;
      } else {
        kc[0] = session.questKillCount0;
        kc[1] = session.questKillCount1;
        kc[2] = session.questKillCount2;
      }
      for (int i = 0; i < q.targetCount; i++) {
        pkt.targets[i].killCount = (uint8_t)kc[i];
        pkt.targets[i].killsRequired = q.targets[i].killsRequired;
      }
    } else {
      pkt.targetCount = 0;
    }
  } else {
    pkt.targetCount = 0;
  }

  session.Send(&pkt, sizeof(pkt));
}

void HandleQuestAccept(Session &session, const std::vector<uint8_t> &packet,
                       Database &db, Server &server) {
  if (packet.size() < sizeof(PMSG_QUEST_ACCEPT_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_QUEST_ACCEPT_RECV *>(packet.data());

  // Determine which chain based on the NPC the player is talking to
  bool devias = IsDeviasGuard(recv->guardNpcType);

  int questIdx, kc0, kc1, kc2;
  bool accepted;
  GetChainState(session, devias, questIdx, kc0, kc1, kc2, accepted);

  if (questIdx >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried accept but chain done (idx=%d)\n",
           session.GetFd(), questIdx);
    return;
  }

  if (accepted) {
    printf("[Quest] fd=%d quest %d already accepted\n", session.GetFd(), questIdx);
    return;
  }

  // If current quest is a TRAVEL quest and the guard matches,
  // auto-complete travel then accept next kill quest
  const auto &curQ = g_quests[questIdx];
  if (curQ.questType == 1 && recv->guardNpcType == curQ.guardNpcType) {
    session.zen += curQ.zenReward;
    session.experience += curQ.xpReward;

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

    PMSG_QUEST_REWARD_SEND reward{};
    reward.h = MakeC1SubHeader(sizeof(reward), Opcode::QUEST,
                               Opcode::SUB_QUEST_REWARD);
    reward.zenReward = curQ.zenReward;
    reward.xpReward = curQ.xpReward;
    reward.nextQuestIndex = (uint8_t)(questIdx + 1);
    session.Send(&reward, sizeof(reward));

    db.UpdateCharacterMoney(session.characterId, session.zen);

    char buf[128];
    snprintf(buf, sizeof(buf), "Quest complete! +%u Zen, +%u Experience",
             curQ.zenReward, curQ.xpReward);
    server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
    if (leveledUp) {
      snprintf(buf, sizeof(buf), "Congratulations! Level %d reached!",
               (int)session.level);
      server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
    }

    printf("[Quest] fd=%d completed travel quest %d (zen+%u xp+%u)\n",
           session.GetFd(), questIdx, curQ.zenReward, curQ.xpReward);

    questIdx++;
    SetChainState(session, devias, questIdx, 0, 0, 0, false);
    SaveChainState(session, db, devias);

    if (questIdx >= QUEST_COUNT) {
      SendQuestState(session);
      CharacterHandler::SendCharStats(session);
      return;
    }
  }

  // Accept the current quest (should be a kill quest)
  const auto &q = g_quests[questIdx];
  if (recv->guardNpcType != q.guardNpcType) {
    printf("[Quest] fd=%d wrong guard %d for quest %d (expected %d)\n",
           session.GetFd(), recv->guardNpcType, questIdx, q.guardNpcType);
    return;
  }

  SetChainState(session, devias, questIdx, 0, 0, 0, true);
  SaveChainState(session, db, devias);

  SendQuestState(session);
  CharacterHandler::SendCharStats(session);

  printf("[Quest] fd=%d accepted quest %d from guard %d\n", session.GetFd(),
         questIdx, recv->guardNpcType);
}

void HandleQuestComplete(Session &session, const std::vector<uint8_t> &packet,
                         Database &db, Server &server) {
  if (packet.size() < sizeof(PMSG_QUEST_COMPLETE_RECV))
    return;
  auto *recv =
      reinterpret_cast<const PMSG_QUEST_COMPLETE_RECV *>(packet.data());

  bool devias = IsDeviasGuard(recv->guardNpcType);
  int questIdx, kc0, kc1, kc2;
  bool accepted;
  GetChainState(session, devias, questIdx, kc0, kc1, kc2, accepted);

  if (questIdx >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried complete but chain done\n", session.GetFd());
    return;
  }

  const auto &q = g_quests[questIdx];

  if (q.questType != 0) {
    printf("[Quest] fd=%d tried complete non-kill quest %d\n", session.GetFd(),
           questIdx);
    return;
  }

  if (recv->guardNpcType != q.guardNpcType) {
    printf("[Quest] fd=%d wrong guard %d for completing quest %d\n",
           session.GetFd(), recv->guardNpcType, questIdx);
    return;
  }

  // Verify all targets are complete
  int kc[3] = {kc0, kc1, kc2};
  for (int i = 0; i < q.targetCount; i++) {
    if (kc[i] < q.targets[i].killsRequired) {
      printf("[Quest] fd=%d quest %d target %d not complete (%d/%d)\n",
             session.GetFd(), questIdx, i, kc[i],
             q.targets[i].killsRequired);
      return;
    }
  }

  // Award zen + XP rewards
  session.zen += q.zenReward;
  session.experience += q.xpReward;

  // Give item rewards
  GiveItemReward(session, db, q.dkReward);
  GiveItemReward(session, db, q.dwReward);
  GiveItemReward(session, db, q.orbReward);
  GiveItemReward(session, db, q.scrollReward);

  // Check for level ups
  bool leveledUp = false;
  while (true) {
    uint64_t nextXP = Database::GetXPForLevel(session.level);
    if (session.experience >= nextXP && session.level < 400) {
      session.level++;
      CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
      session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls);
      session.maxHp = StatCalculator::CalculateMaxHP(charCls, session.level,
                                                      session.vitality) +
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

  // Advance quest
  int nextIndex = questIdx + 1;
  SetChainState(session, devias, nextIndex, 0, 0, 0, false);
  SaveChainState(session, db, devias);
  db.UpdateCharacterMoney(session.characterId, session.zen);

  // Send reward notification
  PMSG_QUEST_REWARD_SEND reward{};
  reward.h =
      MakeC1SubHeader(sizeof(reward), Opcode::QUEST, Opcode::SUB_QUEST_REWARD);
  reward.zenReward = q.zenReward;
  reward.xpReward = q.xpReward;
  reward.nextQuestIndex = (uint8_t)nextIndex;
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
  }

  printf("[Quest] fd=%d completed quest %d -> next=%d (zen+%u xp+%u)\n",
         session.GetFd(), questIdx, nextIndex, q.zenReward, q.xpReward);
}

void HandleQuestAbandon(Session &session, Database &db) {
  // Abandon the active chain based on current map
  bool devias = (session.mapId == 2);
  int questIdx, kc0, kc1, kc2;
  bool accepted;
  GetChainState(session, devias, questIdx, kc0, kc1, kc2, accepted);

  if (questIdx >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried abandon but chain done\n", session.GetFd());
    return;
  }

  if (!accepted) {
    printf("[Quest] fd=%d tried abandon but quest %d not accepted\n",
           session.GetFd(), questIdx);
    return;
  }

  const auto &q = g_quests[questIdx];
  if (q.questType != 0) {
    printf("[Quest] fd=%d tried abandon travel quest %d\n", session.GetFd(),
           questIdx);
    return;
  }

  printf("[Quest] fd=%d abandoned quest %d (was %d/%d/%d)\n", session.GetFd(),
         questIdx, kc0, kc1, kc2);

  SetChainState(session, devias, questIdx, 0, 0, 0, false);
  SaveChainState(session, db, devias);
  SendQuestState(session);
}

// Helper: try to advance kill count for a specific chain
static bool TryMonsterKillForChain(Session &session, uint16_t monsterType,
                                    Database &db, bool devias) {
  int questIdx, kc0, kc1, kc2;
  bool accepted;
  GetChainState(session, devias, questIdx, kc0, kc1, kc2, accepted);

  if (questIdx >= QUEST_COUNT || !accepted)
    return false;

  const auto &q = g_quests[questIdx];
  if (q.questType != 0)
    return false;

  int *kcPtrs[3];
  if (devias) {
    kcPtrs[0] = &session.deviasKillCount0;
    kcPtrs[1] = &session.deviasKillCount1;
    kcPtrs[2] = &session.deviasKillCount2;
  } else {
    kcPtrs[0] = &session.questKillCount0;
    kcPtrs[1] = &session.questKillCount1;
    kcPtrs[2] = &session.questKillCount2;
  }

  for (int i = 0; i < q.targetCount; i++) {
    if (q.targets[i].monsterType == monsterType &&
        *kcPtrs[i] < q.targets[i].killsRequired) {
      (*kcPtrs[i])++;
      printf("[Quest] fd=%d quest %d target %d: kill %d/%d (monType=%d)\n",
             session.GetFd(), questIdx, i, *kcPtrs[i],
             q.targets[i].killsRequired, monsterType);
      SaveChainState(session, db, devias);
      SendQuestState(session);
      return true;
    }
  }
  return false;
}

void OnMonsterKill(Session &session, uint16_t monsterType, Database &db) {
  // Check both chains — a monster kill could count for either
  if (TryMonsterKillForChain(session, monsterType, db, false))
    return;
  TryMonsterKillForChain(session, monsterType, db, true);
}

} // namespace QuestHandler

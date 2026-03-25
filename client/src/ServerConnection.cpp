#include "ServerConnection.hpp"
#include "PacketDefs.hpp"
#include <iostream>

bool ServerConnection::Connect(const char *host, uint16_t port) {
  bool ok = m_client.Connect(host, port);
  if (ok) {
    m_client.onPacket = [this](const uint8_t *pkt, int size) {
      if (onPacket)
        onPacket(pkt, size);
    };
  }
  return ok;
}

void ServerConnection::Poll() { m_client.Poll(); }

void ServerConnection::Flush() { m_client.Flush(); }

void ServerConnection::Disconnect() { m_client.Disconnect(); }

bool ServerConnection::IsConnected() const { return m_client.IsConnected(); }

void ServerConnection::SendPrecisePosition(float worldX, float worldZ) {
  PMSG_PRECISE_POS_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::PRECISE_POS);
  pkt.worldX = worldX;
  pkt.worldZ = worldZ;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendAttack(uint16_t monsterIndex) {
  PMSG_ATTACK_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::ATTACK);
  pkt.monsterIndex = monsterIndex;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendSkillAttack(uint16_t monsterIndex, uint8_t skillId,
                                       float targetX, float targetZ) {
  PMSG_SKILL_ATTACK_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::SKILL_USE);
  pkt.monsterIndex = monsterIndex;
  pkt.skillId = skillId;
  pkt.targetX = targetX;
  pkt.targetZ = targetZ;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendTeleport(uint8_t gridX, uint8_t gridY) {
  PMSG_SKILL_TELEPORT_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::SKILL_TELEPORT);
  pkt.targetGridX = gridX;
  pkt.targetGridY = gridY;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendWarpCommand(uint8_t mapId, uint8_t spawnX,
                                       uint8_t spawnY) {
  PMSG_WARP_COMMAND_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::WARP_COMMAND);
  pkt.mapId = mapId;
  pkt.spawnX = spawnX;
  pkt.spawnY = spawnY;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCameraZoom(uint16_t zoomTimes10) {
  PMSG_CLIENT_SETTINGS pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CLIENT_SETTINGS);
  pkt.cameraZoom = zoomTimes10;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendPickup(uint16_t dropIndex) {
  PMSG_PICKUP_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::PICKUP);
  pkt.dropIndex = dropIndex;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCharSave(
    uint16_t charId, uint16_t level, uint16_t str, uint16_t dex, uint16_t vit,
    uint16_t ene, uint16_t life, uint16_t maxLife, uint16_t mana,
    uint16_t maxMana, uint16_t ag, uint16_t maxAg, uint16_t levelUpPoints,
    uint64_t experience, const int8_t *skillBar, const int16_t *potionBar,
    int8_t rmcSkillId) {
  PMSG_CHARSAVE_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CHARSAVE);
  pkt.characterId = charId;
  pkt.level = level;
  pkt.strength = str;
  pkt.dexterity = dex;
  pkt.vitality = vit;
  pkt.energy = ene;
  pkt.life = life;
  pkt.maxLife = maxLife;
  pkt.mana = mana;
  pkt.maxMana = maxMana;
  pkt.ag = ag;
  pkt.maxAg = maxAg;
  pkt.levelUpPoints = levelUpPoints;
  pkt.experienceLo = static_cast<uint32_t>(experience & 0xFFFFFFFF);
  pkt.experienceHi = static_cast<uint32_t>(experience >> 32);
  if (skillBar)
    std::memcpy(pkt.skillBar, skillBar, 10);
  if (potionBar)
    std::memcpy(pkt.potionBar, potionBar, 8); // 4 × int16_t
  pkt.rmcSkillId = rmcSkillId;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendEquip(uint16_t charId, uint8_t slot, uint8_t cat,
                                 uint8_t idx, uint8_t lvl) {
  PMSG_EQUIP_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::EQUIP);
  pkt.characterId = charId;
  pkt.slot = slot;
  pkt.category = cat;
  pkt.itemIndex = idx;
  pkt.itemLevel = lvl;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendUnequip(uint16_t charId, uint8_t slot) {
  SendEquip(charId, slot, 0xFF, 0, 0);
}

void ServerConnection::SendStatAlloc(uint8_t statType) {
  PMSG_STAT_ALLOC_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::STAT_ALLOC);
  pkt.statType = statType;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendInventoryMove(uint8_t from, uint8_t to) {
  PMSG_INVENTORY_MOVE_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::INV_MOVE);
  pkt.fromSlot = from;
  pkt.toSlot = to;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendItemUse(uint8_t slot) {
  PMSG_ITEM_USE_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::ITEM_USE);
  pkt.slot = slot;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendItemUpgrade(uint8_t jewelSlot, uint8_t targetSlot) {
  PMSG_ITEM_UPGRADE_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::ITEM_UPGRADE);
  pkt.jewelSlot = jewelSlot;
  pkt.targetSlot = targetSlot;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendDropItem(uint8_t bagSlot) {
  PMSG_ITEM_DROP_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::ITEM_DROP);
  pkt.slot = bagSlot;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendGridMove(uint8_t gridX, uint8_t gridY) {
  PMSG_MOVE_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::MOVE);
  pkt.x = gridX;
  pkt.y = gridY;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCharCreate(const char *name, uint8_t classCode) {
  PMSG_CHARCREATE_RECV pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::CHARSELECT,
                          Opcode::SUB_CHARCREATE);
  std::memset(pkt.name, 0, 10);
  std::strncpy(pkt.name, name, 10);
  pkt.classCode = classCode;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCharDelete(uint8_t slot, const char *name) {
  PMSG_CHARDELETE_RECV pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::CHARSELECT,
                          Opcode::SUB_CHARDELETE);
  pkt.slot = slot;
  std::memset(pkt.name, 0, 10);
  std::strncpy(pkt.name, name, 10);
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCharSelect(const char *name) {
  PMSG_CHARSELECT_RECV pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::CHARSELECT,
                          Opcode::SUB_CHARSELECT);
  std::memset(pkt.name, 0, 10);
  std::strncpy(pkt.name, name, 10);
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendCharListRequest() {
  // Request character list — server will save current character first
  PSBMSG_HEAD pkt = MakeC1SubHeader(sizeof(pkt), Opcode::CHARSELECT,
                                     Opcode::SUB_CHARLIST);
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendNpcInteract(uint16_t npcType, bool open) {
  PMSG_NPC_INTERACT_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::NPC_INTERACT);
  pkt.npcType = npcType;
  pkt.action = open ? 1 : 0;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendQuestAccept(uint8_t questId) {
  PMSG_QUEST_ACCEPT_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::QUEST, Opcode::SUB_QUEST_ACCEPT);
  pkt.questId = questId;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendQuestComplete(uint8_t questId) {
  PMSG_QUEST_COMPLETE_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::QUEST, Opcode::SUB_QUEST_COMPLETE);
  pkt.questId = questId;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendQuestAbandon(uint8_t questId) {
  PMSG_QUEST_ABANDON_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::QUEST, Opcode::SUB_QUEST_ABANDON);
  pkt.questId = questId;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendShopOpen(uint16_t npcType) {
  std::cout << "[Client] Sending SHOP_OPEN for npcType: " << npcType
            << std::endl;
  PMSG_SHOP_OPEN_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::SHOP_OPEN);
  pkt.npcType = npcType;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendShopBuy(int16_t defIndex, uint8_t itemLevel,
                                   uint8_t quantity, uint8_t targetSlot) {
  PMSG_SHOP_BUY_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::SHOP_BUY);
  pkt.defIndex = defIndex;
  pkt.itemLevel = itemLevel;
  pkt.quantity = quantity;
  pkt.targetSlot = targetSlot;
  m_client.Send(&pkt, sizeof(pkt));
}

void ServerConnection::SendShopSell(uint8_t bagSlot) {
  PMSG_SHOP_SELL_RECV pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::SHOP_SELL);
  pkt.bagSlot = bagSlot;
  m_client.Send(&pkt, sizeof(pkt));
}

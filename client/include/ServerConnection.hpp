#ifndef MU_SERVER_CONNECTION_HPP
#define MU_SERVER_CONNECTION_HPP

#include "NetworkClient.hpp"
#include <cstdint>
#include <functional>

// Typed send API wrapping NetworkClient.
// Replaces scattered raw g_net.Send() calls with named methods that
// build the correct packet struct internally.
class ServerConnection {
public:
  bool Connect(const char *host, uint16_t port);
  void Poll();
  void Flush();
  void Disconnect();
  bool IsConnected() const;

  // Typed sends (build packet internally)
  void SendPrecisePosition(float worldX, float worldZ);
  void SendAttack(uint16_t monsterIndex);
  void SendSkillAttack(uint16_t monsterIndex, uint8_t skillId,
                       float targetX = 0, float targetZ = 0);
  void SendTeleport(uint8_t gridX, uint8_t gridY);
  void SendPickup(uint16_t dropIndex);
  void SendCharSave(uint16_t charId, uint16_t level, uint16_t str, uint16_t dex,
                    uint16_t vit, uint16_t ene, uint16_t life, uint16_t maxLife,
                    uint16_t mana, uint16_t maxMana, uint16_t ag,
                    uint16_t maxAg, uint16_t levelUpPoints, uint64_t experience,
                    const int8_t *skillBar, const int16_t *potionBar,
                    int8_t rmcSkillId);
  void SendEquip(uint16_t charId, uint8_t slot, uint8_t cat, uint8_t idx,
                 uint8_t lvl);
  void SendUnequip(uint16_t charId, uint8_t slot);
  void SendStatAlloc(uint8_t statType);
  void SendInventoryMove(uint8_t from, uint8_t to);
  void SendItemUse(uint8_t slot);
  void SendItemUpgrade(uint8_t jewelSlot, uint8_t targetSlot);
  void SendDropItem(uint8_t bagSlot);
  void SendGridMove(uint8_t gridX, uint8_t gridY);

  // Character Select
  void SendCharCreate(const char *name, uint8_t classCode);
  void SendCharDelete(uint8_t slot, const char *name);
  void SendCharSelect(const char *name);

  // Game Menu
  void SendCharListRequest();

  // NPC Interaction (guard quest dialog)
  void SendNpcInteract(uint16_t npcType, bool open);

  // Quest system
  void SendQuestAccept(uint8_t questId);
  void SendQuestComplete(uint8_t questId);
  void SendQuestAbandon(uint8_t questId);

  // Warp
  void SendWarpCommand(uint8_t mapId, uint8_t spawnX = 0, uint8_t spawnY = 0);

  // Client settings
  void SendCameraZoom(uint16_t zoomTimes10);

  // Shop
  void SendShopOpen(uint16_t npcType);
  void SendShopBuy(int16_t defIndex, uint8_t itemLevel, uint8_t quantity,
                   uint8_t targetSlot = 0xFF);
  void SendShopSell(uint8_t bagSlot);

  // Packet receive callback (set by caller)
  std::function<void(const uint8_t *, int)> onPacket;

private:
  NetworkClient m_client;
};

#endif // MU_SERVER_CONNECTION_HPP

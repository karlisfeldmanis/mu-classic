#include "PacketHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "handlers/CharacterSelectHandler.hpp"
#include "handlers/QuestHandler.hpp"
#include "handlers/ShopHandler.hpp"
#include <cstdio>

namespace PacketHandler {

void Handle(Session &session, const std::vector<uint8_t> &packet, Database &db,
            GameWorld &world, Server &server) {
  if (packet.size() < 3)
    return;

  uint8_t type = packet[0];
  uint8_t headcode;
  uint8_t subcode = 0;

  if (type == 0xC1 || type == 0xC3) {
    headcode = packet[2];
    if (packet.size() >= 4)
      subcode = packet[3];
  } else if (type == 0xC2 || type == 0xC4) {
    headcode = packet[3];
    if (packet.size() >= 5)
      subcode = packet[4];
  } else {
    return;
  }

  switch (headcode) {
  // Auth / World entry
  case Opcode::AUTH:
    if (subcode == Opcode::SUB_LOGIN)
      WorldHandler::HandleLogin(session, packet, db);
    break;
  case Opcode::CHARSELECT:
    if (subcode == Opcode::SUB_CHARLIST) {
      // Save current character before returning to char select
      if (session.characterId > 0 && session.inWorld)
        server.SaveSession(session);
      CharacterSelectHandler::SendCharList(session, db);
    }
    else if (subcode == Opcode::SUB_CHARCREATE)
      CharacterSelectHandler::HandleCharCreate(session, packet, db);
    else if (subcode == Opcode::SUB_CHARDELETE)
      CharacterSelectHandler::HandleCharDelete(session, packet, db);
    else if (subcode == Opcode::SUB_CHARSELECT)
      CharacterSelectHandler::HandleCharSelect(session, packet, db, world,
                                               server);
    break;

  // Movement
  case Opcode::MOVE:
    WorldHandler::HandleMove(session, packet, db);
    break;
  case Opcode::PRECISE_POS:
    WorldHandler::HandlePrecisePosition(session, packet, world);
    break;

  // Character
  case Opcode::CHARSAVE: {
    bool wasDead = session.dead;
    CharacterHandler::HandleCharSave(session, packet, db);
    // Respawn teleport: if player was dead and is now alive, move to map safe zone
    if (wasDead && !session.dead) {
      uint8_t spawnX = 0, spawnY = 0;
      switch (session.mapId) {
      case 0: spawnX = 125; spawnY = 125; break;   // Lorencia center
      case 1: spawnX = 108; spawnY = 247; break;   // Dungeon entrance
      case 2: spawnX = 210; spawnY = 40;  break;   // Devias center
      case 3: spawnX = 174; spawnY = 110; break;   // Noria center
      case 4: spawnX = 208; spawnY = 75;  break;   // Lost Tower entrance
      case 7: spawnX = 24;  spawnY = 22;  break;   // Atlans safe zone
      }
      if (spawnX != 0 || spawnY != 0) {
        server.TransitionMap(session, session.mapId, spawnX, spawnY);
      }
    }
    break;
  }
  case Opcode::EQUIP:
    CharacterHandler::HandleEquip(session, packet, db);
    break;
  case Opcode::STAT_ALLOC:
    CharacterHandler::HandleStatAlloc(session, packet, db);
    break;

  // Combat
  case Opcode::ATTACK:
    CombatHandler::HandleAttack(session, packet, world, server);
    break;
  case Opcode::SKILL_USE:
    CombatHandler::HandleSkillAttack(session, packet, world, server);
    break;
  case Opcode::SKILL_TELEPORT:
    CombatHandler::HandleTeleport(session, packet, world, server);
    break;

  // Inventory
  case Opcode::PICKUP:
    InventoryHandler::HandlePickup(session, packet, world, server, db);
    break;
  case Opcode::INV_MOVE:
    InventoryHandler::HandleInventoryMove(session, packet, db);
    break;
  case Opcode::ITEM_USE:
    InventoryHandler::HandleItemUse(session, packet, db);
    break;
  case Opcode::ITEM_DROP:
    InventoryHandler::HandleItemDrop(session, packet, world, server, db);
    break;
  case Opcode::ITEM_UPGRADE:
    InventoryHandler::HandleItemUpgrade(session, packet, db);
    break;

  // Shop
  case Opcode::SHOP_OPEN:
    ShopHandler::HandleShopOpen(session, packet, db);
    break;
  case Opcode::SHOP_BUY:
    ShopHandler::HandleShopBuy(session, packet, db);
    break;
  case Opcode::SHOP_SELL:
    ShopHandler::HandleShopSell(session, packet, db);
    break;

  // NPC Interaction (guard quest dialog)
  case Opcode::NPC_INTERACT:
    if (packet.size() >= sizeof(PMSG_NPC_INTERACT_RECV)) {
      auto *recv =
          reinterpret_cast<const PMSG_NPC_INTERACT_RECV *>(packet.data());
      world.SetGuardInteracting(recv->npcType, session.GetFd(),
                                recv->action == 1);
    }
    break;

  // Warp command
  case Opcode::WARP_COMMAND: {
    if (packet.size() < sizeof(PMSG_WARP_COMMAND_RECV))
      break;
    auto *warp =
        reinterpret_cast<const PMSG_WARP_COMMAND_RECV *>(packet.data());
    uint8_t mapId = warp->mapId;
    uint8_t sx = warp->spawnX;
    uint8_t sy = warp->spawnY;
    // Default spawn points per map
    if (sx == 0 && sy == 0) {
      if (mapId == 0) { sx = 125; sy = 125; }       // Lorencia center
      else if (mapId == 1) { sx = 108; sy = 247; }   // Dungeon entrance
      else if (mapId == 2) { sx = 210; sy = 40; }    // Devias center
      else if (mapId == 3) { sx = 174; sy = 110; }   // Noria center
      else if (mapId == 4) { sx = 208; sy = 75; }    // Lost Tower NPC area
      else if (mapId == 7) { sx = 24; sy = 22; }     // Atlans entry (outside return gate)
    }
    if (mapId <= 4 || mapId == 7) { // Allow maps 0-4 + Atlans (7)
      server.TransitionMap(session, mapId, sx, sy);
    }
    break;
  }

  // Quest system
  case Opcode::QUEST:
    if (subcode == Opcode::SUB_QUEST_ACCEPT)
      QuestHandler::HandleQuestAccept(session, packet, db);
    else if (subcode == Opcode::SUB_QUEST_COMPLETE)
      QuestHandler::HandleQuestComplete(session, packet, db, server);
    else if (subcode == Opcode::SUB_QUEST_ABANDON)
      QuestHandler::HandleQuestAbandon(session, packet, db);
    break;

  // Client settings (camera zoom)
  case Opcode::CLIENT_SETTINGS:
    if (packet.size() >= sizeof(PMSG_CLIENT_SETTINGS)) {
      auto *settings =
          reinterpret_cast<const PMSG_CLIENT_SETTINGS *>(packet.data());
      session.cameraZoom = settings->cameraZoom;
    }
    break;

  default:
    break;
  }
}

} // namespace PacketHandler

#include "PacketHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "handlers/CharacterSelectHandler.hpp"
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

  printf(
      "[PacketHandler] Received packet type: %02X, headcode: %02X, size: %zu\n",
      type, headcode, packet.size());

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
    WorldHandler::HandlePrecisePosition(session, packet);
    break;

  // Character
  case Opcode::CHARSAVE:
    CharacterHandler::HandleCharSave(session, packet, db);
    break;
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
    CombatHandler::HandleTeleport(session, packet, world);
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

  default:
    break;
  }
}

} // namespace PacketHandler

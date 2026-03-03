#include "handlers/ShopHandler.hpp"
#include "PacketDefs.hpp"
#include "handlers/InventoryHandler.hpp"
#include <map>

namespace ShopHandler {

// Hardcoded shop inventories
static std::map<uint16_t, std::vector<std::pair<uint8_t, uint8_t>>> s_shops = {
    // Amy (253) - Potions
    {253,
     {{14, 0}, {14, 1}, {14, 2}, {14, 3}, {14, 4}, {14, 5}, {14, 6}, {14, 8}}},
    // Harold (250) - DK Armor sets
    {250,
     {{7, 0},
      {8, 0},
      {9, 0},
      {10, 0},
      {11, 0},
      {7, 5},
      {8, 5},
      {9, 5},
      {10, 5},
      {11, 5},
      {7, 6},
      {8, 6},
      {9, 6},
      {10, 6},
      {11, 6}}},
    // Hanzo (251) - Weapons, Shields & Skill Orbs
    {251, {{0, 0},   {0, 1},   {0, 2},   {0, 3},   {0, 4},   {0, 5}, {1, 0},
           {1, 1},   {1, 2},   {2, 0},   {2, 1},   {3, 0},   {3, 1}, {4, 0},
           {4, 1},   {4, 7},   {4, 15},  {6, 0},   {6, 1},   {6, 2}, {6, 3},
           {12, 20}, {12, 21}, {12, 22}, {12, 23}, {12, 24}, // Basic DK orbs
           {12, 7},  {12, 12}, {12, 19}}},                   // BK orbs
    // Pasi (254) - DW Scrolls, Staves & Armor
    {254,
     {// Scrolls
      {15, 0},  // Poison
      {15, 1},  // Meteorite
      {15, 2},  // Lightning
      {15, 3},  // Fire Ball
      {15, 4},  // Flame
      {15, 5},  // Teleport
      {15, 6},  // Ice
      {15, 7},  // Twister
      {15, 8},  // Evil Spirit
      {15, 9},  // Hellfire
      {15, 11}, // Aqua Beam
      {15, 12}, // Cometfall
      {15, 13}, // Inferno
      // Staves (up to Gorgon Staff lvl 52)
      {5, 0},
      {5, 1},
      {5, 2},
      {5, 3},
      {5, 4},
      // Pad Set (beginner DW)
      {7, 2},
      {8, 2},
      {9, 2},
      {10, 2},
      {11, 2},
      // Bone Set (mid DW)
      {7, 4},
      {8, 4},
      {9, 4},
      {10, 4},
      {11, 4},
      // Sphinx Set (mid-high DW)
      {7, 7},
      {8, 7},
      {9, 7},
      {10, 7},
      {11, 7},
      // Legendary Set (high DW)
      {7, 3},
      {8, 3},
      {9, 3},
      {10, 3},
      {11, 3}}},
    // Lumen (255) - Barmaid
    {255, {{14, 9}, {14, 10}}}};

void HandleShopOpen(Session &session, const std::vector<uint8_t> &packet,
                    Database &db) {
  if (packet.size() < sizeof(PMSG_SHOP_OPEN_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_SHOP_OPEN_RECV *>(packet.data());

  uint16_t npcType = recv->npcType;

  if (s_shops.find(npcType) == s_shops.end()) {
    return; // NPC has no shop (guards, etc.)
  }

  session.shopNpcType = npcType;

  const auto &items = s_shops[npcType];

  std::vector<PMSG_SHOP_ITEM> shopItems;
  for (const auto &it : items) {
    auto def = db.GetItemDefinition(it.first, it.second);
    if (def.category == it.first && def.itemIndex == it.second) {
      PMSG_SHOP_ITEM si;
      si.defIndex = def.category * 32 + def.itemIndex;
      si.itemLevel = 0;
      si.buyPrice = def.buyPrice;
      shopItems.push_back(si);
    }
  }

  uint16_t packetSize =
      sizeof(PWMSG_HEAD) + 1 + shopItems.size() * sizeof(PMSG_SHOP_ITEM);
  std::vector<uint8_t> sendBuf(packetSize);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(sendBuf.data());
  *head = MakeC2Header(packetSize, Opcode::SHOP_LIST);

  sendBuf[sizeof(PWMSG_HEAD)] = static_cast<uint8_t>(shopItems.size());

  memcpy(sendBuf.data() + sizeof(PWMSG_HEAD) + 1, shopItems.data(),
         shopItems.size() * sizeof(PMSG_SHOP_ITEM));

  session.Send(sendBuf.data(), sendBuf.size());
}

bool FindEmptySpace(Session &session, uint8_t w, uint8_t h, uint8_t &outSlot) {
  for (int startY = 0; startY <= 8 - h; ++startY) {
    for (int startX = 0; startX <= 8 - w; ++startX) {
      bool fits = true;
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          int slot = (startY + y) * 8 + (startX + x);
          if (session.bag[slot].occupied) {
            fits = false;
            break;
          }
        }
        if (!fits)
          break;
      }
      if (fits) {
        outSlot = startY * 8 + startX;
        return true;
      }
    }
  }
  return false;
}

void HandleShopBuy(Session &session, const std::vector<uint8_t> &packet,
                   Database &db) {
  if (packet.size() < sizeof(PMSG_SHOP_BUY_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_SHOP_BUY_RECV *>(packet.data());

  PMSG_SHOP_BUY_RESULT_SEND res = {0};
  res.h = MakeC1Header(sizeof(res), Opcode::SHOP_BUY_RESULT);
  res.result = 0;
  res.defIndex = recv->defIndex;
  res.quantity = recv->quantity;

  if (session.shopNpcType == -1) {
    session.Send(&res, sizeof(res));
    return; // No shop open
  }

  uint8_t cat = recv->defIndex / 32;
  uint8_t idx = recv->defIndex % 32;

  auto def = db.GetItemDefinition(cat, idx);
  if (def.category != cat || def.itemIndex != idx) {
    session.Send(&res, sizeof(res));
    return;
  }

  uint32_t price = def.buyPrice * (recv->quantity > 0 ? recv->quantity : 1);
  if (session.zen < price) {
    printf("[Shop] Buy failed: not enough Zen (need %u, have %u, defIdx=%d)\n",
           price, session.zen, recv->defIndex);
    session.Send(&res, sizeof(res));
    return; // Not enough zen
  }

  printf("[Shop] Buying item defIdx=%d (price=%u)\n", recv->defIndex, price);

  static constexpr uint8_t MAX_STACK = 20;

  // Stackable items (category 14 = potions): try to merge into existing stack
  if (cat == 14 && def.width == 1 && def.height == 1) {
    for (int i = 0; i < 64; i++) {
      if (session.bag[i].occupied && session.bag[i].primary &&
          session.bag[i].defIndex == recv->defIndex &&
          session.bag[i].quantity < MAX_STACK) {
        // Merge into existing stack
        session.zen -= price;
        db.UpdateCharacterMoney(session.characterId, session.zen);
        session.bag[i].quantity++;
        db.SaveCharacterInventory(
            session.characterId, recv->defIndex, session.bag[i].quantity,
            session.bag[i].itemLevel, static_cast<uint8_t>(i));
        res.result = 1;
        session.Send(&res, sizeof(res));
        InventoryHandler::SendInventorySync(session);
        return;
      }
    }
  }

  uint8_t slot = 0;
  bool slotFound = false;

  if (recv->targetSlot != 0xFF && recv->targetSlot < 64) {
    // Try specific slot
    bool fits = true;
    for (int y = 0; y < def.height; ++y) {
      for (int x = 0; x < def.width; ++x) {
        int r = recv->targetSlot / 8;
        int c = recv->targetSlot % 8;
        if (r + y >= 8 || c + x >= 8) {
          fits = false;
          break;
        }
        int s = (r + y) * 8 + (c + x);
        if (session.bag[s].occupied) {
          fits = false;
          break;
        }
      }
      if (!fits)
        break;
    }
    if (fits) {
      slot = recv->targetSlot;
      slotFound = true;
    }
  }

  if (!slotFound) {
    if (!FindEmptySpace(session, def.width, def.height, slot)) {
      session.Send(&res, sizeof(res));
      return; // Inventory full
    }
  }

  session.zen -= price;
  db.UpdateCharacterMoney(session.characterId, session.zen);

  for (int y = 0; y < def.height; ++y) {
    for (int x = 0; x < def.width; ++x) {
      int s = slot + y * 8 + x;
      session.bag[s].occupied = true;
      session.bag[s].primary = (y == 0 && x == 0);
      session.bag[s].defIndex = recv->defIndex;
      session.bag[s].category = cat;
      session.bag[s].itemIndex = idx;
      session.bag[s].itemLevel = recv->itemLevel;
    }
  }
  session.bag[slot].defIndex = recv->defIndex;
  session.bag[slot].quantity = recv->quantity > 0 ? recv->quantity : 1;
  session.bag[slot].itemLevel = recv->itemLevel;
  session.bag[slot].category = cat;
  session.bag[slot].itemIndex = idx;

  db.SaveCharacterInventory(session.characterId, recv->defIndex,
                            session.bag[slot].quantity, recv->itemLevel, slot);

  res.result = 1;
  session.Send(&res, sizeof(res));

  InventoryHandler::SendInventorySync(session);
}

void HandleShopSell(Session &session, const std::vector<uint8_t> &packet,
                    Database &db) {
  if (packet.size() < sizeof(PMSG_SHOP_SELL_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_SHOP_SELL_RECV *>(packet.data());

  PMSG_SHOP_SELL_RESULT_SEND res = {0};
  res.h = MakeC1Header(sizeof(res), Opcode::SHOP_SELL_RESULT);
  res.result = 0;
  res.bagSlot = recv->bagSlot;

  if (session.shopNpcType == -1 || recv->bagSlot >= 64 ||
      !session.bag[recv->bagSlot].primary) {
    session.Send(&res, sizeof(res));
    return;
  }

  auto &item = session.bag[recv->bagSlot];
  auto def = db.GetItemDefinition(item.category, item.itemIndex);

  if (def.category != item.category || def.itemIndex != item.itemIndex) {
    session.Send(&res, sizeof(res));
    return;
  }

  uint32_t sellPrice = def.buyPrice / 3;
  if (sellPrice == 0) {
    session.Send(&res, sizeof(res));
    return; // Cannot sell this item
  }

  session.zen += sellPrice;
  db.UpdateCharacterMoney(session.characterId, session.zen);

  for (int y = 0; y < def.height; ++y) {
    for (int x = 0; x < def.width; ++x) {
      int s = recv->bagSlot + y * 8 + x;
      session.bag[s].occupied = false;
      session.bag[s].primary = false;
      session.bag[s].defIndex = -2;
    }
  }

  db.DeleteCharacterInventoryItem(session.characterId, recv->bagSlot);

  res.result = 1;
  res.zenGained = sellPrice;
  session.Send(&res, sizeof(res));

  InventoryHandler::SendInventorySync(session);
}

} // namespace ShopHandler

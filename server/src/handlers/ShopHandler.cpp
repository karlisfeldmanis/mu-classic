#include "handlers/ShopHandler.hpp"
#include "PacketDefs.hpp"
#include "handlers/InventoryHandler.hpp"
#include <algorithm>
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
      {15, 10}, // Power Wave
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
    // Lumen (255) - Barmaid: Potions, Rings, Pendants & Pets
    {255,
     {{14, 9},  {14, 10},              // Potions (Ale, Town Portal)
      {13, 0},  {13, 1},  {13, 2},  {13, 3},  // Guardian Angel, Imp, Uniria, Dinorant
      {13, 8},  {13, 9},  {13, 10},    // Ring of Ice, Poison, Transformation
      {13, 12}, {13, 13}}},             // Pendant of Lighting, Fire
    // ── Devias Vendors ──
    // Caren the Barmaid (302) - Potions, Rings, Pendants & Pets
    {302,
     {{14, 0}, {14, 1}, {14, 2}, {14, 3}, {14, 4}, {14, 5}, {14, 6}, {14, 8},
      {14, 9},  {14, 10},              // Ale, Town Portal
      {13, 0},  {13, 1},  {13, 2},  {13, 3},  // Guardian Angel, Imp, Uniria, Dinorant
      {13, 8},  {13, 9},  {13, 10},    // Ring of Ice, Poison, Transformation
      {13, 12}, {13, 13}}},            // Pendant of Lighting, Fire
    // Izabel the Wizard (303) - DW Scrolls, Staves & Armor (mid-high level)
    {303,
     {// Scrolls (all)
      {15, 10}, {15, 0}, {15, 1}, {15, 2}, {15, 3}, {15, 4}, {15, 5},
      {15, 6}, {15, 7}, {15, 8}, {15, 9}, {15, 11}, {15, 12}, {15, 13},
      // Staves (Serpent through Legendary)
      {5, 2}, {5, 3}, {5, 4}, {5, 5},
      // Bone Set (mid DW)
      {7, 4}, {8, 4}, {9, 4}, {10, 4}, {11, 4},
      // Sphinx Set (mid-high DW)
      {7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7},
      // Legendary Set (high DW)
      {7, 3}, {8, 3}, {9, 3}, {10, 3}, {11, 3}}},
    // Zienna the Arms Dealer (304) - DK Weapons, Shields & Armor (mid-high level)
    {304,
     {// Swords (Gladius through Heliacal)
      {0, 6}, {0, 7}, {0, 8}, {0, 9}, {0, 10}, {0, 11}, {0, 12},
      // Axes (mid-high)
      {1, 2}, {1, 3}, {1, 4},
      // Maces (mid-high)
      {2, 2}, {2, 3}, {2, 4},
      // Spears
      {3, 2}, {3, 3},
      // Shields
      {6, 0}, {6, 1}, {6, 2}, {6, 3},
      // Plate Set (mid DK)
      {7, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5},
      // Dragon Set (high DK)
      {7, 6}, {8, 6}, {9, 6}, {10, 6}, {11, 6},
      // Skill Orbs
      {12, 20}, {12, 21}, {12, 22}, {12, 23}, {12, 24},
      {12, 7}, {12, 13}, {12, 14}}},  // Twisting Slash, Impale, Greater Fortitude
    // ── Noria Vendors ──
    // Elf Lala (242) - Elf Armor & Skill Orbs
    {242,
     {// Vine Set (Elf beginner armor, index 10)
      {7, 10}, {8, 10}, {9, 10}, {10, 10}, {11, 10},
      // Silk Set (Elf mid armor, index 11)
      {7, 11}, {8, 11}, {9, 11}, {10, 11}, {11, 11},
      // Elf skill orbs (OpenMU Version075)
      {12, 8}, {12, 9}, {12, 10}, {12, 11},
      // Summoning orbs
      {12, 25}, {12, 26}, {12, 27}, {12, 28}, {12, 29}}},
    // Eo the Craftsman (243) - Bows, Crossbows & Ammo
    {243,
     {// Bows (Short Bow through Silver Bow)
      {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5},
      // Crossbows (Crossbow through Bluewing)
      {4, 8}, {4, 9}, {4, 10}, {4, 11}, {4, 12}, {4, 13},
      // Arrows & Bolts (ammo)
      {4, 15}, {4, 7}}}};

void HandleShopOpen(Session &session, const std::vector<uint8_t> &packet,
                    Database &db) {
  if (packet.size() < sizeof(PMSG_SHOP_OPEN_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_SHOP_OPEN_RECV *>(packet.data());

  uint16_t npcType = recv->npcType;

  if (s_shops.find(npcType) == s_shops.end())
    return; // NPC has no shop (guards, etc.)

  session.shopNpcType = npcType;

  const auto &items = s_shops[npcType];

  std::vector<PMSG_SHOP_ITEM> shopItems;
  for (const auto &it : items) {
    auto def = db.GetItemDefinition(it.first, it.second);
    if (def.category == it.first && def.itemIndex == it.second) {
      PMSG_SHOP_ITEM si;
      si.defIndex = def.category * 32 + def.itemIndex;
      si.itemLevel = 0;
      // Send stack price so client zen check matches server charge
      bool isAmmo = (def.category == 4 && (def.itemIndex == 7 || def.itemIndex == 15));
      bool isPotion = (def.category == 14 && def.width == 1 && def.height == 1);
      uint8_t buyStack = isAmmo ? 255 : (isPotion ? 20 : 1);
      si.buyPrice = def.buyPrice * buyStack;
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

// FindEmptySpace is now in InventoryHandler namespace
using InventoryHandler::FindEmptySpace;

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

  // Stack sizes: arrows/bolts = 255 (uint8_t max), potions = 20
  bool isAmmo = (cat == 4 && (idx == 7 || idx == 15));
  bool isPotion = (cat == 14 && def.width == 1 && def.height == 1);
  bool isStackable = isAmmo || isPotion;
  uint8_t maxStack = isAmmo ? 255 : 20;
  uint8_t buyStack = isStackable ? maxStack : 1;

  uint32_t price = def.buyPrice * buyStack;
  if (session.zen < price) {
    printf("[Shop] Buy failed: not enough Zen (need %u, have %u, defIdx=%d)\n",
           price, session.zen, recv->defIndex);
    session.Send(&res, sizeof(res));
    return; // Not enough zen
  }

  // Mounts are unique — only one of each type allowed
  if (cat == 13 && (idx == 2 || idx == 3)) {
    for (int i = 0; i < 64; i++) {
      if (session.bag[i].occupied && session.bag[i].primary &&
          session.bag[i].category == cat && session.bag[i].itemIndex == idx) {
        printf("[Shop] Buy rejected: already owns mount cat=%d idx=%d\n", cat, idx);
        session.Send(&res, sizeof(res));
        return;
      }
    }
  }

  printf("[Shop] Buying item defIdx=%d (qty=%u, price=%u)\n", recv->defIndex,
         buyStack, price);

  // Stackable items: try merge into existing stack first
  if (isStackable) {
    for (int i = 0; i < 64; i++) {
      if (session.bag[i].occupied && session.bag[i].primary &&
          session.bag[i].defIndex == recv->defIndex &&
          session.bag[i].quantity < maxStack) {
        // Merge into existing stack (fill up to max)
        uint8_t space = maxStack - session.bag[i].quantity;
        uint8_t toAdd = std::min(buyStack, space);
        session.zen -= price;
        db.UpdateCharacterMoney(session.characterId, session.zen);
        session.bag[i].quantity += toAdd;
        db.SaveCharacterInventory(
            session.characterId, recv->defIndex, session.bag[i].quantity,
            session.bag[i].itemLevel, static_cast<uint8_t>(i));
        res.result = 1;
        res.quantity = session.bag[i].quantity;
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
      session.bag[s].itemLevel = 0; // Shop items always +0
    }
  }
  session.bag[slot].defIndex = recv->defIndex;
  session.bag[slot].quantity = buyStack;
  session.bag[slot].itemLevel = 0; // Shop items always +0
  session.bag[slot].category = cat;
  session.bag[slot].itemIndex = idx;

  db.SaveCharacterInventory(session.characterId, recv->defIndex,
                            session.bag[slot].quantity, 0, slot);

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

  if (session.shopNpcType == -1) {
    session.Send(&res, sizeof(res));
    return;
  }

  // Determine source: 0-63 = bag slot, 64+ = equipment slot
  bool fromEquip = (recv->bagSlot >= 64);
  uint8_t slot = fromEquip ? (recv->bagSlot - 64) : recv->bagSlot;

  uint8_t itemCat, itemIdx;
  uint8_t itemLevel = 0;
  uint32_t qty = 1;

  if (fromEquip) {
    if (slot >= Session::NUM_EQUIP_SLOTS || session.equipment[slot].category == 0xFF) {
      session.Send(&res, sizeof(res));
      return;
    }
    itemCat = session.equipment[slot].category;
    itemIdx = session.equipment[slot].itemIndex;
    itemLevel = session.equipment[slot].itemLevel;
  } else {
    if (slot >= 64 || !session.bag[slot].primary) {
      session.Send(&res, sizeof(res));
      return;
    }
    itemCat = session.bag[slot].category;
    itemIdx = session.bag[slot].itemIndex;
    itemLevel = session.bag[slot].itemLevel;
    qty = (session.bag[slot].quantity > 1) ? session.bag[slot].quantity : 1;
  }

  auto def = db.GetItemDefinition(itemCat, itemIdx);
  if (def.category != itemCat || def.itemIndex != itemIdx) {
    session.Send(&res, sizeof(res));
    return;
  }

  // Sell price accounts for stack quantity (potions/ammo sold as stacks)
  uint32_t sellPrice = (def.buyPrice * qty) / 3;
  if (sellPrice == 0) {
    sellPrice = std::max(1u, (uint32_t)(def.level * 100));
    if (sellPrice == 0)
      sellPrice = 10;
  }

  session.zen += sellPrice;
  db.UpdateCharacterMoney(session.characterId, session.zen);

  if (fromEquip) {
    // Clear equipment slot
    session.equipment[slot] = {};
    db.UpdateEquipment(session.characterId, slot, 0xFF, 0, 0, 0, 0);
  } else {
    for (int y = 0; y < def.height; ++y) {
      for (int x = 0; x < def.width; ++x) {
        int s = slot + y * 8 + x;
        session.bag[s].occupied = false;
        session.bag[s].primary = false;
        session.bag[s].defIndex = -2;
      }
    }
    db.DeleteCharacterInventoryItem(session.characterId, slot);
  }

  res.result = 1;
  res.zenGained = sellPrice;
  session.Send(&res, sizeof(res));

  InventoryHandler::SendInventorySync(session);
}

} // namespace ShopHandler

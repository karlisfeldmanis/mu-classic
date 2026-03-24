#include "handlers/InventoryHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "handlers/CharacterHandler.hpp"
#include <cstdio>
#include <cstring>

namespace InventoryHandler {

void SendInventorySync(Session &session) {
  // Count primary slots only
  int count = 0;
  for (int i = 0; i < 64; i++)
    if (session.bag[i].occupied && session.bag[i].primary)
      count++;

  size_t totalSize = 4 + 4 + 1 + count * sizeof(PMSG_INVENTORY_ITEM);
  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::INV_SYNC);

  uint32_t zen = session.zen;
  std::memcpy(packet.data() + 4, &zen, 4);
  packet[8] = static_cast<uint8_t>(count);

  auto *items = reinterpret_cast<PMSG_INVENTORY_ITEM *>(packet.data() + 9);
  int idx = 0;
  for (int i = 0; i < 64; i++) {
    if (session.bag[i].occupied && session.bag[i].primary) {
      items[idx].slot = static_cast<uint8_t>(i);
      items[idx].category = session.bag[i].category;
      items[idx].itemIndex = session.bag[i].itemIndex;
      items[idx].quantity = session.bag[i].quantity;
      items[idx].itemLevel = session.bag[i].itemLevel;
      items[idx].optionFlags = session.bag[i].optionFlags;
      idx++;
    }
  }

  session.Send(packet.data(), packet.size());
  printf("[Inventory] Sent inventory sync: %d items, %u zen\n", count,
         session.zen);
}

void LoadInventory(Session &session, Database &db, int characterId) {
  // Clear bag before loading to avoid stale data
  for (int i = 0; i < 64; i++) {
    session.bag[i] = {};
  }
  auto invItems = db.GetCharacterInventory(characterId);
  printf("[Inventory] Loading %zu inventory items from DB for char %d\n",
         invItems.size(), characterId);
  for (auto &item : invItems) {
    if (item.slot < 64) {
      ItemDefinition itemDef = db.GetItemDefinition(item.defIndex);
      int w = itemDef.width > 0 ? itemDef.width : 1;
      int h = itemDef.height > 0 ? itemDef.height : 1;
      int r = item.slot / 8;
      int c = item.slot % 8;

      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int slot = (r + hh) * 8 + (c + ww);
          if (slot < 64) {
            session.bag[slot].defIndex = item.defIndex;
            session.bag[slot].quantity = item.quantity;
            session.bag[slot].itemLevel = item.itemLevel;
            session.bag[slot].optionFlags = item.optionFlags;
            session.bag[slot].occupied = true;
            session.bag[slot].primary = (hh == 0 && ww == 0);
            session.bag[slot].category =
                static_cast<uint8_t>(item.defIndex / 32);
            session.bag[slot].itemIndex =
                static_cast<uint8_t>(item.defIndex % 32);
          }
        }
      }
      if (item.defIndex >= 0) {
        session.bag[item.slot].category =
            static_cast<uint8_t>(item.defIndex / 32);
        session.bag[item.slot].itemIndex =
            static_cast<uint8_t>(item.defIndex % 32);
        auto def = db.GetItemDefinition(session.bag[item.slot].category,
                                        session.bag[item.slot].itemIndex);
        if (def.id > 0) {
          printf("[Inventory] Inv slot %d: defIdx=%d cat=%d idx=%d '%s'\n",
                 item.slot, item.defIndex, def.category, def.itemIndex,
                 def.name.c_str());
        } else {
          printf("[Inventory] Inv slot %d: defIdx=%d (cat=%d idx=%d) - no DB "
                 "def\n",
                 item.slot, item.defIndex, session.bag[item.slot].category,
                 session.bag[item.slot].itemIndex);
        }
      }
    }
  }
}

void HandleInventoryMove(Session &session, const std::vector<uint8_t> &packet,
                         Database &db) {
  if (packet.size() < sizeof(PMSG_INVENTORY_MOVE_RECV))
    return;
  const auto *mv =
      reinterpret_cast<const PMSG_INVENTORY_MOVE_RECV *>(packet.data());

  uint8_t from = mv->fromSlot;
  uint8_t to = mv->toSlot;
  if (from >= 64 || to >= 64)
    return;

  if (session.bag[from].occupied && session.bag[from].primary) {
    int16_t defIdx = session.bag[from].defIndex;
    uint8_t qty = session.bag[from].quantity;
    uint8_t lvl = session.bag[from].itemLevel;
    uint8_t opt = session.bag[from].optionFlags;

    auto itemDef = db.GetItemDefinition(defIdx);
    int w = itemDef.width > 0 ? itemDef.width : 1;
    int h = itemDef.height > 0 ? itemDef.height : 1;

    int fromRow = from / 8, fromCol = from % 8;
    for (int hh = 0; hh < h; hh++) {
      for (int ww = 0; ww < w; ww++) {
        int s = (fromRow + hh) * 8 + (fromCol + ww);
        if (s < 64) {
          session.bag[s].occupied = false;
          session.bag[s].primary = false;
        }
      }
    }

    int toRow = to / 8, toCol = to % 8;
    bool canFit = (toCol + w <= 8 && toRow + h <= 8);
    if (canFit) {
      for (int hh = 0; hh < h && canFit; hh++) {
        for (int ww = 0; ww < w && canFit; ww++) {
          if (session.bag[(toRow + hh) * 8 + (toCol + ww)].occupied)
            canFit = false;
        }
      }
    }

    if (canFit) {
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (toRow + hh) * 8 + (toCol + ww);
          session.bag[s].occupied = true;
          session.bag[s].primary = (hh == 0 && ww == 0);
          session.bag[s].defIndex = defIdx;
          if (session.bag[s].primary) {
            session.bag[s].quantity = qty;
            session.bag[s].itemLevel = lvl;
            session.bag[s].optionFlags = opt;
          }
        }
      }
      db.DeleteCharacterInventoryItem(session.characterId, from);
      db.SaveCharacterInventory(session.characterId, defIdx, qty, lvl, to, opt);
      printf("[Inventory] Server-side move def=%d from %d to %d\n", defIdx,
             from, to);
    } else {
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (fromRow + hh) * 8 + (fromCol + ww);
          session.bag[s].occupied = true;
          session.bag[s].primary = (hh == 0 && ww == 0);
          session.bag[s].defIndex = defIdx;
          if (session.bag[s].primary) {
            session.bag[s].quantity = qty;
            session.bag[s].itemLevel = lvl;
          }
        }
      }
      printf("[Inventory] Authoritative move FAILED for def=%d to %d\n", defIdx,
             to);
      SendInventorySync(session);
    }
  }
}

void HandlePickup(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server, Database &db) {
  if (packet.size() < sizeof(PMSG_PICKUP_RECV))
    return;
  const auto *pick = reinterpret_cast<const PMSG_PICKUP_RECV *>(packet.data());

  auto *drop = world.FindDrop(pick->dropIndex);

  PMSG_PICKUP_RESULT_SEND result{};
  result.h = MakeC1Header(sizeof(result), Opcode::PICKUP_RESULT);
  result.dropIndex = pick->dropIndex;

  if (drop) {
    result.defIndex = drop->defIndex;
    result.quantity = drop->quantity;
    result.itemLevel = drop->itemLevel;
    result.optionFlags = drop->optionFlags;

    if (drop->defIndex == -1) {
      // Zen pickup
      session.zen += drop->quantity;
      db.UpdateCharacterMoney(session.characterId, session.zen);
      result.success = 1;
      session.Send(&result, sizeof(result));
      SendInventorySync(session);
      printf("[Inventory] Zen pickup: +%d (total=%u)\n", drop->quantity,
             session.zen);
    } else {
      uint8_t cat = static_cast<uint8_t>(drop->defIndex / 32);
      uint8_t idx = static_cast<uint8_t>(drop->defIndex % 32);
      ItemDefinition itemDef = db.GetItemDefinition(cat, idx);

      if (itemDef.id == 0) {
        printf("[Inventory] Unknown item defIndex=%d (cat=%d idx=%d)\n",
               drop->defIndex, cat, idx);
      }

      int w = itemDef.width;
      int h = itemDef.height;
      if (w == 0 || h == 0) {
        w = 1;
        h = 1;
      }

      // Stackable items (category 14 = potions): try to merge into existing
      // stack
      static constexpr uint8_t MAX_STACK = 20;
      if (cat == 14 && w == 1 && h == 1) {
        for (int i = 0; i < 64; i++) {
          if (session.bag[i].occupied && session.bag[i].primary &&
              session.bag[i].defIndex == drop->defIndex &&
              session.bag[i].quantity < MAX_STACK) {
            session.bag[i].quantity++;
            db.SaveCharacterInventory(
                session.characterId, drop->defIndex, session.bag[i].quantity,
                session.bag[i].itemLevel, static_cast<uint8_t>(i));
            printf("[Inventory] Stacked potion def=%d in slot %d (qty=%d)\n",
                   drop->defIndex, i, session.bag[i].quantity);
            result.success = 1;
            session.Send(&result, sizeof(result));
            SendInventorySync(session);

            // Remove the drop from world and notify all clients
            world.RemoveDrop(pick->dropIndex);
            PMSG_DROP_REMOVE_SEND rmPkt{};
            rmPkt.h = MakeC1Header(sizeof(rmPkt), Opcode::DROP_REMOVE);
            rmPkt.dropIndex = pick->dropIndex;
            server.Broadcast(&rmPkt, sizeof(rmPkt));
            return;
          }
        }
      }

      bool placed = false;
      int startSlot = 0;

      for (int r = 0; r <= 8 - h; r++) {
        for (int c = 0; c <= 8 - w; c++) {
          bool fits = true;
          for (int rr = 0; rr < h; rr++) {
            for (int cc = 0; cc < w; cc++) {
              int slot = (r + rr) * 8 + (c + cc);
              if (session.bag[slot].occupied) {
                fits = false;
                break;
              }
            }
            if (!fits)
              break;
          }

          if (fits) {
            startSlot = r * 8 + c;
            for (int rr = 0; rr < h; rr++) {
              for (int cc = 0; cc < w; cc++) {
                int s = (r + rr) * 8 + (c + cc);
                session.bag[s].occupied = true;
                session.bag[s].primary = (rr == 0 && cc == 0);
                session.bag[s].category = cat;
                session.bag[s].itemIndex = idx;
                session.bag[s].defIndex = drop->defIndex;
                session.bag[s].quantity = drop->quantity;
                session.bag[s].itemLevel = drop->itemLevel;
                session.bag[s].optionFlags = drop->optionFlags;
              }
            }
            db.SaveCharacterInventory(session.characterId, drop->defIndex,
                                      drop->quantity, drop->itemLevel,
                                      static_cast<uint8_t>(startSlot),
                                      drop->optionFlags);
            printf(
                "[Inventory] Pickup: def=%d (cat=%d idx=%d) -> bag slot %d\n",
                drop->defIndex, cat, idx, startSlot);
            placed = true;
            goto done_placement;
          }
        }
      }
    done_placement:;

      if (placed) {
        result.success = 1;
        session.Send(&result, sizeof(result));
        SendInventorySync(session);
      } else {
        result.success = 0;
        session.Send(&result, sizeof(result));
        printf("[Inventory] Bag full, cannot pick up item def=%d\n",
               drop->defIndex);
      }
    }

    printf("[Inventory] Pickup drop %d by fd=%d (def=%d qty=%d +%d)\n",
           pick->dropIndex, session.GetFd(), drop->defIndex, drop->quantity,
           drop->itemLevel);

    // Only remove drop from world if pickup succeeded
    if (result.success) {
      world.RemoveDrop(pick->dropIndex);
      PMSG_DROP_REMOVE_SEND rmPkt{};
      rmPkt.h = MakeC1Header(sizeof(rmPkt), Opcode::DROP_REMOVE);
      rmPkt.dropIndex = pick->dropIndex;
      server.Broadcast(&rmPkt, sizeof(rmPkt));
    }
  } else {
    result.success = 0;
    result.defIndex = 0;
    result.quantity = 0;
    result.itemLevel = 0;
    session.Send(&result, sizeof(result));
  }
}

void HandleItemUse(Session &session, const std::vector<uint8_t> &packet,
                   Database &db) {
  if (packet.size() < sizeof(PMSG_ITEM_USE_RECV))
    return;
  const auto *req = reinterpret_cast<const PMSG_ITEM_USE_RECV *>(packet.data());

  if (req->slot >= 64)
    return;

  if (session.dead)
    return;

  if (session.potionCooldown > 0.0f) {
    printf("[Inventory] Rejecting item use fd=%d: Cooldown active (%.1fs)\n",
           session.GetFd(), session.potionCooldown);
    return;
  }

  auto &item = session.bag[req->slot];
  if (!item.occupied || !item.primary) {
    printf("[Inventory] Rejecting item use fd=%d: Slot %d empty or secondary\n",
           session.GetFd(), req->slot);
    return;
  }

  // Skill orbs (category 12) — learn skill on use
  if (item.category == 12) {
    // Map orb itemIndex to skill ID
    static const struct {
      uint8_t orbIndex;
      uint8_t skillId;
    } orbSkillMap[] = {
        {20, 19}, // Orb of Falling Slash → Falling Slash
        {21, 20}, // Orb of Lunge → Lunge
        {22, 21}, // Orb of Uppercut → Uppercut
        {23, 22}, // Orb of Cyclone → Cyclone
        {24, 23}, // Orb of Slash → Slash
        {7, 41},  // Orb of Twisting Slash → Twisting Slash
        {12, 42}, // Orb of Rageful Blow → Rageful Blow
        {19, 43}, // Orb of Death Stab → Death Stab
        // Elf orbs (OpenMU Version075)
        {8, 26},  // Orb of Healing → Heal
        {9, 27},  // Orb of Greater Defense → Greater Defense
        {10, 28}, // Orb of Greater Damage → Greater Damage
        {11, 30}, // Orb of Goblin → Summon Goblin
        {25, 31}, // Orb of Golem Summoning → Summon Stone Golem
        {26, 32}, // Orb of Assassin Summoning → Summon Assassin
        {27, 33}, // Orb of Yeti Summoning → Summon Elite Yeti
        {28, 34}, // Orb of Knight Summoning → Summon Dark Knight
        {29, 35}, // Orb of Bali Summoning → Summon Bali
    };

    uint8_t skillId = 0;
    for (auto &m : orbSkillMap) {
      if (m.orbIndex == item.itemIndex) {
        skillId = m.skillId;
        break;
      }
    }

    if (skillId > 0) {
      if (db.HasSkill(session.characterId, skillId)) {
        printf("[Inventory] fd=%d already knows skill %d\n", session.GetFd(),
               skillId);
        return;
      }

      // Check requirements (level, stats, class)
      ItemDefinition itemDef = db.GetItemDefinition(item.category, item.itemIndex);
      if (session.level < itemDef.level) {
        printf("[Inventory] fd=%d orb rejected: level %d < %d required\n",
               session.GetFd(), session.level, itemDef.level);
        return;
      }
      if (session.energy < itemDef.reqEnergy) {
        printf("[Inventory] fd=%d orb rejected: energy %d < %d required\n",
               session.GetFd(), session.energy, itemDef.reqEnergy);
        return;
      }
      int bitIndex = session.classCode >> 4;
      if (!(itemDef.classFlags & (1 << bitIndex))) {
        printf("[Inventory] fd=%d orb rejected: class mismatch (class=%d flags=0x%X)\n",
               session.GetFd(), session.classCode, itemDef.classFlags);
        return;
      }

      // Learn the skill
      db.LearnSkill(session.characterId, skillId);
      session.learnedSkills.push_back(skillId);

      // Consume the orb
      if (item.quantity > 1) {
        item.quantity--;
        db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel, req->slot);
      } else {
        session.bag[req->slot] = {};
        db.DeleteCharacterInventoryItem(session.characterId, req->slot);
      }

      // Send updated skill list and inventory
      CharacterHandler::SendSkillList(session);
      SendInventorySync(session);

      printf("[Inventory] fd=%d learned skill %d from orb idx=%d\n",
             session.GetFd(), skillId, item.itemIndex);
      return;
    }
  }

  // DW Scrolls (category 15) — learn spell on use
  if (item.category == 15) {
    // Map scroll itemIndex to spell skill ID (OpenMU Version075)
    static const struct {
      uint8_t scrollIndex;
      uint8_t skillId;
    } scrollSkillMap[] = {
        {0, 1},   // Scroll of Poison → Poison
        {1, 2},   // Scroll of Meteorite → Meteorite
        {2, 3},   // Scroll of Lightning → Lightning
        {3, 4},   // Scroll of Fire Ball → Fire Ball
        {4, 5},   // Scroll of Flame → Flame
        {5, 6},   // Scroll of Teleport → Teleport
        {6, 7},   // Scroll of Ice → Ice
        {7, 8},   // Scroll of Twister → Twister
        {8, 9},   // Scroll of Evil Spirit → Evil Spirit
        {9, 10},  // Scroll of Hellfire → Hellfire
        {11, 12}, // Scroll of Aqua Beam → Aqua Beam
        {12, 13}, // Scroll of Cometfall → Cometfall
        {13, 14}, // Scroll of Inferno → Inferno
    };

    uint8_t skillId = 0;
    for (auto &m : scrollSkillMap) {
      if (m.scrollIndex == item.itemIndex) {
        skillId = m.skillId;
        break;
      }
    }

    if (skillId > 0) {
      if (db.HasSkill(session.characterId, skillId)) {
        printf("[Inventory] fd=%d already knows spell %d\n", session.GetFd(),
               skillId);
        return;
      }

      // Check requirements (level, energy, class)
      ItemDefinition itemDef = db.GetItemDefinition(item.category, item.itemIndex);
      if (session.level < itemDef.level) {
        printf("[Inventory] fd=%d scroll rejected: level %d < %d required\n",
               session.GetFd(), session.level, itemDef.level);
        return;
      }
      if (session.energy < itemDef.reqEnergy) {
        printf("[Inventory] fd=%d scroll rejected: energy %d < %d required\n",
               session.GetFd(), session.energy, itemDef.reqEnergy);
        return;
      }
      int bitIndex = session.classCode >> 4;
      if (!(itemDef.classFlags & (1 << bitIndex))) {
        printf("[Inventory] fd=%d scroll rejected: class mismatch (class=%d flags=0x%X)\n",
               session.GetFd(), session.classCode, itemDef.classFlags);
        return;
      }

      // Learn the spell
      db.LearnSkill(session.characterId, skillId);
      session.learnedSkills.push_back(skillId);

      // Consume the scroll
      if (item.quantity > 1) {
        item.quantity--;
        db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel, req->slot);
      } else {
        session.bag[req->slot] = {};
        db.DeleteCharacterInventoryItem(session.characterId, req->slot);
      }

      // Send updated skill list and inventory
      CharacterHandler::SendSkillList(session);
      SendInventorySync(session);

      printf("[Inventory] fd=%d learned spell %d from scroll idx=%d\n",
             session.GetFd(), skillId, item.itemIndex);
      return;
    }
  }

  if (item.category == 14) {
    // HP potions (itemIndex 0-3)
    int healAmount = 0;
    if (item.itemIndex == 0)
      healAmount = 10;
    else if (item.itemIndex == 1)
      healAmount = 20;
    else if (item.itemIndex == 2)
      healAmount = 50;
    else if (item.itemIndex == 3)
      healAmount = 100;

    // Mana potions (itemIndex 4-6)
    int manaAmount = 0;
    if (item.itemIndex == 4)
      manaAmount = 20;  // Small Mana Potion
    else if (item.itemIndex == 5)
      manaAmount = 50;  // Medium Mana Potion
    else if (item.itemIndex == 6)
      manaAmount = 100; // Large Mana Potion

    if (healAmount > 0) {
      if (session.hp >= session.maxHp) {
        printf(
            "[Inventory] Rejecting item use fd=%d: HP already full (%d/%d)\n",
            session.GetFd(), session.hp, session.maxHp);
        return;
      }

      session.hp += healAmount;
      if (session.hp > session.maxHp)
        session.hp = session.maxHp;

      printf("[Inventory] Item used fd=%d: Healed %d HP. New HP: %d/%d. "
             "Cooldown started.\n",
             session.GetFd(), healAmount, session.hp, session.maxHp);
    } else if (manaAmount > 0) {
      // DK uses AG instead of mana — mana potions restore AG for DK
      bool isDK = (session.classCode == 16);
      if (isDK) {
        if (session.ag >= session.maxAg) {
          printf("[Inventory] Rejecting item use fd=%d: AG already full (%d/%d)\n",
                 session.GetFd(), session.ag, session.maxAg);
          return;
        }
        session.ag += manaAmount;
        if (session.ag > session.maxAg)
          session.ag = session.maxAg;
        printf("[Inventory] Item used fd=%d: Restored %d AG. New AG: %d/%d. "
               "Cooldown started.\n",
               session.GetFd(), manaAmount, session.ag, session.maxAg);
      } else {
        if (session.mana >= session.maxMana) {
          printf("[Inventory] Rejecting item use fd=%d: Mana already full (%d/%d)\n",
                 session.GetFd(), session.mana, session.maxMana);
          return;
        }
        session.mana += manaAmount;
        if (session.mana > session.maxMana)
          session.mana = session.maxMana;
        printf("[Inventory] Item used fd=%d: Restored %d Mana. New Mana: %d/%d. "
               "Cooldown started.\n",
               session.GetFd(), manaAmount, session.mana, session.maxMana);
      }
    } else {
      return; // Unknown potion type
    }

    // Common: start cooldown, consume item, sync client
    session.potionCooldown = 30.0f;

    if (item.quantity > 1) {
      item.quantity--;
      db.SaveCharacterInventory(session.characterId, item.defIndex,
                                item.quantity, item.itemLevel, req->slot);
    } else {
      ItemDefinition def = db.GetItemDefinition(item.defIndex);
      int w = def.width > 0 ? def.width : 1;
      int h = def.height > 0 ? def.height : 1;
      int r = req->slot / 8;
      int c = req->slot % 8;
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (r + hh) * 8 + (c + ww);
          if (s < 64)
            session.bag[s] = {};
        }
      }
      db.DeleteCharacterInventoryItem(session.characterId, req->slot);
    }

    CharacterHandler::SendCharStats(session);
    SendInventorySync(session);

    {
      uint8_t posX = static_cast<uint8_t>(session.worldZ / 100.0f);
      uint8_t posY = static_cast<uint8_t>(session.worldX / 100.0f);
      db.SaveCharacterFull(
          session.characterId, session.level, session.strength,
          session.dexterity, session.vitality, session.energy,
          static_cast<uint16_t>(session.hp),
          static_cast<uint16_t>(session.maxHp),
          static_cast<uint16_t>(std::max(session.mana, 0)),
          static_cast<uint16_t>(session.maxMana),
          static_cast<uint16_t>(std::max(session.ag, 0)),
          static_cast<uint16_t>(session.maxAg), session.levelUpPoints,
          session.experience, session.zen, posX, posY, session.mapId,
          session.skillBar, session.potionBar, session.rmcSkillId, -1, &session);
    }
  }
}

void HandleItemDrop(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world, Server &server, Database &db) {
  if (packet.size() < sizeof(PMSG_ITEM_DROP_RECV))
    return;
  const auto *req =
      reinterpret_cast<const PMSG_ITEM_DROP_RECV *>(packet.data());

  if (req->slot >= 64)
    return;
  if (session.dead)
    return;

  auto &item = session.bag[req->slot];
  if (!item.occupied || !item.primary) {
    printf("[Inventory] Drop rejected fd=%d: slot %d empty\n", session.GetFd(),
           req->slot);
    return;
  }

  int16_t defIndex = item.defIndex;
  uint8_t quantity = item.quantity;
  uint8_t itemLevel = item.itemLevel;

  // Get item dimensions to clear all occupied slots
  ItemDefinition def = db.GetItemDefinition(defIndex);
  int w = def.width > 0 ? def.width : 1;
  int h = def.height > 0 ? def.height : 1;
  int r = req->slot / 8;
  int c = req->slot % 8;

  // Clear all slots occupied by this item
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s < 64)
        session.bag[s] = {};
    }
  }
  db.DeleteCharacterInventoryItem(session.characterId, req->slot);

  // Spawn drop on the ground near the player
  GroundDrop drop{};
  drop.index = world.AllocDropIndex();
  drop.defIndex = defIndex;
  drop.quantity = quantity;
  drop.itemLevel = itemLevel;
  drop.worldX = session.worldX + (float)(rand() % 60 - 30);
  drop.worldZ = session.worldZ + (float)(rand() % 60 - 30);
  drop.age = 0.0f;
  world.AddDrop(drop);

  // Broadcast drop spawn to all clients
  PMSG_DROP_SPAWN_SEND spawnPkt{};
  spawnPkt.h = MakeC1Header(sizeof(spawnPkt), Opcode::DROP_SPAWN);
  spawnPkt.dropIndex = drop.index;
  spawnPkt.defIndex = drop.defIndex;
  spawnPkt.quantity = drop.quantity;
  spawnPkt.itemLevel = drop.itemLevel;
  spawnPkt.worldX = drop.worldX;
  spawnPkt.worldZ = drop.worldZ;
  server.Broadcast(&spawnPkt, sizeof(spawnPkt));

  // Sync inventory to client
  SendInventorySync(session);

  printf("[Inventory] Player fd=%d dropped item def=%d from slot %d at "
         "(%.0f,%.0f)\n",
         session.GetFd(), defIndex, req->slot, drop.worldX, drop.worldZ);
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

} // namespace InventoryHandler

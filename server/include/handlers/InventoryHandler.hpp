#ifndef MU_INVENTORY_HANDLER_HPP
#define MU_INVENTORY_HANDLER_HPP

#include "../Database.hpp"
#include "../GameWorld.hpp"
#include "../Session.hpp"
#include <cstdint>
#include <vector>

class Server;

namespace InventoryHandler {

// Send full inventory sync to client
void SendInventorySync(Session &session);

// Load inventory from DB into session bag
void LoadInventory(Session &session, Database &db, int characterId);

// Find empty space in inventory grid for an item of given dimensions
bool FindEmptySpace(Session &session, uint8_t w, uint8_t h, uint8_t &outSlot);

// Packet handlers
void HandleInventoryMove(Session &session, const std::vector<uint8_t> &packet,
                         Database &db);
void HandlePickup(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server, Database &db);
void HandleItemUse(Session &session, const std::vector<uint8_t> &packet,
                   Database &db);
void HandleItemDrop(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world, Server &server, Database &db);
void HandleItemUpgrade(Session &session, const std::vector<uint8_t> &packet,
                       Database &db);

} // namespace InventoryHandler

#endif // MU_INVENTORY_HANDLER_HPP

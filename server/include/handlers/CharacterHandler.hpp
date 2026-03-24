#ifndef MU_CHARACTER_HANDLER_HPP
#define MU_CHARACTER_HANDLER_HPP

#include "../Database.hpp"
#include "../Session.hpp"
#include <cstdint>
#include <vector>

namespace CharacterHandler {

// Send full character stats packet (loads from DB, syncs session)
void SendCharStats(Session &session, Database &db, int characterId);

// Send character stats from current session state (no DB reload)
void SendCharStats(Session &session);

// Send equipment list to client
void SendEquipment(Session &session, Database &db, int characterId);

// Recalculate weapon damage and defense from equipment DB
// Used by HandleCharSelect, HandleEquip, and OnClientConnected
void RefreshCombatStats(Session &session, Database &db, int characterId);

// Send learned skill list to client
void SendSkillList(Session &session);

// Send full item definition catalog to client
void SendItemCatalog(Session &session, Database &db);

// Packet handlers
void HandleCharSave(Session &session, const std::vector<uint8_t> &packet,
                    Database &db);
void HandleEquip(Session &session, const std::vector<uint8_t> &packet,
                 Database &db);
void HandleStatAlloc(Session &session, const std::vector<uint8_t> &packet,
                     Database &db);

} // namespace CharacterHandler

#endif // MU_CHARACTER_HANDLER_HPP

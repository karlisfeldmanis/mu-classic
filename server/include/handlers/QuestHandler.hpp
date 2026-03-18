#ifndef MU_QUEST_HANDLER_HPP
#define MU_QUEST_HANDLER_HPP

#include "Database.hpp"
#include "Session.hpp"
#include <cstdint>
#include <vector>

class Server;

namespace QuestHandler {

// Send full quest catalog to client (on login)
void SendQuestCatalog(Session &session);

// Send current quest state to client (on login + after changes)
void SendQuestState(Session &session);

// C->S: Player accepts quest by ID
void HandleQuestAccept(Session &session, const std::vector<uint8_t> &packet,
                       Database &db);

// C->S: Player completes (turns in) quest by ID
void HandleQuestComplete(Session &session, const std::vector<uint8_t> &packet,
                         Database &db, Server &server);

// C->S: Player abandons active quest by ID
void HandleQuestAbandon(Session &session, const std::vector<uint8_t> &packet,
                        Database &db);

// Called by CombatHandler when a monster is killed
void OnMonsterKill(Session &session, uint16_t monsterType, Database &db);

} // namespace QuestHandler

#endif // MU_QUEST_HANDLER_HPP

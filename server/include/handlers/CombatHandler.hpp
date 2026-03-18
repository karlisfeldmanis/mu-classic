#ifndef MU_COMBAT_HANDLER_HPP
#define MU_COMBAT_HANDLER_HPP

#include "../GameWorld.hpp"
#include "../Session.hpp"
#include <cstdint>
#include <vector>

class Server;

namespace CombatHandler {

void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server);
void HandleSkillAttack(Session &session, const std::vector<uint8_t> &packet,
                       GameWorld &world, Server &server);
void HandleTeleport(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world, Server &server);

} // namespace CombatHandler

#endif // MU_COMBAT_HANDLER_HPP

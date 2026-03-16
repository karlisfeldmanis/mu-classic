#include "Server.hpp"
#include "PacketDefs.hpp"
#include "PacketHandler.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/CharacterSelectHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include "handlers/WorldHandler.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

static const char *GetMonsterName(uint16_t type) {
  static const std::unordered_map<uint16_t, const char *> names = {
      {0, "Bull Fighter"},     {1, "Hound"},
      {2, "Budge Dragon"},     {3, "Spider"},
      {4, "Elite Bull Fighter"},{5, "Hell Hound"},
      {6, "Lich"},             {7, "Giant"},
      {8, "Poison Bull"},      {9, "Thunder Lich"},
      {10, "Dark Knight"},     {11, "Ghost"},
      {12, "Larva"},           {13, "Hell Spider"},
      {14, "Skeleton Warrior"},{15, "Skeleton Archer"},
      {16, "Elite Skeleton"},  {17, "Cyclops"},
      {18, "Gorgon"},
      {19, "Yeti"},           {20, "Elite Yeti"},
      {21, "Assassin"},       {22, "Ice Monster"},
      {23, "Hommerd"},        {24, "Worm"},
      {25, "Ice Queen"}};
  auto it = names.find(type);
  return it != names.end() ? it->second : "Monster";
}

static volatile bool g_sigint = false;
static void sigHandler(int) { g_sigint = true; }

bool Server::Start(uint16_t port) {
  // Open database
  if (!m_db.Open("mu_server.db")) {
    printf("[Server] Failed to open database\n");
    return false;
  }
  m_db.CreateDefaultAccount();
  m_db.SeedNpcSpawns();
  m_db.SeedMonsterSpawns();
  m_db.SeedItemDefinitions();

  // No longer seeding default equipment by name; use DB status

  // Load terrain attributes for both maps (walkability checks / monster AI)
  // CMake symlinks client/Data/ into server/build/Data/
  m_world.LoadTerrainAttributesForMap(0, "Data/World1/EncTerrain1.att");
  m_world.LoadTerrainAttributesForMap(1, "Data/World2/EncTerrain2.att");
  m_world.LoadTerrainAttributesForMap(2, "Data/World3/EncTerrain3.att");
  m_world.SetActiveMap(0);

  // Load NPC and monster data from database
  m_world.LoadNpcsFromDB(m_db, 0); // map 0 = Lorencia
  m_world.LoadMonstersFromDB(m_db, 0);

  // Create listen socket
  m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenFd < 0) {
    perror("[Server] socket");
    return false;
  }

  int opt = 1;
  setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Set non-blocking
  int flags = fcntl(m_listenFd, F_GETFL, 0);
  fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    perror("[Server] bind");
    close(m_listenFd);
    return false;
  }

  if (listen(m_listenFd, 16) < 0) {
    perror("[Server] listen");
    close(m_listenFd);
    return false;
  }

  printf("[Server] Listening on port %d\n", port);
  m_running = true;
  return true;
}

void Server::Run() {
  signal(SIGINT, sigHandler);
  signal(SIGPIPE, SIG_IGN); // Ignore broken pipe

  srand(static_cast<unsigned int>(time(NULL)));

  auto lastTick = std::chrono::steady_clock::now();
  float autosaveTimer = 0.0f;
  static constexpr float AUTOSAVE_INTERVAL =
      60.0f; // Save all characters every 60s

  while (m_running && !g_sigint) {
    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;

    // Game tick: update monster states, drop aging, wander AI, guard patrol
    std::vector<GameWorld::MonsterMoveUpdate> wanderMoves;
    std::vector<GameWorld::NpcMoveUpdate> npcMoves;
    m_world.Update(
        dt,
        [this](uint16_t dropIndex) {
          // Drop expired — broadcast removal to all clients
          PMSG_DROP_REMOVE_SEND pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), Opcode::DROP_REMOVE);
          pkt.dropIndex = dropIndex;
          Broadcast(&pkt, sizeof(pkt));
        },
        &wanderMoves, &npcMoves,
        [this](uint16_t monsterIndex) {
          // Guard killed a monster — broadcast death (no XP reward)
          PMSG_MONSTER_DEATH_SEND deathPkt{};
          deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
          deathPkt.monsterIndex = monsterIndex;
          deathPkt.killerCharId = 0; // Guard kill, no player credit
          deathPkt.xpReward = 0;
          Broadcast(&deathPkt, sizeof(deathPkt));
        });

    // Broadcast wander moves to all clients
    for (auto &mv : wanderMoves) {
      PMSG_MONSTER_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::MON_MOVE);
      movePkt.monsterIndex = mv.monsterIndex;
      movePkt.targetX = mv.targetX;
      movePkt.targetY = mv.targetY;
      movePkt.chasing = mv.chasing;
      Broadcast(&movePkt, sizeof(movePkt));
    }

    // Broadcast guard patrol moves to all clients
    for (auto &mv : npcMoves) {
      PMSG_NPC_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::NPC_MOVE);
      movePkt.npcIndex = mv.npcIndex;
      movePkt.targetX = mv.targetX;
      movePkt.targetY = mv.targetY;
      Broadcast(&movePkt, sizeof(movePkt));
    }

    // Check for monster respawns and broadcast them
    for (auto &mon : const_cast<std::vector<MonsterInstance> &>(
             m_world.GetMonsterInstances())) {
      if (mon.justRespawned) {
        mon.justRespawned = false;
        PMSG_MONSTER_RESPAWN_SEND pkt{};
        pkt.h = MakeC1Header(sizeof(pkt), Opcode::MON_RESPAWN);
        pkt.monsterIndex = mon.index;
        pkt.x = mon.gridX;
        pkt.y = mon.gridY;
        pkt.hp = static_cast<uint16_t>(mon.hp);
        Broadcast(&pkt, sizeof(pkt));
      }
    }

    // Monster AI: aggro + attack players
    {
      std::vector<GameWorld::PlayerTarget> targets;
      for (auto &s : m_sessions) {
        if (!s->IsAlive() || !s->inWorld)
          continue;
        GameWorld::PlayerTarget pt;
        pt.fd = s->GetFd();
        pt.worldX = s->worldX;
        pt.worldZ = s->worldZ;
        pt.gridX = static_cast<uint8_t>(s->worldZ / 100.0f);
        pt.gridY = static_cast<uint8_t>(s->worldX / 100.0f);
        CharacterClass cls = static_cast<CharacterClass>(s->classCode);
        pt.defense = StatCalculator::CalculateDefense(cls, s->dexterity) +
                     s->totalDefense;
        pt.defenseRate =
            StatCalculator::CalculateDefenseRate(cls, s->dexterity);
        pt.life = s->hp;
        pt.dead = s->dead;
        pt.level = s->level;
        pt.petDamageReduction = s->petDamageReduction;
        targets.push_back(pt);
      }
      std::vector<GameWorld::MonsterMoveUpdate> moves;
      auto attacks = m_world.ProcessMonsterAI(dt, targets, moves);

      // Broadcast monster target updates to all clients (event-driven)
      for (auto &mv : moves) {
        PMSG_MONSTER_MOVE_SEND movePkt{};
        movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::MON_MOVE);
        movePkt.monsterIndex = mv.monsterIndex;
        movePkt.targetX = mv.targetX;
        movePkt.targetY = mv.targetY;
        movePkt.chasing = mv.chasing;
        Broadcast(&movePkt, sizeof(movePkt));
      }

      for (auto &atk : attacks) {
        // Find the target session and apply damage server-side
        for (auto &s : m_sessions) {
          if (s->GetFd() == atk.targetFd && s->IsAlive()) {
            // Subtract damage from server-tracked HP
            s->hp -= atk.damage;
            if (s->hp <= 0) {
              s->hp = 0;
              s->dead = true;
              // Player died: drop aggro and return to spawn (evade mode)
              auto *mon = m_world.FindMonster(atk.monsterIndex);
              if (mon) {
                mon->aggroTargetFd = -1;
                mon->aiState = MonsterInstance::AIState::RETURNING;
                mon->evading = true;
                mon->currentPath.clear();
                mon->pathStep = 0;
                printf("[Combat] Mon %d killed player fd=%d (mon HP=%d/%d)\n",
                       mon->index, s->GetFd(), mon->hp, mon->maxHp);
              }
            }

            // AG recovery when hit (DK only)
            if (atk.damage > 0 && s->classCode == 16 && s->ag < s->maxAg) {
              int agGain = std::max(1, s->maxAg / 30); // ~3% of maxAG
              s->ag = std::min(s->ag + agGain, s->maxAg);
              CharacterHandler::SendCharStats(*s);
            }

            // Send monster attack packet to client
            PMSG_MONSTER_ATTACK_SEND pkt{};
            pkt.h = MakeC1Header(sizeof(pkt), Opcode::MON_ATTACK);
            pkt.monsterIndex = atk.monsterIndex;
            pkt.damage = atk.damage;
            pkt.remainingHp = static_cast<float>(s->hp);
            s->Send(&pkt, sizeof(pkt));

            // Save monster→player damage to chat log
            {
              auto *atkMon = m_world.FindMonster(atk.monsterIndex);
              char chatBuf[128];
              const char *mName = atkMon
                  ? GetMonsterName(atkMon->type) : "Monster";
              if (atk.damage > 0)
                snprintf(chatBuf, sizeof(chatBuf), "%s hits you for %d damage.",
                         mName, atk.damage);
              else
                snprintf(chatBuf, sizeof(chatBuf), "%s misses you.", mName);
              // color: 0xFF8C8CFF = IM_COL32(255, 140, 140, 255) (light red)
              m_db.SaveChatMessage(s->characterId, 1, 0xFF8C8CFF, chatBuf);
            }
            break;
          }
        }
      }
    }

    // Process poison DoT ticks and broadcast damage
    auto poisonTicks = m_world.ProcessPoisonTicks(dt);
    for (auto &tick : poisonTicks) {
      // Broadcast poison damage to all clients (damageType=4 = poison green)
      PMSG_DAMAGE_SEND dmgPkt{};
      dmgPkt.h = MakeC1Header(sizeof(dmgPkt), Opcode::DAMAGE);
      dmgPkt.monsterIndex = tick.monsterIndex;
      dmgPkt.damage = tick.damage;
      dmgPkt.damageType = 4; // Poison (client renders green)
      dmgPkt.remainingHp = tick.remainingHp;

      // Find attacker session for charId and XP
      Session *attacker = nullptr;
      for (auto &s : m_sessions) {
        if (s->GetFd() == tick.attackerFd && s->IsAlive()) {
          attacker = s.get();
          break;
        }
      }
      dmgPkt.attackerCharId = attacker
          ? static_cast<uint16_t>(attacker->characterId) : 0;
      Broadcast(&dmgPkt, sizeof(dmgPkt));

      // If poison killed the monster, handle death/XP/drops
      auto *mon = m_world.FindMonster(tick.monsterIndex);
      if (mon && mon->aiState == MonsterInstance::AIState::DYING &&
          mon->hp <= 0 && attacker) {
        int xp = ServerConfig::CalculateXP(attacker->level, mon->level);

        PMSG_MONSTER_DEATH_SEND deathPkt{};
        deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
        deathPkt.monsterIndex = mon->index;
        deathPkt.killerCharId = static_cast<uint16_t>(attacker->characterId);
        deathPkt.xpReward = static_cast<uint32_t>(xp);
        Broadcast(&deathPkt, sizeof(deathPkt));

        attacker->experience += xp;
        bool leveledUp = false;
        while (true) {
          uint64_t nextXP = Database::GetXPForLevel(attacker->level);
          if (attacker->experience >= nextXP && attacker->level < 400) {
            attacker->level++;
            CharacterClass cls =
                static_cast<CharacterClass>(attacker->classCode);
            attacker->levelUpPoints += StatCalculator::GetLevelUpPoints(cls);
            attacker->maxHp = StatCalculator::CalculateMaxHP(
                cls, attacker->level, attacker->vitality) +
                attacker->petBonusMaxHp;
            attacker->maxMana = StatCalculator::CalculateMaxMP(
                cls, attacker->level, attacker->energy);
            attacker->maxAg = StatCalculator::CalculateMaxAG(
                attacker->strength, attacker->dexterity,
                attacker->vitality, attacker->energy);
            attacker->hp = attacker->maxHp;
            attacker->mana = attacker->maxMana;
            attacker->ag = attacker->maxAg;
            leveledUp = true;
          } else {
            break;
          }
        }
        if (leveledUp || xp > 0)
          CharacterHandler::SendCharStats(*attacker);

        auto drops = m_world.SpawnDrops(mon->worldX, mon->worldZ, mon->level,
                                        mon->type, m_db);
        for (auto &drop : drops) {
          PMSG_DROP_SPAWN_SEND dropPkt{};
          dropPkt.h = MakeC1Header(sizeof(dropPkt), Opcode::DROP_SPAWN);
          dropPkt.dropIndex = drop.index;
          dropPkt.defIndex = drop.defIndex;
          dropPkt.quantity = drop.quantity;
          dropPkt.itemLevel = drop.itemLevel;
          dropPkt.worldX = drop.worldX;
          dropPkt.worldZ = drop.worldZ;
          Broadcast(&dropPkt, sizeof(dropPkt));
        }
        printf("[Poison] Mon %d killed by poison (fd=%d, xp=%d, drops=%zu)\n",
               mon->index, tick.attackerFd, xp, drops.size());
      }
    }

    // Periodic autosave (every 60 seconds)
    autosaveTimer += dt;
    if (autosaveTimer >= AUTOSAVE_INTERVAL) {
      autosaveTimer = 0.0f;
      int saved = 0;
      for (auto &s : m_sessions) {
        if (s->IsAlive() && s->inWorld) {
          SaveSession(*s);
          saved++;
        }
      }
      if (saved > 0)
        printf("[Server] Autosave: saved %d character(s)\n", saved);
    }

    // Build poll fd array: listen socket + all sessions
    std::vector<struct pollfd> fds;
    fds.push_back({m_listenFd, POLLIN, 0});
    for (auto &s : m_sessions) {
      short events = POLLIN;
      events |= POLLOUT;
      fds.push_back({s->GetFd(), events, 0});
    }

    int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()),
                   16); // 16ms for ~60Hz tick
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      perror("[Server] poll");
      break;
    }

    // Check listen socket
    if (fds[0].revents & POLLIN) {
      AcceptNewClients();
    }

    // Process sessions
    for (size_t i = 0; i < m_sessions.size(); i++) {
      auto &session = m_sessions[i];
      auto &pfd = fds[i + 1];

      // Tick cooldowns
      if (session->potionCooldown > 0.0f) {
        session->potionCooldown -= dt;
        if (session->potionCooldown < 0.0f)
          session->potionCooldown = 0.0f;
      }
      if (session->attackCooldown > 0.0f) {
        session->attackCooldown -= dt;
        if (session->attackCooldown < 0.0f)
          session->attackCooldown = 0.0f;
      }
      if (session->gateTransitionCooldown > 0.0f) {
        session->gateTransitionCooldown -= dt;
        if (session->gateTransitionCooldown < 0.0f)
          session->gateTransitionCooldown = 0.0f;
      }

      // Deferred viewport send after map transition
      // (gives client time to process MAP_CHANGE and reload terrain)
      if (session->pendingViewportDelay > 0.0f) {
        session->pendingViewportDelay -= dt;
        if (session->pendingViewportDelay <= 0.0f) {
          session->pendingViewportDelay = 0.0f;
          WorldHandler::SendNpcViewport(*session, m_world);
          auto v2pkt = m_world.BuildMonsterViewportV2Packet();
          if (!v2pkt.empty())
            session->Send(v2pkt.data(), v2pkt.size());
          printf("[Server] Deferred viewport sent: %zu NPCs, %zu monsters\n",
                 m_world.GetNpcs().size(),
                 m_world.GetMonsterInstances().size());
        }
      }

      // Safe Zone HP Regeneration (~2% per second)
      // Works while walking — don't reset accumulator on brief boundary flicker
      bool inSafe = m_world.IsSafeZone(session->worldX, session->worldZ);
      static float szDbgTimer = 0.0f;
      szDbgTimer += dt;
      if (szDbgTimer >= 3.0f && session->inWorld && session->hp < session->maxHp) {
        printf("[SafeZone] fd=%d wX=%.1f wZ=%.1f inSafe=%d hp=%d/%d\n",
               session->GetFd(), session->worldX, session->worldZ,
               inSafe, session->hp, session->maxHp);
        szDbgTimer = 0.0f;
      }
      if (session->inWorld && !session->dead && session->hp < session->maxHp && inSafe) {
        session->hpRemainder += 0.02f * (float)session->maxHp * dt;
        if (session->hpRemainder >= 1.0f) {
          int gain = (int)session->hpRemainder;
          session->hp = std::min(session->hp + gain, (int)session->maxHp);
          session->hpRemainder -= (float)gain;
          CharacterHandler::SendCharStats(*session);
        }
      }

      // Idle HP Regeneration (standing still 5+ seconds, outside safe zone)
      // Very slow: ~0.5% maxHP per second
      if (session->inWorld && !session->dead && session->hp < session->maxHp && !inSafe) {
        session->idleTimer += dt;
        if (session->idleTimer >= 5.0f) {
          session->idleHpRemainder += 0.005f * (float)session->maxHp * dt;
          if (session->idleHpRemainder >= 1.0f) {
            int gain = (int)session->idleHpRemainder;
            session->hp = std::min(session->hp + gain, (int)session->maxHp);
            session->idleHpRemainder -= (float)gain;
            CharacterHandler::SendCharStats(*session);
          }
        }
      }

      // AG/Mana recovery logic
      if (session->inWorld && !session->dead) {
        bool isDK = session->classCode == 16;

        // Mana recovery: DK: 5%/s (fast, AG-style). DW/ELF/MG: 2%/s everywhere
        if (session->mana < session->maxMana) {
          float rate = isDK ? 0.05f : 0.02f;
          session->manaRemainder += rate * (float)session->maxMana * dt;
          if (session->manaRemainder >= 1.0f) {
            int gain = (int)session->manaRemainder;
            session->manaRemainder -= (float)gain;
            session->mana = std::min(session->mana + gain, session->maxMana);
            CharacterHandler::SendCharStats(*session);
          }
        } else {
          session->manaRemainder = 0.0f;
        }

        // AG (Ability Gauge) recovery every 3 seconds
        if (session->ag < session->maxAg) {
          session->agRegenTimer += dt;
          if (session->agRegenTimer >= 3.0f) {
            session->agRegenTimer = 0.0f;

            // TotalRate: Base (DK=5%, others=3%) + Idle Bonus (3% if idle >5s)
            float totalRate = isDK ? 5.0f : 3.0f;
            uint32_t nowMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());

            if (nowMs - session->lastAgUseTime >= 5000) {
              totalRate += 3.0f;
            }

            int gain = static_cast<int>((session->maxAg * totalRate) / 100.0f);
            if (gain < 1)
              gain = 1;

            session->ag = std::min(session->ag + gain, session->maxAg);
            printf("[Regen] FD=%d AG +%d (%d/%d) Rate: %.0f%%\n",
                   session->GetFd(), gain, session->ag, session->maxAg,
                   totalRate);
            CharacterHandler::SendCharStats(*session);
          }
        }
      }

      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        session->Kill();
        continue;
      }

      if (pfd.revents & POLLIN) {
        auto packets = session->ReadPackets();
        for (auto &pkt : packets) {
          HandlePacket(*session, pkt);
        }
        // Check if player walked into a gate zone (after position updates)
        if (session->inWorld && session->IsAlive()) {
          CheckGateZones(*session);
        }
      }

      if (pfd.revents & POLLOUT) {
        session->FlushSend();
      }
    }

    // Remove dead sessions (save before removing)
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
                       [this](const auto &s) {
                         if (!s->IsAlive()) {
                           if (s->inWorld)
                             SaveSession(*s);
                           m_world.ClearGuardInteractionsForPlayer(s->GetFd());
                           printf("[Server] Client fd=%d disconnected\n",
                                  s->GetFd());
                           return true;
                         }
                         return false;
                       }),
        m_sessions.end());
  }

  // Save all sessions before shutdown
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld)
      SaveSession(*s);
  }
  printf("[Server] Shutting down...\n");
}

void Server::SaveSession(Session &session) {
  if (session.characterId <= 0)
    return;

  // Convert world position back to grid coordinates
  uint8_t posX = static_cast<uint8_t>(session.worldZ / 100.0f);
  uint8_t posY = static_cast<uint8_t>(session.worldX / 100.0f);

  // Save stats, HP, mana, position, money, map
  m_db.SaveCharacterFull(session.characterId, session.level, session.strength,
                         session.dexterity, session.vitality, session.energy,
                         static_cast<uint16_t>(std::max(session.hp, 0)),
                         static_cast<uint16_t>(session.maxHp),
                         static_cast<uint16_t>(std::max(session.mana, 0)),
                         static_cast<uint16_t>(session.maxMana),
                         static_cast<uint16_t>(std::max(session.ag, 0)),
                         static_cast<uint16_t>(session.maxAg),
                         session.levelUpPoints, session.experience, session.zen,
                         posX, posY, session.mapId, session.skillBar,
                         session.potionBar, session.rmcSkillId);

  // Save full inventory (clear + rewrite all occupied slots)
  m_db.DeleteCharacterInventoryAll(session.characterId);
  for (int i = 0; i < 64; i++) {
    auto &item = session.bag[i];
    if (item.primary && item.defIndex >= 0) {
      m_db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel,
                                  static_cast<uint8_t>(i));
    }
  }

  // Save only occupied equipment slots (skip empty 0xFF to reduce DB writes)
  for (int i = 0; i < Session::NUM_EQUIP_SLOTS; i++) {
    auto &eq = session.equipment[i];
    if (eq.category != 0xFF) {
      m_db.UpdateEquipment(session.characterId, static_cast<uint8_t>(i),
                           eq.category, eq.itemIndex, eq.itemLevel);
    }
  }
}

void Server::Stop() {
  m_running = false;
  if (m_listenFd >= 0) {
    close(m_listenFd);
    m_listenFd = -1;
  }
  m_sessions.clear();
  m_db.Close();
}

void Server::AcceptNewClients() {
  while (true) {
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd =
        accept(m_listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &addrLen);
    if (clientFd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      perror("[Server] accept");
      break;
    }

    // Set non-blocking
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

    // Disable Nagle's algorithm for low latency
    int tcpNoDelay = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &tcpNoDelay,
               sizeof(tcpNoDelay));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    printf("[Server] New client from %s:%d (fd=%d)\n", ip,
           ntohs(clientAddr.sin_port), clientFd);

    auto session = std::make_unique<Session>(clientFd);
    OnClientConnected(*session);
    m_sessions.push_back(std::move(session));
  }
}

void Server::OnClientConnected(Session &session) {
  // Send welcome immediately
  WorldHandler::SendWelcome(session);

  // Auto-assign account (no login flow yet)
  session.accountId = 1;

  // Send character list — client enters character select screen
  CharacterSelectHandler::SendCharList(session, m_db);

  printf("[Server] FD=%d connected, sent char list\n", session.GetFd());
}

void Server::HandlePacket(Session &session,
                          const std::vector<uint8_t> &packet) {
  PacketHandler::Handle(session, packet, m_db, m_world, *this);
}

void Server::Broadcast(const void *data, size_t len) {
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld) {
      s->Send(data, len);
    }
  }
}

void Server::BroadcastExcept(int excludeFd, const void *data, size_t len) {
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld && s->GetFd() != excludeFd) {
      s->Send(data, len);
    }
  }
}

void Server::CheckGateZones(Session &session) {
  // Don't check gates right after a transition (prevents instant re-warp)
  if (session.gateTransitionCooldown > 0.0f)
    return;

  uint8_t gx = static_cast<uint8_t>(session.worldZ / 100.0f);
  uint8_t gy = static_cast<uint8_t>(session.worldX / 100.0f);

  if (session.mapId == 0 && gx >= 121 && gx <= 123 && gy >= 232 && gy <= 233) {
    // Lorencia → Dungeon 1 (gate at south Lorencia near cobra gate)
    // OpenMU: maps[0].EnterGate(1, target=2, 121,232,123,233)
    TransitionMap(session, 1, 108, 247);
  } else if (session.mapId == 1 && gx >= 108 && gx <= 109 && gy >= 248 && gy <= 249) {
    // Dungeon 1 → Lorencia (return near cobra gate entrance)
    // OpenMU: maps[1].EnterGate(3, target=4, 108,248,109,248)
    TransitionMap(session, 0, 121, 228);
  } else if (session.mapId == 1 && gx == 239 && gy >= 149 && gy <= 150) {
    // Dungeon 1 → Dungeon 2 (forward)
    // OpenMU: maps[1].EnterGate(5, target=6, 239,149,239,150) → spawn (231-234,126-127)
    TransitionMap(session, 1, 232, 126);
  } else if (session.mapId == 1 && gx >= 232 && gx <= 233 && gy >= 127 && gy <= 128) {
    // Dungeon 2 → Dungeon 1 (return)
    // OpenMU: maps[1].EnterGate(7, target=8, 232,127,233,128) → spawn (240-241,149-151)
    TransitionMap(session, 1, 240, 150);
  } else if (session.mapId == 1 && gx == 2 && gy >= 17 && gy <= 18) {
    // Dungeon 2 → Dungeon 3 (forward)
    // OpenMU: maps[1].EnterGate(9, target=10, 2,17,2,18) → spawn (3-4,83-86)
    TransitionMap(session, 1, 3, 84);
  } else if (session.mapId == 1 && gx == 2 && gy >= 84 && gy <= 85) {
    // Dungeon 3 → Dungeon 2 (return)
    // OpenMU: maps[1].EnterGate(11, target=12, 2,84,2,85) → spawn (3-6,16-17)
    TransitionMap(session, 1, 4, 16);
  } else if (session.mapId == 1 && gx >= 5 && gx <= 6 && gy == 34) {
    // Dungeon 3 passage forward
    // OpenMU: maps[1].EnterGate(13, target=14, 5,34,6,34) → spawn (29-30,125-126)
    TransitionMap(session, 1, 29, 125);
  } else if (session.mapId == 1 && gx >= 29 && gx <= 30 && gy == 127) {
    // Dungeon 3 passage return
    // OpenMU: maps[1].EnterGate(15, target=16, 29,127,30,127) → spawn (5-7,32-33)
    TransitionMap(session, 1, 6, 32);
  }
  // ── Lorencia ↔ Devias gates ──
  else if (session.mapId == 0 && gx >= 5 && gx <= 6 && gy >= 38 && gy <= 41) {
    // Lorencia → Devias (Gate.txt gate 17→18: spawn at Devias east entry)
    TransitionMap(session, 2, 229, 35);
  } else if (session.mapId == 2 && gx >= 244 && gx <= 245 && gy >= 34 && gy <= 37) {
    // Devias → Lorencia (east edge return)
    TransitionMap(session, 0, 5, 36);
  }
}

void Server::TransitionMap(Session &session, uint8_t newMapId,
                           uint8_t spawnX, uint8_t spawnY) {
  printf("[Server] Map transition: fd=%d map %d -> %d spawn (%d,%d)\n",
         session.GetFd(), session.mapId, newMapId, spawnX, spawnY);

  // Update session
  session.mapId = newMapId;
  session.gateTransitionCooldown = 3.0f; // 3 second cooldown
  session.worldX = spawnY * 100.0f;
  session.worldZ = spawnX * 100.0f;

  // Save position to DB immediately (including map change)
  m_db.UpdatePosition(session.characterId, spawnX, spawnY, newMapId);

  // Reload world data for new map
  m_world.ClearWorldData();
  m_world.SetActiveMap(newMapId);
  m_world.LoadNpcsFromDB(m_db, newMapId);
  m_world.LoadMonstersFromDB(m_db, newMapId);

  // Send map change packet to client
  PMSG_MAP_CHANGE_SEND pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::MAP_CHANGE);
  pkt.mapId = newMapId;
  pkt.spawnX = spawnX;
  pkt.spawnY = spawnY;
  session.Send(&pkt, sizeof(pkt));

  // Defer NPC/monster viewport sending — wait for client "ready" signal
  // (SendPrecisePosition after ChangeMap completes). 5s safety fallback.
  session.pendingViewportDelay = 5.0f;

  printf("[Server] Map %d loaded: %zu NPCs, %zu monsters (viewport deferred)\n",
         newMapId, m_world.GetNpcs().size(),
         m_world.GetMonsterInstances().size());
}

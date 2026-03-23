#include "Database.hpp"
#include "Session.hpp"
#include <cstdio>

bool Database::Open(const std::string &dbPath) {
  if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
    printf("[DB] Failed to open: %s\n", sqlite3_errmsg(m_db));
    return false;
  }
  CreateTables();
  printf("[DB] Opened %s\n", dbPath.c_str());
  return true;
}

void Database::Close() {
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}

void Database::CreateTables() {
  const char *sql = R"(
        CREATE TABLE IF NOT EXISTS accounts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            blocked INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS npc_spawns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            map_id INTEGER NOT NULL DEFAULT 0,
            pos_x INTEGER NOT NULL,
            pos_y INTEGER NOT NULL,
            direction INTEGER NOT NULL DEFAULT 2,
            name TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS monster_spawns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            map_id INTEGER NOT NULL DEFAULT 0,
            pos_x INTEGER NOT NULL,
            pos_y INTEGER NOT NULL,
            direction INTEGER NOT NULL DEFAULT 2
        );
        CREATE TABLE IF NOT EXISTS characters (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            account_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            name TEXT UNIQUE NOT NULL,
            class INTEGER DEFAULT 0,
            level INTEGER DEFAULT 1,
            map_id INTEGER DEFAULT 0,
            pos_x INTEGER DEFAULT 130,
            pos_y INTEGER DEFAULT 130,
            direction INTEGER DEFAULT 2,
            strength INTEGER DEFAULT 28,
            dexterity INTEGER DEFAULT 20,
            vitality INTEGER DEFAULT 25,
            energy INTEGER DEFAULT 10,
            life INTEGER DEFAULT 110,
            max_life INTEGER DEFAULT 110,
            mana INTEGER DEFAULT 20,
            max_mana INTEGER DEFAULT 20,
            money INTEGER DEFAULT 0,
            experience INTEGER DEFAULT 0,
            level_up_points INTEGER DEFAULT 0,
            skill_bar BLOB,
            potion_bar BLOB,
            rmc_skill_id INTEGER DEFAULT -1,
            summon_type INTEGER DEFAULT -1,
            FOREIGN KEY (account_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS item_definitions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            category INTEGER NOT NULL,
            item_index INTEGER NOT NULL,
            name TEXT NOT NULL,
            model_file TEXT NOT NULL,
            level_req INTEGER DEFAULT 0,
            damage_min INTEGER DEFAULT 0,
            damage_max INTEGER DEFAULT 0,
            attack_speed INTEGER DEFAULT 0,
            two_handed INTEGER DEFAULT 0,
            defense INTEGER DEFAULT 0,
            width INTEGER DEFAULT 1,
            height INTEGER DEFAULT 1,
            req_str INTEGER DEFAULT 0,
            req_dex INTEGER DEFAULT 0,
            req_vit INTEGER DEFAULT 0,
            req_ene INTEGER DEFAULT 0,
            class_flags INTEGER DEFAULT 4294967295,
            UNIQUE(category, item_index)
        );
        CREATE TABLE IF NOT EXISTS character_equipment (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            item_category INTEGER NOT NULL,
            item_index INTEGER NOT NULL,
            item_level INTEGER DEFAULT 0,
            quantity INTEGER DEFAULT 0,
            UNIQUE(character_id, slot),
            FOREIGN KEY (character_id) REFERENCES characters(id),
            FOREIGN KEY (item_category, item_index) REFERENCES item_definitions(category, item_index)
        );
    )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] CreateTables error: %s\n", err);
    sqlite3_free(err);
  }

  // Inventory bag table
  const char *invSql = R"(
        CREATE TABLE IF NOT EXISTS character_inventory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            def_index INTEGER NOT NULL,
            quantity INTEGER NOT NULL DEFAULT 1,
            item_level INTEGER NOT NULL DEFAULT 0,
            UNIQUE(character_id, slot),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
  char *invErr = nullptr;
  if (sqlite3_exec(m_db, invSql, nullptr, nullptr, &invErr) != SQLITE_OK) {
    printf("[DB] character_inventory create error: %s\n", invErr);
    sqlite3_free(invErr);
  }

  // Migration: add level_up_points column to existing databases
  sqlite3_exec(
      m_db,
      "ALTER TABLE characters ADD COLUMN level_up_points INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr); // Silently ignores if column exists

  // Migration: add defense column to item_definitions
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN defense INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN ag INTEGER DEFAULT 50",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN max_ag INTEGER DEFAULT 50",
               nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN width INTEGER DEFAULT 1",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN height INTEGER DEFAULT 1",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_str INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_dex INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_vit INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_ene INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
               "ALTER TABLE item_definitions ADD COLUMN class_flags INTEGER "
               "DEFAULT 4294967295",
               nullptr, nullptr, nullptr);

  // Migration: add quick_slot_def_index to characters (default -1)
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN quick_slot_def_index INTEGER "
               "DEFAULT -1",
               nullptr, nullptr, nullptr);

  // Migration: add buy_price to item_definitions (default 0)
  sqlite3_exec(
      m_db,
      "ALTER TABLE item_definitions ADD COLUMN buy_price INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);

  // Migration: add magic_power to item_definitions (Staff Rise, default 0)
  sqlite3_exec(
      m_db,
      "ALTER TABLE item_definitions ADD COLUMN magic_power INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);

  // Migration: add rmc_skill_id to characters (default -1)
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN rmc_skill_id INTEGER "
               "DEFAULT -1",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN skill_bar BLOB",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN potion_bar BLOB",
               nullptr, nullptr, nullptr);

  // Migration: camera zoom per character (default 800.0 stored as 8000)
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN camera_zoom INTEGER DEFAULT 8000",
               nullptr, nullptr, nullptr);

  // Character skills table (learned skills)
  const char *skillSql = R"(
        CREATE TABLE IF NOT EXISTS character_skills (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            skill_id INTEGER NOT NULL,
            UNIQUE(character_id, skill_id),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
  char *skillErr = nullptr;
  if (sqlite3_exec(m_db, skillSql, nullptr, nullptr, &skillErr) != SQLITE_OK) {
    printf("[DB] character_skills create error: %s\n", skillErr);
    sqlite3_free(skillErr);
  }

  // Chat log table (persistent per-character message history)
  const char *chatSql = R"(
        CREATE TABLE IF NOT EXISTS chat_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            category INTEGER NOT NULL,
            color INTEGER NOT NULL,
            message TEXT NOT NULL,
            timestamp INTEGER DEFAULT (strftime('%s','now')),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
  char *chatErr = nullptr;
  if (sqlite3_exec(m_db, chatSql, nullptr, nullptr, &chatErr) != SQLITE_OK) {
    printf("[DB] chat_log create error: %s\n", chatErr);
    sqlite3_free(chatErr);
  }

  // Quest progress table (per-quest tracking, replaces old chain system)
  const char *questSql = R"(
        CREATE TABLE IF NOT EXISTS character_quest_progress (
            character_id INTEGER NOT NULL,
            quest_id INTEGER NOT NULL,
            kill_count_0 INTEGER NOT NULL DEFAULT 0,
            kill_count_1 INTEGER NOT NULL DEFAULT 0,
            kill_count_2 INTEGER NOT NULL DEFAULT 0,
            completed INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (character_id, quest_id),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
  char *questErr = nullptr;
  if (sqlite3_exec(m_db, questSql, nullptr, nullptr, &questErr) != SQLITE_OK) {
    printf("[DB] character_quest_progress create error: %s\n", questErr);
    sqlite3_free(questErr);
  }

  // Migration: add summon_type to characters (default -1 = no summon)
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN summon_type INTEGER DEFAULT -1",
      nullptr, nullptr, nullptr);

  // Migration: add buff aura columns for persistence across logout
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_def_type INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_def_remaining REAL DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_def_value INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_dmg_type INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_dmg_remaining REAL DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
      "ALTER TABLE characters ADD COLUMN buff_dmg_value INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
}

void Database::CreateDefaultAccount() {
  // Check if test account exists
  int accountId = 0;
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(m_db, "SELECT id FROM accounts WHERE username='test'", -1,
                     &stmt, nullptr);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    accountId = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (accountId == 0) {
    sqlite3_exec(
        m_db,
        "INSERT INTO accounts (username, password) VALUES ('test', 'test')",
        nullptr, nullptr, nullptr);
    accountId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
    printf("[DB] Created test account (ID: %d)\n", accountId);
  }

  // Only seed default characters if account has none
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM characters WHERE account_id=?",
                     -1, &stmt, nullptr);
  sqlite3_bind_int(stmt, 1, accountId);
  int charCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    charCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (charCount == 0) {
    // No seed characters — user creates their own from character select
    printf("[DB] Account %d has no characters (create from character select)\n",
           accountId);
  }
}

int Database::ValidateLogin(const std::string &username,
                            const std::string &password) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, blocked FROM accounts WHERE username=? AND password=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return 0;

  sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

  int accountId = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int blocked = sqlite3_column_int(stmt, 1);
    if (!blocked) {
      accountId = sqlite3_column_int(stmt, 0);
    }
  }
  sqlite3_finalize(stmt);
  return accountId;
}

std::vector<CharacterData> Database::GetCharacterList(int accountId) {
  std::vector<CharacterData> chars;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id FROM characters WHERE account_id=? ORDER BY "
      "slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return chars;

  sqlite3_bind_int(stmt, 1, accountId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    CharacterData c;
    c.id = sqlite3_column_int(stmt, 0);
    c.accountId = sqlite3_column_int(stmt, 1);
    c.slot = sqlite3_column_int(stmt, 2);
    c.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    c.charClass = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    c.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
    c.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
    c.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 7));
    c.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
    c.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    c.strength = static_cast<uint16_t>(sqlite3_column_int(stmt, 10));
    c.dexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 11));
    c.vitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 12));
    c.energy = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
    c.life = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
    c.maxLife = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
    c.mana = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
    c.maxMana = static_cast<uint16_t>(sqlite3_column_int(stmt, 17));
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
    chars.push_back(c);
  }
  sqlite3_finalize(stmt);
  return chars;
}

bool Database::CharacterNameExists(const std::string &name) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT COUNT(*) FROM characters WHERE name=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return true; // Assume exists on error
  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return count > 0;
}

int Database::CreateCharacter(int accountId, const std::string &name,
                              uint8_t classCode) {
  // Find next free slot (0-3) for this account
  auto chars = GetCharacterList(accountId);
  if (chars.size() >= 4)
    return -1; // Max 4 characters

  // Find lowest unused slot
  bool slotUsed[4] = {};
  for (auto &c : chars) {
    if (c.slot >= 0 && c.slot < 4)
      slotUsed[c.slot] = true;
  }
  int freeSlot = -1;
  for (int i = 0; i < 4; i++) {
    if (!slotUsed[i]) {
      freeSlot = i;
      break;
    }
  }
  if (freeSlot < 0)
    return -1;

  // Starting stats per class (OpenMU ClassDarkKnight.cs etc.)
  uint16_t str, dex, vit, ene, hp, maxHp, mp, maxMp;
  switch (classCode) {
  case 0: // DW
    str = 18; dex = 18; vit = 15; ene = 30; hp = 60; maxHp = 60; mp = 60; maxMp = 60;
    break;
  case 16: // DK
    str = 28; dex = 20; vit = 25; ene = 10; hp = 110; maxHp = 110; mp = 20; maxMp = 20;
    break;
  case 32: // ELF
    str = 22; dex = 25; vit = 20; ene = 15; hp = 80; maxHp = 80; mp = 30; maxMp = 30;
    break;
  case 48: // MG
    str = 26; dex = 26; vit = 26; ene = 26; hp = 110; maxHp = 110; mp = 60; maxMp = 60;
    break;
  default:
    return -1; // Invalid class
  }

  // Starting position per class (OpenMU: DW/DK/MG → Lorencia, ELF → Noria)
  int startMap = 0; uint8_t startX = 142, startY = 116; // Lorencia default
  if (classCode == 32) { startMap = 3; startX = 174; startY = 110; } // Noria

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO characters (account_id, slot, name, class, level, "
      "strength, dexterity, vitality, energy, life, max_life, mana, "
      "max_mana, map_id, pos_x, pos_y, money, level_up_points) "
      "VALUES (?, ?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0)";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    printf("[DB] CreateCharacter prepare error: %s\n", sqlite3_errmsg(m_db));
    return -1;
  }
  sqlite3_bind_int(stmt, 1, accountId);
  sqlite3_bind_int(stmt, 2, freeSlot);
  sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, classCode);
  sqlite3_bind_int(stmt, 5, str);
  sqlite3_bind_int(stmt, 6, dex);
  sqlite3_bind_int(stmt, 7, vit);
  sqlite3_bind_int(stmt, 8, ene);
  sqlite3_bind_int(stmt, 9, hp);
  sqlite3_bind_int(stmt, 10, maxHp);
  sqlite3_bind_int(stmt, 11, mp);
  sqlite3_bind_int(stmt, 12, maxMp);
  sqlite3_bind_int(stmt, 13, startMap);
  sqlite3_bind_int(stmt, 14, startX);
  sqlite3_bind_int(stmt, 15, startY);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    printf("[DB] CreateCharacter error: %s\n", sqlite3_errmsg(m_db));
    sqlite3_finalize(stmt);
    return -1;
  }
  int charId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
  sqlite3_finalize(stmt);
  printf("[DB] Created character '%s' (class=%d, slot=%d, id=%d)\n",
         name.c_str(), classCode, freeSlot, charId);
  return charId;
}

bool Database::DeleteCharacter(int accountId, int charId) {
  // Verify character belongs to account
  sqlite3_stmt *stmt = nullptr;
  const char *checkSql =
      "SELECT id FROM characters WHERE id=? AND account_id=?";
  if (sqlite3_prepare_v2(m_db, checkSql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(stmt, 1, charId);
  sqlite3_bind_int(stmt, 2, accountId);
  bool found = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  if (!found)
    return false;

  // Delete associated data
  char sql[256];
  snprintf(sql, sizeof(sql),
           "DELETE FROM character_equipment WHERE character_id=%d", charId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  snprintf(sql, sizeof(sql),
           "DELETE FROM character_inventory WHERE character_id=%d", charId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  snprintf(sql, sizeof(sql),
           "DELETE FROM character_skills WHERE character_id=%d", charId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  snprintf(sql, sizeof(sql), "DELETE FROM characters WHERE id=%d", charId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  printf("[DB] Deleted character id=%d\n", charId);
  return true;
}

CharacterData Database::GetCharacter(const std::string &name) {
  CharacterData c;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id, summon_type, camera_zoom, "
      "buff_def_type, buff_def_remaining, buff_def_value, "
      "buff_dmg_type, buff_dmg_remaining, buff_dmg_value "
      "FROM characters WHERE name=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return c;

  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    c.id = sqlite3_column_int(stmt, 0);
    c.accountId = sqlite3_column_int(stmt, 1);
    c.slot = sqlite3_column_int(stmt, 2);
    c.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    c.charClass = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    c.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
    c.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
    c.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 7));
    c.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
    c.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    c.strength = static_cast<uint16_t>(sqlite3_column_int(stmt, 10));
    c.dexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 11));
    c.vitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 12));
    c.energy = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
    c.life = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
    c.maxLife = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
    c.mana = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
    c.maxMana = static_cast<uint16_t>(sqlite3_column_int(stmt, 17));
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
    c.summonType = static_cast<int16_t>(sqlite3_column_int(stmt, 26));
    c.cameraZoom = static_cast<uint16_t>(sqlite3_column_int(stmt, 27));
    c.buffDefType = static_cast<uint8_t>(sqlite3_column_int(stmt, 28));
    c.buffDefRemaining = static_cast<float>(sqlite3_column_double(stmt, 29));
    c.buffDefValue = sqlite3_column_int(stmt, 30);
    c.buffDmgType = static_cast<uint8_t>(sqlite3_column_int(stmt, 31));
    c.buffDmgRemaining = static_cast<float>(sqlite3_column_double(stmt, 32));
    c.buffDmgValue = sqlite3_column_int(stmt, 33);
  }
  sqlite3_finalize(stmt);
  return c;
}

CharacterData Database::GetCharacterById(int id) {
  CharacterData c;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id, summon_type, camera_zoom, "
      "buff_def_type, buff_def_remaining, buff_def_value, "
      "buff_dmg_type, buff_dmg_remaining, buff_dmg_value "
      "FROM characters WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return c;

  sqlite3_bind_int(stmt, 1, id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    c.id = sqlite3_column_int(stmt, 0);
    c.accountId = sqlite3_column_int(stmt, 1);
    c.slot = sqlite3_column_int(stmt, 2);
    c.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    c.charClass = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    c.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
    c.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
    c.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 7));
    c.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
    c.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    c.strength = static_cast<uint16_t>(sqlite3_column_int(stmt, 10));
    c.dexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 11));
    c.vitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 12));
    c.energy = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
    c.life = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
    c.maxLife = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
    c.mana = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
    c.maxMana = static_cast<uint16_t>(sqlite3_column_int(stmt, 17));
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
    c.summonType = static_cast<int16_t>(sqlite3_column_int(stmt, 26));
    c.cameraZoom = static_cast<uint16_t>(sqlite3_column_int(stmt, 27));
    c.buffDefType = static_cast<uint8_t>(sqlite3_column_int(stmt, 28));
    c.buffDefRemaining = static_cast<float>(sqlite3_column_double(stmt, 29));
    c.buffDefValue = sqlite3_column_int(stmt, 30);
    c.buffDmgType = static_cast<uint8_t>(sqlite3_column_int(stmt, 31));
    c.buffDmgRemaining = static_cast<float>(sqlite3_column_double(stmt, 32));
    c.buffDmgValue = sqlite3_column_int(stmt, 33);
  }
  sqlite3_finalize(stmt);
  return c;
}

void Database::UpdateCameraZoom(int charId, uint16_t zoom) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET camera_zoom=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, zoom);
  sqlite3_bind_int(stmt, 2, charId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::UpdateCharacterStats(
    int charId, uint16_t level, uint16_t strength, uint16_t dexterity,
    uint16_t vitality, uint16_t energy, uint16_t life, uint16_t maxLife,
    uint16_t mana, uint16_t maxMana, uint16_t ag, uint16_t maxAg,
    uint16_t levelUpPoints, uint64_t experience, const int8_t *skillBar,
    const int16_t *potionBar, int8_t rmcSkillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET level=?, strength=?, dexterity=?, "
                    "vitality=?, energy=?, life=?, max_life=?, mana=?, "
                    "max_mana=?, ag=?, max_ag=?, level_up_points=?, "
                    "experience=?, skill_bar=?, potion_bar=?, rmc_skill_id=? "
                    "WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, level);
  sqlite3_bind_int(stmt, 2, strength);
  sqlite3_bind_int(stmt, 3, dexterity);
  sqlite3_bind_int(stmt, 4, vitality);
  sqlite3_bind_int(stmt, 5, energy);
  sqlite3_bind_int(stmt, 6, life);
  sqlite3_bind_int(stmt, 7, maxLife);
  sqlite3_bind_int(stmt, 8, mana);
  sqlite3_bind_int(stmt, 9, maxMana);
  sqlite3_bind_int(stmt, 10, ag);
  sqlite3_bind_int(stmt, 11, maxAg);
  sqlite3_bind_int(stmt, 12, levelUpPoints);
  sqlite3_bind_int64(stmt, 13, experience);
  sqlite3_bind_blob(stmt, 14, skillBar, 10, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 15, potionBar, 8, SQLITE_TRANSIENT); // 4 × int16_t
  sqlite3_bind_int(stmt, 16, rmcSkillId);
  sqlite3_bind_int(stmt, 17, charId);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  printf("[DB] Saved character %d stats: Lv%d STR=%d DEX=%d VIT=%d ENE=%d "
         "HP=%d/%d MP=%d/%d AG=%d/%d XP=%llu pts=%d RMC=%d\n",
         charId, level, strength, dexterity, vitality, energy, life, maxLife,
         mana, maxMana, ag, maxAg, (unsigned long long)experience,
         levelUpPoints, rmcSkillId);
}

void Database::SaveCharacterFull(int charId, uint16_t level, uint16_t strength,
                                 uint16_t dexterity, uint16_t vitality,
                                 uint16_t energy, uint16_t life,
                                 uint16_t maxLife, uint16_t mana,
                                 uint16_t maxMana, uint16_t ag, uint16_t maxAg,
                                 uint16_t levelUpPoints, uint64_t experience,
                                 uint32_t money, uint8_t posX, uint8_t posY,
                                 uint8_t mapId, const int8_t *skillBar,
                                 const int16_t *potionBar, int8_t rmcSkillId,
                                 int16_t summonType, const Session *session) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE characters SET level=?, strength=?, dexterity=?, vitality=?, "
      "energy=?, life=?, max_life=?, mana=?, max_mana=?, ag=?, max_ag=?, "
      "level_up_points=?, experience=?, money=?, pos_x=?, pos_y=?, "
      "map_id=?, skill_bar=?, potion_bar=?, rmc_skill_id=?, summon_type=?, "
      "buff_def_type=?, buff_def_remaining=?, buff_def_value=?, "
      "buff_dmg_type=?, buff_dmg_remaining=?, buff_dmg_value=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, level);
  sqlite3_bind_int(stmt, 2, strength);
  sqlite3_bind_int(stmt, 3, dexterity);
  sqlite3_bind_int(stmt, 4, vitality);
  sqlite3_bind_int(stmt, 5, energy);
  sqlite3_bind_int(stmt, 6, life);
  sqlite3_bind_int(stmt, 7, maxLife);
  sqlite3_bind_int(stmt, 8, mana);
  sqlite3_bind_int(stmt, 9, maxMana);
  sqlite3_bind_int(stmt, 10, ag);
  sqlite3_bind_int(stmt, 11, maxAg);
  sqlite3_bind_int(stmt, 12, levelUpPoints);
  sqlite3_bind_int64(stmt, 13, experience);
  sqlite3_bind_int(stmt, 14, money);
  sqlite3_bind_int(stmt, 15, posX);
  sqlite3_bind_int(stmt, 16, posY);
  sqlite3_bind_int(stmt, 17, mapId);
  sqlite3_bind_blob(stmt, 18, skillBar, 10, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 19, potionBar, 8, SQLITE_TRANSIENT); // 4 × int16_t
  sqlite3_bind_int(stmt, 20, rmcSkillId);
  sqlite3_bind_int(stmt, 21, summonType);
  // Buff aura persistence
  if (session) {
    sqlite3_bind_int(stmt, 22, session->buffs[0].active ? session->buffs[0].type : 0);
    sqlite3_bind_double(stmt, 23, session->buffs[0].active ? session->buffs[0].remaining : 0.0);
    sqlite3_bind_int(stmt, 24, session->buffs[0].active ? session->buffs[0].value : 0);
    sqlite3_bind_int(stmt, 25, session->buffs[1].active ? session->buffs[1].type : 0);
    sqlite3_bind_double(stmt, 26, session->buffs[1].active ? session->buffs[1].remaining : 0.0);
    sqlite3_bind_int(stmt, 27, session->buffs[1].active ? session->buffs[1].value : 0);
  } else {
    sqlite3_bind_int(stmt, 22, 0);
    sqlite3_bind_double(stmt, 23, 0.0);
    sqlite3_bind_int(stmt, 24, 0);
    sqlite3_bind_int(stmt, 25, 0);
    sqlite3_bind_double(stmt, 26, 0.0);
    sqlite3_bind_int(stmt, 27, 0);
  }
  sqlite3_bind_int(stmt, 28, charId);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::UpdatePosition(int charId, uint8_t x, uint8_t y, int mapId) {
  sqlite3_stmt *stmt = nullptr;
  if (mapId >= 0) {
    const char *sql = "UPDATE characters SET pos_x=?, pos_y=?, map_id=? WHERE id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return;
    sqlite3_bind_int(stmt, 1, x);
    sqlite3_bind_int(stmt, 2, y);
    sqlite3_bind_int(stmt, 3, mapId);
    sqlite3_bind_int(stmt, 4, charId);
  } else {
    const char *sql = "UPDATE characters SET pos_x=?, pos_y=? WHERE id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return;
    sqlite3_bind_int(stmt, 1, x);
    sqlite3_bind_int(stmt, 2, y);
    sqlite3_bind_int(stmt, 3, charId);
  }
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::SeedNpcSpawns() {
  // Check if NPCs already seeded
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM npc_spawns WHERE map_id=0", -1,
                     &stmt, nullptr);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (count > 0) {
    printf("[DB] NPC spawns already seeded (%d entries)\n", count);
    return;
  }

  // Lorencia (map 0) NPCs from MonsterSetBase.txt + OpenMU Lorencia.cs
  // Direction convention: DB stores MonsterSetBase values + 1.
  // Client formula: facing = (dir - 2) * PI/4
  //   dir=2→West(0°), 3→SW(45°), 4→South(90°), 5→SE(135°),
  //   6→East(180°), 7→NE(225°), 8→North(270°), 1→NW(315°)
  const char *sql = R"(
        INSERT INTO npc_spawns (type, map_id, pos_x, pos_y, direction, name) VALUES
            (253, 0, 127, 86,  3, 'Potion Girl Amy'),
            (250, 0, 183, 137, 3, 'Weapon Merchant'),
            (251, 0, 116, 141, 4, 'Hanzo the Blacksmith'),
            (254, 0, 118, 113, 4, 'Pasi the Mage'),
            (255, 0, 123, 135, 2, 'Lumen the Barmaid'),
            (240, 0, 146, 110, 4, 'Safety Guardian'),
            (240, 0, 147, 145, 2, 'Safety Guardian'),
            (245, 0, 94,  125, 3, 'Warden Aldric'),
            (246, 0, 123, 84,  3, 'Corporal Brynn'),
            (247, 0, 130, 115, 4, 'Sergeant Dorian'),
            (248, 0, 173, 125, 5, 'Lieutenant Kael'),
            (249, 0, 131, 148, 3, 'Captain Marcus');
    )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedNpcSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded 12 Lorencia NPC spawns (7 vendors + 5 guards)\n");
  }

  // Seed Devias (map_id=2) NPCs if not already present
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM npc_spawns WHERE map_id=2",
                     -1, &stmt, nullptr);
  int deviasNpcCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    deviasNpcCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (deviasNpcCount > 0) {
    printf("[DB] Devias NPC spawns already seeded (%d entries)\n", deviasNpcCount);
    return;
  }

  const char *deviasSql = R"(
        INSERT INTO npc_spawns (type, map_id, pos_x, pos_y, direction, name) VALUES
            (300, 2, 218, 63, 4, 'Baz the Vault Keeper'),
            (301, 2, 215, 45, 3, 'Guild Master'),
            (302, 2, 226, 25, 5, 'Caren the Barmaid'),
            (303, 2, 225, 41, 5, 'Izabel the Wizard'),
            (304, 2, 186, 47, 5, 'Zienna the Arms Dealer'),
            (310, 2, 218, 79, 4, 'Ranger Elise'),
            (311, 2, 195, 55, 5, 'Tracker Nolan'),
            (312, 2, 217, 14, 3, 'Warden Hale');
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, deviasSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedDeviasNpcSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded 8 Devias NPC spawns (5 vendors + 3 quest guards)\n");
  }

  // Seed Noria (map_id=3) NPCs if not already present
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM npc_spawns WHERE map_id=3",
                     -1, &stmt, nullptr);
  int noriaNpcCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    noriaNpcCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (noriaNpcCount > 0) {
    printf("[DB] Noria NPC spawns already seeded (%d entries)\n", noriaNpcCount);
    return;
  }

  const char *noriaSql = R"(
        INSERT INTO npc_spawns (type, map_id, pos_x, pos_y, direction, name) VALUES
            (253, 3, 169, 109, 4, 'Potion Girl Amy'),
            (253, 3, 193, 110, 3, 'Potion Girl Amy'),
            (242, 3, 173, 125, 2, 'Elf Lala'),
            (243, 3, 195, 124, 3, 'Eo the Craftsman'),
            (240, 3, 172, 96,  4, 'Baz the Vault Keeper'),
            (238, 3, 180, 103, 2, 'Chaos Goblin'),
            (256, 3, 170, 88,  4, 'Sentinel Arwen');
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, noriaSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedNoriaNpcSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded 7 Noria NPC spawns (3 vendors + vault + chaos goblin + quest guard)\n");
  }
}

std::vector<NpcSpawnData> Database::GetNpcSpawns(uint8_t mapId) {
  std::vector<NpcSpawnData> npcs;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction, name "
                    "FROM npc_spawns WHERE map_id=? ORDER BY id";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return npcs;

  sqlite3_bind_int(stmt, 1, mapId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    NpcSpawnData n;
    n.id = sqlite3_column_int(stmt, 0);
    n.type = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
    n.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    n.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    n.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    n.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 5));
    n.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    npcs.push_back(n);
  }
  sqlite3_finalize(stmt);
  return npcs;
}

void Database::SeedItemDefinitions() {
  // Force re-seeding to apply model filename fixes
  sqlite3_exec(m_db, "DELETE FROM item_definitions", nullptr, nullptr, nullptr);

  // Seed all item definitions (MU 0.97d complete database — OpenMU
  // authoritative) Format: (category, item_index, name, model_file, level_req,
  // damage_min, damage_max, defense, attack_speed, two_handed, width, height,
  // req_str, req_dex, req_vit, req_ene, class_flags) class_flags bitmask: 1=DW,
  // 2=DK, 4=Elf, 8=MG
  const char *sql = R"(
        INSERT INTO item_definitions (category, item_index, name, model_file, level_req, damage_min, damage_max, defense, attack_speed, two_handed, width, height, req_str, req_dex, req_vit, req_ene, class_flags) VALUES

            -- Category 0: Swords (OpenMU 0.95d Weapons.cs)
            (0, 0, 'Kris', 'Sword01.bmd', 1, 6, 11, 0, 50, 0, 1, 2, 10, 8, 0, 0, 11),
            (0, 1, 'Short Sword', 'Sword02.bmd', 1, 3, 7, 0, 20, 0, 1, 3, 20, 0, 0, 0, 7),
            (0, 2, 'Rapier', 'Sword03.bmd', 9, 9, 15, 0, 40, 0, 1, 3, 50, 40, 0, 0, 6),
            (0, 3, 'Katana', 'Sword04.bmd', 16, 16, 26, 0, 35, 0, 1, 3, 80, 40, 0, 0, 2),
            (0, 4, 'Sword of Assassin', 'Sword05.bmd', 12, 12, 18, 0, 30, 0, 1, 3, 60, 40, 0, 0, 2),
            (0, 5, 'Blade', 'Sword06.bmd', 36, 36, 47, 0, 30, 0, 1, 3, 80, 50, 0, 0, 7),
            (0, 6, 'Gladius', 'Sword07.bmd', 20, 20, 30, 0, 20, 0, 1, 3, 110, 0, 0, 0, 6),
            (0, 7, 'Falchion', 'Sword08.bmd', 24, 24, 34, 0, 25, 0, 1, 3, 120, 0, 0, 0, 2),
            (0, 8, 'Serpent Sword', 'Sword09.bmd', 30, 30, 40, 0, 20, 0, 1, 3, 130, 0, 0, 0, 2),
            (0, 9, 'Sword of Salamander', 'Sword10.bmd', 32, 32, 46, 0, 30, 1, 2, 3, 103, 0, 0, 0, 2),
            (0, 10, 'Light Saber', 'Sword11.bmd', 40, 47, 61, 0, 25, 1, 2, 4, 80, 60, 0, 0, 6),
            (0, 11, 'Legendary Sword', 'Sword12.bmd', 44, 56, 72, 0, 20, 1, 2, 3, 120, 0, 0, 0, 2),
            (0, 12, 'Heliacal Sword', 'Sword13.bmd', 56, 73, 98, 0, 25, 1, 2, 3, 140, 0, 0, 0, 2),
            (0, 13, 'Double Blade', 'Sword14.bmd', 48, 48, 56, 0, 30, 0, 1, 3, 70, 70, 0, 0, 6),
            (0, 14, 'Lightning Sword', 'Sword15.bmd', 59, 59, 67, 0, 30, 0, 1, 3, 90, 50, 0, 0, 6),
            (0, 15, 'Giant Sword', 'Sword16.bmd', 52, 60, 85, 0, 20, 1, 2, 3, 140, 0, 0, 0, 2),
            (0, 16, 'Sword of Destruction', 'Sword17.bmd', 82, 82, 90, 0, 35, 0, 1, 4, 160, 60, 0, 0, 10),
            (0, 17, 'Dark Breaker', 'Sword18.bmd', 104, 128, 153, 0, 40, 1, 2, 4, 180, 50, 0, 0, 2),
            (0, 18, 'Thunder Blade', 'Sword19.bmd', 105, 140, 168, 0, 40, 1, 2, 3, 180, 50, 0, 0, 8),

            -- Category 1: Axes (OpenMU 0.95d Weapons.cs)
            (1, 0, 'Small Axe', 'Axe01.bmd', 1, 1, 6, 0, 20, 0, 1, 3, 20, 0, 0, 0, 7),
            (1, 1, 'Hand Axe', 'Axe02.bmd', 4, 4, 9, 0, 30, 0, 1, 3, 70, 0, 0, 0, 7),
            (1, 2, 'Double Axe', 'Axe03.bmd', 14, 14, 24, 0, 20, 0, 1, 3, 90, 0, 0, 0, 2),
            (1, 3, 'Tomahawk', 'Axe04.bmd', 18, 18, 28, 0, 30, 0, 1, 3, 100, 0, 0, 0, 2),
            (1, 4, 'Elven Axe', 'Axe05.bmd', 26, 26, 38, 0, 40, 0, 1, 3, 50, 70, 0, 0, 5),
            (1, 5, 'Battle Axe', 'Axe06.bmd', 30, 36, 44, 0, 20, 1, 2, 3, 120, 0, 0, 0, 6),
            (1, 6, 'Nikkea Axe', 'Axe07.bmd', 34, 38, 50, 0, 30, 1, 2, 3, 130, 0, 0, 0, 6),
            (1, 7, 'Larkan Axe', 'Axe08.bmd', 46, 54, 67, 0, 25, 1, 2, 3, 140, 0, 0, 0, 2),
            (1, 8, 'Crescent Axe', 'Axe09.bmd', 54, 69, 89, 0, 30, 1, 2, 3, 100, 40, 0, 0, 3),

            -- Category 2: Maces (OpenMU 0.95d Weapons.cs)
            (2, 0, 'Mace', 'Mace01.bmd', 7, 7, 13, 0, 15, 0, 1, 3, 100, 0, 0, 0, 2),
            (2, 1, 'Morning Star', 'Mace02.bmd', 13, 13, 22, 0, 15, 0, 1, 3, 100, 0, 0, 0, 2),
            (2, 2, 'Flail', 'Mace03.bmd', 22, 22, 32, 0, 15, 0, 1, 3, 80, 50, 0, 0, 2),
            (2, 3, 'Great Hammer', 'Mace04.bmd', 38, 45, 56, 0, 15, 1, 2, 3, 150, 0, 0, 0, 2),
            (2, 4, 'Crystal Morning Star', 'Mace05.bmd', 66, 78, 107, 0, 30, 1, 2, 3, 130, 0, 0, 0, 7),
            (2, 5, 'Crystal Sword', 'Mace06.bmd', 72, 89, 120, 0, 40, 1, 2, 4, 130, 70, 0, 0, 7),
            (2, 6, 'Chaos Dragon Axe', 'Mace07.bmd', 75, 102, 130, 0, 35, 1, 2, 4, 140, 50, 0, 0, 2),

            -- Category 3: Spears (OpenMU 0.95d Weapons.cs)
            (3, 0, 'Light Spear', 'Spear01.bmd', 42, 50, 63, 0, 25, 1, 2, 4, 60, 70, 0, 0, 6),
            (3, 1, 'Spear', 'Spear02.bmd', 23, 30, 41, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 2, 'Dragon Lance', 'Spear03.bmd', 15, 21, 33, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 3, 'Giant Trident', 'Spear04.bmd', 29, 35, 43, 0, 25, 1, 2, 4, 90, 30, 0, 0, 6),
            (3, 4, 'Serpent Spear', 'Spear05.bmd', 46, 58, 80, 0, 20, 1, 2, 4, 90, 30, 0, 0, 6),
            (3, 5, 'Double Poleaxe', 'Spear06.bmd', 13, 19, 31, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 6, 'Halberd', 'Spear07.bmd', 19, 25, 35, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 7, 'Berdysh', 'Spear08.bmd', 37, 42, 54, 0, 30, 1, 2, 4, 80, 50, 0, 0, 6),
            (3, 8, 'Great Scythe', 'Spear09.bmd', 54, 71, 92, 0, 25, 1, 2, 4, 90, 50, 0, 0, 6),
            (3, 9, 'Bill of Balrog', 'Spear10.bmd', 63, 76, 102, 0, 25, 1, 2, 4, 80, 50, 0, 0, 6),

            -- Category 4: Bows & Crossbows (OpenMU 0.95d Weapons.cs)
            -- Bows are NOT two-handed: arrows/bolts go in slot 1 (off-hand)
            (4, 0, 'Short Bow', 'Bow01.bmd', 2, 3, 5, 0, 30, 0, 2, 3, 20, 80, 0, 0, 4),
            (4, 1, 'Bow', 'Bow02.bmd', 8, 9, 13, 0, 30, 0, 2, 3, 30, 90, 0, 0, 4),
            (4, 2, 'Elven Bow', 'Bow03.bmd', 16, 17, 24, 0, 30, 0, 2, 3, 30, 90, 0, 0, 4),
            (4, 3, 'Battle Bow', 'Bow04.bmd', 26, 28, 37, 0, 30, 0, 2, 3, 30, 90, 0, 0, 4),
            (4, 4, 'Tiger Bow', 'Bow05.bmd', 40, 42, 52, 0, 30, 0, 2, 4, 30, 100, 0, 0, 4),
            (4, 5, 'Silver Bow', 'Bow06.bmd', 56, 59, 71, 0, 40, 0, 2, 4, 30, 100, 0, 0, 4),
            (4, 6, 'Chaos Nature Bow', 'Bow07.bmd', 75, 88, 106, 0, 35, 0, 2, 4, 40, 150, 0, 0, 4),
            (4, 7, 'Bolt', 'Arrows01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
            (4, 8, 'Crossbow', 'CrossBow01.bmd', 4, 5, 8, 0, 40, 0, 2, 2, 20, 90, 0, 0, 4),
            (4, 9, 'Golden Crossbow', 'CrossBow02.bmd', 12, 13, 19, 0, 40, 0, 2, 2, 30, 90, 0, 0, 4),
            (4, 10, 'Arquebus', 'CrossBow03.bmd', 20, 22, 30, 0, 40, 0, 2, 2, 30, 90, 0, 0, 4),
            (4, 11, 'Light Crossbow', 'CrossBow04.bmd', 32, 35, 44, 0, 40, 0, 2, 3, 30, 90, 0, 0, 4),
            (4, 12, 'Serpent Crossbow', 'CrossBow05.bmd', 48, 50, 61, 0, 40, 0, 2, 3, 30, 100, 0, 0, 4),
            (4, 13, 'Bluewing Crossbow', 'CrossBow06.bmd', 68, 68, 82, 0, 40, 0, 2, 3, 40, 110, 0, 0, 4),
            (4, 14, 'Aquagold Crossbow', 'CrossBow07.bmd', 72, 78, 92, 0, 30, 0, 2, 3, 50, 130, 0, 0, 4),
            (4, 15, 'Arrows', 'Arrows02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
            (4, 16, 'Saint Crossbow', 'CrossBow08.bmd', 83, 90, 108, 0, 35, 0, 2, 3, 50, 130, 0, 0, 4),

            -- Category 5: Staves (OpenMU 0.95d Weapons.cs)
            (5, 0, 'Skull Staff', 'Staff01.bmd', 6, 3, 4, 0, 20, 0, 1, 3, 40, 0, 0, 0, 1),
            (5, 1, 'Angelic Staff', 'Staff02.bmd', 18, 10, 12, 0, 25, 0, 2, 3, 50, 0, 0, 0, 1),
            (5, 2, 'Serpent Staff', 'Staff03.bmd', 30, 17, 18, 0, 25, 0, 2, 3, 50, 0, 0, 0, 1),
            (5, 3, 'Thunder Staff', 'Staff04.bmd', 42, 23, 25, 0, 25, 0, 2, 4, 40, 10, 0, 0, 1),
            (5, 4, 'Gorgon Staff', 'Staff05.bmd', 52, 29, 32, 0, 25, 0, 2, 4, 60, 0, 0, 0, 1),
            (5, 5, 'Legendary Staff', 'Staff06.bmd', 59, 29, 31, 0, 25, 0, 1, 4, 50, 0, 0, 0, 1),
            (5, 6, 'Staff of Resurrection', 'Staff07.bmd', 70, 35, 39, 0, 25, 0, 1, 4, 60, 10, 0, 0, 1),
            (5, 7, 'Chaos Lightning Staff', 'Staff08.bmd', 75, 47, 48, 0, 30, 0, 2, 4, 60, 10, 0, 0, 1),
            (5, 8, 'Staff of Destruction', 'Staff09.bmd', 90, 55, 60, 0, 35, 0, 2, 4, 60, 10, 0, 0, 9),

            -- Category 6: Shields (OpenMU v0.75)
            (6, 0, 'Small Shield', 'Shield01.bmd', 3, 0, 0, 3, 0, 0, 2, 2, 70, 0, 0, 0, 15),
            (6, 1, 'Horn Shield', 'Shield02.bmd', 9, 0, 0, 9, 0, 0, 2, 2, 100, 0, 0, 0, 2),
            (6, 2, 'Kite Shield', 'Shield03.bmd', 12, 0, 0, 12, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (6, 3, 'Elven Shield', 'Shield04.bmd', 21, 0, 0, 21, 0, 0, 2, 2, 30, 100, 0, 0, 4),
            (6, 4, 'Buckler', 'Shield05.bmd', 6, 0, 0, 6, 0, 0, 2, 2, 80, 0, 0, 0, 15),
            (6, 5, 'Dragon Slayer Shield', 'Shield06.bmd', 35, 0, 0, 36, 0, 0, 2, 2, 100, 40, 0, 0, 2),
            (6, 6, 'Skull Shield', 'Shield07.bmd', 15, 0, 0, 15, 0, 0, 2, 2, 110, 0, 0, 0, 15),
            (6, 7, 'Spiked Shield', 'Shield08.bmd', 30, 0, 0, 30, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (6, 8, 'Tower Shield', 'Shield09.bmd', 40, 0, 0, 40, 0, 0, 2, 2, 130, 0, 0, 0, 11),
            (6, 9, 'Plate Shield', 'Shield10.bmd', 25, 0, 0, 25, 0, 0, 2, 2, 120, 0, 0, 0, 2),
            (6, 10, 'Big Round Shield', 'Shield11.bmd', 18, 0, 0, 18, 0, 0, 2, 2, 120, 0, 0, 0, 2),
            (6, 11, 'Serpent Shield', 'Shield12.bmd', 45, 0, 0, 45, 0, 0, 2, 2, 130, 0, 0, 0, 11),
            (6, 12, 'Bronze Shield', 'Shield13.bmd', 54, 0, 0, 54, 0, 0, 2, 2, 140, 0, 0, 0, 2),
            (6, 13, 'Dragon Shield', 'Shield14.bmd', 60, 0, 0, 60, 0, 0, 2, 2, 120, 40, 0, 0, 2),
            (6, 14, 'Legendary Shield', 'Shield15.bmd', 48, 0, 0, 48, 0, 0, 2, 3, 90, 25, 0, 0, 5),

            -- Category 7-11: Armors (OpenMU v0.75 Armors.cs - defense values corrected)
            -- Helmets (7) - defense from OpenMU CreateArmor(num,slot,w,h,name,dropLvl,DEFENSE,dur,...)
            (7, 0, 'Bronze Helm', 'HelmMale01.bmd', 1, 0, 0, 9, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (7, 1, 'Dragon Helm', 'HelmMale02.bmd', 57, 0, 0, 24, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (7, 2, 'Pad Helm', 'HelmMale03.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (7, 3, 'Legendary Helm', 'HelmMale04.bmd', 50, 0, 0, 18, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 4, 'Bone Helm', 'HelmMale05.bmd', 18, 0, 0, 9, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 5, 'Leather Helm', 'HelmMale06.bmd', 1, 0, 0, 5, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (7, 6, 'Scale Helm', 'HelmMale07.bmd', 26, 0, 0, 12, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (7, 7, 'Sphinx Mask', 'HelmMale08.bmd', 32, 0, 0, 13, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 8, 'Brass Helm', 'HelmMale09.bmd', 36, 0, 0, 17, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (7, 9, 'Plate Helm', 'HelmMale10.bmd', 46, 0, 0, 20, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (7, 10, 'Vine Helm', 'HelmElf01.bmd', 6, 0, 0, 4, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (7, 11, 'Silk Helm', 'HelmElf02.bmd', 1, 0, 0, 8, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (7, 12, 'Wind Helm', 'HelmElf03.bmd', 28, 0, 0, 12, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (7, 13, 'Spirit Helm', 'HelmElf04.bmd', 40, 0, 0, 16, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (7, 14, 'Guardian Helm', 'HelmElf05.bmd', 53, 0, 0, 23, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Armors (8)
            (8, 0, 'Bronze Armor', 'ArmorMale01.bmd', 1, 0, 0, 14, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (8, 1, 'Dragon Armor', 'ArmorMale02.bmd', 59, 0, 0, 37, 0, 0, 2, 3, 120, 30, 0, 0, 2),
            (8, 2, 'Pad Armor', 'ArmorMale03.bmd', 1, 0, 0, 7, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (8, 3, 'Legendary Armor', 'ArmorMale04.bmd', 56, 0, 0, 22, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (8, 4, 'Bone Armor', 'ArmorMale05.bmd', 22, 0, 0, 13, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (8, 5, 'Leather Armor', 'ArmorMale06.bmd', 1, 0, 0, 10, 0, 0, 2, 3, 20, 0, 0, 0, 2),
            (8, 6, 'Scale Armor', 'ArmorMale07.bmd', 28, 0, 0, 18, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (8, 7, 'Sphinx Armor', 'ArmorMale08.bmd', 38, 0, 0, 17, 0, 0, 2, 3, 40, 0, 0, 0, 1),
            (8, 8, 'Brass Armor', 'ArmorMale09.bmd', 38, 0, 0, 22, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (8, 9, 'Plate Armor', 'ArmorMale10.bmd', 48, 0, 0, 30, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (8, 10, 'Vine Armor', 'ArmorElf01.bmd', 10, 0, 0, 8, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (8, 11, 'Silk Armor', 'ArmorElf02.bmd', 1, 0, 0, 12, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (8, 12, 'Wind Armor', 'ArmorElf03.bmd', 32, 0, 0, 16, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (8, 13, 'Spirit Armor', 'ArmorElf04.bmd', 44, 0, 0, 21, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (8, 14, 'Guardian Armor', 'ArmorElf05.bmd', 57, 0, 0, 29, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Pants (9)
            (9, 0, 'Bronze Pants', 'PantMale01.bmd', 1, 0, 0, 10, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (9, 1, 'Dragon Pants', 'PantMale02.bmd', 55, 0, 0, 26, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (9, 2, 'Pad Pants', 'PantMale03.bmd', 1, 0, 0, 5, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (9, 3, 'Legendary Pants', 'PantMale04.bmd', 53, 0, 0, 20, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 4, 'Bone Pants', 'PantMale05.bmd', 20, 0, 0, 10, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 5, 'Leather Pants', 'PantMale06.bmd', 1, 0, 0, 7, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (9, 6, 'Scale Pants', 'PantMale07.bmd', 25, 0, 0, 14, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (9, 7, 'Sphinx Pants', 'PantMale08.bmd', 34, 0, 0, 15, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 8, 'Brass Pants', 'PantMale09.bmd', 35, 0, 0, 18, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (9, 9, 'Plate Pants', 'PantMale10.bmd', 45, 0, 0, 22, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (9, 10, 'Vine Pants', 'PantElf01.bmd', 8, 0, 0, 6, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (9, 11, 'Silk Pants', 'PantElf02.bmd', 1, 0, 0, 10, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (9, 12, 'Wind Pants', 'PantElf03.bmd', 30, 0, 0, 14, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (9, 13, 'Spirit Pants', 'PantElf04.bmd', 42, 0, 0, 18, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (9, 14, 'Guardian Pants', 'PantElf05.bmd', 54, 0, 0, 25, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Gloves (10) - defense from OpenMU CreateGloves(num,name,dropLvl,DEFENSE,atkSpd,dur,...)
            (10, 0, 'Bronze Gloves', 'GloveMale01.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (10, 1, 'Dragon Gloves', 'GloveMale02.bmd', 52, 0, 0, 14, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (10, 2, 'Pad Gloves', 'GloveMale03.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (10, 3, 'Legendary Gloves', 'GloveMale04.bmd', 44, 0, 0, 11, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 4, 'Bone Gloves', 'GloveMale05.bmd', 14, 0, 0, 5, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 5, 'Leather Gloves', 'GloveMale06.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (10, 6, 'Scale Gloves', 'GloveMale07.bmd', 22, 0, 0, 7, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (10, 7, 'Sphinx Gloves', 'GloveMale08.bmd', 28, 0, 0, 8, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 8, 'Brass Gloves', 'GloveMale09.bmd', 32, 0, 0, 9, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (10, 9, 'Plate Gloves', 'GloveMale10.bmd', 42, 0, 0, 12, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (10, 10, 'Vine Gloves', 'GloveElf01.bmd', 4, 0, 0, 2, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (10, 11, 'Silk Gloves', 'GloveElf02.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (10, 12, 'Wind Gloves', 'GloveElf03.bmd', 26, 0, 0, 6, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (10, 13, 'Spirit Gloves', 'GloveElf04.bmd', 38, 0, 0, 9, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (10, 14, 'Guardian Gloves', 'GloveElf05.bmd', 50, 0, 0, 15, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Boots (11) - defense from OpenMU CreateBoots(num,slot,w,h,name,dropLvl,DEFENSE,walkSpd,dur,...)
            (11, 0, 'Bronze Boots', 'BootMale01.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (11, 1, 'Dragon Boots', 'BootMale02.bmd', 54, 0, 0, 15, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (11, 2, 'Pad Boots', 'BootMale03.bmd', 1, 0, 0, 3, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (11, 3, 'Legendary Boots', 'BootMale04.bmd', 46, 0, 0, 12, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 4, 'Bone Boots', 'BootMale05.bmd', 16, 0, 0, 6, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 5, 'Leather Boots', 'BootMale06.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (11, 6, 'Scale Boots', 'BootMale07.bmd', 22, 0, 0, 8, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (11, 7, 'Sphinx Boots', 'BootMale08.bmd', 30, 0, 0, 9, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 8, 'Brass Boots', 'BootMale09.bmd', 32, 0, 0, 10, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (11, 9, 'Plate Boots', 'BootMale10.bmd', 42, 0, 0, 12, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (11, 10, 'Vine Boots', 'BootElf01.bmd', 5, 0, 0, 2, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (11, 11, 'Silk Boots', 'BootElf02.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (11, 12, 'Wind Boots', 'BootElf03.bmd', 27, 0, 0, 7, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (11, 13, 'Spirit Boots', 'BootElf04.bmd', 40, 0, 0, 10, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (11, 14, 'Guardian Boots', 'BootElf05.bmd', 52, 0, 0, 16, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Category 12: Wings/Orbs/Jewels (Group 12 — OpenMU 0.95d)
            -- Wings (Level 1 only in 0.95d)
             (12, 0, 'Wings of Elf', 'Wing01.bmd', 180, 0, 0, 10, 0, 0, 3, 2, 0, 0, 0, 0, 4),
             (12, 1, 'Wings of Heaven', 'Wing02.bmd', 180, 0, 0, 10, 0, 0, 3, 2, 0, 0, 0, 0, 1),
             (12, 2, 'Wings of Satan', 'Wing03.bmd', 180, 0, 0, 20, 0, 0, 3, 2, 0, 0, 0, 0, 2),
             (12, 3, 'Wings of Spirits', 'Wing04.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 4),
             (12, 4, 'Wings of Soul', 'Wing05.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 1),
             (12, 5, 'Wings of Dragon', 'Wing06.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 2),
             (12, 6, 'Wings of Darkness', 'Wing07.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 8),
             -- Orbs (Research confirmed: GemXX.bmd)
             (12, 7, 'Orb of Twisting Slash', 'Gem01.bmd', 30, 0, 0, 0, 0, 0, 1, 1, 80, 0, 0, 0, 10),
             (12, 8, 'Orb of Healing', 'Gem02.bmd', 8, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 9, 'Orb of Greater Defense', 'Gem03.bmd', 13, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 10, 'Orb of Greater Damage', 'Gem04.bmd', 18, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 11, 'Orb of Goblin', 'Gem05.bmd', 3, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
             (12, 12, 'Orb of Rageful Blow', 'Gem06.bmd', 170, 0, 0, 0, 0, 0, 1, 1, 170, 0, 0, 0, 2),
             (12, 13, 'Orb of Impale', 'Gem07.bmd', 20, 0, 0, 0, 0, 0, 1, 1, 28, 0, 0, 0, 2),
             (12, 14, 'Orb of Greater Fortitude', 'Gem08.bmd', 60, 0, 0, 0, 0, 0, 1, 1, 120, 0, 0, 0, 2),
             (12, 16, 'Orb of Fire Slash', 'Gem10.bmd', 60, 0, 0, 0, 0, 0, 1, 1, 320, 0, 0, 0, 8),
             (12, 17, 'Orb of Penetration', 'Gem11.bmd', 64, 0, 0, 0, 0, 0, 1, 1, 130, 0, 0, 0, 4),
             (12, 18, 'Orb of Ice Arrow', 'Gem12.bmd', 81, 0, 0, 0, 0, 0, 1, 1, 0, 258, 0, 0, 4),
             (12, 19, 'Orb of Death Stab', 'Gem13.bmd', 160, 0, 0, 0, 0, 0, 1, 1, 160, 0, 0, 0, 2),
             -- Basic DK skill orbs (indices 20-24)
             (12, 20, 'Orb of Falling Slash', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 21, 'Orb of Lunge', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 22, 'Orb of Uppercut', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 23, 'Orb of Cyclone', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 24, 'Orb of Slash', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             -- Elf summon orbs (indices 25-29)
             (12, 25, 'Orb of Golem Summoning', 'Gem05.bmd', 18, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 60, 4),
             (12, 26, 'Orb of Assassin Summoning', 'Gem05.bmd', 26, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 90, 4),
             (12, 27, 'Orb of Yeti Summoning', 'Gem05.bmd', 36, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 130, 4),
             (12, 28, 'Orb of Knight Summoning', 'Gem05.bmd', 48, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 170, 4),
             (12, 29, 'Orb of Bali Summoning', 'Gem05.bmd', 52, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 210, 4),
            -- Jewel of Chaos
            (12, 15, 'Jewel of Chaos', 'Jewel01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Category 13: Pets/Jewelry (OpenMU 0.95d)
            (13, 0, 'Guardian Angel', 'Helper01.bmd', 23, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 1, 'Imp', 'Helper02.bmd', 28, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 2, 'Horn of Uniria', 'Helper03.bmd', 25, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 3, 'Horn of Dinorant', 'Helper04.bmd', 110, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 8, 'Ring of Ice', 'Ring01.bmd', 20, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 9, 'Ring of Poison', 'Ring02.bmd', 17, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 10, 'Transformation Ring', 'Ring01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 12, 'Pendant of Lighting', 'Necklace01.bmd', 21, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 13, 'Pendant of Fire', 'Necklace02.bmd', 13, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Category 14: Consumables (Potions & Jewels — OpenMU 0.97d)
            (14, 0, 'Apple', 'Apple.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 1, 'Small Healing Potion', 'Potion01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 2, 'Medium Healing Potion', 'Potion02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 3, 'Large Healing Potion', 'Potion03.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 4, 'Small Mana Potion', 'Potion04.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 5, 'Medium Mana Potion', 'Potion05.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 6, 'Large Mana Potion', 'Potion06.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 8, 'Antidote', 'Antidote01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 9, 'Ale', 'Potion09.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15),
            (14, 10, 'Town Portal Scroll', 'Scroll01.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15),
            (14, 13, 'Jewel of Bless', 'Jewel02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 14, 'Jewel of Soul', 'Jewel03.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 16, 'Jewel of Life', 'Jewel04.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 22, 'Jewel of Creation', 'Jewel05.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

             -- Category 15: Scrolls (Dark Wizard — Research confirmed: BookXX.bmd)
             (15, 10, 'Scroll of Power Wave', 'Book11.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 56, 1),
             (15, 0, 'Scroll of Poison', 'Book01.bmd', 30, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 140, 1),
             (15, 1, 'Scroll of Meteorite', 'Book02.bmd', 21, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 104, 1),
             (15, 2, 'Scroll of Lightning', 'Book03.bmd', 13, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 72, 1),
             (15, 3, 'Scroll of Fire Ball', 'Book04.bmd', 5, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 40, 1),
             (15, 4, 'Scroll of Flame', 'Book05.bmd', 35, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 160, 1),
             (15, 5, 'Scroll of Teleport', 'Book06.bmd', 17, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 88, 1),
             (15, 6, 'Scroll of Ice', 'Book07.bmd', 25, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 120, 1),
             (15, 7, 'Scroll of Twister', 'Book08.bmd', 40, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 180, 1),
             (15, 8, 'Scroll of Evil Spirit', 'Book09.bmd', 50, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 220, 1),
             (15, 9, 'Scroll of Hellfire', 'Book10.bmd', 60, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 260, 1),
             (15, 11, 'Scroll of Aqua Beam', 'Book12.bmd', 74, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 345, 1),
             (15, 12, 'Scroll of Cometfall', 'Book13.bmd', 80, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 436, 1),
             (15, 13, 'Scroll of Inferno', 'Book14.bmd', 88, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 578, 1),

            -- Additional 0.97d items (Main 5.2 deep dive)

            -- Missing Swords (0)
            (0, 19, 'Sword of Destruction', 'Sword20.bmd', 76, 68, 93, 0, 30, 0, 1, 4, 124, 44, 0, 0, 8),
            (0, 20, 'Spirit Sword', 'Sword21.bmd', 88, 92, 112, 0, 35, 0, 1, 4, 140, 48, 0, 0, 2),
            (0, 21, 'Dark Master Sword', 'Sword22.bmd', 98, 108, 132, 0, 40, 0, 1, 4, 154, 50, 0, 0, 8),

            -- Missing Maces (2)
            (2, 7, 'Battle Scepter', 'Mace08.bmd', 80, 85, 110, 0, 30, 1, 2, 4, 132, 32, 0, 0, 2),
            (2, 8, 'Master Scepter', 'Mace09.bmd', 86, 92, 126, 0, 30, 1, 2, 4, 142, 38, 0, 0, 2),
            (2, 9, 'Great Scepter', 'Mace10.bmd', 92, 105, 140, 0, 35, 1, 2, 4, 152, 42, 0, 0, 2),
            (2, 10, 'Lord Scepter', 'Mace11.bmd', 96, 110, 148, 0, 35, 1, 2, 4, 158, 44, 0, 0, 2),
            (2, 11, 'Great Lord Scepter', 'Mace12.bmd', 100, 118, 156, 0, 35, 1, 2, 4, 164, 48, 0, 0, 2),
            (2, 12, 'Divine Scepter', 'Mace13.bmd', 104, 125, 168, 0, 40, 1, 2, 4, 170, 50, 0, 0, 2),
            (2, 13, 'Saint Scepter', 'Saint.bmd', 96, 106, 144, 0, 35, 0, 1, 3, 72, 18, 0, 0, 1),

            -- Missing Spears (3)
            (3, 10, 'Dragon Spear', 'Spear11.bmd', 92, 112, 140, 0, 35, 1, 2, 4, 170, 60, 0, 0, 2),

            -- Missing Bows (4)
            (4, 17, 'Celestial Bow', 'Bow18.bmd', 92, 127, 155, 0, 35, 1, 2, 4, 54, 198, 0, 0, 4),
            (4, 18, 'Divine CB of Archangel', 'CrossBow17.bmd', 100, 144, 166, 0, 35, 0, 2, 3, 40, 110, 0, 0, 4),

            -- Missing Staffs (5)
            (5, 9, 'Dragon Soul Staff', 'Staff10.bmd', 100, 46, 48, 0, 30, 0, 1, 4, 52, 16, 0, 0, 1),
            (5, 10, 'Staff of Imperial', 'Staff11.bmd', 104, 50, 53, 0, 30, 0, 2, 4, 36, 4, 0, 0, 1),
            (5, 11, 'Divine Staff of Archangel', 'Staff12.bmd', 104, 53, 55, 0, 20, 0, 2, 4, 36, 4, 0, 0, 1),

            -- Missing Shields (6)
            (6, 15, 'Grand Soul Shield', 'Shield16.bmd', 74, 0, 0, 55, 0, 0, 2, 3, 70, 23, 0, 0, 1),
            (6, 16, 'Elemental Shield', 'Shield17.bmd', 78, 0, 0, 58, 0, 0, 2, 3, 50, 110, 0, 0, 4),

            -- Missing Helms (7)
            (7, 15, 'Storm Crow Helm', 'HelmMale11.bmd', 72, 0, 0, 50, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (7, 16, 'Black Dragon Helm', 'HelmMale12.bmd', 82, 0, 0, 55, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (7, 17, 'Dark Phoenix Helm', 'HelmMale13.bmd', 92, 0, 0, 60, 0, 0, 2, 2, 205, 62, 0, 0, 10),
            (7, 18, 'Grand Soul Helm', 'HelmClass10.bmd', 81, 0, 0, 48, 0, 0, 2, 2, 59, 20, 0, 0, 1),
            (7, 19, 'Divine Helm', 'HelmClass11.bmd', 85, 0, 0, 52, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (7, 20, 'Thunder Hawk Helm', 'HelmMale14.bmd', 88, 0, 0, 54, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (7, 21, 'Great Dragon Helm', 'HelmMale15.bmd', 104, 0, 0, 66, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Armors (8)
            (8, 15, 'Storm Crow Armor', 'ArmorMale11.bmd', 80, 0, 0, 58, 0, 0, 2, 3, 150, 70, 0, 0, 8),
            (8, 16, 'Black Dragon Armor', 'ArmorMale12.bmd', 90, 0, 0, 63, 0, 0, 2, 3, 170, 60, 0, 0, 2),
            (8, 17, 'Dark Phoenix Armor', 'ArmorMale13.bmd', 100, 0, 0, 70, 0, 0, 2, 3, 214, 65, 0, 0, 10),
            (8, 18, 'Grand Soul Armor', 'ArmorClass10.bmd', 91, 0, 0, 52, 0, 0, 2, 3, 59, 20, 0, 0, 1),
            (8, 19, 'Divine Armor', 'ArmorClass11.bmd', 92, 0, 0, 56, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (8, 20, 'Thunder Hawk Armor', 'ArmorMale14.bmd', 107, 0, 0, 68, 0, 0, 2, 3, 170, 70, 0, 0, 8),
            (8, 21, 'Great Dragon Armor', 'ArmorMale15.bmd', 126, 0, 0, 80, 0, 0, 2, 3, 200, 58, 0, 0, 10),

            -- Missing Pants (9)
            (9, 15, 'Storm Crow Pants', 'PantMale11.bmd', 74, 0, 0, 50, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (9, 16, 'Black Dragon Pants', 'PantMale12.bmd', 84, 0, 0, 55, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (9, 17, 'Dark Phoenix Pants', 'PantMale13.bmd', 96, 0, 0, 62, 0, 0, 2, 2, 207, 63, 0, 0, 10),
            (9, 18, 'Grand Soul Pants', 'PantClass10.bmd', 86, 0, 0, 48, 0, 0, 2, 2, 59, 20, 0, 0, 1),
            (9, 19, 'Divine Pants', 'PantClass11.bmd', 88, 0, 0, 52, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (9, 20, 'Thunder Hawk Pants', 'PantMale14.bmd', 99, 0, 0, 60, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (9, 21, 'Great Dragon Pants', 'PantMale15.bmd', 113, 0, 0, 72, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Gloves (10)
            (10, 15, 'Storm Crow Gloves', 'GloveMale11.bmd', 70, 0, 0, 46, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (10, 16, 'Black Dragon Gloves', 'GloveMale12.bmd', 76, 0, 0, 50, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (10, 17, 'Dark Phoenix Gloves', 'GloveMale13.bmd', 86, 0, 0, 56, 0, 0, 2, 2, 205, 63, 0, 0, 10),
            (10, 18, 'Grand Soul Gloves', 'GloveClass10.bmd', 70, 0, 0, 44, 0, 0, 2, 2, 49, 10, 0, 0, 1),
            (10, 19, 'Divine Gloves', 'GloveClass11.bmd', 72, 0, 0, 48, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (10, 20, 'Thunder Hawk Gloves', 'GloveMale14.bmd', 88, 0, 0, 54, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (10, 21, 'Great Dragon Gloves', 'GloveMale15.bmd', 94, 0, 0, 64, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Boots (11)
            (11, 15, 'Storm Crow Boots', 'BootMale11.bmd', 72, 0, 0, 48, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (11, 16, 'Black Dragon Boots', 'BootMale12.bmd', 78, 0, 0, 52, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (11, 17, 'Dark Phoenix Boots', 'BootMale13.bmd', 93, 0, 0, 58, 0, 0, 2, 2, 198, 60, 0, 0, 10),
            (11, 18, 'Grand Soul Boots', 'BootClass10.bmd', 76, 0, 0, 44, 0, 0, 2, 2, 59, 10, 0, 0, 1),
            (11, 19, 'Divine Boots', 'BootClass11.bmd', 81, 0, 0, 50, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (11, 20, 'Thunder Hawk Boots', 'BootMale14.bmd', 92, 0, 0, 56, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (11, 21, 'Great Dragon Boots', 'BootMale15.bmd', 98, 0, 0, 68, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Helpers/Jewelry (13)
            (13, 4, 'Dark Horse Horn', 'DarkHorseHorn.bmd', 110, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 5, 'Spirit Bill', 'SpiritBill.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 7, 'Covenant', 'Covenant.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 11, 'Summon Book', 'SummonBook.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 21, 'Fire Ring', 'FireRing.bmd', 68, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 22, 'Ground Ring', 'GroundRing.bmd', 76, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 23, 'Wind Ring', 'WindRing.bmd', 84, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 24, 'Mana Ring', 'ManaRing.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 25, 'Ice Necklace', 'IceNecklace.bmd', 68, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 26, 'Wind Necklace', 'WindNecklace.bmd', 76, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 27, 'Water Necklace', 'WaterNecklace.bmd', 84, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 28, 'AG Necklace', 'AgNecklace.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 29, 'Chaos Castle Invitation', 'EventChaosCastle.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Missing Potions/Consumables (14)
            (14, 7, 'Special Healing Potion', 'SpecialPotion.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 11, 'Box of Luck', 'MagicBox01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 12, 'Heart of Love', 'Event01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 15, 'Zen', 'Gold01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 17, 'Devil Square Key (Bronze)', 'Devil00.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 18, 'Devil Square Key (Silver)', 'Devil01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 19, 'Devil Square Key (Gold)', 'Devil02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 20, 'Remedy of Love', 'Drink00.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 31, 'Guardian Angel Scroll', 'Suho.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15)
     )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedItemDefinitions error: %s\n", err);
    sqlite3_free(err);
  } else {
    // Count actual items seeded
    sqlite3_stmt *countStmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM item_definitions", -1,
                       &countStmt, nullptr);
    int seeded = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW)
      seeded = sqlite3_column_int(countStmt, 0);
    sqlite3_finalize(countStmt);
    printf("[DB] Seeded %d 0.97d item definitions\n", seeded);

    // Set buy prices by category formula + specific overrides
    // Weapons (cats 0-5): level_req * 100 + damage_max * 20
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 100 + "
                 "damage_max * 20 "
                 "WHERE category <= 5 AND buy_price = 0",
                 nullptr, nullptr, nullptr);
    // Shields (cat 6): level_req * 80 + defense * 30
    sqlite3_exec(
        m_db,
        "UPDATE item_definitions SET buy_price = level_req * 80 + defense * 30 "
        "WHERE category = 6 AND buy_price = 0",
        nullptr, nullptr, nullptr);
    // Armor (cats 7-11): level_req * 80 + defense * 30
    sqlite3_exec(
        m_db,
        "UPDATE item_definitions SET buy_price = level_req * 80 + defense * 30 "
        "WHERE category >= 7 AND category <= 11 AND buy_price = 0",
        nullptr, nullptr, nullptr);
    // Wings (cat 12, idx 0-6): fixed 50000
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 50000 "
                 "WHERE category = 12 AND item_index <= 6",
                 nullptr, nullptr, nullptr);
    // Orbs (cat 12, idx 7-24): level_req * 200
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 200 "
                 "WHERE category = 12 AND item_index >= 7 AND item_index <= 24"
                 " AND item_index != 15",
                 nullptr, nullptr, nullptr);
    // Jewel of Chaos (12,15): 810000
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 810000 "
                 "WHERE category = 12 AND item_index = 15",
                 nullptr, nullptr, nullptr);
    // Pets/Jewelry (cat 13): level_req * 300
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 300 "
                 "WHERE category = 13 AND buy_price = 0",
                 nullptr, nullptr, nullptr);
    // Potions (cat 14) — specific prices
    const char *potionPrices = R"(
        UPDATE item_definitions SET buy_price = CASE item_index
            WHEN 0 THEN 20 WHEN 1 THEN 80 WHEN 2 THEN 300 WHEN 3 THEN 1000
            WHEN 4 THEN 120 WHEN 5 THEN 450 WHEN 6 THEN 1500
            WHEN 7 THEN 3500 WHEN 8 THEN 100 WHEN 9 THEN 1000 WHEN 10 THEN 2000
            WHEN 13 THEN 9000000 WHEN 14 THEN 6000000 WHEN 16 THEN 45000000
            WHEN 20 THEN 900 WHEN 22 THEN 36000000
            ELSE 500 END
        WHERE category = 14;
    )";
    sqlite3_exec(m_db, potionPrices, nullptr, nullptr, nullptr);
    // Scrolls (cat 15) — specific prices
    const char *scrollPrices = R"(
        UPDATE item_definitions SET buy_price = CASE item_index
            WHEN 0 THEN 3800 WHEN 1 THEN 3100 WHEN 2 THEN 2400
            WHEN 3 THEN 1500 WHEN 4 THEN 4400 WHEN 5 THEN 2800
            WHEN 6 THEN 3500 WHEN 7 THEN 5000 WHEN 8 THEN 6200
            WHEN 9 THEN 7500 WHEN 10 THEN 500
            WHEN 11 THEN 12000 WHEN 12 THEN 18000 WHEN 13 THEN 30000
            ELSE 1000 END
        WHERE category = 15;
    )";
    sqlite3_exec(m_db, scrollPrices, nullptr, nullptr, nullptr);
    // Ammo — Bolt (4,7) and Arrows (4,15) — very cheap (255 zen per stack)
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 1 "
                 "WHERE (category = 4 AND item_index = 7) OR (category = 4 AND "
                 "item_index = 15)",
                 nullptr, nullptr, nullptr);
    printf("[DB] Set buy prices for all items\n");

    // Staff Rise magicPower values (OpenMU Version075 Weapons.cs)
    const char *staffMagicPower = R"(
        UPDATE item_definitions SET magic_power = CASE item_index
            WHEN 0 THEN 6   WHEN 1 THEN 20  WHEN 2 THEN 34
            WHEN 3 THEN 46  WHEN 4 THEN 58  WHEN 5 THEN 59
            WHEN 6 THEN 70  WHEN 7 THEN 94  WHEN 8 THEN 115
            ELSE 0 END
        WHERE category = 5;
    )";
    sqlite3_exec(m_db, staffMagicPower, nullptr, nullptr, nullptr);
    printf("[DB] Set staff magicPower values\n");
  }
}

ItemDefinition Database::GetItemDefinition(int id) {
  return GetItemDefinition(static_cast<uint8_t>(id / 32),
                           static_cast<uint8_t>(id % 32));
}

ItemDefinition Database::GetItemDefinition(uint8_t category,
                                           uint8_t itemIndex) {
  ItemDefinition item;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, category, item_index, name, model_file, level_req, "
      "damage_min, damage_max, defense, attack_speed, two_handed, "
      "width, height, req_str, req_dex, req_vit, req_ene, class_flags, "
      "buy_price, magic_power "
      "FROM item_definitions WHERE category=? AND item_index=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return item;

  sqlite3_bind_int(stmt, 1, category);
  sqlite3_bind_int(stmt, 2, itemIndex);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    item.id = sqlite3_column_int(stmt, 0);
    item.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
    item.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    item.modelFile =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    item.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
    item.damageMin = static_cast<uint16_t>(sqlite3_column_int(stmt, 6));
    item.damageMax = static_cast<uint16_t>(sqlite3_column_int(stmt, 7));
    item.defense = static_cast<uint16_t>(sqlite3_column_int(stmt, 8));
    item.attackSpeed = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    item.twoHanded = static_cast<uint8_t>(sqlite3_column_int(stmt, 10));
    item.width = static_cast<uint8_t>(sqlite3_column_int(stmt, 11));
    item.height = static_cast<uint8_t>(sqlite3_column_int(stmt, 12));
    item.reqStrength = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
    item.reqDexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
    item.reqVitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
    item.reqEnergy = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
    item.classFlags = static_cast<uint32_t>(sqlite3_column_int64(stmt, 17));
    item.buyPrice = static_cast<uint32_t>(sqlite3_column_int(stmt, 18));
    item.magicPower = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
  }
  sqlite3_finalize(stmt);
  return item;
}

std::vector<ItemDropInfo> Database::GetItemsByLevelRange(int minLevel,
                                                         int maxLevel) {
  std::vector<ItemDropInfo> items;
  sqlite3_stmt *stmt = nullptr;
  // Exclude Wings (12+), Orbs, Quest Items, etc. Only allow Categories 0-11
  // (Gear)
  const char *sql =
      "SELECT category, item_index, name, level_req FROM item_definitions "
      "WHERE level_req BETWEEN ? AND ? AND category <= 11";

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return items;

  sqlite3_bind_int(stmt, 1, minLevel);
  sqlite3_bind_int(stmt, 2, maxLevel);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ItemDropInfo info;
    info.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
    info.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
    info.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    info.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 3));
    items.push_back(info);
  }
  sqlite3_finalize(stmt);
  return items;
}

std::vector<EquipmentSlot> Database::GetCharacterEquipment(int characterId) {
  std::vector<EquipmentSlot> equip;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT slot, item_category, item_index, item_level, quantity "
      "FROM character_equipment WHERE character_id=? ORDER BY slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return equip;

  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    EquipmentSlot e;
    e.slot = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
    e.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
    e.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    e.itemLevel = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    e.quantity = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    equip.push_back(e);
  }
  sqlite3_finalize(stmt);
  return equip;
}

void Database::SeedMonsterSpawns() {
  sqlite3_stmt *stmt = nullptr;
  char *err = nullptr;

  // Check if Lorencia already seeded
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=0",
                     -1, &stmt, nullptr);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (count > 0) {
    printf("[DB] Lorencia monster spawns already seeded (%d entries)\n", count);
  } else {

  // Lorencia monster spawns from MonsterSetBase.txt (Main 5.2 / 0.97k
  // reference) Coordinates are grid (X,Y) from the reference spawn data
  const char *sql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Bull Fighter (type 0): Level 6 — northwest fields (MonsterSetBase: ~152,53)
            (0, 0, 152, 53, 2), (0, 0, 154, 51, 4), (0, 0, 155, 54, 6),
            (0, 0, 148, 50, 1), (0, 0, 156, 48, 3), (0, 0, 150, 56, 5),
            (0, 0, 160, 55, 0), (0, 0, 145, 45, 7), (0, 0, 165, 60, 2),
            (0, 0, 158, 42, 4), (0, 0, 140, 52, 6), (0, 0, 170, 50, 1),
            -- Hound (type 1): Level 9 — west fields (MonsterSetBase: ~143,76 + west region 8-94)
            (1, 0, 143, 76, 2), (1, 0, 145, 75, 5),
            (1, 0, 140, 80, 1), (1, 0, 138, 72, 3),
            (1, 0, 35, 55, 4), (1, 0, 50, 85, 6), (1, 0, 65, 115, 0),
            (1, 0, 30, 155, 7), (1, 0, 55, 195, 2), (1, 0, 80, 35, 1),
            (1, 0, 45, 130, 3), (1, 0, 70, 200, 5),
            -- Budge Dragon (type 2): Level 4 — east/southeast (MonsterSetBase: ~191,142 + 180-226,90-244)
            (2, 0, 191, 142, 2), (2, 0, 193, 143, 4), (2, 0, 190, 140, 6), (2, 0, 194, 141, 0),
            (2, 0, 198, 126, 3), (2, 0, 200, 130, 5), (2, 0, 195, 135, 1),
            (2, 0, 206, 151, 2), (2, 0, 208, 153, 4), (2, 0, 204, 149, 6),
            (2, 0, 224, 168, 0), (2, 0, 226, 170, 3),
            -- Spider (type 3): Level 2 — near town east (MonsterSetBase: ~187,119 + 181,128)
            (3, 0, 187, 119, 2), (3, 0, 188, 117, 4), (3, 0, 186, 120, 6), (3, 0, 189, 118, 1),
            (3, 0, 181, 128, 3), (3, 0, 183, 130, 5), (3, 0, 180, 126, 0), (3, 0, 182, 129, 7),
            (3, 0, 179, 154, 2), (3, 0, 175, 155, 4),
            (3, 0, 185, 135, 1), (3, 0, 191, 125, 3),
            (3, 0, 195, 115, 5), (3, 0, 192, 122, 6),

            -- Elite Bull Fighter (type 4): Level 12 — west region (OpenMU: X:8-94, Y:11-244)
            (4, 0, 25, 30, 2), (4, 0, 40, 50, 4), (4, 0, 55, 70, 6),
            (4, 0, 30, 90, 1), (4, 0, 45, 110, 3), (4, 0, 60, 130, 5),
            (4, 0, 35, 150, 0), (4, 0, 50, 170, 7), (4, 0, 65, 190, 2),
            (4, 0, 40, 210, 4), (4, 0, 55, 230, 6), (4, 0, 70, 40, 1),
            (4, 0, 85, 60, 3), (4, 0, 20, 80, 5), (4, 0, 75, 100, 0),

            -- Lich (type 6): Level 14 — southeast region (OpenMU: X:95-175, Y:168-244)
            (6, 0, 110, 180, 2), (6, 0, 125, 195, 4), (6, 0, 140, 210, 6),
            (6, 0, 155, 225, 1), (6, 0, 100, 190, 3), (6, 0, 115, 205, 5),
            (6, 0, 130, 220, 0), (6, 0, 145, 235, 7), (6, 0, 160, 175, 2),
            (6, 0, 170, 200, 4), (6, 0, 105, 240, 6),

            -- Giant (type 7): Level 17 — far west (OpenMU: X:8-60, Y:11-80 — reduced count, high level)
            (7, 0, 15, 20, 2), (7, 0, 25, 35, 4), (7, 0, 40, 50, 6),
            (7, 0, 55, 65, 1), (7, 0, 20, 45, 3), (7, 0, 35, 60, 5),
            (7, 0, 50, 75, 0), (7, 0, 30, 25, 7), (7, 0, 45, 40, 2),

            -- Skeleton Warrior (type 14): Level 19 — southeast (OpenMU: X:95-175, Y:168-244)
            (14, 0, 100, 175, 2), (14, 0, 120, 185, 4), (14, 0, 135, 200, 6),
            (14, 0, 150, 215, 1), (14, 0, 165, 230, 3), (14, 0, 108, 195, 5),
            (14, 0, 142, 240, 0), (14, 0, 155, 180, 7), (14, 0, 170, 210, 2)
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Lorencia monster spawns (8 types: Bull Fighter, Hound, "
           "Budge Dragon, Spider, Elite Bull Fighter, Lich, Giant, Skeleton "
           "Warrior)\n");
  }
  } // end Lorencia else

  // Seed Dungeon (map_id=1) spawns if not already present
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=1",
                     -1, &stmt, nullptr);
  int dungeonCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    dungeonCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (dungeonCount > 0) {
    printf("[DB] Dungeon spawns already seeded (%d entries)\n", dungeonCount);
  } else {

  // Dungeon monster spawns — exact coordinates from OpenMU Version075 Dungeon.cs
  // 258 unique spawns (IDs 101-359, skipping 300) across 12 monster types
  // Types: 5=Hell Hound, 8=Poison Bull, 9=Thunder Lich, 10=Dark Knight,
  //        11=Ghost, 12=Larva, 13=Hell Spider, 14=Skeleton Warrior,
  //        15=Skeleton Archer, 16=Elite Skeleton, 17=Cyclops, 18=Gorgon
  const char *dungeonSql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Poison Bull (type 8) — west corridors + deep areas
            (8,1,42,122,2),(8,1,4,73,2),(8,1,12,56,2),(8,1,2,78,2),
            (8,1,187,15,2),(8,1,158,17,2),(8,1,119,47,2),(8,1,25,100,2),
            (8,1,21,82,2),(8,1,16,55,2),(8,1,35,55,2),(8,1,116,47,2),
            (8,1,122,46,2),(8,1,193,30,2),(8,1,39,112,2),(8,1,26,111,2),
            (8,1,15,113,2),(8,1,12,97,2),(8,1,44,56,2),(8,1,205,24,2),
            -- Elite Skeleton (type 16) — north + deep corridors
            (16,1,108,9,2),(16,1,159,41,2),(16,1,115,7,2),(16,1,124,20,2),
            (16,1,90,6,2),(16,1,212,83,2),(16,1,211,90,2),(16,1,226,58,2),
            (16,1,239,56,2),(16,1,235,23,2),(16,1,29,80,2),(16,1,86,105,2),
            (16,1,88,104,2),(16,1,240,77,2),(16,1,247,77,2),(16,1,248,20,2),
            (16,1,244,9,2),(16,1,229,11,2),(16,1,155,32,2),(16,1,137,33,2),
            (16,1,122,29,2),(16,1,48,11,2),
            -- Skeleton Warrior (type 14) — entrance + south corridors
            (14,1,120,232,2),(14,1,98,221,2),(14,1,100,220,2),(14,1,100,225,2),
            (14,1,108,232,2),(14,1,107,230,2),(14,1,84,244,2),(14,1,94,242,2),
            (14,1,108,195,2),(14,1,94,208,2),(14,1,62,181,2),(14,1,83,198,2),
            (14,1,33,209,2),(14,1,45,201,2),(14,1,31,247,2),(14,1,3,246,2),
            (14,1,29,246,2),(14,1,120,243,2),(14,1,99,49,2),(14,1,152,247,2),
            (14,1,156,247,2),(14,1,97,187,2),(14,1,105,208,2),(14,1,85,208,2),
            (14,1,84,220,2),(14,1,85,229,2),(14,1,68,227,2),(14,1,61,238,2),
            (14,1,73,245,2),(14,1,3,232,2),(14,1,3,219,2),(14,1,7,219,2),
            (14,1,17,211,2),
            -- Larva (type 12) — south + mid corridors
            (12,1,65,205,3),(12,1,87,177,7),(12,1,70,195,1),(12,1,61,213,5),
            (12,1,69,222,8),(12,1,57,247,2),(12,1,86,196,6),
            (12,1,46,210,4),(12,1,137,241,1),(12,1,130,244,7),
            (12,1,193,224,3),(12,1,79,154,5),(12,1,100,198,2),(12,1,95,192,8),
            -- Ghost (type 11) — most common, throughout dungeon
            (11,1,69,167,2),(11,1,136,149,2),(11,1,133,212,2),(11,1,135,212,2),
            (11,1,151,212,2),(11,1,168,220,2),(11,1,46,146,2),(11,1,49,182,2),
            (11,1,44,162,2),(11,1,45,180,2),(11,1,45,183,2),(11,1,164,232,2),
            (11,1,92,157,2),(11,1,102,174,2),(11,1,113,166,2),(11,1,222,223,2),
            (11,1,163,184,2),(11,1,164,186,2),(11,1,150,193,2),(11,1,142,218,2),
            (11,1,199,116,2),(11,1,240,247,2),(11,1,242,247,2),(11,1,241,226,2),
            (11,1,246,220,2),(11,1,243,201,2),(11,1,244,192,2),(11,1,213,248,2),
            (11,1,148,149,2),(11,1,247,7,2),(11,1,240,9,2),(11,1,241,9,2),
            (11,1,227,248,2),(11,1,181,246,2),(11,1,86,150,2),(11,1,98,145,2),
            (11,1,45,188,2),(11,1,53,188,2),(11,1,52,166,2),(11,1,62,154,2),
            (11,1,70,154,2),(11,1,77,152,2),(11,1,110,145,2),(11,1,119,146,2),
            (11,1,120,159,2),(11,1,111,188,2),(11,1,125,195,2),(11,1,135,196,2),
            (11,1,144,187,2),(11,1,125,218,2),(11,1,197,245,2),(11,1,247,211,2),
            (11,1,231,186,2),(11,1,223,187,2),(11,1,213,186,2),(11,1,209,175,2),
            (11,1,183,128,2),
            -- Skeleton Archer (type 15) — ranged, scattered
            (15,1,61,169,2),(15,1,217,229,2),(15,1,141,110,2),(15,1,98,54,2),
            (15,1,114,110,2),(15,1,68,110,2),(15,1,76,111,2),(15,1,156,125,2),
            (15,1,193,148,2),(15,1,188,146,2),(15,1,206,152,2),(15,1,212,151,2),
            (15,1,247,176,2),(15,1,175,127,2),(15,1,175,118,2),(15,1,183,123,2),
            (15,1,68,125,2),(15,1,80,97,2),
            -- Cyclops (type 17) — mid-level areas
            (17,1,18,220,2),(17,1,230,173,2),(17,1,236,167,2),(17,1,241,173,2),
            (17,1,173,200,2),(17,1,165,171,2),(17,1,208,162,2),(17,1,220,161,2),
            (17,1,176,188,2),(17,1,168,150,2),(17,1,156,171,2),(17,1,176,161,2),
            (17,1,185,161,2),(17,1,181,171,2),(17,1,198,168,2),(17,1,190,172,2),
            (17,1,128,232,2),(17,1,143,232,2),(17,1,50,211,2),(17,1,220,172,2),
            (17,1,247,168,2),(17,1,52,222,2),(17,1,50,235,2),(17,1,38,234,2),
            (17,1,16,227,2),(17,1,25,220,2),(17,1,235,108,2),(17,1,228,105,2),
            (17,1,214,116,2),(17,1,13,235,2),(17,1,27,236,2),
            -- Hell Hound (type 5) — mid-deep areas
            (5,1,170,126,2),(5,1,163,114,2),(5,1,163,125,2),(5,1,145,118,2),
            (5,1,123,120,2),(5,1,138,86,2),(5,1,120,75,2),(5,1,81,58,2),
            (5,1,71,53,2),(5,1,69,88,2),(5,1,98,111,2),(5,1,87,114,2),
            (5,1,65,127,2),(5,1,70,76,2),
            -- Hell Spider (type 13) — ranged, deep areas
            (13,1,112,93,2),(13,1,104,65,2),(13,1,183,27,2),(13,1,114,20,2),
            (13,1,247,87,2),(13,1,149,94,2),(13,1,126,94,2),(13,1,168,96,2),
            (13,1,196,100,2),
            -- Thunder Lich (type 9) — ranged, northwest + scattered
            (9,1,28,18,2),(9,1,22,4,2),(9,1,18,29,2),(9,1,18,23,2),
            (9,1,34,4,2),(9,1,22,90,2),(9,1,33,78,2),(9,1,42,117,2),
            (9,1,118,11,2),(9,1,38,23,2),(9,1,160,10,2),(9,1,194,18,2),
            (9,1,213,12,2),(9,1,221,14,2),(9,1,243,16,2),(9,1,233,66,2),
            (9,1,30,105,2),(9,1,27,53,2),(9,1,10,68,2),(9,1,33,90,2),
            (9,1,12,121,2),
            -- Dark Knight (type 10) — elite, west corridors
            (10,1,14,107,2),(10,1,22,65,2),(10,1,39,77,2),(10,1,29,66,2),
            (10,1,41,88,2),(10,1,19,120,2),(10,1,37,98,2),
            -- Gorgon (type 18) — boss, west area
            (18,1,43,111,2),(18,1,8,123,2),(18,1,7,56,2),(18,1,26,67,2)
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, dungeonSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns (dungeon) error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Dungeon monster spawns (258 spawns, 12 types from "
           "OpenMU Version075 Dungeon.cs)\n");
  }
  } // end Dungeon else

  // Seed Dungeon traps (types 100-102) — separate check so existing DB gets traps
  sqlite3_prepare_v2(m_db,
      "SELECT COUNT(*) FROM monster_spawns WHERE map_id=1 AND type>=100 AND type<=102",
      -1, &stmt, nullptr);
  int trapCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    trapCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (trapCount > 0) {
    printf("[DB] Dungeon trap spawns already seeded (%d entries)\n", trapCount);
  } else {
  // 58 trap spawns from OpenMU Version075 Dungeon.cs (lines 560-618)
  // Type 100 = Lance Trap (27 spawns), Type 101 = Iron Stick Trap (26 spawns),
  // Type 102 = Fire Trap (5 spawns)
  // Direction: 2=SouthWest, 4=SouthEast, 6=NorthEast
  const char *trapSql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Iron Stick Trap (type 101) — pressure plate, 27 spawns
            (101,1,10,26,2),(101,1,11,26,2),(101,1,27,12,2),(101,1,24,5,2),
            (101,1,24,4,2),(101,1,27,11,2),(101,1,23,24,2),(101,1,27,21,2),
            (101,1,19,19,2),(101,1,22,24,2),(101,1,23,29,2),(101,1,23,28,2),
            (101,1,33,9,2),(101,1,35,9,2),(101,1,39,18,2),(101,1,39,17,2),
            (101,1,39,16,2),(101,1,48,193,2),(101,1,49,193,2),
            (101,1,90,164,2),(101,1,92,164,2),(101,1,91,164,2),
            (101,1,128,212,2),(101,1,130,213,2),(101,1,143,214,2),
            (101,1,155,230,6),
            -- Lance Trap (type 100) — 33 spawns
            (100,1,126,99,2),(100,1,123,99,2),(100,1,120,99,2),(100,1,117,99,2),
            (100,1,136,95,2),(100,1,139,95,2),(100,1,172,12,2),(100,1,166,12,2),
            (100,1,178,12,2),(100,1,177,103,2),(100,1,180,103,2),(100,1,183,103,2),
            (100,1,186,103,2),(100,1,189,103,2),(100,1,186,151,4),
            (100,1,196,33,2),(100,1,193,33,2),(100,1,202,93,2),(100,1,205,93,2),
            (100,1,198,130,2),(100,1,202,150,2),(100,1,232,46,4),(100,1,232,40,4),
            (100,1,232,37,4),(100,1,227,61,2),(100,1,229,93,2),(100,1,232,93,2),
            -- Fire Trap (type 102) — directional area attack, 8 spawns
            (102,1,45,224,4),(102,1,66,71,4),(102,1,80,61,2),
            (102,1,169,12,2),(102,1,175,12,2)
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, trapSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns (dungeon traps) error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Dungeon trap spawns (types 100-102 from OpenMU Version075)\n");
  }
  } // end trap else

  // Seed Devias (map_id=2) spawns if not already present
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=2",
                     -1, &stmt, nullptr);
  int deviasCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    deviasCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (deviasCount > 0) {
    printf("[DB] Devias spawns already seeded (%d entries)\n", deviasCount);
  } else {

  // Devias monster spawns — converted from OpenMU Version075 Devias.cs
  // OpenMU uses rectangular area spawns; we distribute points within each.
  // Safe zone: x=[169,255], y=[0,78] — all spawns MUST be outside this region.
  // Total OpenMU: 550 spawns across 6 types. We use ~400 representative points.
  const char *deviasMSql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Worm (type 24, Lv20): OpenMU rect x=128-251,y=0-128, qty 65 → 50 points
            -- Region A: x=128-168 (west strip, any y 0-128)
            (24,2,130,10,2),(24,2,135,25,4),(24,2,140,42,6),(24,2,145,58,1),
            (24,2,150,72,3),(24,2,155,88,5),(24,2,160,105,0),(24,2,165,118,7),
            (24,2,132,50,2),(24,2,148,95,4),(24,2,133,35,6),(24,2,138,68,1),
            (24,2,143,82,3),(24,2,152,100,5),(24,2,158,115,0),(24,2,163,20,7),
            (24,2,137,48,2),(24,2,142,75,4),(24,2,156,110,6),(24,2,161,8,1),
            -- Region B: x=169-245, y=79-128 (south of safe zone)
            (24,2,175,82,6),(24,2,185,90,1),(24,2,195,85,3),(24,2,205,95,5),
            (24,2,215,88,0),(24,2,225,100,7),(24,2,235,92,2),(24,2,240,110,4),
            (24,2,180,105,6),(24,2,200,118,1),(24,2,172,95,3),(24,2,190,108,5),
            (24,2,210,82,0),(24,2,220,115,7),(24,2,230,88,2),(24,2,245,100,4),
            (24,2,178,118,6),(24,2,198,82,1),(24,2,212,98,3),(24,2,228,108,5),
            (24,2,238,120,0),(24,2,248,85,7),(24,2,170,90,2),(24,2,188,112,4),
            (24,2,208,105,6),(24,2,218,92,1),(24,2,232,118,3),(24,2,242,82,5),
            (24,2,182,98,0),(24,2,202,88,7),
            -- Assassin (type 21, Lv26): OpenMU rect x=128-251,y=0-128, qty 35 → 30 points
            -- Region A: x=128-168
            (21,2,130,30,2),(21,2,138,60,4),(21,2,146,90,6),(21,2,155,120,1),
            (21,2,135,75,3),(21,2,162,45,5),(21,2,142,110,0),(21,2,133,18,7),
            (21,2,148,52,2),(21,2,158,85,4),(21,2,165,100,6),(21,2,131,42,1),
            -- Region B: x=169-245, y=79-128
            (21,2,178,95,7),(21,2,192,100,2),(21,2,208,88,4),(21,2,222,108,6),
            (21,2,238,95,1),(21,2,248,115,3),(21,2,185,118,5),(21,2,215,82,0),
            (21,2,175,88,7),(21,2,195,112,2),(21,2,205,82,4),(21,2,225,92,6),
            (21,2,235,105,1),(21,2,245,118,3),(21,2,182,82,5),(21,2,212,100,0),
            (21,2,228,115,7),(21,2,240,88,2),
            -- Ice Monster (type 22, Lv22): OpenMU rect x=0-128,y=0-128, qty 75 → 60 points
            (22,2,10,12,2),(22,2,25,28,4),(22,2,40,45,6),(22,2,55,58,1),
            (22,2,70,72,3),(22,2,85,88,5),(22,2,100,102,0),(22,2,115,118,7),
            (22,2,15,55,2),(22,2,30,78,4),(22,2,45,100,6),(22,2,60,120,1),
            (22,2,75,15,3),(22,2,90,42,5),(22,2,105,68,0),(22,2,120,95,7),
            (22,2,20,92,2),(22,2,38,112,4),(22,2,52,22,6),(22,2,68,48,1),
            (22,2,82,75,3),(22,2,98,110,5),(22,2,112,35,0),(22,2,125,62,7),
            (22,2,8,85,2),(22,2,18,38,4),(22,2,33,65,6),(22,2,48,82,1),
            (22,2,63,105,3),(22,2,78,8,5),(22,2,93,32,0),(22,2,108,55,7),
            (22,2,123,78,2),(22,2,5,108,4),(22,2,22,18,6),(22,2,42,42,1),
            (22,2,58,68,3),(22,2,72,95,5),(22,2,88,115,0),(22,2,102,25,7),
            (22,2,118,48,2),(22,2,12,72,4),(22,2,28,98,6),(22,2,35,8,1),
            (22,2,50,35,3),(22,2,65,58,5),(22,2,80,82,0),(22,2,95,108,7),
            (22,2,110,15,2),(22,2,125,42,4),(22,2,14,118,6),(22,2,32,52,1),
            (22,2,46,75,3),(22,2,62,28,5),(22,2,76,48,0),(22,2,92,68,7),
            (22,2,106,92,2),(22,2,122,112,4),(22,2,8,30,6),(22,2,24,62,1),
            -- Hommerd (type 23, Lv24): OpenMU rect x=0-128,y=0-128, qty 75 → 60 points
            (23,2,5,18,2),(23,2,20,35,4),(23,2,35,52,6),(23,2,50,68,1),
            (23,2,65,82,3),(23,2,80,98,5),(23,2,95,112,0),(23,2,110,125,7),
            (23,2,12,62,2),(23,2,28,85,4),(23,2,42,108,6),(23,2,58,10,1),
            (23,2,72,38,3),(23,2,88,55,5),(23,2,102,78,0),(23,2,118,100,7),
            (23,2,18,95,2),(23,2,32,115,4),(23,2,48,28,6),(23,2,62,52,1),
            (23,2,78,72,3),(23,2,92,105,5),(23,2,108,42,0),(23,2,122,18,7),
            (23,2,6,48,2),(23,2,22,75,4),(23,2,38,98,6),(23,2,54,15,1),
            (23,2,68,42,3),(23,2,84,65,5),(23,2,98,88,0),(23,2,114,112,7),
            (23,2,10,105,2),(23,2,26,22,4),(23,2,44,48,6),(23,2,56,72,1),
            (23,2,74,95,3),(23,2,86,118,5),(23,2,104,8,0),(23,2,116,35,7),
            (23,2,16,58,2),(23,2,30,82,4),(23,2,46,108,6),(23,2,60,25,1),
            (23,2,76,55,3),(23,2,90,78,5),(23,2,106,102,0),(23,2,120,125,7),
            (23,2,8,32,2),(23,2,24,58,4),(23,2,40,85,6),(23,2,52,112,1),
            (23,2,66,12,3),(23,2,82,38,5),(23,2,96,62,0),(23,2,112,88,7),
            (23,2,126,115,2),(23,2,14,42,4),(23,2,34,68,6),(23,2,70,22,1),
            -- Elite Yeti (type 20, Lv36): OpenMU total 235 → 130 points
            -- Point spawns near (194,165) and (36,25) from OpenMU
            (20,2,194,165,2),(20,2,190,162,4),(20,2,198,168,6),(20,2,192,170,1),
            (20,2,196,160,3),(20,2,36,25,1),(20,2,32,22,3),(20,2,40,28,5),
            (20,2,34,30,0),(20,2,38,20,7),
            -- Rect x=210-242,y=210-220 → 10 points
            (20,2,212,210,0),(20,2,216,213,7),(20,2,220,216,2),(20,2,224,219,4),
            (20,2,228,212,6),(20,2,232,215,1),(20,2,236,218,3),(20,2,240,211,5),
            (20,2,214,217,0),(20,2,238,214,7),
            -- Rect x=0-251,y=128-245 (200 spawns) → 110 points spread across south
            (20,2,10,132,1),(20,2,25,142,3),(20,2,42,155,5),(20,2,58,168,0),
            (20,2,72,178,7),(20,2,88,188,2),(20,2,102,198,4),(20,2,118,210,6),
            (20,2,132,222,1),(20,2,148,235,3),(20,2,162,240,5),(20,2,178,138,0),
            (20,2,192,148,7),(20,2,208,158,2),(20,2,225,168,4),(20,2,240,178,6),
            (20,2,15,188,1),(20,2,32,200,3),(20,2,48,215,5),(20,2,65,228,0),
            (20,2,80,238,7),(20,2,95,135,2),(20,2,112,145,4),(20,2,128,158,6),
            (20,2,145,170,1),(20,2,160,182,3),(20,2,175,195,5),(20,2,190,208,0),
            (20,2,205,220,7),(20,2,220,232,2),(20,2,238,242,4),(20,2,22,150,6),
            (20,2,55,180,1),(20,2,85,210,3),(20,2,115,240,5),(20,2,140,130,0),
            (20,2,170,160,7),(20,2,200,190,2),(20,2,230,225,4),
            (20,2,8,145,1),(20,2,18,158,3),(20,2,35,172,5),(20,2,50,185,0),
            (20,2,68,195,7),(20,2,82,205,2),(20,2,98,218,4),(20,2,108,230,6),
            (20,2,125,242,1),(20,2,138,135,3),(20,2,155,148,5),(20,2,168,162,0),
            (20,2,185,175,7),(20,2,195,188,2),(20,2,215,200,4),(20,2,232,215,6),
            (20,2,245,230,1),(20,2,12,165,3),(20,2,28,180,5),(20,2,45,192,0),
            (20,2,62,205,7),(20,2,78,220,2),(20,2,92,232,4),(20,2,110,245,6),
            (20,2,122,132,1),(20,2,142,148,3),(20,2,158,165,5),(20,2,172,178,0),
            (20,2,188,192,7),(20,2,202,205,2),(20,2,218,218,4),(20,2,235,235,6),
            (20,2,248,245,1),(20,2,5,138,3),(20,2,20,155,5),(20,2,38,170,0),
            (20,2,52,182,7),(20,2,70,195,2),(20,2,86,208,4),(20,2,100,222,6),
            (20,2,116,235,1),(20,2,130,245,3),(20,2,150,132,5),(20,2,165,145,0),
            (20,2,182,158,7),(20,2,198,172,2),(20,2,210,185,4),(20,2,228,198,6),
            (20,2,242,210,1),(20,2,30,225,3),(20,2,60,240,5),(20,2,90,130,0),
            (20,2,120,150,7),(20,2,152,175,2),(20,2,180,200,4),(20,2,212,228,6),
            (20,2,240,240,1),(20,2,16,172,3),(20,2,44,200,5),(20,2,74,230,0),
            (20,2,105,138,7),(20,2,135,165,2),(20,2,164,192,4),(20,2,196,222,6),
            (20,2,222,245,1),(20,2,250,132,3),(20,2,40,240,5),(20,2,126,180,0),
            -- Ice Queen (type 25, Lv52): OpenMU rect x=0-128,y=128-245, qty 75 → 60 points
            (25,2,10,135,2),(25,2,25,152,4),(25,2,40,168,6),(25,2,55,182,1),
            (25,2,70,198,3),(25,2,85,212,5),(25,2,100,228,0),(25,2,115,242,7),
            (25,2,15,162,2),(25,2,30,178,4),(25,2,45,195,6),(25,2,60,208,1),
            (25,2,75,225,3),(25,2,90,238,5),(25,2,105,148,0),(25,2,120,168,7),
            (25,2,20,188,2),(25,2,35,205,4),(25,2,50,218,6),(25,2,65,235,1),
            (25,2,8,142,2),(25,2,22,158,4),(25,2,38,175,6),(25,2,52,192,1),
            (25,2,68,205,3),(25,2,82,222,5),(25,2,98,235,0),(25,2,112,145,7),
            (25,2,5,172,2),(25,2,18,188,4),(25,2,32,202,6),(25,2,48,215,1),
            (25,2,62,232,3),(25,2,78,242,5),(25,2,92,138,0),(25,2,108,158,7),
            (25,2,122,178,2),(25,2,12,195,4),(25,2,28,210,6),(25,2,42,228,1),
            (25,2,58,240,3),(25,2,72,132,5),(25,2,88,152,0),(25,2,102,172,7),
            (25,2,118,192,2),(25,2,6,210,4),(25,2,24,225,6),(25,2,36,238,1),
            (25,2,54,135,3),(25,2,66,155,5),(25,2,80,175,0),(25,2,96,195,7),
            (25,2,110,215,2),(25,2,126,232,4),(25,2,14,148,6),(25,2,46,162,1),
            (25,2,76,185,3),(25,2,104,208,5),(25,2,16,228,0),(25,2,84,245,7),
            (25,2,56,142,2),(25,2,114,165,4)
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, deviasMSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns (Devias) error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Devias monster spawns (155 spawns, 6 types from OpenMU "
           "Version075 Devias.cs)\n");
  }
  } // end Devias else

  // Seed Noria (map_id=3) spawns if not already present
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=3",
                     -1, &stmt, nullptr);
  int noriaCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    noriaCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (noriaCount > 0) {
    printf("[DB] Noria monster spawns already seeded (%d entries)\n", noriaCount);
  } else {

  // Noria monster spawns from OpenMU Version075 Noria.cs
  // Spawn zones: Goblin/ChainScorpion NE quadrant, Beetle/Hunter NW,
  //              ForestMonster/Agon south half, StoneGolem SE, EliteGoblin SW
  const char *noriaMSql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Goblin (type 26, Lv3): NE quadrant x=128-251, y=0-128
            (26,3,135,15,2),(26,3,145,25,4),(26,3,158,38,6),(26,3,170,12,1),
            (26,3,182,48,3),(26,3,195,22,5),(26,3,208,55,0),(26,3,220,35,7),
            (26,3,232,18,2),(26,3,245,42,4),(26,3,140,65,6),(26,3,155,78,1),
            (26,3,168,92,3),(26,3,180,108,5),(26,3,192,72,0),(26,3,205,85,7),
            (26,3,218,98,2),(26,3,230,62,4),(26,3,242,115,6),(26,3,148,50,1),
            -- Chain Scorpion (type 27, Lv5): NE quadrant x=128-251, y=0-128
            (27,3,132,20,2),(27,3,148,35,4),(27,3,162,52,6),(27,3,175,18,1),
            (27,3,188,68,3),(27,3,200,42,5),(27,3,215,28,0),(27,3,228,58,7),
            (27,3,240,75,2),(27,3,138,88,4),(27,3,152,102,6),(27,3,165,115,1),
            (27,3,178,82,3),(27,3,190,95,5),(27,3,202,110,0),(27,3,222,48,7),
            (27,3,235,32,2),(27,3,248,62,4),
            -- Beetle Monster (type 28, Lv10): NW quadrant x=0-128, y=0-128
            (28,3,12,15,2),(28,3,25,32,4),(28,3,38,48,6),(28,3,52,65,1),
            (28,3,68,82,3),(28,3,82,98,5),(28,3,95,112,0),(28,3,108,22,7),
            (28,3,18,42,2),(28,3,35,58,4),(28,3,48,75,6),(28,3,62,92,1),
            (28,3,75,108,3),(28,3,88,18,5),(28,3,102,35,0),(28,3,118,52,7),
            (28,3,22,68,2),(28,3,42,85,4),
            -- Hunter (type 29, Lv13, ranged): NW quadrant x=0-128, y=0-128
            (29,3,15,25,2),(29,3,28,42,4),(29,3,42,58,6),(29,3,55,75,1),
            (29,3,72,92,3),(29,3,85,108,5),(29,3,98,18,0),(29,3,112,35,7),
            (29,3,20,52,2),(29,3,32,68,4),(29,3,45,85,6),(29,3,58,102,1),
            (29,3,78,118,3),(29,3,92,28,5),(29,3,105,45,0),(29,3,122,62,7),
            (29,3,8,78,2),(29,3,48,115,4),
            -- Forest Monster (type 30, Lv15): South half x=0-251, y=128-245
            (30,3,15,135,2),(30,3,35,152,4),(30,3,55,168,6),(30,3,75,185,1),
            (30,3,95,202,3),(30,3,115,218,5),(30,3,135,235,0),(30,3,155,142,7),
            (30,3,175,158,2),(30,3,195,175,4),(30,3,215,192,6),(30,3,235,208,1),
            (30,3,25,225,3),(30,3,45,240,5),(30,3,65,132,0),(30,3,85,148,7),
            (30,3,105,165,2),(30,3,125,182,4),
            -- Agon (type 31, Lv16): South half x=0-251, y=128-245
            (31,3,20,138,2),(31,3,42,155,4),(31,3,62,172,6),(31,3,82,188,1),
            (31,3,102,205,3),(31,3,122,222,5),(31,3,142,238,0),(31,3,162,145,7),
            (31,3,182,162,2),(31,3,202,178,4),(31,3,222,195,6),(31,3,242,212,1),
            (31,3,30,228,3),(31,3,52,242,5),(31,3,72,135,0),(31,3,92,152,7),
            (31,3,112,168,2),(31,3,132,185,4),
            -- Stone Golem (type 32, Lv18): SE quadrant x=128-251, y=128-245
            (32,3,135,135,2),(32,3,152,152,4),(32,3,168,168,6),(32,3,185,185,1),
            (32,3,202,202,3),(32,3,218,218,5),(32,3,235,235,0),(32,3,145,145,7),
            (32,3,162,162,2),(32,3,178,178,4),(32,3,195,195,6),(32,3,212,212,1),
            (32,3,228,228,3),(32,3,245,240,5),(32,3,138,158,0),
            -- Elite Goblin (type 33, Lv8): SW quadrant x=0-128, y=128-245
            (33,3,12,135,2),(33,3,25,152,4),(33,3,38,168,6),(33,3,52,185,1),
            (33,3,68,202,3),(33,3,82,218,5),(33,3,95,235,0),(33,3,108,142,7),
            (33,3,18,158,2),(33,3,32,175,4),(33,3,45,192,6),(33,3,58,208,1),
            (33,3,72,225,3),(33,3,85,240,5),(33,3,98,148,0),(33,3,115,165,7),
            (33,3,22,182,2),(33,3,42,198,4)
    )";
  err = nullptr;
  if (sqlite3_exec(m_db, noriaMSql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns (Noria) error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Noria monster spawns (8 types from OpenMU Version075 "
           "Noria.cs)\n");
  }
  } // end Noria else
}

std::vector<MonsterSpawnData> Database::GetMonsterSpawns(uint8_t mapId) {
  std::vector<MonsterSpawnData> monsters;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction "
                    "FROM monster_spawns WHERE map_id=? ORDER BY id";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return monsters;

  sqlite3_bind_int(stmt, 1, mapId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MonsterSpawnData m;
    m.id = sqlite3_column_int(stmt, 0);
    m.type = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
    m.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    m.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    m.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
    m.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 5));
    monsters.push_back(m);
  }
  sqlite3_finalize(stmt);
  return monsters;
}

void Database::SeedDefaultEquipment(int characterId) {
  char sql[256];
  // Reset character stats to Level 1 defaults
  snprintf(sql, sizeof(sql),
           "UPDATE characters SET level=1, strength=28, dexterity=20, "
           "vitality=25, energy=10, life=110, max_life=110, mana=20, "
           "max_mana=20, experience=0, level_up_points=0 "
           "WHERE id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  // Clear equipment and inventory
  snprintf(sql, sizeof(sql),
           "DELETE FROM character_equipment WHERE character_id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  snprintf(sql, sizeof(sql),
           "DELETE FROM character_inventory WHERE character_id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  printf("[DB] Reset character %d to Level 1 DK defaults (EMPTY)\n",
         characterId);
}

void Database::UpdateEquipment(int characterId, uint8_t slot, uint8_t category,
                               uint8_t itemIndex, uint8_t itemLevel,
                               uint8_t quantity) {
  sqlite3_stmt *stmt = nullptr;
  // UPSERT: insert or replace on (character_id, slot) unique constraint
  const char *sql =
      "INSERT INTO character_equipment (character_id, slot, item_category, "
      "item_index, item_level, quantity) "
      "VALUES (?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(character_id, slot) DO UPDATE SET "
      "item_category=excluded.item_category, item_index=excluded.item_index, "
      "item_level=excluded.item_level, quantity=excluded.quantity";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, slot);
  sqlite3_bind_int(stmt, 3, category);
  sqlite3_bind_int(stmt, 4, itemIndex);
  sqlite3_bind_int(stmt, 5, itemLevel);
  sqlite3_bind_int(stmt, 6, quantity);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  printf("[DB] Equipment updated: char=%d slot=%d cat=%d idx=%d +%d qty=%d\n",
         characterId, slot, category, itemIndex, itemLevel, quantity);
}

std::vector<Database::InventorySlotData>
Database::GetCharacterInventory(int characterId) {
  std::vector<InventorySlotData> items;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT slot, def_index, quantity, item_level "
      "FROM character_inventory WHERE character_id=? ORDER BY slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return items;
  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    InventorySlotData d;
    d.slot = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
    d.defIndex = static_cast<int16_t>(sqlite3_column_int(stmt, 1));
    d.quantity = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    d.itemLevel = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    items.push_back(d);
  }
  sqlite3_finalize(stmt);
  return items;
}

void Database::SaveCharacterInventory(int characterId, int16_t defIndex,
                                      uint8_t quantity, uint8_t itemLevel,
                                      uint8_t slot) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO character_inventory (character_id, slot, "
                    "def_index, quantity, item_level) "
                    "VALUES (?, ?, ?, ?, ?) "
                    "ON CONFLICT(character_id, slot) DO UPDATE SET "
                    "def_index=excluded.def_index, quantity=excluded.quantity, "
                    "item_level=excluded.item_level";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, slot);
  sqlite3_bind_int(stmt, 3, defIndex);
  sqlite3_bind_int(stmt, 4, quantity);
  sqlite3_bind_int(stmt, 5, itemLevel);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::DeleteCharacterInventoryItem(int characterId, uint8_t slot) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "DELETE FROM character_inventory WHERE character_id=? AND slot=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, slot);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::DeleteCharacterInventoryAll(int characterId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM character_inventory WHERE character_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::UpdateCharacterMoney(int characterId, uint32_t money) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET money=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, static_cast<int>(money));
  sqlite3_bind_int(stmt, 2, characterId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

uint64_t Database::GetXPForLevel(int level) {
  // Cubic curve, MaxLevel=400 (matches client logic)
  static constexpr double kScale =
      ((double)0xFFFFFFFF * 0.95) / (400.0 * 400.0 * 400.0);
  return (uint64_t)(kScale * (double)level * (double)level * (double)level);
}

std::vector<uint8_t> Database::GetCharacterSkills(int characterId) {
  std::vector<uint8_t> skills;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT skill_id FROM character_skills WHERE character_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return skills;
  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    skills.push_back(static_cast<uint8_t>(sqlite3_column_int(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  return skills;
}

void Database::LearnSkill(int characterId, uint8_t skillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT OR IGNORE INTO character_skills (character_id, "
                    "skill_id) VALUES (?, ?)";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, skillId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  printf("[DB] Character %d learned skill %d\n", characterId, skillId);
}

bool Database::HasSkill(int characterId, uint8_t skillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT 1 FROM character_skills WHERE character_id=? AND "
                    "skill_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, skillId);
  bool found = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return found;
}

void Database::SetRmcSkillId(int characterId, int8_t skillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE characters SET rmc_skill_id=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, skillId);
  sqlite3_bind_int(stmt, 2, characterId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

// ─── Chat Log Persistence ────────────────────────────────────────────────────

void Database::SaveChatMessage(int characterId, uint8_t category,
                               uint32_t color, const std::string &message) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO chat_log (character_id, category, color, message) "
      "VALUES (?, ?, ?, ?)";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, category);
  sqlite3_bind_int64(stmt, 3, (int64_t)color);
  sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  // Trim: keep only last 500 messages per character
  sqlite3_stmt *trimStmt = nullptr;
  const char *trimSql =
      "DELETE FROM chat_log WHERE character_id=? AND id NOT IN "
      "(SELECT id FROM chat_log WHERE character_id=? ORDER BY id DESC LIMIT 500)";
  if (sqlite3_prepare_v2(m_db, trimSql, -1, &trimStmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(trimStmt, 1, characterId);
    sqlite3_bind_int(trimStmt, 2, characterId);
    sqlite3_step(trimStmt);
    sqlite3_finalize(trimStmt);
  }
}

std::vector<Database::ChatLogEntry>
Database::GetChatHistory(int characterId, int limit) {
  std::vector<ChatLogEntry> entries;
  sqlite3_stmt *stmt = nullptr;
  // Get most recent N messages, ordered oldest-first for display
  const char *sql =
      "SELECT category, color, message FROM "
      "(SELECT category, color, message FROM chat_log "
      "WHERE character_id=? ORDER BY id DESC LIMIT ?) "
      "ORDER BY rowid ASC";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return entries;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ChatLogEntry e;
    e.category = (uint8_t)sqlite3_column_int(stmt, 0);
    e.color = (uint32_t)sqlite3_column_int64(stmt, 1);
    const char *msg = (const char *)sqlite3_column_text(stmt, 2);
    e.message = msg ? msg : "";
    entries.push_back(std::move(e));
  }
  sqlite3_finalize(stmt);
  return entries;
}

std::vector<Database::QuestProgress> Database::LoadAllQuestProgress(int characterId) {
  std::vector<QuestProgress> result;
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(m_db,
          "SELECT quest_id, kill_count_0, kill_count_1, kill_count_2, completed "
          "FROM character_quest_progress WHERE character_id=?",
          -1, &stmt, nullptr) != SQLITE_OK)
    return result;
  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    QuestProgress qp;
    qp.questId = sqlite3_column_int(stmt, 0);
    qp.kc[0] = sqlite3_column_int(stmt, 1);
    qp.kc[1] = sqlite3_column_int(stmt, 2);
    qp.kc[2] = sqlite3_column_int(stmt, 3);
    qp.completed = sqlite3_column_int(stmt, 4) != 0;
    result.push_back(qp);
  }
  sqlite3_finalize(stmt);
  return result;
}

void Database::SaveQuestProgress(int characterId, int questId,
                                 int kc0, int kc1, int kc2, bool completed) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(m_db,
          "INSERT INTO character_quest_progress "
          "(character_id, quest_id, kill_count_0, kill_count_1, kill_count_2, completed) "
          "VALUES (?,?,?,?,?,?) "
          "ON CONFLICT(character_id, quest_id) DO UPDATE SET "
          "kill_count_0=excluded.kill_count_0, "
          "kill_count_1=excluded.kill_count_1, "
          "kill_count_2=excluded.kill_count_2, "
          "completed=excluded.completed",
          -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, questId);
  sqlite3_bind_int(stmt, 3, kc0);
  sqlite3_bind_int(stmt, 4, kc1);
  sqlite3_bind_int(stmt, 5, kc2);
  sqlite3_bind_int(stmt, 6, completed ? 1 : 0);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::DeleteQuestProgress(int characterId, int questId) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(m_db,
          "DELETE FROM character_quest_progress WHERE character_id=? AND quest_id=?",
          -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, questId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

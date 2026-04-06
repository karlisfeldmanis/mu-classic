---
name: inspect-db
description: Query the MU Online game database (mu_server.db) for item definitions, NPC spawns, character data, or schema info.
allowed-tools: Bash
argument-hint: [table-name or query]
---

Inspect the game database: $ARGUMENTS

## Database Location

`/Users/karlisfeldmanis/Desktop/mu_remaster/server/build/mu_server.db`

## Schema (Tables)

| Table | Content |
|-------|---------|
| characters | Player characters (name, class, level, stats, position) |
| character_inventory | Inventory items per character |
| character_equipment | Equipped items per character |
| character_skills | Learned skills per character |
| item_definitions | All 293 item definitions (category, index, name, stats) |
| npc_spawns | NPC and monster spawn positions |

## Common Queries

```bash
# Schema overview
sqlite3 mu_server.db ".schema"

# Item lookup by name
sqlite3 mu_server.db "SELECT * FROM item_definitions WHERE name LIKE '%sword%'"

# Item lookup by defIndex (category * 32 + itemIndex)
sqlite3 mu_server.db "SELECT * FROM item_definitions WHERE defIndex = 32"

# NPC spawns for a map
sqlite3 mu_server.db "SELECT * FROM npc_spawns WHERE mapId = 0"

# Character info
sqlite3 mu_server.db "SELECT * FROM characters"

# Skill list for a character
sqlite3 mu_server.db "SELECT * FROM character_skills WHERE characterId = 1"
```

## Notes
- defIndex = category * 32 + itemIndex (unique key across all systems)
- Classes: DW=0, DK=16, ELF=32, MG=48
- Delete mu_server.db to reset/re-seed all data

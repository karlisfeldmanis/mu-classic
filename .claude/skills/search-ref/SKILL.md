---
name: search-ref
description: Search Main 5.2 and OpenMU reference source code for functions, patterns, enums, or data layouts. Use when investigating how the original game implements something.
allowed-tools: Grep, Read, Glob
argument-hint: [search-term]
---

Search the original MU Online reference sources for: $ARGUMENTS

## Reference Locations

- **Main 5.2 source**: `references/other/Main5.2/Source Main 5.2/source/`
- **OpenMU Version075**: `references/OpenMU/`
- **Other references**: `references/other/MuClientTools16/`

## Key Files to Search

| File | Content |
|------|---------|
| ZzzObject.cpp | World objects, item angles, item attributes |
| ZzzCharacter.cpp | Character rendering, weapon config, mount rendering |
| ZzzOpenData.cpp | PlaySpeed tables, animation data |
| ZzzBMD.cpp | Model rendering, shadows, RENDER_WAVE |
| ZzzLodTerrain.cpp | Terrain rendering, lightmap, dynamic lights |
| ZzzMathLib.cpp | AngleMatrix, rotation math |
| GMItem.cpp | Item creation, drop logic |
| _enum.h | Action indices, model IDs |
| _define.h | Constants, terrain flags |

## Instructions

1. Use Grep to search across all reference directories for the search term
2. Read relevant matches with surrounding context
3. Explain how the original code handles the pattern
4. Note any constants, formulas, or magic numbers
5. Always provide exact file paths and line numbers

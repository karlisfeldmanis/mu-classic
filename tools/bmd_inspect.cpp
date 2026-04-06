// BMD Inspector — Universal reverse-engineering tool for MU Online BMD model files
// Build: cd client/build && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja BmdInspect
//
// Advanced features:
// - Model metadata (name, version, encryption, internal name)
// - Mesh data (vertices, normals, triangles, textures, BlendMesh analysis)
// - Bone hierarchy (names, parents, depth, weapon/VFX attachment markers)
// - Action analysis (keys, speeds, root motion distance, impact frame detection)
// - Bounding box / model dimensions / scale estimation
// - Texture file existence verification (--texcheck)
// - Sound file existence verification (--sndcheck)
// - VFX attachment point suggestions from bone structure
// - BlendMesh rendering recommendations
// - Per-action bone displacement analysis (find "hit" frames)
// - Main 5.2 VFX/rendering cross-reference
// - Main 5.2 sound assignments cross-reference
// - Weapon bone (LinkBone) assignments
// - Server type mapping

#include "BMDParser.hpp"
#include "BMDStructs.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════
// Main 5.2 Cross-Reference Database
// ═══════════════════════════════════════════════════════════════════════

struct MonsterSoundInfo {
    int modelIndex;
    const char *name;
    const char *idle1;
    const char *idle2;
    const char *attack1;
    const char *attack2;
    const char *death;
};

static const MonsterSoundInfo s_monsterSounds[] = {
    {0,  "Bull Fighter",    "mBull1.wav",      "mBull2.wav",      "mBullAttack1.wav",      "mBullAttack2.wav",      "mBullDie.wav"},
    {1,  "Hound",           "mHound1.wav",     "mHound2.wav",     "mHoundAttack1.wav",     "mHoundAttack2.wav",     "mHoundDie.wav"},
    {2,  "Budge Dragon",    "mBudgeDragon1.wav","mBudgeDragon2.wav","mBudgeDragonAttack1.wav","",                   "mBudgeDragonDie.wav"},
    {3,  "Spider",          "mSpider1.wav",    "mSpider2.wav",    "mSpiderAttack1.wav",    "mSpiderAttack2.wav",    "mSpiderDie.wav"},
    {4,  "Lich",            "mLich1.wav",      "mLich2.wav",      "mLichAttack1.wav",      "mLichAttack2.wav",      "mLichDie.wav"},
    {5,  "Giant",           "mGiant1.wav",     "mGiant2.wav",     "mGiantAttack1.wav",     "mGiantAttack2.wav",     "mGiantDie.wav"},
    {6,  "Skeleton",        "mSkeleton1.wav",  "mSkeleton2.wav",  "mSkeletonAttack1.wav",  "mSkeletonAttack2.wav",  "mSkeletonDie.wav"},
    {7,  "Larva",           "mLarva1.wav",     "mLarva2.wav",     "mLarvaAttack1.wav",     "mLarvaAttack2.wav",     "mLarvaDie.wav"},
    {8,  "Hellhound",       "mHound1.wav",     "mHound2.wav",     "mHoundAttack1.wav",     "mHoundAttack2.wav",     "mHoundDie.wav"},
    {9,  "Yeti",            "mYeti1.wav",      "mYeti2.wav",      "mYetiAttack1.wav",      "mYetiAttack2.wav",      "mYetiDie.wav"},
    {10, "Goblin (Elite)",  "mGoblin1.wav",    "mGoblin2.wav",    "mGoblinAttack1.wav",    "mGoblinAttack2.wav",    "mGoblinDie.wav"},
    {11, "Gorgon",          "mGorgon1.wav",    "mGorgon2.wav",    "mGorgonAttack1.wav",    "mGorgonAttack2.wav",    "mGorgonDie.wav"},
    {12, "Ice Monster",     "mIceMonster1.wav","mIceMonster2.wav","mIceMonsterAttack1.wav","mIceMonsterAttack2.wav","mIceMonsterDie.wav"},
    {13, "Goblin",          "mGoblin1.wav",    "mGoblin2.wav",    "mGoblinAttack1.wav",    "mGoblinAttack2.wav",    "mGoblinDie.wav"},
    {14, "Assassin",        "mAssassin1.wav",  "mAssassin2.wav",  "mAssassinAttack1.wav",  "mAssassinAttack2.wav",  "mAssassinDie.wav"},
    {15, "Ice Queen",       "mIceQueen1.wav",  "mIceQueen2.wav",  "mIceQueenAttack1.wav",  "mIceQueenAttack2.wav",  "mIceQueenDie.wav"},
    {16, "Goblin Guard",    "mGoblin1.wav",    "mGoblin2.wav",    "mGoblinAttack1.wav",    "mGoblinAttack2.wav",    "mGoblinDie.wav"},
    {17, "Alquamos",        "mGoblin1.wav",    "mGoblin2.wav",    "mGoblinAttack1.wav",    "mGoblinAttack2.wav",    "mGoblinDie.wav"},
    {18, "Queen Bee",       "mQueenBee1.wav",  "mQueenBee2.wav",  "mQueenBeeAttack1.wav",  "mQueenBeeAttack2.wav",  "mQueenBeeDie.wav"},
    {26, "Devil/Hydra",     "mHydra1.wav",     "mHydra2.wav",     "mSatanAttack1.wav",     "",                      "mYetiDie.wav"},
    {27, "Balrog",          "mBalrog1.wav",    "mBalrog2.wav",    "mBalrogAttack1.wav",    "mBalrogAttack2.wav",    "mBalrogDie.wav"},
    {28, "Shadow",          "mShadow1.wav",    "mShadow2.wav",    "mShadowAttack1.wav",    "mShadowAttack2.wav",    "mShadowDie.wav"},
    {29, "Death Knight",    "mDarkKnight1.wav","mDarkKnight2.wav","mDarkKnightAttack1.wav","mDarkKnightAttack2.wav","mDarkKnightDie.wav"},
    {30, "Death Cow",       "mBull1.wav",      "mBull2.wav",      "mBullAttack1.wav",      "mBullAttack2.wav",      "mBullDie.wav"},
    {31, "Valkyrie",        "mValkyrie1.wav",  "mValkyrie2.wav",  "mValkyrieAttack1.wav",  "",                      "mValkyrieDie.wav"},
    {32, "Lizard King",     "mLizardKing1.wav","mLizardKing2.wav","mLizardKingAttack1.wav","mLizardKingAttack2.wav","mLizardKingDie.wav"},
    {33, "Vepar",           "mHydra1.wav",     "mHydra2.wav",     "mHydraAttack1.wav",     "",                      "mHydraDie.wav"},
    {34, "Hydra",           "mHydra1.wav",     "mHydra2.wav",     "mHydraAttack1.wav",     "",                      "mHydraDie.wav"},
    {35, "Sea Worm",        "mHydra1.wav",     "mHydra2.wav",     "mHydraAttack1.wav",     "",                      "mHydraDie.wav"},
    {41, "Iron Wheel",      "",                "",                "",                      "",                      ""},
    {42, "Tantalos",        "mGiant1.wav",     "mGiant2.wav",     "mGiantAttack1.wav",     "mGiantAttack2.wav",     "mGiantDie.wav"},
    {43, "Zaikan",          "mGiant1.wav",     "mGiant2.wav",     "mGiantAttack1.wav",     "mGiantAttack2.wav",     "mGiantDie.wav"},
    {44, "Blade Hunter",    "mDarkKnight1.wav","mDarkKnight2.wav","mDarkKnightAttack1.wav","mDarkKnightAttack2.wav","mDarkKnightDie.wav"},
    {45, "Death Tree",      "mSpider1.wav",    "mSpider2.wav",    "mSpiderAttack1.wav",    "",                      "mSpiderDie.wav"},
};

struct MonsterVFXInfo {
    int modelIndex;
    const char *name;
    const char *description;
    const char *pointLight; // rgb,range or empty
    const char *blendMesh;  // BlendMesh value or empty
    const char *vfxBones;   // comma-separated bone indices for VFX
};

static const MonsterVFXInfo s_monsterVFX[] = {
    {2,  "Budge Dragon", "Hover +50. Eye glow particles.", "", "", ""},
    {5,  "Giant",        "No blood particles on hit.", "", "", ""},
    {7,  "Larva",        "RENDER_WAVE=true (wave mesh distortion). No blood.", "", "", ""},
    {11, "Gorgon",       "BlendMesh=1 (additive glow). Eye sparkle. Boss-tier.", "", "1", "30,39"},
    {15, "Ice Queen",    "Ice particles. Chrome mesh pass.", "0.5,0.7,1.0,400", "", ""},
    {18, "Queen Bee",    "Wing flutter. Flying creature (hover).", "", "", ""},
    {26, "Devil/Hydra",  "Lightning at bone 63(+20). Pulsing light. BlendMeshLight anim.", "0.4,0.4,1.0,500", "", "63"},
    {27, "Balrog",       "Fire/flame aura. Strong point light. BlendMeshLight.", "1.0,0.5,0.2,600", "", ""},
    {28, "Shadow",       "Eye glow bones 42,43. Spark effects bones 26,31,36,41.", "", "", "42,43,26,31,36,41"},
    {29, "Death Knight", "Weapon glow. Dark aura.", "", "", ""},
    {30, "Death Cow",    "Bulky melee. Ground shake on attack.", "", "", ""},
    {34, "Hydra",        "Multi-head. Lightning at multiple bones. Large hitbox.", "", "", ""},
    {41, "Iron Wheel",   "Spinning trap. HiddenMesh=-2. Chrome pass.", "", "-2", ""},
    {51, "Egg (wave)",   "RENDER_WAVE=true (exclusive wave mesh distortion).", "", "", ""},
};

struct MonsterWeaponInfo {
    int modelIndex;
    const char *name;
    int linkBone0;
    int linkBone1;
    const char *weapon0;
    const char *weapon1;
};

static const MonsterWeaponInfo s_monsterWeapons[] = {
    {0,  "Bull Fighter",    38, 28, "Sword/Axe (right)", "Shield (left)"},
    {6,  "Skeleton",        38, 28, "Sword (right)", "Shield (left)"},
    {7,  "Larva",           -1, -1, "No weapons", ""},
    {9,  "Yeti",            38, 28, "Mace (right)", "Shield (left)"},
    {10, "Goblin (Elite)",  38, 28, "Mace (right)", "Shield (left)"},
    {11, "Gorgon",          30, 39, "Dual axes", ""},
    {14, "Assassin",        38, 28, "Knife (right)", "Knife (left)"},
    {27, "Balrog",          17, 28, "Spear (right)", "Shield (left)"},
    {28, "Shadow",          -1, -1, "No weapons (claws)", ""},
    {29, "Death Knight",    30, 39, "Sword (right)", "Polearm (left)"},
    {30, "Death Cow",       42, 33, "Mace (right)", "Mace (left)"},
    {31, "Valkyrie",        38, 28, "Spear (right)", "Shield (left)"},
    {32, "Lizard King",     38, 28, "Sword (right)", "Shield (left)"},
    {42, "Tantalos",        38, 28, "Sword (right)", "Shield (left)"},
    {43, "Zaikan",          38, 28, "Mace (right)", "Shield (left)"},
    {44, "Blade Hunter",    38, 28, "Blade (right)", "Shield (left)"},
};

struct MonsterTypeMap {
    int serverType;
    int modelIndex;
    const char *name;
    const char *map;
};

static const MonsterTypeMap s_monsterTypes[] = {
    // Lorencia (map 0)
    {0,  0,  "Bull Fighter",      "Lorencia"},
    {1,  1,  "Hound",             "Lorencia"},
    {2,  2,  "Budge Dragon",      "Lorencia"},
    {3,  9,  "Spider",            "Lorencia"},
    {4,  0,  "Elite Bull Fighter", "Lorencia"},
    {6,  4,  "Lich",              "Lorencia"},
    {7,  5,  "Giant",             "Lorencia"},
    {14, 6,  "Skeleton Warrior",  "Lorencia"},
    // Dungeon (map 1)
    {5,  1,  "Hell Hound",        "Dungeon"},
    {8,  0,  "Poison Bull",       "Dungeon"},
    {9,  4,  "Thunder Lich",      "Dungeon"},
    {10, 3,  "Dark Knight",       "Dungeon"},
    {11, 7,  "Ghost",             "Dungeon"},
    {12, 6,  "Larva",             "Dungeon"},
    {13, 8,  "Hell Spider",       "Dungeon"},
    {15, 6,  "Skeleton Archer",   "Dungeon"},
    {16, 6,  "Elite Skeleton",    "Dungeon"},
    {17, 10, "Cyclops",           "Dungeon"},
    {18, 11, "Gorgon",            "Dungeon"},
    // Devias (map 2)
    {20, 13, "Elite Yeti",        "Devias"},
    {21, 14, "Assassin",          "Devias"},
    {22, 15, "Ice Monster",       "Devias"},
    {23, 16, "Hommerd",           "Devias"},
    {24, 17, "Worm",              "Devias"},
    {25, 18, "Ice Queen",         "Devias"},
    // Noria (map 3)
    {26, 19, "Goblin",            "Noria"},
    {27, 20, "Chain Scorpion",    "Noria"},
    {28, 21, "Beetle Monster",    "Noria"},
    {29, 22, "Hunter",            "Noria"},
    {30, 23, "Forest Monster",    "Noria"},
    {31, 24, "Agon",              "Noria"},
    {32, 25, "Stone Golem",       "Noria"},
    {33, 26, "Elite Goblin",      "Noria"},
    // Lost Tower (map 4)
    {34, 27, "Cursed Wizard",     "Lost Tower"},
    {35, 11, "Death Gorgon",      "Lost Tower"},
    {36, 28, "Shadow",            "Lost Tower"},
    {37, 26, "Devil",             "Lost Tower"},
    {38, 27, "Balrog",            "Lost Tower"},
    {39, 28, "Poison Shadow",     "Lost Tower"},
    {40, 29, "Death Knight",      "Lost Tower"},
    {41, 30, "Death Cow",         "Lost Tower"},
    // Atlans (map 7)
    {46, 33, "Vepar",             "Atlans"},
    {47, 31, "Valkyrie",          "Atlans"},
    {48, 32, "Lizard King",       "Atlans"},
    {49, 34, "Hydra",             "Atlans"},
    {52, 35, "Sea Worm",          "Atlans"},
    // Tarkan (map 8)
    {57, 41, "Iron Wheel",        "Tarkan"},
    {58, 42, "Tantalos",          "Tarkan"},
    {59, 43, "Zaikan",            "Tarkan"},
    {60, 44, "Blade Hunter",      "Tarkan"},
    {62, 45, "Death Tree",        "Tarkan"},
};

static const char *s_monsterActionNames[] = {
    "STOP1 (idle 1)",       // 0
    "STOP2 (idle 2)",       // 1
    "WALK",                 // 2
    "ATTACK1",              // 3
    "ATTACK2",              // 4
    "SHOCK (hit react)",    // 5
    "DIE",                  // 6
    "ATTACK3",              // 7
    "ATTACK4",              // 8
    "APEAR (appear/spawn)", // 9
};

// Known texture search directories
static const char *s_texSearchDirs[] = {
    "Data/Monster/",
    "Data/Player/",
    "Data/Item/",
    "Data/Skill/",
    "Data/NPC/",
    "Data/Object1/",
    "Data/Object2/",
    "Data/Object3/",
    "Data/Object4/",
    "Data/Object5/",
    "Data/Interface/",
};

// ═══════════════════════════════════════════════════════════════════════
// Options
// ═══════════════════════════════════════════════════════════════════════

struct InspectOptions {
    bool showBones     = false;
    bool showActions   = false;
    bool showMeshDetail = false;
    bool showVFX       = false;
    bool brief         = false;
    bool texCheck      = false; // --texcheck: verify texture files exist
    bool sndCheck      = false; // --sndcheck: verify sound files exist
    bool animDetail    = false; // --anim: deep animation analysis
    bool attachPoints  = false; // --attach: VFX attachment suggestions
    bool blendAnalysis = false; // --blend: BlendMesh rendering analysis
    bool recursive     = false; // -r: recursive directory scan
    std::string filter;
    std::string dataDir = "Data"; // base data directory
};

// ═══════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════

static std::string humanSize(long bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
        return buf;
    }
    char buf[32]; snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

static void computeBounds(const BMDData &bmd, glm::vec3 &outMin, glm::vec3 &outMax) {
    outMin = glm::vec3(1e9f);
    outMax = glm::vec3(-1e9f);
    for (const auto &mesh : bmd.Meshes)
        for (const auto &v : mesh.Vertices) {
            outMin = glm::min(outMin, v.Position);
            outMax = glm::max(outMax, v.Position);
        }
}

static std::set<int> getUsedBones(const BMDData &bmd) {
    std::set<int> used;
    for (const auto &mesh : bmd.Meshes) {
        for (const auto &v : mesh.Vertices) used.insert(v.Node);
        for (const auto &n : mesh.Normals)  used.insert(n.Node);
    }
    return used;
}

static std::map<int, int> getBoneDepths(const BMDData &bmd) {
    std::map<int, int> depths;
    for (int i = 0; i < (int)bmd.Bones.size(); ++i) {
        int depth = 0, p = i;
        while (p >= 0 && p < (int)bmd.Bones.size() && !bmd.Bones[p].Dummy &&
               bmd.Bones[p].Parent >= 0 && depth < 50) {
            p = bmd.Bones[p].Parent;
            depth++;
        }
        depths[i] = depth;
    }
    return depths;
}

static int extractModelIndex(const std::string &filename) {
    auto pos = filename.find("Monster");
    if (pos == std::string::npos) return -1;
    pos += 7;
    std::string numStr;
    while (pos < filename.size() && std::isdigit(filename[pos]))
        numStr += filename[pos++];
    if (numStr.empty()) return -1;
    return std::stoi(numStr) - 1;
}

// ═══════════════════════════════════════════════════════════════════════
// Classification & Feature Detection
// ═══════════════════════════════════════════════════════════════════════

static std::string classifyModel(const BMDData &bmd, const std::string &filename, long) {
    int numBones = (int)bmd.Bones.size();
    int numActions = (int)bmd.Actions.size();

    if (filename.find("Player") != std::string::npos)      return "PLAYER_CHARACTER";
    if (filename.find("Skeleton") != std::string::npos)    return "SKELETON_SKIN";
    if (filename.find("Helm") != std::string::npos)        return "BODY_PART_HELM";
    if (filename.find("Armor") != std::string::npos)       return "BODY_PART_ARMOR";
    if (filename.find("Pant") != std::string::npos)        return "BODY_PART_PANTS";
    if (filename.find("Glove") != std::string::npos)       return "BODY_PART_GLOVES";
    if (filename.find("Boot") != std::string::npos)        return "BODY_PART_BOOTS";
    if (filename.find("Wing") != std::string::npos)        return "WINGS";
    if (filename.find("Helper") != std::string::npos)      return "HELPER_ITEM";
    if (filename.find("Rider") != std::string::npos)       return "MOUNT_RIDER";
    if (filename.find("Sword") != std::string::npos ||
        filename.find("Axe") != std::string::npos ||
        filename.find("Mace") != std::string::npos ||
        filename.find("Staff") != std::string::npos ||
        filename.find("Spear") != std::string::npos ||
        filename.find("Bow") != std::string::npos)         return "WEAPON";
    if (filename.find("Shield") != std::string::npos)      return "SHIELD";
    if (filename.find("Object") != std::string::npos)      return "WORLD_OBJECT";
    if (filename.find("Arrow") != std::string::npos)       return "PROJECTILE";
    if (filename.find("EarthQuake") != std::string::npos)  return "SKILL_VFX (Rageful Blow)";
    if (filename.find("Fire") != std::string::npos &&
        filename.find("Monster") == std::string::npos)     return "SKILL_VFX (Fire)";
    if (filename.find("Tornado") != std::string::npos)     return "SKILL_VFX (Twister)";
    if (filename.find("plancrack") != std::string::npos)   return "SKILL_VFX (Strike of Dest.)";

    if (filename.find("Monster") != std::string::npos) {
        if (numBones > 60)  return "MONSTER_COMPLEX";
        if (numBones > 30)  return "MONSTER_STANDARD";
        if (numBones > 10)  return "MONSTER_SIMPLE";
        return "MONSTER_MINIMAL";
    }

    if (numActions > 100) return "CHARACTER";
    if (numBones == 0 && numActions <= 2) return "STATIC_PROP";
    if (numBones > 20) return "ANIMATED_CREATURE";
    return "UNKNOWN";
}

static std::vector<std::string> detectFeatures(const BMDData &bmd) {
    std::vector<std::string> features;
    std::set<std::string> texNames;
    int totalVerts = 0;

    for (const auto &m : bmd.Meshes) {
        texNames.insert(m.TextureName);
        totalVerts += m.NumVertices;
    }

    if (bmd.Meshes.size() > 1)
        features.push_back("MULTI_MESH(" + std::to_string(bmd.Meshes.size()) + ")");

    for (const auto &t : texNames) {
        std::string lo = t;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        if (lo.find("eye") != std::string::npos)       features.push_back("HAS_EYES");
        if (lo.find("glow") != std::string::npos ||
            lo.find("light") != std::string::npos)     features.push_back("HAS_GLOW_TEX");
        if (lo.find("chrome") != std::string::npos ||
            lo.find("metal") != std::string::npos)     features.push_back("CHROME/METAL_TEX");
        if (lo.find("wing") != std::string::npos)      features.push_back("HAS_WINGS");
        if (lo.find("fire") != std::string::npos ||
            lo.find("flame") != std::string::npos)     features.push_back("HAS_FIRE_TEX");
        if (lo.find("boss") != std::string::npos)      features.push_back("BOSS_TEX");
        if (lo.find("wheel") != std::string::npos)     features.push_back("WHEEL_TEX");
        if (lo.find("shadow") != std::string::npos)    features.push_back("SHADOW_TEX");
        if (lo.find("satan") != std::string::npos ||
            lo.find("devil") != std::string::npos)     features.push_back("DEMON_TEX");
        if (lo.find("npc") != std::string::npos)       features.push_back("NPC_TEX");
        if (lo.find("skin") != std::string::npos)      features.push_back("SKIN_TEX");
        if (lo.find("head_") != std::string::npos)     features.push_back("HEAD_TEX");
        if (lo.find("hide") != std::string::npos)      features.push_back("HIDE_TEX");
        if (lo.find("hair") != std::string::npos)      features.push_back("HAIR_TEX");
        if (lo.find(".tga") != std::string::npos)      features.push_back("USES_TGA (has alpha)");
    }

    // BlendMesh candidates
    std::map<int, int> texIdCounts;
    for (const auto &m : bmd.Meshes) texIdCounts[m.Texture]++;
    for (const auto &[id, count] : texIdCounts)
        if (count > 1)
            features.push_back("SHARED_TEX_ID(" + std::to_string(id) + "x" + std::to_string(count) + ")");

    if (totalVerts > 2000)     features.push_back("HIGH_POLY(" + std::to_string(totalVerts) + "v)");
    else if (totalVerts < 100) features.push_back("LOW_POLY(" + std::to_string(totalVerts) + "v)");

    if (!bmd.Actions.empty()) {
        bool hasLock = false;
        float maxSpd = 0;
        for (const auto &a : bmd.Actions) {
            if (a.LockPositions) hasLock = true;
            if (a.PlaySpeed > maxSpd) maxSpd = a.PlaySpeed;
        }
        if (hasLock) features.push_back("ROOT_MOTION_LOCK");
        if (maxSpd > 10.0f) features.push_back("FAST_ANIM(speed=" + std::to_string((int)maxSpd) + ")");
    }

    int nb = (int)bmd.Bones.size();
    if (nb == 56)      features.push_back("PLAYER_BODY_PART (56 bones)");
    else if (nb == 60) features.push_back("PLAYER_SKELETON (60 bones)");
    else if (nb == 1)  features.push_back("SINGLE_BONE (static/simple)");

    return features;
}

static std::string suggestIdentity(const BMDData &bmd, const std::string &) {
    std::string allTex;
    for (const auto &m : bmd.Meshes) {
        std::string lo = m.TextureName;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        allTex += lo + " ";
    }

    struct TM { const char *pat; const char *id; };
    static const TM matches[] = {
        {"bull",       "Bull Fighter (Lorencia)"},
        {"boss_eye",   "Gorgon / Death Gorgon"},
        {"boss.jpg",   "Gorgon / Death Gorgon"},
        {"gp01",       "Valkyrie (Atlans)"},
        {"hp00",       "Vepar (Atlans)"},
        {"fr00",       "Lizard King (Atlans)"},
        {"bbbb00",     "Hydra (Atlans)"},
        {"uo00",       "Sea Worm (Atlans)"},
        {"gh1",        "Tantalos (Tarkan)"},
        {"npc_g",      "Zaikan (Tarkan)"},
        {"wheel",      "Iron Wheel (Tarkan)"},
        {"shadow_c",   "Shadow / Poison Shadow (LT)"},
        {"shadow_b",   "Shadow / Death Knight (LT)"},
        {"satan",      "Devil (LT)"},
        {"ho9",        "Balrog (LT)"},
        {"wwo",        "Death Cow (LT)"},
        {"spider",     "Spider (Lorencia)"},
        {"dk_",        "Dark Knight (Guard)"},
        {"hound",      "Hound (Lorencia)"},
        {"dragon",     "Dragon / Budge Dragon"},
        {"lich",       "Lich (Dungeon)"},
        {"giant",      "Giant (Dungeon)"},
        {"ghost",      "Ghost (Dungeon)"},
        {"larva",      "Larva (Dungeon)"},
        {"yeti",       "Yeti (Devias)"},
        {"assassin",   "Assassin (Devias)"},
        {"goblin",     "Goblin (Devias/Noria)"},
        {"golem",      "Golem"},
        {"scorpion",   "Scorpion"},
        {"beetle",     "Beetle Monster"},
        {"ice",        "Ice Monster / Queen (Devias)"},
        {"queen_bee",  "Queen Bee (Noria)"},
        {"skeleton",   "Skeleton (Dungeon)"},
        {"polearms",   "Death Knight (LT)"},
        {"flamset",    "Cursed Wizard armor set"},
        {"hoblinset",  "Dark Knight armor set"},
    };

    for (const auto &tm : matches)
        if (allTex.find(tm.pat) != std::string::npos)
            return tm.id;
    return "";
}

// ═══════════════════════════════════════════════════════════════════════
// Texture File Verification
// ═══════════════════════════════════════════════════════════════════════

static bool findTextureFile(const std::string &texName, const std::string &bmdDir,
                            const std::string &dataDir, std::string &foundPath) {
    // 1. Check same directory as BMD
    std::string sameDirPath = bmdDir + "/" + texName;
    if (fs::exists(sameDirPath)) { foundPath = sameDirPath; return true; }

    // 2. Check common data directories
    for (const auto *dir : s_texSearchDirs) {
        std::string path = dataDir + "/" + std::string(dir).substr(5) + texName; // strip "Data/"
        if (fs::exists(path)) { foundPath = path; return true; }
    }

    // 3. Case-insensitive search in BMD directory
    std::string texLo = texName;
    std::transform(texLo.begin(), texLo.end(), texLo.begin(), ::tolower);
    try {
        for (const auto &entry : fs::directory_iterator(bmdDir)) {
            std::string fname = entry.path().filename().string();
            std::string flo = fname;
            std::transform(flo.begin(), flo.end(), flo.begin(), ::tolower);
            if (flo == texLo) { foundPath = entry.path().string(); return true; }
        }
    } catch (...) {}

    // 4. Check OZJ/OZT variants
    auto baseName = fs::path(texName).stem().string();
    auto ext = fs::path(texName).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".jpg" || ext == ".jpeg") {
        std::string ozj = bmdDir + "/" + baseName + ".OZJ";
        if (fs::exists(ozj)) { foundPath = ozj + " (OZJ)"; return true; }
        ozj = bmdDir + "/" + baseName + ".ozj";
        if (fs::exists(ozj)) { foundPath = ozj + " (OZJ)"; return true; }
    }
    if (ext == ".tga") {
        std::string ozt = bmdDir + "/" + baseName + ".OZT";
        if (fs::exists(ozt)) { foundPath = ozt + " (OZT)"; return true; }
        ozt = bmdDir + "/" + baseName + ".ozt";
        if (fs::exists(ozt)) { foundPath = ozt + " (OZT)"; return true; }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// Sound File Verification
// ═══════════════════════════════════════════════════════════════════════

static bool checkSoundFile(const std::string &wavName, const std::string &dataDir) {
    if (wavName[0] == '\0' || wavName[0] == 0) return true; // empty = no sound
    std::string path = dataDir + "/Sound/" + wavName;
    return fs::exists(path);
}

// ═══════════════════════════════════════════════════════════════════════
// Animation Analysis
// ═══════════════════════════════════════════════════════════════════════

struct ActionAnalysis {
    int actionIdx;
    int numKeys;
    float playSpeed;
    float durationSec;         // estimated duration in seconds
    float rootMotionDist;      // total root bone displacement (if locked)
    int   impactFrame;         // frame with maximum bone velocity (for attacks)
    float impactVelocity;      // velocity at impact frame
    float maxBoneDisplacement; // max displacement of any bone across the action
    int   maxDisplaceBone;     // which bone had max displacement
    std::string maxDisplaceBoneName;
};

static ActionAnalysis analyzeAction(const BMDData &bmd, int actionIdx) {
    ActionAnalysis aa{};
    aa.actionIdx = actionIdx;

    if (actionIdx < 0 || actionIdx >= (int)bmd.Actions.size()) return aa;
    const auto &act = bmd.Actions[actionIdx];
    aa.numKeys = act.NumAnimationKeys;
    aa.playSpeed = act.PlaySpeed;

    // Duration: keys / speed = ticks, ticks / 25 = seconds
    if (act.PlaySpeed > 0.001f && act.NumAnimationKeys > 1)
        aa.durationSec = (float)act.NumAnimationKeys / act.PlaySpeed / 25.0f;

    // Root motion distance (from lock positions)
    if (act.LockPositions && act.Positions.size() >= 2) {
        float totalDist = 0;
        for (int k = 1; k < (int)act.Positions.size(); ++k) {
            glm::vec3 delta = act.Positions[k] - act.Positions[k - 1];
            totalDist += glm::length(delta);
        }
        aa.rootMotionDist = totalDist;
    }

    // Per-bone displacement analysis: find which bone moves most, and identify impact frame
    float maxVelocity = 0;
    aa.impactFrame = -1;
    aa.maxBoneDisplacement = 0;
    aa.maxDisplaceBone = -1;

    for (int bi = 0; bi < (int)bmd.Bones.size(); ++bi) {
        const auto &bone = bmd.Bones[bi];
        if (bone.Dummy) continue;
        if (bi >= (int)bone.BoneMatrixes.size()) continue;
        if (actionIdx >= (int)bone.BoneMatrixes.size()) continue;

        const auto &bm = bone.BoneMatrixes[actionIdx];
        if (bm.Position.size() < 2) continue;

        // Track total displacement of this bone
        float totalDisp = 0;
        for (int k = 1; k < (int)bm.Position.size(); ++k) {
            glm::vec3 d = bm.Position[k] - bm.Position[k - 1];
            float vel = glm::length(d);
            totalDisp += vel;

            // Track max velocity frame (impact frame)
            if (vel > maxVelocity) {
                maxVelocity = vel;
                aa.impactFrame = k;
                aa.impactVelocity = vel;
            }
        }

        if (totalDisp > aa.maxBoneDisplacement) {
            aa.maxBoneDisplacement = totalDisp;
            aa.maxDisplaceBone = bi;
            aa.maxDisplaceBoneName = bone.Name;
        }
    }

    return aa;
}

// ═══════════════════════════════════════════════════════════════════════
// VFX Attachment Point Analysis
// ═══════════════════════════════════════════════════════════════════════

struct AttachPoint {
    int boneIdx;
    std::string boneName;
    std::string category; // "HEAD", "HAND_R", "HAND_L", "SPINE", "FOOT", etc.
    glm::vec3 bindPos;    // position at keyframe 0 of action 0
    float animRange;      // how much this bone moves in idle (stability metric)
};

static std::vector<AttachPoint> findAttachPoints(const BMDData &bmd) {
    std::vector<AttachPoint> points;

    for (int bi = 0; bi < (int)bmd.Bones.size(); ++bi) {
        const auto &bone = bmd.Bones[bi];
        if (bone.Dummy) continue;

        std::string name = bone.Name;
        std::string nameLo = name;
        std::transform(nameLo.begin(), nameLo.end(), nameLo.begin(), ::tolower);

        AttachPoint ap;
        ap.boneIdx = bi;
        ap.boneName = name;
        ap.bindPos = glm::vec3(0);
        ap.animRange = 0;

        // Categorize by name
        if (nameLo.find("head") != std::string::npos)          ap.category = "HEAD";
        else if (nameLo.find("r hand") != std::string::npos ||
                 nameLo.find("r_hand") != std::string::npos)   ap.category = "HAND_R";
        else if (nameLo.find("l hand") != std::string::npos ||
                 nameLo.find("l_hand") != std::string::npos)   ap.category = "HAND_L";
        else if (nameLo.find("r finger") != std::string::npos) ap.category = "FINGER_R";
        else if (nameLo.find("l finger") != std::string::npos) ap.category = "FINGER_L";
        else if (nameLo.find("r forearm") != std::string::npos) ap.category = "FOREARM_R";
        else if (nameLo.find("l forearm") != std::string::npos) ap.category = "FOREARM_L";
        else if (nameLo.find("r upperarm") != std::string::npos) ap.category = "UPPER_ARM_R";
        else if (nameLo.find("l upperarm") != std::string::npos) ap.category = "UPPER_ARM_L";
        else if (nameLo.find("neck") != std::string::npos)     ap.category = "NECK";
        else if (nameLo.find("spine") != std::string::npos)    ap.category = "SPINE";
        else if (nameLo.find("pelvis") != std::string::npos)   ap.category = "PELVIS";
        else if (nameLo.find("r foot") != std::string::npos)   ap.category = "FOOT_R";
        else if (nameLo.find("l foot") != std::string::npos)   ap.category = "FOOT_L";
        else if (nameLo.find("r toe") != std::string::npos)    ap.category = "TOE_R";
        else if (nameLo.find("l toe") != std::string::npos)    ap.category = "TOE_L";
        else if (nameLo.find("r calf") != std::string::npos)   ap.category = "CALF_R";
        else if (nameLo.find("l calf") != std::string::npos)   ap.category = "CALF_L";
        else if (nameLo.find("r thigh") != std::string::npos)  ap.category = "THIGH_R";
        else if (nameLo.find("l thigh") != std::string::npos)  ap.category = "THIGH_L";
        else if (nameLo.find("eye") != std::string::npos)      ap.category = "EYE";
        else if (nameLo.find("knife") != std::string::npos ||
                 nameLo.find("weapon") != std::string::npos ||
                 nameLo.find("sword") != std::string::npos)    ap.category = "WEAPON_SLOT";
        else if (nameLo.find("tail") != std::string::npos)     ap.category = "TAIL";
        else if (nameLo.find("wing") != std::string::npos)     ap.category = "WING";
        else if (nameLo.find("jaw") != std::string::npos ||
                 nameLo.find("mouth") != std::string::npos)    ap.category = "MOUTH";
        else if (nameLo.find("box") != std::string::npos ||
                 nameLo.find("bone") != std::string::npos)     ap.category = "AUX";
        else continue; // skip unrecognized bones for cleaner output

        // Get bind-pose position (action 0, key 0)
        if (bi < (int)bmd.Bones.size() && !bmd.Actions.empty()) {
            if (bi < (int)bone.BoneMatrixes.size() && !bone.BoneMatrixes.empty()) {
                if (bone.BoneMatrixes[0].Position.size() > 0)
                    ap.bindPos = bone.BoneMatrixes[0].Position[0];
            }
        }

        // Compute stability: how much does this bone move in idle (action 0)?
        if (!bmd.Actions.empty() && bi < (int)bone.BoneMatrixes.size()) {
            const auto &bm = bone.BoneMatrixes[0];
            if (bm.Position.size() >= 2) {
                glm::vec3 minP(1e9f), maxP(-1e9f);
                for (const auto &p : bm.Position) {
                    minP = glm::min(minP, p);
                    maxP = glm::max(maxP, p);
                }
                ap.animRange = glm::length(maxP - minP);
            }
        }

        points.push_back(ap);
    }

    return points;
}

// ═══════════════════════════════════════════════════════════════════════
// BlendMesh Analysis
// ═══════════════════════════════════════════════════════════════════════

struct BlendMeshInfo {
    int meshIdx;
    int texId;
    std::string texName;
    int vertCount;
    bool isLikelyGlow;    // texture name suggests glow/effect
    bool isLikelySkin;    // texture name suggests character skin
    bool hasTGA;          // uses TGA (has alpha channel)
    std::string recommendation; // "ADDITIVE", "NORMAL", "ALPHA_BLEND"
};

static std::vector<BlendMeshInfo> analyzeBlendMeshes(const BMDData &bmd) {
    std::vector<BlendMeshInfo> result;

    for (int i = 0; i < (int)bmd.Meshes.size(); ++i) {
        const auto &m = bmd.Meshes[i];
        BlendMeshInfo bmi;
        bmi.meshIdx = i;
        bmi.texId = m.Texture;
        bmi.texName = m.TextureName;
        bmi.vertCount = m.NumVertices;

        std::string lo = m.TextureName;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);

        bmi.isLikelyGlow = (lo.find("glow") != std::string::npos ||
                            lo.find("light") != std::string::npos ||
                            lo.find("fire") != std::string::npos ||
                            lo.find("flame") != std::string::npos ||
                            lo.find("eye") != std::string::npos ||
                            lo.find("chrome") != std::string::npos ||
                            lo.find("energy") != std::string::npos ||
                            lo.find("magic") != std::string::npos ||
                            lo.find("spark") != std::string::npos ||
                            lo.find("rgb_light") != std::string::npos);

        bmi.isLikelySkin = (lo.find("skin") != std::string::npos ||
                            lo.find("hide") != std::string::npos ||
                            lo.find("head_") != std::string::npos);

        bmi.hasTGA = (lo.find(".tga") != std::string::npos);

        // Determine rendering recommendation
        if (bmi.isLikelyGlow)
            bmi.recommendation = "ADDITIVE (BlendMesh candidate)";
        else if (bmi.hasTGA)
            bmi.recommendation = "ALPHA_BLEND (TGA with transparency)";
        else if (bmi.isLikelySkin)
            bmi.recommendation = "NORMAL (skin/hide — filter for UI)";
        else if (bmi.vertCount < 20 && bmd.Meshes.size() > 1)
            bmi.recommendation = "ADDITIVE? (small mesh, likely effect)";
        else
            bmi.recommendation = "NORMAL";

        result.push_back(bmi);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// Bone Symmetry Analysis
// ═══════════════════════════════════════════════════════════════════════

struct BoneSymmetryPair {
    int leftIdx, rightIdx;
    std::string leftName, rightName;
};

static std::vector<BoneSymmetryPair> findSymmetryPairs(const BMDData &bmd) {
    std::vector<BoneSymmetryPair> pairs;

    for (int i = 0; i < (int)bmd.Bones.size(); ++i) {
        if (bmd.Bones[i].Dummy) continue;
        std::string name = bmd.Bones[i].Name;
        // Check for " L " or "L " at common positions
        auto lpos = name.find(" L ");
        if (lpos == std::string::npos) lpos = name.find("_L_");
        if (lpos == std::string::npos) continue;

        std::string rName = name;
        rName[lpos + 1] = 'R';

        for (int j = i + 1; j < (int)bmd.Bones.size(); ++j) {
            if (bmd.Bones[j].Dummy) continue;
            if (std::string(bmd.Bones[j].Name) == rName) {
                pairs.push_back({i, j, name, rName});
                break;
            }
        }
    }
    return pairs;
}

// ═══════════════════════════════════════════════════════════════════════
// Main Inspection
// ═══════════════════════════════════════════════════════════════════════

static void inspectFile(const std::string &path, const InspectOptions &opts) {
    long fileSize = 0;
    try { fileSize = (long)fs::file_size(path); }
    catch (...) { fileSize = -1; }

    std::string filename = fs::path(path).filename().string();
    std::string bmdDir = fs::path(path).parent_path().string();

    auto bmd = BMDParser::Parse(path, /*quiet=*/true);
    if (!bmd) {
        std::cerr << "  FAILED to parse: " << filename << std::endl;
        return;
    }

    int numBones = (int)bmd->Bones.size();
    int numMeshes = (int)bmd->Meshes.size();
    int numActions = (int)bmd->Actions.size();
    int totalVerts = 0, totalTris = 0, totalNorms = 0;
    for (const auto &m : bmd->Meshes) {
        totalVerts += m.NumVertices;
        totalTris  += m.NumTriangles;
        totalNorms += m.NumNormals;
    }

    glm::vec3 bbMin, bbMax;
    computeBounds(*bmd, bbMin, bbMax);
    glm::vec3 bbSize = bbMax - bbMin;

    std::string classification = classifyModel(*bmd, filename, fileSize);
    auto features = detectFeatures(*bmd);
    std::string identity = suggestIdentity(*bmd, filename);
    auto usedBones = getUsedBones(*bmd);
    int modelIdx = extractModelIndex(filename);
    bool isMonster = (filename.find("Monster") != std::string::npos);

    // ── Brief mode ──
    if (opts.brief) {
        std::cout << std::left << std::setw(25) << filename << " | "
                  << std::setw(8) << humanSize(fileSize) << " | B:"
                  << std::setw(3) << numBones << " M:" << std::setw(2) << numMeshes
                  << " A:" << std::setw(3) << numActions << " T:" << std::setw(5)
                  << totalTris << " | ";
        for (const auto &m : bmd->Meshes) std::cout << m.TextureName << " ";
        if (!identity.empty()) std::cout << "=> " << identity;
        std::cout << std::endl;
        return;
    }

    // ── Full output ──
    std::cout << "\n=================================================================" << std::endl;
    std::cout << "  FILE: " << filename << std::endl;
    std::cout << "  PATH: " << path << std::endl;
    std::cout << "-----------------------------------------------------------------" << std::endl;

    // Metadata
    std::cout << "  Size:          " << humanSize(fileSize) << " (" << fileSize << " bytes)" << std::endl;
    std::cout << "  Version:       0x" << std::hex << (int)(unsigned char)bmd->Version << std::dec
              << (bmd->Version == 0x0C ? " (encrypted)" : " (plain)") << std::endl;
    std::cout << "  Internal Name: \"" << bmd->Name << "\"" << std::endl;
    std::cout << "  Class:         " << classification << std::endl;
    if (!identity.empty())
        std::cout << "  Identity:      " << identity << std::endl;

    // Server type mapping
    if (modelIdx >= 0) {
        bool found = false;
        for (const auto &tm : s_monsterTypes) {
            if (tm.modelIndex == modelIdx) {
                std::cout << "  Server Type:   " << tm.serverType << " (" << tm.name
                          << ") -- " << tm.map << std::endl;
                found = true;
            }
        }
        if (!found)
            std::cout << "  Model Index:   OpenMonsterModel(" << modelIdx << ") -- unmapped" << std::endl;
    }

    // Geometry
    std::cout << std::endl << "  GEOMETRY:" << std::endl;
    std::cout << "    Meshes:     " << numMeshes << std::endl;
    std::cout << "    Bones:      " << numBones << " (" << usedBones.size() << " used by verts)" << std::endl;
    std::cout << "    Actions:    " << numActions << std::endl;
    std::cout << "    Vertices:   " << totalVerts << std::endl;
    std::cout << "    Normals:    " << totalNorms << std::endl;
    std::cout << "    Triangles:  " << totalTris << std::endl;

    // Bounding box
    std::cout << std::endl << "  BOUNDING BOX (bind pose):" << std::endl;
    std::cout << "    Min: (" << std::fixed << std::setprecision(1) << bbMin.x
              << ", " << bbMin.y << ", " << bbMin.z << ")" << std::endl;
    std::cout << "    Max: (" << bbMax.x << ", " << bbMax.y << ", " << bbMax.z << ")" << std::endl;
    std::cout << "    Dimensions: " << bbSize.x << " W x " << bbSize.y << " H x " << bbSize.z << " D" << std::endl;
    if (bbSize.y > 0.1f) {
        std::cout << "    Est. height: ~" << std::setprecision(0) << bbSize.y
                  << " units (1 terrain cell = 100)" << std::endl;
        // Suggest scale based on expected creature size
        if (isMonster) {
            float suggestedScale = 100.0f / bbSize.y; // normalize to ~1 cell height
            std::cout << "    Scale hint:  " << std::setprecision(2) << suggestedScale
                      << " to reach 100u, " << suggestedScale * 1.5f << " for 150u tall" << std::endl;
        }
    }

    // ── Meshes ──
    std::cout << std::endl << "  MESHES:" << std::endl;
    for (int i = 0; i < numMeshes; ++i) {
        const auto &m = bmd->Meshes[i];
        std::cout << "    [" << i << "] TexID=" << m.Texture << " \"" << m.TextureName << "\"" << std::endl;
        std::cout << "        Verts=" << m.NumVertices << " Norms=" << m.NumNormals
                  << " UVs=" << m.NumTexCoords << " Tris=" << m.NumTriangles << std::endl;

        if (opts.showMeshDetail) {
            std::set<int> meshBones;
            for (const auto &v : m.Vertices) meshBones.insert(v.Node);
            std::cout << "        Bones used: {";
            int cnt = 0;
            for (auto it = meshBones.begin(); it != meshBones.end(); ++it) {
                if (it != meshBones.begin()) std::cout << ", ";
                std::cout << *it;
                if (*it >= 0 && *it < numBones && !bmd->Bones[*it].Dummy)
                    std::cout << "(" << bmd->Bones[*it].Name << ")";
                if (++cnt > 20) { std::cout << " ...+" << ((int)meshBones.size() - cnt); break; }
            }
            std::cout << "}" << std::endl;
        }
    }

    // ── Features ──
    if (!features.empty()) {
        std::cout << std::endl << "  FEATURES:" << std::endl;
        for (const auto &f : features)
            std::cout << "    - " << f << std::endl;
    }

    // ── Texture Verification ──
    if (opts.texCheck) {
        std::cout << std::endl << "  TEXTURE VERIFICATION:" << std::endl;
        std::set<std::string> checked;
        for (const auto &m : bmd->Meshes) {
            if (checked.count(m.TextureName)) continue;
            checked.insert(m.TextureName);

            std::string foundPath;
            bool found = findTextureFile(m.TextureName, bmdDir, opts.dataDir, foundPath);
            if (found)
                std::cout << "    [OK] " << m.TextureName << " -> " << foundPath << std::endl;
            else
                std::cout << "    [!!] " << m.TextureName << " -> NOT FOUND" << std::endl;
        }
    }

    // ── BlendMesh Analysis ──
    if (opts.blendAnalysis && numMeshes > 1) {
        auto blendInfo = analyzeBlendMeshes(*bmd);
        std::cout << std::endl << "  BLENDMESH ANALYSIS:" << std::endl;
        for (const auto &bi : blendInfo) {
            std::cout << "    Mesh[" << bi.meshIdx << "] \"" << bi.texName << "\" ("
                      << bi.vertCount << "v) -> " << bi.recommendation << std::endl;
        }
        // Suggest BlendMesh value
        for (const auto &bi : blendInfo) {
            if (bi.recommendation.find("ADDITIVE") != std::string::npos) {
                std::cout << "    >> Suggested: BlendMesh=" << bi.texId
                          << " (mesh " << bi.meshIdx << " renders additive)" << std::endl;
            }
        }
    }

    // ── Main 5.2 Cross-Reference ──
    if (modelIdx >= 0 || opts.showVFX) {
        bool hdr = false;

        // Sound assignments
        for (const auto &si : s_monsterSounds) {
            if (si.modelIndex == modelIdx) {
                if (!hdr) { std::cout << std::endl << "  MAIN 5.2 CROSS-REFERENCE:" << std::endl; hdr = true; }
                std::cout << "    Sound assignments (" << si.name << "):" << std::endl;
                auto printSnd = [&](const char *label, const char *file) {
                    if (!file[0]) return;
                    std::cout << "      " << std::left << std::setw(10) << label << file;
                    if (opts.sndCheck) {
                        bool exists = checkSoundFile(file, opts.dataDir);
                        std::cout << (exists ? " [OK]" : " [!! MISSING]");
                    }
                    std::cout << std::endl;
                };
                printSnd("Idle 1:", si.idle1);
                printSnd("Idle 2:", si.idle2);
                printSnd("Attack 1:", si.attack1);
                printSnd("Attack 2:", si.attack2);
                printSnd("Death:", si.death);
                break;
            }
        }

        // VFX effects
        for (const auto &vi : s_monsterVFX) {
            if (vi.modelIndex == modelIdx) {
                if (!hdr) { std::cout << std::endl << "  MAIN 5.2 CROSS-REFERENCE:" << std::endl; hdr = true; }
                std::cout << "    VFX/Rendering (" << vi.name << "):" << std::endl;
                std::cout << "      Effects:    " << vi.description << std::endl;
                if (vi.pointLight[0])
                    std::cout << "      PointLight: RGB+Range=" << vi.pointLight << std::endl;
                if (vi.blendMesh[0])
                    std::cout << "      BlendMesh:  " << vi.blendMesh << std::endl;
                if (vi.vfxBones[0])
                    std::cout << "      VFX Bones:  " << vi.vfxBones << std::endl;
                break;
            }
        }

        // Weapon bone assignments
        for (const auto &wi : s_monsterWeapons) {
            if (wi.modelIndex == modelIdx) {
                if (!hdr) { std::cout << std::endl << "  MAIN 5.2 CROSS-REFERENCE:" << std::endl; hdr = true; }
                std::cout << "    Weapon bones (" << wi.name << "):" << std::endl;
                if (wi.linkBone0 >= 0) {
                    std::cout << "      Weapon[0] LinkBone=" << wi.linkBone0;
                    if (wi.linkBone0 < numBones && !bmd->Bones[wi.linkBone0].Dummy)
                        std::cout << " (\"" << bmd->Bones[wi.linkBone0].Name << "\")";
                    std::cout << " -- " << wi.weapon0 << std::endl;
                }
                if (wi.linkBone1 >= 0) {
                    std::cout << "      Weapon[1] LinkBone=" << wi.linkBone1;
                    if (wi.linkBone1 < numBones && !bmd->Bones[wi.linkBone1].Dummy)
                        std::cout << " (\"" << bmd->Bones[wi.linkBone1].Name << "\")";
                    std::cout << " -- " << wi.weapon1 << std::endl;
                }
                break;
            }
        }

        // Server types
        bool typeHdr = false;
        for (const auto &tm : s_monsterTypes) {
            if (tm.modelIndex == modelIdx) {
                if (!typeHdr) {
                    if (!hdr) { std::cout << std::endl << "  MAIN 5.2 CROSS-REFERENCE:" << std::endl; hdr = true; }
                    std::cout << "    Used by server types:" << std::endl;
                    typeHdr = true;
                }
                std::cout << "      Type " << tm.serverType << " = " << tm.name
                          << " (" << tm.map << ")" << std::endl;
            }
        }
    }

    // ── Actions (standard + deep analysis) ──
    if (opts.showActions && numActions > 0) {
        std::cout << std::endl << "  ACTIONS:" << std::endl;

        for (int i = 0; i < numActions; ++i) {
            const auto &a = bmd->Actions[i];
            std::cout << "    [" << std::setw(3) << i << "] Keys=" << std::setw(3)
                      << a.NumAnimationKeys << " Speed=" << std::fixed
                      << std::setprecision(2) << a.PlaySpeed;
            if (a.LockPositions) std::cout << " LOCKED";
            if (a.Loop) std::cout << " LOOP";

            if (a.NumAnimationKeys > 1 && a.PlaySpeed > 0) {
                float dur = (float)a.NumAnimationKeys / a.PlaySpeed / 25.0f;
                std::cout << " (~" << std::setprecision(2) << dur << "s)";
            }

            if (isMonster && i < 10)
                std::cout << "  " << s_monsterActionNames[i];

            std::cout << std::endl;

            // Deep animation analysis
            if (opts.animDetail) {
                auto aa = analyzeAction(*bmd, i);

                if (aa.rootMotionDist > 0.01f)
                    std::cout << "          Root motion: " << std::setprecision(1)
                              << aa.rootMotionDist << " units displacement" << std::endl;

                if (aa.impactFrame >= 0 && i >= 3 && i <= 8) // attack actions
                    std::cout << "          Impact frame: " << aa.impactFrame << "/" << a.NumAnimationKeys
                              << " (velocity=" << std::setprecision(1) << aa.impactVelocity
                              << ") -- play sound/VFX here" << std::endl;

                if (aa.maxDisplaceBone >= 0)
                    std::cout << "          Most active bone: [" << aa.maxDisplaceBone
                              << "] \"" << aa.maxDisplaceBoneName << "\" (disp="
                              << std::setprecision(1) << aa.maxBoneDisplacement << ")" << std::endl;
            }
        }
    }

    // ── VFX Attachment Points ──
    if (opts.attachPoints && numBones > 0) {
        auto attachPts = findAttachPoints(*bmd);
        std::cout << std::endl << "  VFX ATTACHMENT POINTS:" << std::endl;

        // Group by category
        std::map<std::string, std::vector<const AttachPoint *>> groups;
        for (const auto &ap : attachPts)
            groups[ap.category].push_back(&ap);

        for (const auto &[cat, pts] : groups) {
            std::cout << "    " << cat << ":" << std::endl;
            for (const auto *ap : pts) {
                std::cout << "      [" << std::setw(3) << ap->boneIdx << "] \""
                          << ap->boneName << "\" pos=("
                          << std::setprecision(1) << ap->bindPos.x << ","
                          << ap->bindPos.y << "," << ap->bindPos.z << ")";
                if (ap->animRange > 0.1f)
                    std::cout << " idle_range=" << std::setprecision(1) << ap->animRange;
                std::cout << std::endl;
            }
        }

        // Suggest best attachment points
        std::cout << "    SUGGESTIONS:" << std::endl;
        for (const auto &ap : attachPts) {
            if (ap.category == "HEAD")
                std::cout << "      Eye glow / name label: bone " << ap.boneIdx << " (\"" << ap.boneName << "\")" << std::endl;
            if (ap.category == "HAND_R")
                std::cout << "      Right weapon / cast VFX: bone " << ap.boneIdx << " (\"" << ap.boneName << "\")" << std::endl;
            if (ap.category == "HAND_L")
                std::cout << "      Left weapon / shield: bone " << ap.boneIdx << " (\"" << ap.boneName << "\")" << std::endl;
            if (ap.category == "WEAPON_SLOT")
                std::cout << "      Weapon attachment: bone " << ap.boneIdx << " (\"" << ap.boneName << "\")" << std::endl;
            if (ap.category == "PELVIS")
                std::cout << "      Body center / aura origin: bone " << ap.boneIdx << " (\"" << ap.boneName << "\")" << std::endl;
        }
    }

    // ── Bone Symmetry ──
    if (opts.attachPoints && numBones > 6) {
        auto symPairs = findSymmetryPairs(*bmd);
        if (!symPairs.empty()) {
            std::cout << std::endl << "  BONE SYMMETRY PAIRS:" << std::endl;
            for (const auto &sp : symPairs)
                std::cout << "    [" << sp.leftIdx << "] " << sp.leftName
                          << "  <-->  [" << sp.rightIdx << "] " << sp.rightName << std::endl;
        }
    }

    // ── Bones ──
    if (opts.showBones && numBones > 0) {
        auto depths = getBoneDepths(*bmd);
        std::cout << std::endl << "  BONE HIERARCHY:" << std::endl;
        for (int i = 0; i < numBones; ++i) {
            const auto &b = bmd->Bones[i];
            std::string indent(depths[i] * 2, ' ');
            bool isUsed = usedBones.count(i) > 0;

            bool isWeaponBone = false;
            for (const auto &wi : s_monsterWeapons)
                if (wi.modelIndex == modelIdx && (wi.linkBone0 == i || wi.linkBone1 == i))
                    { isWeaponBone = true; break; }

            // Check if it's a VFX bone
            bool isVFXBone = false;
            for (const auto &vi : s_monsterVFX) {
                if (vi.modelIndex == modelIdx && vi.vfxBones[0]) {
                    std::string vb = vi.vfxBones;
                    std::istringstream ss(vb);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        if (!token.empty() && std::stoi(token) == i)
                            { isVFXBone = true; break; }
                    }
                }
            }

            std::cout << "    " << indent;
            if (b.Dummy)
                std::cout << "[" << std::setw(3) << i << "] (dummy)";
            else
                std::cout << "[" << std::setw(3) << i << "] \"" << b.Name << "\" parent=" << b.Parent;

            if (isUsed)       std::cout << " *USED*";
            if (isWeaponBone) std::cout << " <<WEAPON>>";
            if (isVFXBone)    std::cout << " <<VFX>>";
            std::cout << std::endl;
        }
    }

    std::cout << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════
// Usage & Main
// ═══════════════════════════════════════════════════════════════════════

static void printUsage(const char *argv0) {
    std::cout << "BMD Inspector -- MU Online Model Reverse Engineering Tool" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << argv0 << " <file.bmd|directory> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Display options:" << std::endl;
    std::cout << "  --bones       Full bone hierarchy (weapon/VFX bone markers)" << std::endl;
    std::cout << "  --actions     Action/animation details (monster action names)" << std::endl;
    std::cout << "  --meshes      Per-mesh bone usage detail" << std::endl;
    std::cout << "  --brief       One-line summary per file (for directory scan)" << std::endl;
    std::cout << std::endl;
    std::cout << "Analysis options:" << std::endl;
    std::cout << "  --texcheck    Verify texture files exist on disk" << std::endl;
    std::cout << "  --sndcheck    Verify sound files exist in Data/Sound/" << std::endl;
    std::cout << "  --anim        Deep animation analysis (root motion, impact frames)" << std::endl;
    std::cout << "  --attach      VFX attachment point suggestions from bone structure" << std::endl;
    std::cout << "  --blend       BlendMesh rendering analysis & recommendations" << std::endl;
    std::cout << "  --vfx         Show VFX/sound cross-reference for non-monster files" << std::endl;
    std::cout << std::endl;
    std::cout << "Other:" << std::endl;
    std::cout << "  --all         Enable ALL options (bones+actions+meshes+tex+snd+anim+attach+blend+vfx)" << std::endl;
    std::cout << "  --filter=X    Only show files containing X (e.g. --filter=Monster)" << std::endl;
    std::cout << "  --data=DIR    Set data directory (default: Data)" << std::endl;
    std::cout << "  -r            Recursive directory scan" << std::endl;
    std::cout << std::endl;
    std::cout << "Cross-reference data (auto-shown for Monster files):" << std::endl;
    std::cout << "  - Main 5.2 sound assignments (idle/attack/death) with file verification" << std::endl;
    std::cout << "  - Main 5.2 VFX/rendering (glow, lightning, fire, point lights, BlendMesh)" << std::endl;
    std::cout << "  - Main 5.2 weapon bone (LinkBone) assignments with bone name resolution" << std::endl;
    std::cout << "  - Server type -> model index mapping with map locations" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/Monster29.bmd --all" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/ --brief" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/ --brief --filter=Monster2" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/Monster28.bmd --attach --anim" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/Monster12.bmd --blend --texcheck" << std::endl;
    std::cout << "  " << argv0 << " Data/Item/ --brief --filter=Sword" << std::endl;
    std::cout << "  " << argv0 << " Data/Player/ --bones --filter=Helm" << std::endl;
    std::cout << "  " << argv0 << " Data/Monster/ --sndcheck --filter=Monster2" << std::endl;
}

int main(int argc, char **argv) {
    if (argc < 2) { printUsage(argv[0]); return 1; }

    InspectOptions opts;
    std::vector<std::string> paths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bones")        opts.showBones = true;
        else if (arg == "--actions") opts.showActions = true;
        else if (arg == "--meshes")  opts.showMeshDetail = true;
        else if (arg == "--vfx")     opts.showVFX = true;
        else if (arg == "--texcheck") opts.texCheck = true;
        else if (arg == "--sndcheck") opts.sndCheck = true;
        else if (arg == "--anim")    opts.animDetail = true, opts.showActions = true;
        else if (arg == "--attach")  opts.attachPoints = true;
        else if (arg == "--blend")   opts.blendAnalysis = true;
        else if (arg == "-r")        opts.recursive = true;
        else if (arg == "--all") {
            opts.showBones = opts.showActions = opts.showMeshDetail = true;
            opts.showVFX = opts.texCheck = opts.sndCheck = true;
            opts.animDetail = opts.attachPoints = opts.blendAnalysis = true;
        }
        else if (arg == "--brief")   opts.brief = true;
        else if (arg.substr(0, 9) == "--filter=") opts.filter = arg.substr(9);
        else if (arg.substr(0, 7) == "--data=")   opts.dataDir = arg.substr(7);
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
        else paths.push_back(arg);
    }

    if (paths.empty()) {
        std::cerr << "Error: No file or directory specified" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    for (const auto &inputPath : paths) {
        if (fs::is_directory(inputPath)) {
            std::vector<std::string> files;

            auto scanDir = [&](const fs::path &dir) {
                try {
                    for (const auto &entry : fs::directory_iterator(dir)) {
                        if (entry.is_directory() && opts.recursive) {
                            // Will be handled by recursive_directory_iterator
                            continue;
                        }
                        if (!entry.is_regular_file()) continue;
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext != ".bmd") continue;
                        std::string fname = entry.path().filename().string();
                        if (!opts.filter.empty() && fname.find(opts.filter) == std::string::npos)
                            continue;
                        files.push_back(entry.path().string());
                    }
                } catch (...) {}
            };

            if (opts.recursive) {
                try {
                    for (const auto &entry : fs::recursive_directory_iterator(inputPath)) {
                        if (!entry.is_regular_file()) continue;
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext != ".bmd") continue;
                        std::string fname = entry.path().filename().string();
                        if (!opts.filter.empty() && fname.find(opts.filter) == std::string::npos)
                            continue;
                        files.push_back(entry.path().string());
                    }
                } catch (...) {}
            } else {
                scanDir(inputPath);
            }

            std::sort(files.begin(), files.end());

            if (!opts.brief)
                std::cout << "Scanning " << files.size() << " BMD files in " << inputPath
                          << (opts.recursive ? " (recursive)" : "") << std::endl;

            for (const auto &f : files) inspectFile(f, opts);

            if (opts.brief)
                std::cout << "\n" << files.size() << " files scanned." << std::endl;
        } else if (fs::is_regular_file(inputPath)) {
            inspectFile(inputPath, opts);
        } else {
            std::cerr << "Error: Not a file or directory: " << inputPath << std::endl;
        }
    }

    return 0;
}

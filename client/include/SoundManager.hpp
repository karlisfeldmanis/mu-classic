#ifndef MU_SOUND_MANAGER_HPP
#define MU_SOUND_MANAGER_HPP

#include <string>

// Sound IDs matching Main 5.2 DSPlaySound.h for future extensibility
enum SoundId {
  SOUND_WIND01 = 0,
  SOUND_WALK_GRASS = 9,
  SOUND_WALK_SOIL = 10,
  SOUND_BIRD01 = 12,
  SOUND_BIRD02 = 13,
  SOUND_CLICK01 = 25,
  SOUND_ERROR01 = 26,
  SOUND_MENU01 = 27,
  SOUND_INTERFACE01 = 28,
  SOUND_GET_ITEM01 = 29,
  SOUND_DROP_ITEM01 = 30,
  SOUND_DROP_GOLD01 = 31,
  SOUND_DRINK01 = 32,
  SOUND_EAT_APPLE01 = 33,
  SOUND_HEART = 34,
  SOUND_SUMMON = 46,
  SOUND_SWING1 = 60,
  SOUND_SWING2 = 61,
  SOUND_SWING_LIGHT = 62,
  // Bow/crossbow release sounds (Main 5.2: eBow.wav, eCrossbow.wav)
  SOUND_BOW = 63,
  SOUND_CROSSBOW = 64,
  // DW spell impact (Main 5.2: SOUND_METEORITE01)
  SOUND_METEORITE01 = 66,
  SOUND_JEWEL01 = 69,
  SOUND_HIT1 = 70,
  SOUND_HIT2 = 71,
  SOUND_HIT3 = 72,
  SOUND_HIT4 = 73,
  SOUND_HIT5 = 74,
  // Main 5.2: SOUND_THUNDER01 — lightning spell impact
  SOUND_THUNDER01 = 80,
  SOUND_LEVEL_UP = 83,
  // Player hit reaction (Main 5.2: SOUND_HUMAN_SCREAM01-03)
  SOUND_MALE_SCREAM1 = 105,
  SOUND_MALE_SCREAM2 = 106,
  SOUND_MALE_SCREAM3 = 107,
  // Player death (Main 5.2: SOUND_HUMAN_SCREAM04)
  SOUND_MALE_DIE = 108,
  // Female hit/death (Main 5.2: SOUND_FEMALE_SCREAM01-02)
  SOUND_FEMALE_SCREAM1 = 109,
  SOUND_FEMALE_SCREAM2 = 110,
  // DW spell sounds (Main 5.2: DSPlaySound.h)
  SOUND_STORM = 116,    // Twister — sTornado.wav
  SOUND_EVIL = 117,     // Evil Spirit — sEvil.wav
  SOUND_MAGIC = 118,    // Teleport — sMagic.wav
  SOUND_HELLFIRE = 119, // Hellfire — sHellFire.wav
  SOUND_ICE = 120,      // Ice — sIce.wav
  SOUND_FLAME = 121,    // Flame — sFlame.wav
  SOUND_FLASH = 122,    // Aqua Beam — sAquaFlash.wav
  // DK skill sounds
  SOUND_KNIGHT_SKILL1 = 112, // Falling Slash
  SOUND_KNIGHT_SKILL2 = 113, // Lunge
  SOUND_KNIGHT_SKILL3 = 114, // Uppercut
  SOUND_KNIGHT_SKILL4 = 115, // Cyclone
  // Body blow sounds (Main 5.2: SOUND_BLOW01-04 — armor/body hit impacts)
  SOUND_BLOW1 = 130,
  SOUND_BLOW2 = 131,
  SOUND_BLOW3 = 132,
  SOUND_BLOW4 = 133,
  // Missile/spell impact sounds (Main 5.2: eMissileHit1-4)
  SOUND_MISSILE_HIT1 = 134,
  SOUND_MISSILE_HIT2 = 135,
  SOUND_MISSILE_HIT3 = 136,
  SOUND_MISSILE_HIT4 = 137,
  // Soul Barrier cast sound (Main 5.2: SOUND_SOULBARRIER — eSoulBarrier.wav)
  SOUND_SOULBARRIER = 139,
  SOUND_RAGE_BLOW1 = 149,
  SOUND_RAGE_BLOW2 = 150,
  SOUND_RAGE_BLOW3 = 151,
  // Monster sounds — Bull Fighter (type 0)
  SOUND_MONSTER_BULL1 = 200,
  SOUND_MONSTER_BULL2 = 201,
  SOUND_MONSTER_BULLATTACK1 = 202,
  SOUND_MONSTER_BULLATTACK2 = 203,
  SOUND_MONSTER_BULLDIE = 204,
  // Hound (type 1)
  SOUND_MONSTER_HOUND1 = 205,
  SOUND_MONSTER_HOUND2 = 206,
  SOUND_MONSTER_HOUNDATTACK1 = 207,
  SOUND_MONSTER_HOUNDATTACK2 = 208,
  SOUND_MONSTER_HOUNDDIE = 209,
  // Lich / Larva (type 6) — attack reuses idle, death=mLarva2
  SOUND_MONSTER_LARVA1 = 210,
  SOUND_MONSTER_LARVA2 = 211,
  // Mount hoofbeat sounds (Main 5.2: SOUND_RUN_DARK_HORSE_1/2/3)
  SOUND_MOUNT_STEP1 = 236,
  SOUND_MOUNT_STEP2 = 237,
  SOUND_MOUNT_STEP3 = 238,
  // Lich thunder attack (Main 5.2: Naipin-Thunder.wav) — 3D positional for Lich monster
  SOUND_LICH_THUNDER = 240,
  // Same sound, non-positional — for DW lightning bolt cast
  SOUND_LIGHTNING_CAST = 241,
  // Giant (type 7)
  SOUND_MONSTER_GIANT1 = 212,
  SOUND_MONSTER_GIANT2 = 213,
  SOUND_MONSTER_GIANTATTACK1 = 214,
  SOUND_MONSTER_GIANTATTACK2 = 215,
  SOUND_MONSTER_GIANTDIE = 216,
  // Skeleton bone sounds (Main 5.2: SOUND_BONE1/2, ZzzCharacter.cpp)
  SOUND_BONE1 = 123, // Skeleton idle/walk + attack
  SOUND_BONE2 = 124, // Skeleton death
  // Assassin (type 21, Devias) — kept for future use
  SOUND_MONSTER_ASSASSINATTACK1 = 218,
  SOUND_MONSTER_ASSASSINATTACK2 = 219,
  // Budge Dragon (type 2)
  SOUND_MONSTER_BUDGE1 = 220,
  SOUND_MONSTER_BUDGEATTACK1 = 221,
  SOUND_MONSTER_BUDGEDIE = 222,
  // Spider (type 3)
  SOUND_MONSTER_SPIDER1 = 223,
  SOUND_MONSTER_SPIDERDIE = 224,  // mHellSpiderDie.wav (distinct death screech)
  SOUND_MONSTER_ASSASSINDIE = 260,
  // Elite Bull Fighter / Wizard (type 4) — Main 5.2 uses mWizard sounds
  SOUND_MONSTER_WIZARD1 = 225,
  SOUND_MONSTER_WIZARD2 = 226,
  SOUND_MONSTER_WIZARDATTACK1 = 227,
  SOUND_MONSTER_WIZARDATTACK2 = 228,
  SOUND_MONSTER_WIZARDDIE = 229,
  // NPC sounds (Main 5.2: ZzzCharacter.cpp:5906-5926)
  SOUND_NPC_BLACKSMITH = 230,
  SOUND_NPC_HARP = 231,
  SOUND_NPC_MIX = 232,
  // Quest UI sound
  SOUND_QUEST_ACCEPT = 250,
  // Dungeon ambient (Main 5.2: aDungeon.wav — looping cave atmosphere)
  SOUND_DUNGEON01 = 251,
  // Dungeon critter: bat screech (Main 5.2: aBat.wav — 3D positional)
  SOUND_BAT01 = 252,
  // Ghost (type 11) — Main 5.2: SOUND_MONSTER + 35..39
  SOUND_MONSTER_GHOST1 = 245,
  SOUND_MONSTER_GHOST2 = 246,
  SOUND_MONSTER_GHOSTATTACK1 = 247,
  SOUND_MONSTER_GHOSTATTACK2 = 248,
  SOUND_MONSTER_GHOSTDIE = 249,
  // Cyclops/Ogre (type 17) — Main 5.2: SOUND_MONSTER + 40..44
  SOUND_MONSTER_OGRE1 = 253,
  SOUND_MONSTER_OGRE2 = 254,
  SOUND_MONSTER_OGREATTACK1 = 255,
  SOUND_MONSTER_OGREATTACK2 = 256,
  SOUND_MONSTER_OGREDIE = 257,
  // Gorgon (type 18) — Main 5.2: SOUND_MONSTER + 45..49
  SOUND_MONSTER_GORGON1 = 261,
  SOUND_MONSTER_GORGON2 = 262,
  SOUND_MONSTER_GORGONATTACK1 = 263,
  SOUND_MONSTER_GORGONATTACK2 = 264,
  SOUND_MONSTER_GORGONDIE = 265,
  // Shadow (type 13) — Main 5.2: SOUND_MONSTER + 113..117
  SOUND_MONSTER_SHADOW1 = 266,
  SOUND_MONSTER_SHADOW2 = 267,
  SOUND_MONSTER_SHADOWATTACK1 = 268,
  SOUND_MONSTER_SHADOWATTACK2 = 269,
  SOUND_MONSTER_SHADOWDIE = 270,
  // Dark Knight monster (type 10) — Main 5.2: mDarkKnight sounds
  SOUND_MONSTER_DARKKNIGHT1 = 271,
  SOUND_MONSTER_DARKKNIGHT2 = 272,
  // Devias monsters — Yeti (type 19) / Elite Yeti (type 20, reuses Yeti sounds)
  SOUND_MONSTER_YETI1 = 290,
  SOUND_MONSTER_YETI2 = 291,
  SOUND_MONSTER_YETIATTACK1 = 292,
  SOUND_MONSTER_YETIDIE = 293,
  // Assassin (type 21) — idle sound (attack/die already registered above)
  SOUND_MONSTER_ASSASSIN1 = 294,
  // Ice Monster (type 22)
  SOUND_MONSTER_ICEMONSTER1 = 295,
  SOUND_MONSTER_ICEMONSTER2 = 296,
  SOUND_MONSTER_ICEMONSTERDIE = 297,
  // Hommerd (type 23)
  SOUND_MONSTER_HOMMERD1 = 298,
  SOUND_MONSTER_HOMMERD2 = 299,
  SOUND_MONSTER_HOMMERDATTACK1 = 300,
  SOUND_MONSTER_HOMMERDDIE = 301,
  // Worm (type 24)
  SOUND_MONSTER_WORM1 = 302,
  SOUND_MONSTER_WORM2 = 303,
  SOUND_MONSTER_WORMDIE = 304,
  // Ice Queen (type 25)
  SOUND_MONSTER_ICEQUEEN1 = 305,
  SOUND_MONSTER_ICEQUEEN2 = 306,
  SOUND_MONSTER_ICEQUEENATTACK1 = 307,
  SOUND_MONSTER_ICEQUEENATTACK2 = 308,
  SOUND_MONSTER_ICEQUEENDIE = 309,
  // --- Noria monsters (types 26-33) ---
  // Goblin (type 26) + Elite Goblin (type 33)
  SOUND_MONSTER_GOBLIN1 = 310,
  SOUND_MONSTER_GOBLIN2 = 311,
  SOUND_MONSTER_GOBLINATTACK1 = 312,
  SOUND_MONSTER_GOBLINATTACK2 = 313,
  SOUND_MONSTER_GOBLINDIE = 314,
  // Chain Scorpion (type 27)
  SOUND_MONSTER_SCORPION1 = 315,
  SOUND_MONSTER_SCORPION2 = 316,
  SOUND_MONSTER_SCORPIONATTACK1 = 317,
  SOUND_MONSTER_SCORPIONATTACK2 = 318,
  SOUND_MONSTER_SCORPIONDIE = 319,
  // Beetle Monster (type 28)
  SOUND_MONSTER_BEETLE1 = 320,
  SOUND_MONSTER_BEETLEATTACK1 = 321,
  SOUND_MONSTER_BEETLEDIE = 322,
  // Hunter (type 29)
  SOUND_MONSTER_HUNTER1 = 323,
  SOUND_MONSTER_HUNTER2 = 324,
  SOUND_MONSTER_HUNTERATTACK1 = 325,
  SOUND_MONSTER_HUNTERATTACK2 = 326,
  SOUND_MONSTER_HUNTERDIE = 327,
  // Golem (type 32) — also used for Forest Monster (30) and Agon (31)
  SOUND_MONSTER_GOLEM1 = 328,
  SOUND_MONSTER_GOLEM2 = 329,
  SOUND_MONSTER_GOLEMATTACK1 = 330,
  SOUND_MONSTER_GOLEMATTACK2 = 331,
  SOUND_MONSTER_GOLEMDIE = 332,

  // Bali (type 150) — Elf summon
  SOUND_MONSTER_BALI1 = 333,
  SOUND_MONSTER_BALI2 = 334,
  SOUND_MONSTER_BALIATTACK1 = 335,
  SOUND_MONSTER_BALIATTACK2 = 336,

  // Door sounds (Main 5.2: aDoor.wav, aCastleDoor.wav)
  SOUND_DOOR01 = 280, // Swinging wood door
  SOUND_DOOR02 = 281, // Sliding castle door
  // Noria forest ambient (Main 5.2: aForest.wav — intermittent nature sounds)
  SOUND_FOREST01 = 340,
  // Dungeon critter: mouse/rat squeak (Main 5.2: aMouse.wav — 3D positional)
  SOUND_MOUSE01 = 341,
};

namespace SoundManager {
void Init(const std::string &dataPath);
void Shutdown();
void Play(int soundId, float gain = 1.0f);
void PlayPitched(int soundId, float minPitch, float maxPitch);
void Play3D(int soundId, float x, float y, float z);
void Play3DPitched(int soundId, float x, float y, float z, float minPitch,
                   float maxPitch);
void PlayLoop(int soundId);
void Play3DLoop(int soundId, float x, float y, float z, float gain = 1.0f);
void UpdateSource3D(int soundId, float x, float y, float z);
void Stop(int soundId);
void StopAll();
void SetMasterVolume(float vol); // 0.0 - 1.0
void UpdateListener(float x, float y, float z);
// Music (MP3) — one track at a time, looping
void PlayMusic(const std::string &filename);
void StopMusic();
void SetMusicVolume(float vol); // 0.0 - 1.0
bool IsMusicPlaying();
// Crossfade: fade out current track, then fade in new track
void CrossfadeTo(const std::string &filename, float fadeSeconds = 1.5f);
// Fade out current track and stop
void FadeOut(float fadeSeconds = 1.5f);
// Call every frame to process fade transitions
void UpdateMusic(float deltaTime);
} // namespace SoundManager

#endif // MU_SOUND_MANAGER_HPP

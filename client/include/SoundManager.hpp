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
  SOUND_JEWEL01 = 69,
  SOUND_HIT1 = 70,
  SOUND_HIT2 = 71,
  SOUND_HIT3 = 72,
  SOUND_HIT4 = 73,
  SOUND_HIT5 = 74,
  SOUND_LEVEL_UP = 83,
  // Player death (Main 5.2: SOUND_HUMAN_SCREAM04)
  SOUND_MALE_DIE = 108,
  // DK skill sounds
  SOUND_KNIGHT_SKILL1 = 112, // Falling Slash
  SOUND_KNIGHT_SKILL2 = 113, // Lunge
  SOUND_KNIGHT_SKILL3 = 114, // Uppercut
  SOUND_KNIGHT_SKILL4 = 115, // Cyclone
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
  SOUND_MONSTER_ASSASSINDIE = 224,
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
};

namespace SoundManager {
void Init(const std::string &dataPath);
void Shutdown();
void Play(int soundId);
void PlayPitched(int soundId, float minPitch, float maxPitch);
void Play3D(int soundId, float x, float y, float z);
void PlayLoop(int soundId);
void Stop(int soundId);
void StopAll();
void SetMasterVolume(float vol); // 0.0 - 1.0
void UpdateListener(float x, float y, float z);
// Music (MP3) — one track at a time, looping
void PlayMusic(const std::string &filename);
void StopMusic();
void SetMusicVolume(float vol); // 0.0 - 1.0
bool IsMusicPlaying();
} // namespace SoundManager

#endif // MU_SOUND_MANAGER_HPP

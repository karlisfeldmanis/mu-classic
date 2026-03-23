#include "SoundManager.hpp"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#include <al.h>
#include <alc.h>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace SoundManager {

static constexpr int MAX_CHANNELS = 4;

struct SoundSlot {
  ALuint buffer = 0;
  ALuint sources[MAX_CHANNELS] = {};
  int maxCh = 1;
  int nextCh = 0;
  bool positional = false; // 3D positional sound
};

static ALCdevice *s_device = nullptr;
static ALCcontext *s_context = nullptr;
static std::map<int, SoundSlot> s_sounds;
static std::map<int, std::string> s_soundNames; // ID → filename for logging
static float s_masterVolume = 1.0f;
static bool s_initialized = false;

// Per-sound throttle: prevent same sound from stacking when multiple hits land simultaneously
static constexpr float SOUND_THROTTLE_MS = 100.0f; // 100ms cooldown per sound ID
static std::map<int, std::chrono::steady_clock::time_point> s_lastPlayTime;

// ── Music state ──
static ALuint s_musicBuffer = 0;
static ALuint s_musicSource = 0;
static float s_musicVolume = 0.7f;
static std::string s_currentMusic; // Currently playing music file path

// ── Fade state ──
enum class FadeState { NONE, FADING_OUT, FADING_IN };
static FadeState s_fadeState = FadeState::NONE;
static float s_fadeTimer = 0.0f;
static float s_fadeDuration = 1.5f;
static std::string s_pendingTrack; // Track to play after fade-out (empty = just stop)

// ── WAV Loader ──

struct WAVData {
  std::vector<uint8_t> samples;
  uint32_t sampleRate = 0;
  uint16_t numChannels = 0;
  uint16_t bitsPerSample = 0;
};

static WAVData LoadWAV(const std::string &filename) {
  WAVData result;
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "[Sound] Failed to open: " << filename << std::endl;
    return result;
  }

  // Read RIFF header
  char riff[4];
  file.read(riff, 4);
  if (std::memcmp(riff, "RIFF", 4) != 0) {
    std::cerr << "[Sound] Not a RIFF file: " << filename << std::endl;
    return result;
  }

  uint32_t fileSize;
  file.read(reinterpret_cast<char *>(&fileSize), 4);

  char wave[4];
  file.read(wave, 4);
  if (std::memcmp(wave, "WAVE", 4) != 0) {
    std::cerr << "[Sound] Not a WAVE file: " << filename << std::endl;
    return result;
  }

  // Scan chunks for "fmt " and "data"
  bool gotFmt = false, gotData = false;
  uint16_t audioFormat = 0;

  while (file.good() && !file.eof()) {
    char chunkId[4];
    uint32_t chunkSize;
    file.read(chunkId, 4);
    file.read(reinterpret_cast<char *>(&chunkSize), 4);
    if (!file.good())
      break;

    if (std::memcmp(chunkId, "fmt ", 4) == 0) {
      file.read(reinterpret_cast<char *>(&audioFormat), 2);
      file.read(reinterpret_cast<char *>(&result.numChannels), 2);
      file.read(reinterpret_cast<char *>(&result.sampleRate), 4);
      uint32_t byteRate;
      uint16_t blockAlign;
      file.read(reinterpret_cast<char *>(&byteRate), 4);
      file.read(reinterpret_cast<char *>(&blockAlign), 2);
      file.read(reinterpret_cast<char *>(&result.bitsPerSample), 2);
      // Skip any extra fmt bytes
      if (chunkSize > 16)
        file.seekg(chunkSize - 16, std::ios::cur);
      gotFmt = true;
    } else if (std::memcmp(chunkId, "data", 4) == 0) {
      result.samples.resize(chunkSize);
      file.read(reinterpret_cast<char *>(result.samples.data()), chunkSize);
      gotData = true;
      break; // Got data, we're done
    } else {
      // Skip unknown chunk
      file.seekg(chunkSize, std::ios::cur);
    }
  }

  if (!gotFmt || !gotData) {
    std::cerr << "[Sound] Missing fmt/data chunks: " << filename << std::endl;
    result.samples.clear();
    return result;
  }

  if (audioFormat != 1) {
    std::cerr << "[Sound] Not PCM (format=" << audioFormat
              << "): " << filename << std::endl;
    result.samples.clear();
    return result;
  }

  return result;
}

// ── Internal helpers ──

static void LoadSound(int id, const std::string &filename, int maxChannels,
                      bool positional = false) {
  WAVData wav = LoadWAV(filename);
  if (wav.samples.empty())
    return;

  // OpenAL 3D positional audio requires MONO sources — stereo sources ignore
  // position and play at full volume regardless of distance. Downmix stereo
  // to mono at load time for positional sounds.
  if (positional && wav.numChannels == 2) {
    if (wav.bitsPerSample == 16) {
      // Stereo 16-bit → Mono 16-bit: average L+R channels
      const int16_t *stereo =
          reinterpret_cast<const int16_t *>(wav.samples.data());
      size_t numFrames = wav.samples.size() / 4; // 4 bytes per stereo frame
      std::vector<uint8_t> mono(numFrames * 2);
      int16_t *monoOut = reinterpret_cast<int16_t *>(mono.data());
      for (size_t i = 0; i < numFrames; i++)
        monoOut[i] = (int16_t)(((int)stereo[i * 2] + (int)stereo[i * 2 + 1]) / 2);
      wav.samples = std::move(mono);
      wav.numChannels = 1;
    } else if (wav.bitsPerSample == 8) {
      size_t numFrames = wav.samples.size() / 2;
      std::vector<uint8_t> mono(numFrames);
      for (size_t i = 0; i < numFrames; i++)
        mono[i] = (uint8_t)(((int)wav.samples[i * 2] + (int)wav.samples[i * 2 + 1]) / 2);
      wav.samples = std::move(mono);
      wav.numChannels = 1;
    }
  }

  ALenum format = AL_NONE;
  if (wav.numChannels == 1 && wav.bitsPerSample == 16)
    format = AL_FORMAT_MONO16;
  else if (wav.numChannels == 2 && wav.bitsPerSample == 16)
    format = AL_FORMAT_STEREO16;
  else if (wav.numChannels == 1 && wav.bitsPerSample == 8)
    format = AL_FORMAT_MONO8;
  else if (wav.numChannels == 2 && wav.bitsPerSample == 8)
    format = AL_FORMAT_STEREO8;
  else {
    std::cerr << "[Sound] Unsupported format (ch=" << wav.numChannels
              << " bits=" << wav.bitsPerSample << "): " << filename
              << std::endl;
    return;
  }

  SoundSlot slot;
  slot.maxCh = std::min(maxChannels, MAX_CHANNELS);
  slot.nextCh = 0;
  slot.positional = positional;

  alGenBuffers(1, &slot.buffer);
  alBufferData(slot.buffer, format, wav.samples.data(),
               (ALsizei)wav.samples.size(), (ALsizei)wav.sampleRate);

  alGenSources(slot.maxCh, slot.sources);
  for (int i = 0; i < slot.maxCh; i++) {
    alSourcei(slot.sources[i], AL_BUFFER, (ALint)slot.buffer);
    alSourcef(slot.sources[i], AL_GAIN, s_masterVolume);
    if (positional) {
      // World-space positioning with distance attenuation
      alSourcei(slot.sources[i], AL_SOURCE_RELATIVE, AL_FALSE);
      alSourcef(slot.sources[i], AL_REFERENCE_DISTANCE, 300.0f);
      alSourcef(slot.sources[i], AL_MAX_DISTANCE, 2000.0f);
      alSourcef(slot.sources[i], AL_ROLLOFF_FACTOR, 1.0f);
    } else {
      // Non-positional: always at listener (UI, player sounds)
      alSourcei(slot.sources[i], AL_SOURCE_RELATIVE, AL_TRUE);
      alSource3f(slot.sources[i], AL_POSITION, 0.0f, 0.0f, 0.0f);
    }
  }

  s_sounds[id] = slot;
  // Extract just the filename for logging
  auto pos = filename.find_last_of("/\\");
  s_soundNames[id] = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
}

// ── Public API ──

void Init(const std::string &dataPath) {
  s_device = alcOpenDevice(nullptr);
  if (!s_device) {
    std::cerr << "[Sound] Failed to open audio device" << std::endl;
    return;
  }

  s_context = alcCreateContext(s_device, nullptr);
  if (!s_context) {
    std::cerr << "[Sound] Failed to create audio context" << std::endl;
    alcCloseDevice(s_device);
    s_device = nullptr;
    return;
  }
  alcMakeContextCurrent(s_context);

  // 3D audio: inverse distance attenuation with clamping
  alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

  // Set listener defaults
  alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
  alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
  ALfloat orient[] = {0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
  alListenerfv(AL_ORIENTATION, orient);

  std::string sndPath = dataPath + "/Sound/";

  // Non-positional sounds (UI, player actions — always at listener)
  LoadSound(SOUND_WIND01, sndPath + "aWind.wav", 1);
  LoadSound(SOUND_DUNGEON01, sndPath + "aDungeon.wav", 1);
  LoadSound(SOUND_BAT01, sndPath + "aBat.wav", 1, true); // 3D positional
  LoadSound(SOUND_MOUSE01, sndPath + "aMouse.wav", 1, true); // 3D positional
  LoadSound(SOUND_FOREST01, sndPath + "aForest.wav", 1); // Noria forest ambient
  LoadSound(SOUND_DOOR01, sndPath + "aDoor.wav", 1);
  LoadSound(SOUND_DOOR02, sndPath + "aCastleDoor.wav", 1);
  LoadSound(SOUND_WALK_GRASS, sndPath + "pWalk(Grass).wav", 2);
  LoadSound(SOUND_WALK_SOIL, sndPath + "pWalk(Soil).wav", 2);
  LoadSound(SOUND_CLICK01, sndPath + "iButtonClick.wav", 1);
  LoadSound(SOUND_ERROR01, sndPath + "iButtonError.wav", 1);
  LoadSound(SOUND_MENU01, sndPath + "iButtonMove.wav", 2);
  LoadSound(SOUND_INTERFACE01, sndPath + "iCreateWindow.wav", 1);
  LoadSound(SOUND_QUEST_ACCEPT, sndPath + "iDuel_Window.wav", 1);
  LoadSound(SOUND_GET_ITEM01, sndPath + "pGetItem.wav", 1);
  LoadSound(SOUND_DROP_ITEM01, sndPath + "pDropItem.wav", 1);
  LoadSound(SOUND_DROP_GOLD01, sndPath + "pDropMoney.wav", 1);
  LoadSound(SOUND_DRINK01, sndPath + "pDrink.wav", 1);
  LoadSound(SOUND_EAT_APPLE01, sndPath + "pEatApple.wav", 1);
  LoadSound(SOUND_HEART, sndPath + "pHeartBeat.wav", 1);
  LoadSound(SOUND_SWING1, sndPath + "eSwingWeapon1.wav", 2);
  LoadSound(SOUND_SWING2, sndPath + "eSwingWeapon2.wav", 2);
  LoadSound(SOUND_SWING_LIGHT, sndPath + "eSwingLightSword.wav", 2);
  LoadSound(SOUND_HIT1, sndPath + "eMeleeHit1.wav", 2);
  LoadSound(SOUND_HIT2, sndPath + "eMeleeHit2.wav", 2);
  LoadSound(SOUND_HIT3, sndPath + "eMeleeHit3.wav", 2);
  LoadSound(SOUND_HIT4, sndPath + "eMeleeHit4.wav", 2);
  LoadSound(SOUND_HIT5, sndPath + "eMeleeHit5.wav", 2);
  LoadSound(SOUND_LEVEL_UP, sndPath + "pLevelUp.wav", 1);
  LoadSound(SOUND_MALE_SCREAM1, sndPath + "pMaleScream1.wav", 2);
  LoadSound(SOUND_MALE_SCREAM2, sndPath + "pMaleScream2.wav", 2);
  LoadSound(SOUND_MALE_SCREAM3, sndPath + "pMaleScream3.wav", 2);
  LoadSound(SOUND_MALE_DIE, sndPath + "pMaleDie.wav", 1);
  LoadSound(SOUND_FEMALE_SCREAM1, sndPath + "pFemaleScream1.wav", 2);
  LoadSound(SOUND_FEMALE_SCREAM2, sndPath + "pFemaleScream2.wav", 2);

  // Body blow / armor impact sounds (Main 5.2: SOUND_BLOW01-04)
  LoadSound(SOUND_BLOW1, sndPath + "eBlow1.wav", 2);
  LoadSound(SOUND_BLOW2, sndPath + "eBlow2.wav", 2);
  LoadSound(SOUND_BLOW3, sndPath + "eBlow3.wav", 2);
  LoadSound(SOUND_BLOW4, sndPath + "eBlow4.wav", 2);

  // Missile/spell impact sounds
  LoadSound(SOUND_MISSILE_HIT1, sndPath + "eMissileHit1.wav", 2);
  LoadSound(SOUND_MISSILE_HIT2, sndPath + "eMissileHit2.wav", 2);
  LoadSound(SOUND_MISSILE_HIT3, sndPath + "eMissileHit3.wav", 2);
  LoadSound(SOUND_MISSILE_HIT4, sndPath + "eMissileHit4.wav", 2);

  // Teleport / Gem sounds
  LoadSound(SOUND_SUMMON, sndPath + "eSummon.wav", 1);
  LoadSound(SOUND_JEWEL01, sndPath + "eGem.wav", 1);

  // Main 5.2: SOUND_THUNDER01 — lightning spell cast/impact
  LoadSound(SOUND_THUNDER01, sndPath + "eThunder.wav", 2);

  // Lich thunder attack (3D positional for Lich monster)
  LoadSound(SOUND_LICH_THUNDER, sndPath + "w57/Naipin-Thunder.wav", 2, true);
  // Same sound, non-positional — for DW lightning bolt cast
  LoadSound(SOUND_LIGHTNING_CAST, sndPath + "w57/Naipin-Thunder.wav", 2);

  // Skeleton bone sounds (3D positional)
  LoadSound(SOUND_BONE1, sndPath + "mBone1.wav", 2, true);
  LoadSound(SOUND_BONE2, sndPath + "mBone2.wav", 2, true);

  // Mount hoofbeat sounds (Main 5.2: pHorseStep 1-3)
  LoadSound(SOUND_MOUNT_STEP1, sndPath + "pHorseStep1.wav", 2);
  LoadSound(SOUND_MOUNT_STEP2, sndPath + "pHorseStep2.wav", 2);
  LoadSound(SOUND_MOUNT_STEP3, sndPath + "pHorseStep3.wav", 2);

  // DK skill sounds
  LoadSound(SOUND_KNIGHT_SKILL1, sndPath + "sKnightSkill1.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL2, sndPath + "sKnightSkill2.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL3, sndPath + "sKnightSkill3.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL4, sndPath + "sKnightSkill4.wav", 2);
  LoadSound(SOUND_RAGE_BLOW1, sndPath + "eRageBlow_1.wav", 2);
  LoadSound(SOUND_RAGE_BLOW2, sndPath + "eRageBlow_2.wav", 2);
  LoadSound(SOUND_RAGE_BLOW3, sndPath + "eRageBlow_3.wav", 2);

  // Soul Barrier cast sound (Main 5.2: eSoulBarrier.wav — used for Elf buff auras)
  LoadSound(SOUND_SOULBARRIER, sndPath + "eSoulBarrier.wav", 2);

  // DW spell sounds (Main 5.2: ZzzCharacter.cpp PlayBuffer calls)
  LoadSound(SOUND_METEORITE01, sndPath + "eMeteorite.wav", 2);
  LoadSound(SOUND_STORM, sndPath + "sTornado.wav", 2);
  LoadSound(SOUND_EVIL, sndPath + "sEvil.wav", 2);
  LoadSound(SOUND_MAGIC, sndPath + "sMagic.wav", 2);
  LoadSound(SOUND_HELLFIRE, sndPath + "sHellFire.wav", 2);
  LoadSound(SOUND_ICE, sndPath + "sIce.wav", 2);
  LoadSound(SOUND_FLAME, sndPath + "sFlame.wav", 2);
  LoadSound(SOUND_FLASH, sndPath + "sAquaFlash.wav", 2);

  // 3D positional sounds (monsters, ambient creatures)
  LoadSound(SOUND_BIRD01, sndPath + "aBird1.wav", 1, true);
  LoadSound(SOUND_BIRD02, sndPath + "aBird2.wav", 1, true);
  // Bull Fighter (type 0)
  LoadSound(SOUND_MONSTER_BULL1, sndPath + "mBull1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BULL2, sndPath + "mBull2.wav", 2, true);
  LoadSound(SOUND_MONSTER_BULLATTACK1, sndPath + "mBullAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BULLATTACK2, sndPath + "mBullAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_BULLDIE, sndPath + "mBullDie.wav", 2, true);
  // Hound (type 1)
  LoadSound(SOUND_MONSTER_HOUND1, sndPath + "mHound1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOUND2, sndPath + "mHound2.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOUNDATTACK1, sndPath + "mHoundAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOUNDATTACK2, sndPath + "mHoundAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOUNDDIE, sndPath + "mHoundDie.wav", 2, true);
  // Budge Dragon (type 2)
  LoadSound(SOUND_MONSTER_BUDGE1, sndPath + "mBudge1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BUDGEATTACK1, sndPath + "mBudgeAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BUDGEDIE, sndPath + "mBudgeDie.wav", 2, true);
  // Spider (type 3)
  LoadSound(SOUND_MONSTER_SPIDER1, sndPath + "mSpider1.wav", 2, true);
  LoadSound(SOUND_MONSTER_SPIDERDIE, sndPath + "mHellSpiderDie.wav", 2, true);
  // Elite Bull Fighter / Wizard (type 4) — Main 5.2 uses mWizard sounds
  LoadSound(SOUND_MONSTER_WIZARD1, sndPath + "mWizard1.wav", 2, true);
  LoadSound(SOUND_MONSTER_WIZARD2, sndPath + "mWizard2.wav", 2, true);
  LoadSound(SOUND_MONSTER_WIZARDATTACK1, sndPath + "mWizardAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_WIZARDATTACK2, sndPath + "mWizardAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_WIZARDDIE, sndPath + "mWizardDie.wav", 2, true);
  // Lich / Larva (type 6) — attack reuses idle sounds, death=mLarva2
  LoadSound(SOUND_MONSTER_LARVA1, sndPath + "mLarva1.wav", 2, true);
  LoadSound(SOUND_MONSTER_LARVA2, sndPath + "mLarva2.wav", 2, true);
  // Giant (type 7)
  LoadSound(SOUND_MONSTER_GIANT1, sndPath + "mGiant1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GIANT2, sndPath + "mGiant2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GIANTATTACK1, sndPath + "mGiantAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GIANTATTACK2, sndPath + "mGiantAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GIANTDIE, sndPath + "mGiantDie.wav", 2, true);
  // Skeleton / Assassin (type 14) — no idle sounds in Main 5.2
  LoadSound(SOUND_MONSTER_ASSASSINATTACK1, sndPath + "mAssassinAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_ASSASSINATTACK2, sndPath + "mAssassinAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_ASSASSINDIE, sndPath + "mAssassinDie.wav", 2, true);
  // Ghost (type 11) — Main 5.2: mGhost sounds
  LoadSound(SOUND_MONSTER_GHOST1, sndPath + "mGhost1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GHOST2, sndPath + "mGhost2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GHOSTATTACK1, sndPath + "mGhostAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GHOSTATTACK2, sndPath + "mGhostAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GHOSTDIE, sndPath + "mGhostDie.wav", 2, true);
  // Cyclops/Ogre (type 17) — Main 5.2: mOgre sounds
  LoadSound(SOUND_MONSTER_OGRE1, sndPath + "mOgre1.wav", 2, true);
  LoadSound(SOUND_MONSTER_OGRE2, sndPath + "mOgre2.wav", 2, true);
  LoadSound(SOUND_MONSTER_OGREATTACK1, sndPath + "mOgreAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_OGREATTACK2, sndPath + "mOgreAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_OGREDIE, sndPath + "mOgreDie.wav", 2, true);
  // Gorgon (type 18) — Main 5.2: mGorgon sounds
  LoadSound(SOUND_MONSTER_GORGON1, sndPath + "mGorgon1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GORGON2, sndPath + "mGorgon2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GORGONATTACK1, sndPath + "mGorgonAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GORGONATTACK2, sndPath + "mGorgonAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GORGONDIE, sndPath + "mGorgonDie.wav", 2, true);
  // Shadow (type 13) — Main 5.2: mShadow sounds (Hell Spider reuses model)
  LoadSound(SOUND_MONSTER_SHADOW1, sndPath + "mShadow1.wav", 2, true);
  LoadSound(SOUND_MONSTER_SHADOW2, sndPath + "mShadow2.wav", 2, true);
  LoadSound(SOUND_MONSTER_SHADOWATTACK1, sndPath + "mShadowAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_SHADOWATTACK2, sndPath + "mShadowAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_SHADOWDIE, sndPath + "mShadowDie.wav", 2, true);
  // Dark Knight monster (type 10) — Main 5.2: mDarkKnight sounds
  LoadSound(SOUND_MONSTER_DARKKNIGHT1, sndPath + "mDarkKnight1.wav", 2, true);
  LoadSound(SOUND_MONSTER_DARKKNIGHT2, sndPath + "mDarkKnight2.wav", 2, true);
  // Devias monsters
  // Yeti (type 19) / Elite Yeti (type 20, reuses Yeti sounds)
  LoadSound(SOUND_MONSTER_YETI1, sndPath + "mYeti1.wav", 2, true);
  LoadSound(SOUND_MONSTER_YETI2, sndPath + "mYeti2.wav", 2, true);
  LoadSound(SOUND_MONSTER_YETIATTACK1, sndPath + "mYetiAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_YETIDIE, sndPath + "mYetiDie.wav", 2, true);
  // Assassin (type 21) — idle sound
  LoadSound(SOUND_MONSTER_ASSASSIN1, sndPath + "mAssassin1.wav", 2, true);
  // Ice Monster (type 22)
  LoadSound(SOUND_MONSTER_ICEMONSTER1, sndPath + "mIceMonster1.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEMONSTER2, sndPath + "mIceMonster2.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEMONSTERDIE, sndPath + "mIceMonsterDie.wav", 2, true);
  // Hommerd (type 23)
  LoadSound(SOUND_MONSTER_HOMMERD1, sndPath + "mHomord1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOMMERD2, sndPath + "mHomord2.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOMMERDATTACK1, sndPath + "mHomordAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HOMMERDDIE, sndPath + "mHomordDie.wav", 2, true);
  // Worm (type 24)
  LoadSound(SOUND_MONSTER_WORM1, sndPath + "mWorm1.wav", 2, true);
  LoadSound(SOUND_MONSTER_WORM2, sndPath + "mWorm2.wav", 2, true);
  LoadSound(SOUND_MONSTER_WORMDIE, sndPath + "mWormDie.wav", 2, true);
  // Ice Queen (type 25)
  LoadSound(SOUND_MONSTER_ICEQUEEN1, sndPath + "mIceQueen1.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEQUEEN2, sndPath + "mIceQueen2.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEQUEENATTACK1, sndPath + "mIceQueenAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEQUEENATTACK2, sndPath + "mIceQueenAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_ICEQUEENDIE, sndPath + "mIceQueenDie.wav", 2, true);
  // --- Noria monsters (types 26-33) ---
  // Goblin (type 26) + Elite Goblin (type 33)
  LoadSound(SOUND_MONSTER_GOBLIN1, sndPath + "mGoblin1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOBLIN2, sndPath + "mGoblin2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOBLINATTACK1, sndPath + "mGoblinAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOBLINATTACK2, sndPath + "mGoblinAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOBLINDIE, sndPath + "mGoblinDie.wav", 2, true);
  // Chain Scorpion (type 27)
  LoadSound(SOUND_MONSTER_SCORPION1, sndPath + "mScorpion1.wav", 2, true);
  LoadSound(SOUND_MONSTER_SCORPION2, sndPath + "mScorpion2.wav", 2, true);
  LoadSound(SOUND_MONSTER_SCORPIONATTACK1, sndPath + "mScorpionAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_SCORPIONATTACK2, sndPath + "mScorpionAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_SCORPIONDIE, sndPath + "mScorpionDie.wav", 2, true);
  // Beetle Monster (type 28)
  LoadSound(SOUND_MONSTER_BEETLE1, sndPath + "mBeetle1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BEETLEATTACK1, sndPath + "mBeetleAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BEETLEDIE, sndPath + "mBeetleDie.wav", 2, true);
  // Hunter (type 29)
  LoadSound(SOUND_MONSTER_HUNTER1, sndPath + "mHunter1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HUNTER2, sndPath + "mHunter2.wav", 2, true);
  LoadSound(SOUND_MONSTER_HUNTERATTACK1, sndPath + "mHunterAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_HUNTERATTACK2, sndPath + "mHunterAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_HUNTERDIE, sndPath + "mHunterDie.wav", 2, true);
  // Stone Golem (type 32) — also used for Forest Monster (30) and Agon (31)
  LoadSound(SOUND_MONSTER_GOLEM1, sndPath + "mGolem1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOLEM2, sndPath + "mGolem2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOLEMATTACK1, sndPath + "mGolemAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOLEMATTACK2, sndPath + "mGolemAttack2.wav", 2, true);
  LoadSound(SOUND_MONSTER_GOLEMDIE, sndPath + "mGolemDie.wav", 2, true);

  // Bali (type 150) — Elf summon
  LoadSound(SOUND_MONSTER_BALI1, sndPath + "mBali1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BALI2, sndPath + "mBali2.wav", 2, true);
  LoadSound(SOUND_MONSTER_BALIATTACK1, sndPath + "mBaliAttack1.wav", 2, true);
  LoadSound(SOUND_MONSTER_BALIATTACK2, sndPath + "mBaliAttack2.wav", 2, true);

  // Bow/crossbow release sounds
  LoadSound(SOUND_BOW, sndPath + "eBow.wav", 3, false);
  LoadSound(SOUND_CROSSBOW, sndPath + "eCrossbow.wav", 3, false);

  // NPC sounds (3D positional)
  LoadSound(SOUND_NPC_BLACKSMITH, sndPath + "nBlackSmith.wav", 1, true);
  LoadSound(SOUND_NPC_HARP, sndPath + "nHarp.wav", 1, true);
  LoadSound(SOUND_NPC_MIX, sndPath + "nMix.wav", 1, true);

  s_initialized = true;
  std::cout << "[Sound] Initialized (" << s_sounds.size() << " sounds loaded)"
            << std::endl;
}

void Shutdown() {
  if (!s_initialized)
    return;

  StopMusic();

  for (auto &[id, slot] : s_sounds) {
    alDeleteSources(slot.maxCh, slot.sources);
    alDeleteBuffers(1, &slot.buffer);
  }
  s_sounds.clear();

  if (s_context) {
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(s_context);
    s_context = nullptr;
  }
  if (s_device) {
    alcCloseDevice(s_device);
    s_device = nullptr;
  }
  s_initialized = false;
  std::cout << "[Sound] Shutdown" << std::endl;
}

void Play(int soundId, float gain) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  // Throttle: skip if same sound played within cooldown window
  auto now = std::chrono::steady_clock::now();
  auto &lastTime = s_lastPlayTime[soundId];
  float elapsedMs = std::chrono::duration<float, std::milli>(now - lastTime).count();
  if (elapsedMs < SOUND_THROTTLE_MS && lastTime.time_since_epoch().count() > 0)
    return;
  lastTime = now;

  auto &slot = it->second;
  ALuint src = slot.sources[slot.nextCh];
  alSourcef(src, AL_PITCH, 1.0f);
  alSourcei(src, AL_LOOPING, AL_FALSE);
  alSourcef(src, AL_GAIN, s_masterVolume * gain);
  alSourcePlay(src);
  slot.nextCh = (slot.nextCh + 1) % slot.maxCh;
  auto nm = s_soundNames.find(soundId);
  std::cout << "[Sound] Play: " << (nm != s_soundNames.end() ? nm->second : "?")
            << " (id=" << soundId << ")" << std::endl;
}

void PlayPitched(int soundId, float minPitch, float maxPitch) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[slot.nextCh];
  float pitch = minPitch + (float)rand() / (float)RAND_MAX * (maxPitch - minPitch);
  alSourcef(src, AL_PITCH, pitch);
  alSourcei(src, AL_LOOPING, AL_FALSE);
  alSourcef(src, AL_GAIN, s_masterVolume);
  alSourcePlay(src);
  slot.nextCh = (slot.nextCh + 1) % slot.maxCh;
}

void PlayLoop(int soundId) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[0];
  alSourcei(src, AL_LOOPING, AL_TRUE);
  alSourcef(src, AL_GAIN, s_masterVolume);
  alSourcePlay(src);
  auto nm = s_soundNames.find(soundId);
  std::cout << "[Sound] PlayLoop: " << (nm != s_soundNames.end() ? nm->second : "?")
            << " (id=" << soundId << ")" << std::endl;
}

void Play3DLoop(int soundId, float x, float y, float z, float gain) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[0];
  alSource3f(src, AL_POSITION, x, y, z);
  alSourcef(src, AL_PITCH, 1.0f);
  alSourcei(src, AL_LOOPING, AL_TRUE);
  alSourcef(src, AL_GAIN, s_masterVolume * gain);
  alSourcePlay(src);
}

void UpdateSource3D(int soundId, float x, float y, float z) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  ALuint src = it->second.sources[0];
  alSource3f(src, AL_POSITION, x, y, z);
}

void Stop(int soundId) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  for (int i = 0; i < it->second.maxCh; i++)
    alSourceStop(it->second.sources[i]);
}

void StopAll() {
  if (!s_initialized)
    return;
  for (auto &[id, slot] : s_sounds)
    for (int i = 0; i < slot.maxCh; i++)
      alSourceStop(slot.sources[i]);
}

void Play3D(int soundId, float x, float y, float z) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[slot.nextCh];
  alSource3f(src, AL_POSITION, x, y, z);
  alSourcef(src, AL_PITCH, 1.0f);
  alSourcei(src, AL_LOOPING, AL_FALSE);
  alSourcef(src, AL_GAIN, s_masterVolume);
  alSourcePlay(src);
  slot.nextCh = (slot.nextCh + 1) % slot.maxCh;
  auto nm = s_soundNames.find(soundId);
  std::cout << "[Sound] Play3D: " << (nm != s_soundNames.end() ? nm->second : "?")
            << " (id=" << soundId << ") at (" << x << "," << y << "," << z << ")"
            << std::endl;
}

void Play3DPitched(int soundId, float x, float y, float z, float minPitch,
                   float maxPitch) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[slot.nextCh];
  float pitch =
      minPitch + (float)rand() / (float)RAND_MAX * (maxPitch - minPitch);
  alSource3f(src, AL_POSITION, x, y, z);
  alSourcef(src, AL_PITCH, pitch);
  alSourcei(src, AL_LOOPING, AL_FALSE);
  alSourcef(src, AL_GAIN, s_masterVolume);
  alSourcePlay(src);
  slot.nextCh = (slot.nextCh + 1) % slot.maxCh;
}

void UpdateListener(float x, float y, float z) {
  if (!s_initialized)
    return;
  alListener3f(AL_POSITION, x, y, z);
}

void SetMasterVolume(float vol) {
  s_masterVolume = vol;
  if (!s_initialized)
    return;
  for (auto &[id, slot] : s_sounds)
    for (int i = 0; i < slot.maxCh; i++)
      alSourcef(slot.sources[i], AL_GAIN, vol);
}

// ── Music (MP3) ──

void PlayMusic(const std::string &filename) {
  if (!s_initialized)
    return;

  // Don't restart if same track is already playing
  if (s_currentMusic == filename && s_musicSource) {
    ALint state;
    alGetSourcei(s_musicSource, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING)
      return;
  }

  StopMusic();

  // Decode MP3 to PCM using minimp3
  mp3dec_t mp3d;
  mp3dec_file_info_t info;
  mp3dec_init(&mp3d);

  if (mp3dec_load(&mp3d, filename.c_str(), &info, nullptr, nullptr)) {
    std::cerr << "[Music] Failed to decode: " << filename << std::endl;
    return;
  }

  if (info.samples == 0 || !info.buffer) {
    std::cerr << "[Music] Empty MP3: " << filename << std::endl;
    if (info.buffer)
      free(info.buffer);
    return;
  }

  ALenum format = (info.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
  ALsizei dataSize = (ALsizei)(info.samples * sizeof(mp3d_sample_t));

  alGenBuffers(1, &s_musicBuffer);
  alBufferData(s_musicBuffer, format, info.buffer, dataSize, info.hz);
  free(info.buffer);

  alGenSources(1, &s_musicSource);
  alSourcei(s_musicSource, AL_BUFFER, (ALint)s_musicBuffer);
  alSourcei(s_musicSource, AL_LOOPING, AL_TRUE);
  alSourcef(s_musicSource, AL_GAIN, s_musicVolume);
  alSourcei(s_musicSource, AL_SOURCE_RELATIVE, AL_TRUE);
  alSource3f(s_musicSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
  alSourcePlay(s_musicSource);

  s_currentMusic = filename;
  auto pos = filename.find_last_of("/\\");
  std::string name = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
  std::cout << "[Music] Playing: " << name << std::endl;
}

void StopMusic() {
  if (!s_initialized)
    return;

  if (s_musicSource) {
    alSourceStop(s_musicSource);
    alDeleteSources(1, &s_musicSource);
    s_musicSource = 0;
  }
  if (s_musicBuffer) {
    alDeleteBuffers(1, &s_musicBuffer);
    s_musicBuffer = 0;
  }
  if (!s_currentMusic.empty()) {
    std::cout << "[Music] Stopped" << std::endl;
    s_currentMusic.clear();
  }
}

void SetMusicVolume(float vol) {
  s_musicVolume = vol;
  if (s_initialized && s_musicSource)
    alSourcef(s_musicSource, AL_GAIN, vol);
}

bool IsMusicPlaying() {
  if (!s_initialized || !s_musicSource)
    return false;
  ALint state;
  alGetSourcei(s_musicSource, AL_SOURCE_STATE, &state);
  return state == AL_PLAYING;
}

// ── Crossfade / Fade ──

void CrossfadeTo(const std::string &filename, float fadeSeconds) {
  if (!s_initialized)
    return;

  // Already playing this track — skip
  if (s_currentMusic == filename && IsMusicPlaying())
    return;

  // If no music playing, just start directly with fade-in
  if (!IsMusicPlaying()) {
    PlayMusic(filename);
    // Start at 0 volume and fade in
    alSourcef(s_musicSource, AL_GAIN, 0.0f);
    s_fadeState = FadeState::FADING_IN;
    s_fadeTimer = 0.0f;
    s_fadeDuration = fadeSeconds;
    s_pendingTrack.clear();
    return;
  }

  // Fade out current, then play new
  s_pendingTrack = filename;
  s_fadeState = FadeState::FADING_OUT;
  s_fadeTimer = 0.0f;
  s_fadeDuration = fadeSeconds;
}

void FadeOut(float fadeSeconds) {
  if (!s_initialized || !IsMusicPlaying())
    return;

  s_pendingTrack.clear(); // No next track — just stop
  s_fadeState = FadeState::FADING_OUT;
  s_fadeTimer = 0.0f;
  s_fadeDuration = fadeSeconds;
}

void UpdateMusic(float deltaTime) {
  if (!s_initialized || s_fadeState == FadeState::NONE)
    return;

  s_fadeTimer += deltaTime;
  float t = (s_fadeDuration > 0.0f) ? (s_fadeTimer / s_fadeDuration) : 1.0f;
  if (t > 1.0f)
    t = 1.0f;

  if (s_fadeState == FadeState::FADING_OUT) {
    float vol = s_musicVolume * (1.0f - t);
    if (s_musicSource)
      alSourcef(s_musicSource, AL_GAIN, vol);

    if (t >= 1.0f) {
      // Fade-out complete
      if (s_pendingTrack.empty()) {
        // Just stop
        StopMusic();
        s_fadeState = FadeState::NONE;
      } else {
        // Start the pending track with fade-in
        std::string track = s_pendingTrack;
        s_pendingTrack.clear();
        StopMusic();
        PlayMusic(track);
        alSourcef(s_musicSource, AL_GAIN, 0.0f);
        s_fadeState = FadeState::FADING_IN;
        s_fadeTimer = 0.0f;
      }
    }
  } else if (s_fadeState == FadeState::FADING_IN) {
    float vol = s_musicVolume * t;
    if (s_musicSource)
      alSourcef(s_musicSource, AL_GAIN, vol);

    if (t >= 1.0f) {
      s_fadeState = FadeState::NONE;
    }
  }
}

} // namespace SoundManager

#include "SoundManager.hpp"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#include <al.h>
#include <alc.h>
#include <cstdint>
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

// ── Music state ──
static ALuint s_musicBuffer = 0;
static ALuint s_musicSource = 0;
static float s_musicVolume = 0.7f;
static std::string s_currentMusic; // Currently playing music file path

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
  LoadSound(SOUND_WALK_GRASS, sndPath + "pWalk(Grass).wav", 2);
  LoadSound(SOUND_WALK_SOIL, sndPath + "pWalk(Soil).wav", 2);
  LoadSound(SOUND_CLICK01, sndPath + "iButtonClick.wav", 1);
  LoadSound(SOUND_ERROR01, sndPath + "iButtonError.wav", 1);
  LoadSound(SOUND_MENU01, sndPath + "iButtonMove.wav", 2);
  LoadSound(SOUND_INTERFACE01, sndPath + "iCreateWindow.wav", 1);
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
  LoadSound(SOUND_MALE_DIE, sndPath + "pMaleDie.wav", 1);

  // Teleport / Gem sounds
  LoadSound(SOUND_SUMMON, sndPath + "eSummon.wav", 1);
  LoadSound(SOUND_JEWEL01, sndPath + "eGem.wav", 1);

  // Skeleton bone sounds (3D positional)
  LoadSound(SOUND_BONE1, sndPath + "mBone1.wav", 2, true);
  LoadSound(SOUND_BONE2, sndPath + "mBone2.wav", 2, true);

  // DK skill sounds
  LoadSound(SOUND_KNIGHT_SKILL1, sndPath + "sKnightSkill1.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL2, sndPath + "sKnightSkill2.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL3, sndPath + "sKnightSkill3.wav", 2);
  LoadSound(SOUND_KNIGHT_SKILL4, sndPath + "sKnightSkill4.wav", 2);
  LoadSound(SOUND_RAGE_BLOW1, sndPath + "eRageBlow_1.wav", 2);
  LoadSound(SOUND_RAGE_BLOW2, sndPath + "eRageBlow_2.wav", 2);
  LoadSound(SOUND_RAGE_BLOW3, sndPath + "eRageBlow_3.wav", 2);

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
  // Spider (type 3) — all states use mSpider1.wav in Main 5.2
  LoadSound(SOUND_MONSTER_SPIDER1, sndPath + "mSpider1.wav", 2, true);
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

void Play(int soundId) {
  if (!s_initialized)
    return;
  auto it = s_sounds.find(soundId);
  if (it == s_sounds.end())
    return;

  auto &slot = it->second;
  ALuint src = slot.sources[slot.nextCh];
  alSourcef(src, AL_PITCH, 1.0f);
  alSourcei(src, AL_LOOPING, AL_FALSE);
  alSourcef(src, AL_GAIN, s_masterVolume);
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

} // namespace SoundManager

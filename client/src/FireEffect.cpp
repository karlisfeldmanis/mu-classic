#include "FireEffect.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// --- Fire offset table (MU local coordinates from ZzzObject.cpp) ---

static const std::vector<glm::vec3> kNoOffsets;

static const std::vector<glm::vec3> kFireLight01Offsets = {
    glm::vec3(0.0f, 0.0f, 200.0f)};

static const std::vector<glm::vec3> kFireLight02Offsets = {
    glm::vec3(0.0f, -30.0f, 60.0f)};

static const std::vector<glm::vec3> kBonfireOffsets = {
    glm::vec3(0.0f, 0.0f, 60.0f)};

static const std::vector<glm::vec3> kDungeonGateOffsets = {
    glm::vec3(-150.0f, -150.0f, 140.0f), glm::vec3(150.0f, -150.0f, 140.0f)};

static const std::vector<glm::vec3> kBridgeOffsets = {
    glm::vec3(90.0f, -200.0f, 30.0f), glm::vec3(90.0f, 200.0f, 30.0f)};

static const std::vector<glm::vec3> kLight01Offsets = {
    glm::vec3(0.0f, 0.0f, 0.0f)};

// Dungeon torches (Main 5.2: ZzzObject.cpp case WD_1DUNGEON)
// Type 41: tall fire stand  — CreateFire(0, o, 0, -30, 240)
// Type 42: standard lantern — CreateFire(0, o, 0, 0, 190)
static const std::vector<glm::vec3> kDungeonTorch41Offsets = {
    glm::vec3(0.0f, -30.0f, 240.0f)};
static const std::vector<glm::vec3> kDungeonTorch42Offsets = {
    glm::vec3(0.0f, 0.0f, 190.0f)};

// Devias fireplaces (Main 5.2: ZzzObject.cpp WD_2DEVIAS)
// Type 30 (Stone01): fireplace — fire+smoke at z+160 (BITMAP_TRUE_FIRE particles)
// Type 66 (SteelWall02): wall fire — CreateFire(0, o, 0, 0, 50)
static const std::vector<glm::vec3> kDeviasFireplaceOffsets = {
    glm::vec3(0.0f, 0.0f, 160.0f)};
static const std::vector<glm::vec3> kDeviasWallFireOffsets = {
    glm::vec3(0.0f, 0.0f, 50.0f)};

const std::vector<glm::vec3> &GetFireOffsets(int objectType, int mapId) {
  switch (objectType) {
  case 30: // Devias fireplace only (Lorencia type 30 = Stone01, no fire)
    return (mapId == 2) ? kDeviasFireplaceOffsets : kNoOffsets;
  case 41: // Dungeon torches
    return (mapId == 1) ? kDungeonTorch41Offsets : kNoOffsets;
  case 42:
    return (mapId == 1) ? kDungeonTorch42Offsets : kNoOffsets;
  case 50: // FireLight01 — Lorencia only (Devias type 50 is a different model)
    return (mapId == 0) ? kFireLight01Offsets : kNoOffsets;
  case 51: // FireLight02 — Lorencia only
    return (mapId == 0) ? kFireLight02Offsets : kNoOffsets;
  case 52: // Bonfire01 — Lorencia only
    return (mapId == 0) ? kBonfireOffsets : kNoOffsets;
  case 55: // DoungeonGate01 — Lorencia only (dungeon gates use separate VFX)
    return (mapId == 0) ? kDungeonGateOffsets : kNoOffsets;
  case 66: // Devias wall fire only (Lorencia type 66 = different object)
    return (mapId == 2) ? kDeviasWallFireOffsets : kNoOffsets;
  case 80: // Bridge01 — Lorencia only
    return (mapId == 0) ? kBridgeOffsets : kNoOffsets;
  case 130: // Light01 — Lorencia only
    return (mapId == 0) ? kLight01Offsets : kNoOffsets;
  default:
    return kNoOffsets;
  }
}

// Smoke offset table (Main 5.2: ZzzObject.cpp — CreateFire type 1/2)
// Type 131 = Light02 (torch smoke), Type 132 = Light03 (smoke variant)
static const std::vector<glm::vec3> kSmokeTorchOffsets = {
    glm::vec3(0.0f, 0.0f, 0.0f)};


const std::vector<glm::vec3> &GetSmokeOffsets(int objectType, int mapId) {
  switch (objectType) {
  case 30: // Devias fireplace smoke (Main 5.2: BITMAP_SMOKE subtype 21 at z+160)
    return (mapId == 2) ? kDeviasFireplaceOffsets : kNoOffsets;
  case 131: // Light02 torch smoke — Lorencia only
  case 132: // Light03 smoke variant — Lorencia only
    return (mapId == 0) ? kSmokeTorchOffsets : kNoOffsets;
  default:
    return kNoOffsets;
  }
}

int GetFireTypeFromFilename(const std::string &bmdFilename) {
  if (bmdFilename == "FireLight01.bmd")
    return 50;
  if (bmdFilename == "FireLight02.bmd")
    return 51;
  if (bmdFilename == "Bonfire01.bmd")
    return 52;
  if (bmdFilename == "DoungeonGate01.bmd")
    return 55;
  if (bmdFilename == "Bridge01.bmd")
    return 80;
  if (bmdFilename == "Light01.bmd")
    return 130;
  if (bmdFilename == "Light02.bmd")
    return 131;
  if (bmdFilename == "Light03.bmd")
    return 132;
  return -1;
}

// --- Random helpers ---

static float RandFloat(float lo, float hi) {
  return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
}

// --- FireEffect implementation ---

static bgfx::VertexLayout s_quadLayout;
static bool s_quadLayoutInit = false;

void FireEffect::Init(const std::string &effectDataPath) {
  // Load fire sprite sheet texture
  std::string firePath = effectDataPath + "/Fire01.OZJ";
  fireTexture = TextureLoader::LoadOZJ(firePath);
  if (!TexValid(fireTexture)) {
    std::cerr << "[FireEffect] Failed to load fire texture: " << firePath
              << std::endl;
    return;
  }
  std::cout << "[FireEffect] Loaded fire texture: " << firePath << std::endl;

  // Load smoke texture
  std::string smokePath = effectDataPath + "/smoke01.OZJ";
  waterTexture = TextureLoader::LoadOZJ(smokePath);
  if (TexValid(waterTexture))
    std::cout << "[FireEffect] Loaded smoke texture: " << smokePath << std::endl;
  else
    std::cerr << "[FireEffect] Failed to load smoke texture: " << smokePath << std::endl;

  // Load billboard shader
  billboardShader = Shader::Load("vs_billboard.bin", "fs_billboard.bin");
  if (!billboardShader)
    std::cerr << "[FireEffect] Failed to load billboard shader" << std::endl;

  // Initialize vertex layout (vec3 position, z=0)
  if (!s_quadLayoutInit) {
    s_quadLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    s_quadLayoutInit = true;
  }

  // Static quad: 4 corners at +/-0.5 (z=0)
  float quadVerts[] = {
      -0.5f, -0.5f, 0.0f,
       0.5f, -0.5f, 0.0f,
       0.5f,  0.5f, 0.0f,
      -0.5f,  0.5f, 0.0f,
  };
  uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

  quadVBO = bgfx::createVertexBuffer(
      bgfx::copy(quadVerts, sizeof(quadVerts)), s_quadLayout);
  quadEBO = bgfx::createIndexBuffer(
      bgfx::copy(quadIndices, sizeof(quadIndices)));
}

void FireEffect::ClearEmitters() {
  emitters.clear();
  particles.clear();
}

void FireEffect::AddEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f); // stagger initial spawns
  e.smoke = false;
  emitters.push_back(e);
}

void FireEffect::AddSmokeEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f);
  e.smoke = true;
  emitters.push_back(e);
}

void FireEffect::AddWaterSmokeEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f);
  e.smoke = true;
  e.water = true;
  emitters.push_back(e);
}

void FireEffect::Update(float deltaTime) {
  if (!TexValid(fireTexture))
    return;

  // Clamp deltaTime to avoid particle explosion after pause/lag
  if (deltaTime > 0.1f)
    deltaTime = 0.1f;

  // Spawn new particles from emitters
  for (auto &emitter : emitters) {
    // Main 5.2: smoke = rand()%2 (50% at 25fps = 12.5/sec), water = same rate
    // Fire: 25/sec base with 50% stochastic rejection = ~12.5 effective (organic flicker)
    float rate = emitter.water ? 12.5f : (emitter.smoke ? 6.0f : 25.0f);
    emitter.spawnAccum += rate * deltaTime;
    while (emitter.spawnAccum >= 1.0f &&
           (int)particles.size() < MAX_PARTICLES) {
      emitter.spawnAccum -= 1.0f;

      // Main 5.2: CreateFire has 50% spawn chance per call (stochastic flicker)
      if (!emitter.smoke && !emitter.water && rand() % 2 == 0)
        continue;

      Particle p;
      p.position = emitter.position;
      p.position.x += RandFloat(-10.0f, 10.0f);
      p.position.y += RandFloat(-10.0f, 10.0f);
      p.position.z += RandFloat(-10.0f, 10.0f);

      if (emitter.water) {
        // Main 5.2: BITMAP_SMOKE from fountain bones, ±16 spread
        p.isWater = true;
        p.position.x += RandFloat(-16.0f, 16.0f);
        p.position.z += RandFloat(-16.0f, 16.0f);
        p.velocity = glm::vec3(RandFloat(-5.0f, 5.0f), RandFloat(25.0f, 50.0f),
                               RandFloat(-5.0f, 5.0f));
        p.scale = RandFloat(60.0f, 100.0f);
        p.maxLifetime = 1.2f;
        p.lifetime = 1.2f;
        float w = RandFloat(0.7f, 1.0f);
        p.color = glm::vec3(w, w, w);
      } else if (emitter.smoke) {
        // Smoke: slower upward, larger, longer life
        p.isWater = false;
        p.velocity = glm::vec3(RandFloat(-8.0f, 8.0f), RandFloat(20.0f, 45.0f),
                               RandFloat(-8.0f, 8.0f));
        p.scale = RandFloat(80.0f, 140.0f);
        p.maxLifetime = 1.8f;
        p.lifetime = 1.8f;
        float g = RandFloat(0.3f, 0.5f);
        p.color = glm::vec3(g, g, g);
      } else {
        p.isWater = false;
        // Fire: warm orange, faster upward
        p.velocity = glm::vec3(RandFloat(-5.0f, 5.0f), RandFloat(40.0f, 80.0f),
                               RandFloat(-5.0f, 5.0f));
        p.scale = RandFloat(60.0f, 100.0f);
        p.maxLifetime = PARTICLE_LIFETIME;
        p.lifetime = PARTICLE_LIFETIME;
        float lum = RandFloat(0.6f, 1.1f);
        p.color = glm::vec3(lum, lum * 0.6f, lum * 0.4f);
      }

      p.rotation = RandFloat(0.0f, 6.2832f);
      particles.push_back(p);
    }
  }

  // Update existing particles
  for (int i = (int)particles.size() - 1; i >= 0; --i) {
    auto &p = particles[i];
    p.lifetime -= deltaTime;

    if (p.lifetime <= 0.0f) {
      // Remove dead particle (swap with last)
      particles[i] = particles.back();
      particles.pop_back();
      continue;
    }

    p.position += p.velocity * deltaTime;
    // Slight deceleration of upward movement
    p.velocity.y *= (1.0f - 1.5f * deltaTime);
    // Shrink over lifetime
    p.scale -= 50.0f * deltaTime;
    if (p.scale < 2.0f)
      p.scale = 2.0f;
  }
}

void FireEffect::submitBatch(const std::vector<InstanceData> &batch,
                             TexHandle tex) {
  uint32_t count = (uint32_t)batch.size();
  const uint16_t stride = 48; // 3 × vec4

  uint32_t avail = bgfx::getAvailInstanceDataBuffer(count, stride);
  if (avail == 0) return;
  count = std::min(count, avail);

  bgfx::InstanceDataBuffer idb;
  bgfx::allocInstanceDataBuffer(&idb, count, stride);

  uint8_t *data = idb.data;
  for (uint32_t i = 0; i < count; ++i) {
    float *d = (float *)data;
    // i_data0: worldPos.xyz, scale
    d[0] = batch[i].worldPos.x;
    d[1] = batch[i].worldPos.y;
    d[2] = batch[i].worldPos.z;
    d[3] = batch[i].scale;
    // i_data1: rotation, frame, alpha, pad
    d[4] = batch[i].rotation;
    d[5] = batch[i].frame;
    d[6] = batch[i].alpha;
    d[7] = 0.0f;
    // i_data2: color.rgb, pad
    d[8] = batch[i].color.x;
    d[9] = batch[i].color.y;
    d[10] = batch[i].color.z;
    d[11] = 0.0f;
    data += stride;
  }

  bgfx::setVertexBuffer(0, quadVBO);
  bgfx::setIndexBuffer(quadEBO);
  bgfx::setInstanceDataBuffer(&idb);
  billboardShader->setTexture(0, "s_fireTex", tex);

  // Additive blend, depth test but no depth write, no face culling
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                         BGFX_STATE_BLEND_ONE);
  bgfx::setState(state);
  bgfx::submit(0, billboardShader->program);
}

void FireEffect::Render(const glm::mat4 &view, const glm::mat4 &projection) {
  if (!TexValid(fireTexture) || particles.empty() || !billboardShader)
    return;

  int count = std::min((int)particles.size(), MAX_PARTICLES);

  // Separate into fire and water batches
  std::vector<InstanceData> fireBatch, waterBatch;
  fireBatch.reserve(count);

  for (int i = 0; i < count; ++i) {
    auto &p = particles[i];
    float t = 1.0f - p.lifetime / p.maxLifetime;
    int frame = std::min((int)(t * 4.0f), 3);
    float alpha = p.lifetime / p.maxLifetime;

    InstanceData d;
    d.worldPos = p.position;
    d.scale = p.scale;
    d.rotation = p.rotation;
    d.frame = (float)frame;
    d.color = p.color;
    d.alpha = alpha;

    if (p.isWater) {
      d.frame = 0.0f; // water uses full texture
      waterBatch.push_back(d);
    } else {
      fireBatch.push_back(d);
    }
  }

  if (!fireBatch.empty())
    submitBatch(fireBatch, fireTexture);

  if (!waterBatch.empty() && TexValid(waterTexture))
    submitBatch(waterBatch, waterTexture);
}

void FireEffect::Cleanup() {
  if (bgfx::isValid(quadVBO)) { bgfx::destroy(quadVBO); quadVBO = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(quadEBO)) { bgfx::destroy(quadEBO); quadEBO = BGFX_INVALID_HANDLE; }
  TexDestroy(fireTexture);
  TexDestroy(waterTexture);
  billboardShader.reset();
  emitters.clear();
  particles.clear();
}

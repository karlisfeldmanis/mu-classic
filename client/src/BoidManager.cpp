#include "BoidManager.hpp"
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// ── Angle math (ZzzAI.cpp:27-51, 70-100) ─────────────────────────────

// Main 5.2: CreateAngle() — heading angle from (x1,y1) toward (x2,y2)
// Returns degrees: 0=S, 90=W, 180=N, 270=E
static float createAngle(float x1, float y1, float x2, float y2) {
  float nx2 = x2 - x1, ny2 = y2 - y1;
  if (std::abs(nx2) < 0.0001f) {
    return (ny2 < 0.0f) ? 0.0f : 180.0f;
  }
  if (std::abs(ny2) < 0.0001f) {
    return (nx2 < 0.0f) ? 270.0f : 90.0f;
  }
  float angle = std::atan(ny2 / nx2) / 3.1415926536f * 180.0f + 90.0f;
  float r;
  if (nx2 < 0.0f)
    r = angle + 180.0f;
  else
    r = angle;
  return r;
}

// Main 5.2: TurnAngle() — steer iTheta toward iHeading by at most maxTURN deg
static int turnAngle(int iTheta, int iHeading, int maxTURN) {
  int iChange = 0;
  int Delta = std::abs(iTheta - iHeading);
  if (iTheta > iHeading) {
    if (Delta < std::abs((iHeading + 360) - iTheta))
      iChange = -std::min(maxTURN, Delta);
    else
      iChange = std::min(maxTURN, Delta);
  }
  if (iTheta < iHeading) {
    if (Delta < std::abs((iTheta + 360) - iHeading))
      iChange = std::min(maxTURN, Delta);
    else
      iChange = -std::min(maxTURN, Delta);
  }
  iTheta += iChange + 360;
  iTheta %= 360;
  return iTheta;
}

// ── Helpers ──────────────────────────────────────────────────────────

float BoidManager::getTerrainHeight(float worldX, float worldZ) const {
  return TerrainUtils::GetHeight(m_terrainData, worldX, worldZ);
}

glm::vec3 BoidManager::sampleTerrainLight(const glm::vec3 &pos) const {
  return TerrainUtils::SampleLightAt(m_terrainLightmap, pos);
}

uint8_t BoidManager::getTerrainLayer1(float worldX, float worldZ) const {
  return TerrainUtils::GetLayer1(m_terrainData, worldX, worldZ);
}

uint8_t BoidManager::getTerrainAttribute(float worldX, float worldZ) const {
  return TerrainUtils::GetAttribute(m_terrainData, worldX, worldZ);
}

void BoidManager::alphaFade(float &alpha, float target, float dt) {
  // Main 5.2 Alpha() function: 10% blend per tick toward target
  float rate = 10.0f * dt; // ~0.4 per tick at 25fps
  if (alpha < target) {
    alpha += rate;
    if (alpha > target)
      alpha = target;
  } else if (alpha > target) {
    alpha -= rate;
    if (alpha < target)
      alpha = target;
  }
}

// ── Init ─────────────────────────────────────────────────────────────

void BoidManager::Init(const std::string &dataPath) {
  // Load shaders (same as NPC/Monster managers)
  m_shader = Shader::Load("vs_model.bin", "fs_model.bin");
  m_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");

  // Load bird model: Data/Object1/Bird01.bmd + bird.ozt
  std::string birdPath = dataPath + "/Object1/Bird01.bmd";
  auto birdBmd = BMDParser::Parse(birdPath);
  if (birdBmd) {
    m_birdBmd = std::move(birdBmd);
    std::string texDir = dataPath + "/Object1/";
    m_birdBones = ComputeBoneMatrices(m_birdBmd.get());
    AABB aabb{};
    for (auto &mesh : m_birdBmd->Meshes) {
      UploadMeshWithBones(mesh, texDir, m_birdBones, m_birdMeshes, aabb, true);
    }

    // Create shadow buffer (shared, re-uploaded per instance)
    int totalVerts = 0;
    for (auto &mesh : m_birdBmd->Meshes) {
      for (int i = 0; i < mesh.NumTriangles; ++i) {
        totalVerts += (mesh.Triangles[i].Polygon == 4) ? 6 : 3;
      }
    }
    bgfx::VertexLayout shadowLayout;
    shadowLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    m_birdShadow.vbo = bgfx::createDynamicVertexBuffer(
        totalVerts, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
    m_birdShadow.vertexCount = totalVerts;

    std::cout << "[Boid] Loaded Bird01.bmd (" << m_birdBmd->Bones.size()
              << " bones, " << m_birdBmd->Meshes.size() << " meshes)"
              << std::endl;
  } else {
    std::cerr << "[Boid] Failed to load Bird01.bmd" << std::endl;
  }

  // Load bat model: Data/Object2/Bat01.bmd (dungeon critter)
  std::string batPath = dataPath + "/Object2/Bat01.bmd";
  auto batBmd = BMDParser::Parse(batPath);
  if (batBmd) {
    m_batBmd = std::move(batBmd);
    std::string texDir = dataPath + "/Object2/";
    m_batBones = ComputeBoneMatrices(m_batBmd.get());
    AABB aabb{};
    for (auto &mesh : m_batBmd->Meshes) {
      UploadMeshWithBones(mesh, texDir, m_batBones, m_batMeshes, aabb, true);
    }

    // Create shadow buffer
    int totalVerts = 0;
    for (auto &mesh : m_batBmd->Meshes) {
      for (int i = 0; i < mesh.NumTriangles; ++i) {
        totalVerts += (mesh.Triangles[i].Polygon == 4) ? 6 : 3;
      }
    }
    {
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
      m_batShadow.vbo = bgfx::createDynamicVertexBuffer(
          totalVerts, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
    }
    m_batShadow.vertexCount = totalVerts;

    std::cout << "[Boid] Loaded Bat01.bmd (" << m_batBmd->Bones.size()
              << " bones, " << m_batBmd->Meshes.size() << " meshes)"
              << std::endl;
  } else {
    std::cerr << "[Boid] Failed to load Bat01.bmd" << std::endl;
  }

  // Load fish model: Data/Object1/Fish01.bmd + fish.OZT
  std::string fishPath = dataPath + "/Object1/Fish01.bmd";
  auto fishBmd = BMDParser::Parse(fishPath);
  if (fishBmd) {
    m_fishBmd = std::move(fishBmd);
    std::string texDir = dataPath + "/Object1/";
    m_fishBones = ComputeBoneMatrices(m_fishBmd.get());
    AABB aabb{};
    for (auto &mesh : m_fishBmd->Meshes) {
      UploadMeshWithBones(mesh, texDir, m_fishBones, m_fishMeshes, aabb, true);
    }

    // Create shadow buffer
    int totalVerts = 0;
    for (auto &mesh : m_fishBmd->Meshes) {
      for (int i = 0; i < mesh.NumTriangles; ++i) {
        totalVerts += (mesh.Triangles[i].Polygon == 4) ? 6 : 3;
      }
    }
    {
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
      m_fishShadow.vbo = bgfx::createDynamicVertexBuffer(
          totalVerts, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
    }
    m_fishShadow.vertexCount = totalVerts;

    std::cout << "[Boid] Loaded Fish01.bmd (" << m_fishBmd->Bones.size()
              << " bones, " << m_fishBmd->Meshes.size() << " meshes)"
              << std::endl;
  } else {
    std::cerr << "[Boid] Failed to load Fish01.bmd" << std::endl;
  }

  // Initialize all boids/bats/fish as dead with staggered spawn delay
  for (int i = 0; i < MAX_BOIDS; ++i) {
    m_boids[i].live = false;
    m_boids[i].respawnDelay = 2.0f + (float)i * 3.0f; // Stagger: 2s, 5s, 8s, ...
  }
  for (int i = 0; i < MAX_BATS; ++i) {
    m_bats[i].live = false;
    m_bats[i].respawnDelay = 2.0f + (float)i * 3.0f;
  }
  for (auto &f : m_fishs)
    f.live = false;

  // ── Falling leaves (Main 5.2: ZzzEffectFireLeave.cpp) ──────────────
  m_leafShader = Shader::Load("vs_leaf.bin", "fs_leaf.bin");

  // Load leaf texture (OZT for alpha, fallback to OZJ)
  std::string leafPath = dataPath + "/World1/leaf01.OZT";
  m_leafTexture = TextureLoader::LoadOZT(leafPath);
  if (!TexValid(m_leafTexture)) {
    leafPath = dataPath + "/World1/leaf01.OZJ";
    m_leafTexture = TextureLoader::LoadOZJ(leafPath);
  }

  if (TexValid(m_leafTexture)) {
    // Create leaf quad (3x3 unit quad in XZ plane)
    struct LeafVert { float x, y, z, u, v; };
    LeafVert quadVerts[] = {
        {-3.0f, 0.0f, -3.0f, 0.0f, 0.0f},
        { 3.0f, 0.0f, -3.0f, 1.0f, 0.0f},
        { 3.0f, 0.0f,  3.0f, 1.0f, 1.0f},
        {-3.0f, 0.0f,  3.0f, 0.0f, 1.0f},
    };
    uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

    bgfx::VertexLayout leafLayout;
    leafLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    m_leafVBO = bgfx::createVertexBuffer(
        bgfx::copy(quadVerts, sizeof(quadVerts)), leafLayout);
    m_leafEBO = bgfx::createIndexBuffer(
        bgfx::copy(quadIndices, sizeof(quadIndices)));
    std::cout << "[Boid] Leaf texture loaded (BGFX)" << std::endl;
  }

  for (auto &leaf : m_leaves)
    leaf.live = false;

  std::cout << "[Boid] Ambient creature system initialized" << std::endl;
}

// ── Bird AI ──────────────────────────────────────────────────────────

void BoidManager::moveBird(Boid &b, const glm::vec3 &heroPos, int heroAction) {
  // Main 5.2: MoveBird (GOBoid.cpp:948-1001)
  float terrainH = getTerrainHeight(b.position.x, b.position.z);
  float relH = b.position.y - terrainH;

  // Push away from hero to prevent birds appearing huge on screen
  float dx = b.position.x - heroPos.x;
  float dz = b.position.z - heroPos.z;
  float heroDistSq = dx * dx + dz * dz;
  if (heroDistSq < 400.0f * 400.0f && heroDistSq > 0.1f) {
    float dist = std::sqrt(heroDistSq);
    float push = (400.0f - dist) * 0.05f;
    b.position.x += (dx / dist) * push;
    b.position.z += (dz / dist) * push;
  }

  switch (b.ai) {
  case BoidAI::FLY:
    b.velocity = 1.0f;
    b.position.y += (float)(rand() % 16 - 8);
    if (relH < 200.0f)
      b.direction.y = 10.0f;
    else if (relH > 600.0f)
      b.direction.y = -10.0f;

    // Very rarely decide to land (1/4096 per tick ≈ once per ~3 minutes)
    if (rand() % 4096 == 0 && relH > 50.0f) {
      b.ai = BoidAI::DOWN;
      b.timer = 0.0f;
    }
    break;

  case BoidAI::DOWN:
    // Descend toward ground
    b.velocity = 0.6f;
    b.direction.y = -12.0f;
    if (relH <= 15.0f) {
      b.position.y = terrainH + 5.0f;
      b.ai = BoidAI::GROUND;
      b.velocity = 0.0f;
      b.direction.y = 0.0f;
      b.timer = 3.0f + (float)(rand() % 50) * 0.1f; // Rest 3-8 seconds
    }
    break;

  case BoidAI::GROUND:
    // Resting on ground — no movement
    b.velocity = 0.0f;
    b.direction.y = 0.0f;
    b.position.y = terrainH + 5.0f;
    // Take off after timer expires or if hero gets close
    if (b.timer <= 0.0f || heroDistSq < 300.0f * 300.0f) {
      b.ai = BoidAI::UP;
    }
    break;

  case BoidAI::UP:
    // Ascend back to flight altitude
    b.velocity = 0.8f;
    b.direction.y = 15.0f;
    if (relH >= 200.0f) {
      b.ai = BoidAI::FLY;
    }
    break;
  }
}

void BoidManager::moveBoidGroup(Boid &b) {
  // Main 5.2: MoveBoidGroup (GOBoid.cpp:1120-1195)
  if (b.ai == BoidAI::GROUND)
    return;

  // Build rotation matrix from angle.z (heading in degrees)
  float rad = glm::radians(b.angle.z);
  float cosA = std::cos(rad);
  float sinA = std::sin(rad);

  // Forward movement: velocity * 25 in local forward direction
  float fwd = b.velocity * 25.0f;
  // Rotate by heading to get world offset
  // MU coordinate mapping: Position[0]=worldX, Position[1]=worldZ
  float dx = fwd * cosA;
  float dz = -fwd * sinA;

  b.position.x += dx;
  b.position.z += dz;
  b.position.y += b.direction.y;

  // Update look-ahead direction (used by flocking)
  b.direction.x = b.position.x + 3.0f * dx;
  b.direction.z = b.position.z + 3.0f * dz;

  // Random direction.y drift (Main 5.2: GOBoid.cpp:1165)
  b.direction.y += (float)(rand() % 16 - 8);
}

// Main 5.2: MoveBoid (ZzzAI.cpp:190-231) — flocking angle computation
void BoidManager::moveBoidFlock(Boid &b, int selfIdx) {
  int numBirds = 0;
  float targetX = 0.0f;
  float targetZ = 0.0f;

  for (int j = 0; j < MAX_BOIDS; ++j) {
    Boid &t = m_boids[j];
    if (!t.live || j == selfIdx)
      continue;

    float rx = b.position.x - t.position.x;
    float rz = b.position.z - t.position.z;
    float distance = std::sqrt(rx * rx + rz * rz);

    if (distance < 400.0f) {
      float xdist = t.direction.x - t.position.x;
      float zdist = t.direction.z - t.position.z;

      if (distance < 80.0f) {
        // Separation: push away when too close
        xdist -= t.direction.x - b.position.x;
        zdist -= t.direction.z - b.position.z;
      } else {
        // Cohesion: steer toward neighbor's look-ahead
        xdist += t.direction.x - b.position.x;
        zdist += t.direction.z - b.position.z;
      }

      float pdist = std::sqrt(xdist * xdist + zdist * zdist);
      if (pdist > 0.001f) {
        targetX += xdist / pdist;
        targetZ += zdist / pdist;
      }
      numBirds++;
    }
  }

  if (numBirds > 0) {
    targetX = b.position.x + targetX / (float)numBirds;
    targetZ = b.position.z + targetZ / (float)numBirds;

    float heading = createAngle(b.position.x, b.position.z, targetX, targetZ);
    b.angle.z = (float)turnAngle((int)b.angle.z, (int)heading, (int)b.gravity);
  }
}

// ── Update ───────────────────────────────────────────────────────────

void BoidManager::updateBoids(float dt, const glm::vec3 &heroPos,
                               int heroAction) {
  if (!m_birdBmd)
    return;

  for (int i = 0; i < MAX_BOIDS; ++i) {
    Boid &b = m_boids[i];

    // Spawn new boid if slot is empty (with cooldown)
    if (!b.live) {
      // Respect respawn delay — don't instantly respawn
      b.respawnDelay -= dt;
      if (b.respawnDelay > 0.0f)
        continue;

      // Try to find a valid spawn position (not in safe zone / buildings)
      float spawnX = heroPos.x + (float)(rand() % 1024 - 512);
      float spawnZ = heroPos.z + (float)(rand() % 1024 - 512);
      uint8_t attr = getTerrainAttribute(spawnX, spawnZ);
      // Skip safe zone tiles (0x01) and no-move tiles (0x04) = inside buildings
      if (attr & 0x05) {
        b.respawnDelay = 1.0f; // Try again in 1 second
        continue;
      }

      b = Boid{}; // Reset
      b.live = true;
      b.velocity = 1.0f;
      b.alpha = 0.0f;
      b.alphaTarget = 1.0f;
      b.scale = 0.8f;
      b.shadowScale = 10.0f;
      b.ai = BoidAI::FLY;
      b.timer = (float)(rand() % 314) * 0.01f;
      b.subType = 0;
      b.lifetime = 0;
      b.action = 0;
      b.angle = glm::vec3(0.0f, 0.0f, (float)(rand() % 360));
      b.gravity = 13.0f; // Main 5.2: o->Gravity = 13 (GOBoid.cpp:1326)

      b.position.x = spawnX;
      b.position.z = spawnZ;
      float terrainH = getTerrainHeight(b.position.x, b.position.z);
      b.position.y = terrainH + (float)(rand() % 200 + 150);
      continue;
    }

    // Tick ground rest timer
    if (b.ai == BoidAI::GROUND)
      b.timer -= dt;

    // Animate (slower when grounded — subtle idle bob)
    if (b.action >= 0 && b.action < (int)m_birdBmd->Actions.size()) {
      int numKeys = m_birdBmd->Actions[b.action].NumAnimationKeys;
      if (numKeys > 1) {
        float animSpeed = (b.ai == BoidAI::GROUND) ? 0.3f : 1.0f;
        b.animFrame += animSpeed * dt * 25.0f;
        if (b.animFrame >= (float)numKeys)
          b.animFrame = std::fmod(b.animFrame, (float)numKeys);
      }
    }

    // Move
    moveBird(b, heroPos, heroAction);
    if (b.ai != BoidAI::GROUND) {
      moveBoidFlock(b, i); // Flocking: steer toward/away from neighbors
      moveBoidGroup(b);    // Apply velocity in facing direction
    }

    // Distance check — despawn if > 1500 units from hero
    float dx = b.position.x - heroPos.x;
    float dz = b.position.z - heroPos.z;
    float range = std::sqrt(dx * dx + dz * dz);
    if (range >= 1500.0f) {
      b.live = false;
      b.respawnDelay = 3.0f + (float)(rand() % 5); // 3-8 second cooldown
    }

    // Main 5.2 GOBoid.cpp: bird sounds within 600 units, rand_fps_check(512)
    if (range < 600.0f && rand() % 512 == 0) {
      int birdSound = (rand() % 2 == 0) ? SOUND_BIRD01 : SOUND_BIRD02;
      SoundManager::Play3D(birdSound, b.position.x, b.position.y,
                           b.position.z);
    }

    // Random despawn (1/512 per tick at 25fps → ~every 20s average)
    if (rand() % 512 == 0) {
      b.live = false;
      b.respawnDelay = 5.0f + (float)(rand() % 8); // 5-13 second cooldown
    }

    // Lifetime/SubType despawn
    b.lifetime--;
    if (b.subType >= 2) {
      b.live = false;
      b.respawnDelay = 4.0f;
    }

    // Alpha fade
    alphaFade(b.alpha, b.alphaTarget, dt);
  }
}

// ── Bat AI (Main 5.2: GOBoid.cpp — dungeon bats, always flying, erratic) ──

void BoidManager::moveBat(Boid &b, const glm::vec3 &heroPos) {
  float terrainH = getTerrainHeight(b.position.x, b.position.z);
  float relH = b.position.y - terrainH;

  // Push away from hero
  float dx = b.position.x - heroPos.x;
  float dz = b.position.z - heroPos.z;
  float heroDistSq = dx * dx + dz * dz;
  if (heroDistSq < 300.0f * 300.0f && heroDistSq > 0.1f) {
    float dist = std::sqrt(heroDistSq);
    float push = (300.0f - dist) * 0.06f;
    b.position.x += (dx / dist) * push;
    b.position.z += (dz / dist) * push;
  }

  // Boundary clamping — reverse direction if near map edges
  constexpr float MAP_MIN = 500.0f;
  constexpr float MAP_MAX = 25100.0f;
  if (b.position.x < MAP_MIN || b.position.x > MAP_MAX) {
    b.position.x = std::clamp(b.position.x, MAP_MIN, MAP_MAX);
    b.angle.z += 180.0f; // Reverse heading
  }
  if (b.position.z < MAP_MIN || b.position.z > MAP_MAX) {
    b.position.z = std::clamp(b.position.z, MAP_MIN, MAP_MAX);
    b.angle.z += 180.0f;
  }

  // Bats always fly — erratic altitude changes, lower ceiling than birds
  b.velocity = 1.2f; // Slightly faster than birds
  b.position.y += (float)(rand() % 20 - 10); // More vertical jitter
  if (relH < 100.0f)
    b.direction.y = 12.0f;
  else if (relH > 350.0f)
    b.direction.y = -12.0f;

  // Random direction changes more frequent than birds
  if (rand() % 64 == 0)
    b.angle.z += (float)(rand() % 60 - 30); // Sudden heading shifts
}

void BoidManager::updateBats(float dt, const glm::vec3 &heroPos) {
  if (!m_batBmd)
    return;

  for (int i = 0; i < MAX_BATS; ++i) {
    Boid &b = m_bats[i];

    // Spawn new bat
    if (!b.live) {
      b.respawnDelay -= dt;
      if (b.respawnDelay > 0.0f)
        continue;

      float spawnX = heroPos.x + (float)(rand() % 800 - 400);
      float spawnZ = heroPos.z + (float)(rand() % 800 - 400);

      b = Boid{};
      b.live = true;
      b.velocity = 1.2f;
      b.alpha = 0.0f;
      b.alphaTarget = 1.0f;
      b.scale = 0.8f; // Main 5.2: bat scale 0.8
      b.shadowScale = 8.0f;
      b.ai = BoidAI::FLY; // Bats always fly
      b.timer = (float)(rand() % 314) * 0.01f;
      b.subType = 0;
      b.lifetime = 0;
      b.action = 0;
      b.angle = glm::vec3(0.0f, 0.0f, (float)(rand() % 360));
      b.gravity = 15.0f; // Faster turning than birds

      b.position.x = spawnX;
      b.position.z = spawnZ;
      float terrainH = getTerrainHeight(b.position.x, b.position.z);
      b.position.y = terrainH + (float)(rand() % 150 + 80); // Lower altitude
      continue;
    }

    // Animate
    if (b.action >= 0 && b.action < (int)m_batBmd->Actions.size()) {
      int numKeys = m_batBmd->Actions[b.action].NumAnimationKeys;
      if (numKeys > 1) {
        b.animFrame += 1.2f * dt * 25.0f; // Faster wing beats
        if (b.animFrame >= (float)numKeys)
          b.animFrame = std::fmod(b.animFrame, (float)numKeys);
      }
    }

    // Move
    b.prevPosition = b.position;
    moveBat(b, heroPos);
    moveBoidGroup(b); // Apply velocity in facing direction

    // Stuck detection: if barely moved in 2 seconds, force despawn
    float movedDist = glm::length(b.position - b.prevPosition);
    if (movedDist < 0.5f) {
      b.stuckTimer += dt;
      if (b.stuckTimer > 2.0f) {
        b.live = false;
        b.respawnDelay = 2.0f + (float)(rand() % 3);
        continue;
      }
    } else {
      b.stuckTimer = 0.0f;
    }

    // Distance check — despawn if > 1200 units from hero
    float dx = b.position.x - heroPos.x;
    float dz = b.position.z - heroPos.z;
    float range = std::sqrt(dx * dx + dz * dz);
    if (range >= 1200.0f) {
      b.live = false;
      b.respawnDelay = 3.0f + (float)(rand() % 5);
    }

    // Main 5.2 GOBoid.cpp: bat sounds — reduced frequency to prevent spam
    if (range < 600.0f && rand() % 1024 == 0) {
      SoundManager::Play3D(SOUND_BAT01, b.position.x, b.position.y, b.position.z);
    }

    // Random despawn
    if (rand() % 512 == 0) {
      b.live = false;
      b.respawnDelay = 5.0f + (float)(rand() % 8);
    }

    // Alpha fade
    alphaFade(b.alpha, b.alphaTarget, dt);
  }
}

void BoidManager::updateFishs(float dt, const glm::vec3 &heroPos) {
  if (!m_fishBmd)
    return;

  for (int i = 0; i < MAX_FISHS; ++i) {
    Fish &f = m_fishs[i];

    // Spawn new fish if slot empty
    if (!f.live) {
      // Random position near hero
      glm::vec3 spawnPos;
      spawnPos.x = heroPos.x + (float)(rand() % 1024 - 512);
      spawnPos.z = heroPos.z + (float)(rand() % 1024 - 512);
      spawnPos.y = heroPos.y;

      // Check if on water tile (layer1 == 5 for Lorencia)
      uint8_t layer1 = getTerrainLayer1(spawnPos.x, spawnPos.z);
      if (layer1 != 5)
        continue;

      f = Fish{}; // Reset
      f.live = true;
      f.alpha = 0.0f;
      f.alphaTarget = (float)(rand() % 2 + 2) * 0.1f; // 0.2 or 0.3
      f.scale = (float)(rand() % 4 + 4) * 0.1f;       // 0.4-0.7
      f.velocity = 0.6f / f.scale;
      f.subType = 0;
      f.lifetime = rand() % 128;
      f.action = 0;
      f.position = spawnPos;
      f.position.y = getTerrainHeight(spawnPos.x, spawnPos.z);
      f.angle = glm::vec3(0.0f, 0.0f, 0.0f);
      continue;
    }

    // Animate: PlaySpeed = velocity * 0.5
    if (f.action >= 0 && f.action < (int)m_fishBmd->Actions.size()) {
      int numKeys = m_fishBmd->Actions[f.action].NumAnimationKeys;
      if (numKeys > 1) {
        f.animFrame += f.velocity * 0.5f * dt * 25.0f;
        if (f.animFrame >= (float)numKeys)
          f.animFrame = std::fmod(f.animFrame, (float)numKeys);
      }
    }

    // Move: forward in facing direction, snap to terrain height
    // Main 5.2: GOBoid.cpp:1803-1808
    float rad = glm::radians(f.angle.z);
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);
    float speed = f.velocity * (float)(rand() % 4 + 6);
    float dx = speed * cosA;
    float dz = -speed * sinA;
    f.position.x += dx * dt * 25.0f;
    f.position.z += dz * dt * 25.0f;
    f.position.y = getTerrainHeight(f.position.x, f.position.z);

    // Check if still on water tile — if not, reverse and count wall hits
    uint8_t layer1 = getTerrainLayer1(f.position.x, f.position.z);
    if (layer1 != 5) {
      f.angle.z += 180.0f;
      if (f.angle.z >= 360.0f)
        f.angle.z -= 360.0f;
      f.subType++;
    } else {
      if (f.subType > 0)
        f.subType--;
    }

    // Despawn if hit wall twice
    if (f.subType >= 2)
      f.live = false;

    // Distance despawn
    float distX = f.position.x - heroPos.x;
    float distZ = f.position.z - heroPos.z;
    float range = std::sqrt(distX * distX + distZ * distZ);
    if (range >= 1500.0f)
      f.live = false;

    // Lifetime management
    f.lifetime--;
    if (f.lifetime <= 0) {
      if (rand() % 64 == 0)
        f.lifetime = rand() % 128;
    }

    // Alpha fade
    alphaFade(f.alpha, f.alphaTarget, dt);
  }
}

// ── Falling Leaves ───────────────────────────────────────────────────

void BoidManager::spawnLeaf(LeafParticle &leaf, const glm::vec3 &heroPos) {
  // Main 5.2: CreateLorenciaLeaf (ZzzEffectFireLeave.cpp:205-224)
  leaf.live = true;
  leaf.alpha = 1.0f;
  leaf.onGround = false;

  leaf.position.x = heroPos.x + (float)(rand() % 1600 - 800);
  leaf.position.z = heroPos.z + (float)(rand() % 1400 - 500);
  leaf.position.y = heroPos.y + (float)(rand() % 300 + 50);

  // Variety: ~50% fall to ground, ~50% drift/fly away
  bool drifter = (rand() % 100) < 50;
  float windDir = (leaf.position.z < heroPos.z + 400.0f) ? 1.0f : -1.0f;

  if (drifter) {
    // Drifters: stronger horizontal wind, gentle fall — fly off screen
    leaf.velocity.x = windDir * ((float)(rand() % 80 + 60));  // 60-140 u/s
    leaf.velocity.z = (float)(rand() % 60 - 30);              // ±30 u/s lateral
    leaf.velocity.y = -(float)(rand() % 8 + 2);               // -2 to -10 u/s very gentle
  } else {
    // Fallers: less wind, stronger downward — land on terrain
    leaf.velocity.x = windDir * ((float)(rand() % 40 + 20));  // 20-60 u/s
    leaf.velocity.z = (float)(rand() % 30 - 15);              // ±15 u/s lateral
    leaf.velocity.y = -(float)(rand() % 25 + 15);             // -15 to -40 u/s downward
  }

  // Angular velocity (degrees/second)
  leaf.turningForce.x = (float)(rand() % 90 - 45);
  leaf.turningForce.z = (float)(rand() % 180 - 90);
  leaf.turningForce.y = (float)(rand() % 90 - 45);

  leaf.angle = glm::vec3(0.0f);
}

void BoidManager::updateLeaves(float dt, const glm::vec3 &heroPos) {
  if (!TexValid(m_leafTexture))
    return;

  for (int i = 0; i < MAX_LEAVES; ++i) {
    LeafParticle &leaf = m_leaves[i];

    if (!leaf.live) {
      spawnLeaf(leaf, heroPos);
      continue;
    }

    float terrainH = getTerrainHeight(leaf.position.x, leaf.position.z);

    // Kill leaves that drifted too far — this keeps the spawn loop alive
    float dx = leaf.position.x - heroPos.x;
    float dz = leaf.position.z - heroPos.z;
    float distSq = dx * dx + dz * dz;
    if (distSq > 1200.0f * 1200.0f) {
      leaf.live = false;
      continue;
    }

    if (leaf.position.y <= terrainH) {
      // On ground: snap to terrain, fade out
      leaf.position.y = terrainH;
      leaf.onGround = true;
      leaf.alpha -= 1.2f * dt; // ~0.8s to fade out
      if (leaf.alpha <= 0.0f)
        leaf.live = false;
    } else {
      // Airborne: gravity + random gusts (units/second)
      leaf.velocity.y -= 15.0f * dt; // Gravity pull
      // Random turbulence — scale by sqrt(dt) for framerate-independent Brownian motion
      float sqrtDt = sqrtf(dt);
      leaf.velocity.x += (float)(rand() % 100 - 50) * 1.0f * sqrtDt;
      leaf.velocity.z += (float)(rand() % 100 - 50) * 1.0f * sqrtDt;
      leaf.velocity.y += (float)(rand() % 60 - 25) * 0.8f * sqrtDt; // Slight upward bias
      // Damping — prevents runaway speed while keeping leaves lively
      float damping = expf(-0.5f * dt);
      leaf.velocity *= damping;
      leaf.position += leaf.velocity * dt;
    }

    // Tumble rotation (degrees/second)
    leaf.angle += leaf.turningForce * dt;
  }
}

void BoidManager::Update(float deltaTime, const glm::vec3 &heroPos,
                          int heroAction, float worldTime) {
  m_worldTime = worldTime;
  if (m_mapId == 0) {
    // Lorencia: birds, fish, leaves
    updateBoids(deltaTime, heroPos, heroAction);
    updateFishs(deltaTime, heroPos);
    updateLeaves(deltaTime, heroPos);
  } else if (m_mapId == 1) {
    // Dungeon: bats only (no birds, fish, or leaves underground)
    updateBats(deltaTime, heroPos);
  }
}

// ── Render ───────────────────────────────────────────────────────────

void BoidManager::renderBoid(const Boid &b, const glm::mat4 &view,
                              const glm::mat4 &proj) {
  if (!b.live || b.alpha <= 0.001f || !m_birdBmd)
    return;

  // Compute interpolated bones for this boid's animation state
  auto bones = ComputeBoneMatricesInterpolated(m_birdBmd.get(), b.action,
                                                b.animFrame);

  // Re-skin bird mesh with these bones
  for (int mi = 0; mi < (int)m_birdMeshes.size() && mi < (int)m_birdBmd->Meshes.size(); ++mi) {
    RetransformMeshWithBones(m_birdBmd->Meshes[mi], bones, m_birdMeshes[mi]);
  }

  // Build model matrix
  // Main 5.2 adds 90 degrees to angle.z for rendering (GOBoid.cpp:1514)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, glm::radians(b.angle.z + 90.0f), glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(b.scale));

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  glm::vec3 tLight = sampleTerrainLight(b.position);
  m_shader->setVec4("u_shadowParams", glm::vec4(0.0f));
  for (auto &mb : m_birdMeshes) {
    if (mb.indexCount == 0 || mb.hidden) continue;
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    m_shader->setTexture(0, "s_texColor", mb.texture);
    m_shader->setVec4("u_params", glm::vec4(b.alpha, 1.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_shader->setVec4("u_fogParams", glm::vec4(1500.0f, 3500.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_fogColor", glm::vec4(0.117f, 0.078f, 0.039f, 0.0f));
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  }
}

void BoidManager::renderBat(const Boid &b, const glm::mat4 &view,
                             const glm::mat4 &proj) {
  if (!b.live || b.alpha <= 0.001f || !m_batBmd)
    return;

  auto bones = ComputeBoneMatricesInterpolated(m_batBmd.get(), b.action,
                                                b.animFrame);
  for (int mi = 0; mi < (int)m_batMeshes.size() && mi < (int)m_batBmd->Meshes.size(); ++mi) {
    RetransformMeshWithBones(m_batBmd->Meshes[mi], bones, m_batMeshes[mi]);
  }

  glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, glm::radians(b.angle.z + 90.0f), glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(b.scale));

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  glm::vec3 tLight = sampleTerrainLight(b.position);
  for (auto &mb : m_batMeshes) {
    if (mb.indexCount == 0 || mb.hidden) continue;
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    m_shader->setTexture(0, "s_texColor", mb.texture);
    m_shader->setVec4("u_params", glm::vec4(b.alpha, 1.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_shader->setVec4("u_fogParams", glm::vec4(800.0f, 2500.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_fogColor", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  }
}

void BoidManager::renderFish(const Fish &f, const glm::mat4 &view,
                              const glm::mat4 &proj) {
  if (!f.live || f.alpha <= 0.001f || !m_fishBmd)
    return;

  // Compute interpolated bones
  auto bones = ComputeBoneMatricesInterpolated(m_fishBmd.get(), f.action,
                                                f.animFrame);

  // Re-skin fish mesh
  for (int mi = 0; mi < (int)m_fishMeshes.size() && mi < (int)m_fishBmd->Meshes.size(); ++mi) {
    RetransformMeshWithBones(m_fishBmd->Meshes[mi], bones, m_fishMeshes[mi]);
  }

  // Build model matrix (Main 5.2: RenderFishs adds 90 degrees)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), f.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, glm::radians(f.angle.z + 90.0f), glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(f.scale));

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  glm::vec3 tLight = sampleTerrainLight(f.position);
  for (auto &mb : m_fishMeshes) {
    if (mb.indexCount == 0 || mb.hidden) continue;
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    m_shader->setTexture(0, "s_texColor", mb.texture);
    m_shader->setVec4("u_params", glm::vec4(f.alpha, 1.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_shader->setVec4("u_fogParams", glm::vec4(1500.0f, 3500.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_fogColor", glm::vec4(0.117f, 0.078f, 0.039f, 0.0f));
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  }
}

void BoidManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                          const glm::vec3 &camPos) {
  if (!m_shader)
    return;

  // BGFX: uniforms are set per-submit in renderBoid/renderBat/renderFish
  if (m_mapId == 0) {
    for (int i = 0; i < MAX_BOIDS; ++i)
      renderBoid(m_boids[i], view, proj);
    for (int i = 0; i < MAX_FISHS; ++i)
      renderFish(m_fishs[i], view, proj);
  } else if (m_mapId == 1) {
    for (int i = 0; i < MAX_BATS; ++i)
      renderBat(m_bats[i], view, proj);
  }
}

void BoidManager::RenderShadows(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_shadowShader)
    return;


  const float sx = 2000.0f;
  const float sy = 4000.0f;

  uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint32_t shadowStencil = BGFX_STENCIL_TEST_EQUAL
                         | BGFX_STENCIL_FUNC_REF(0)
                         | BGFX_STENCIL_FUNC_RMASK(0xFF)
                         | BGFX_STENCIL_OP_FAIL_S_KEEP
                         | BGFX_STENCIL_OP_FAIL_Z_KEEP
                         | BGFX_STENCIL_OP_PASS_Z_INCR;

  // Bird shadows
  if (m_birdBmd && bgfx::isValid(m_birdShadow.vbo)) {
    for (int i = 0; i < MAX_BOIDS; ++i) {
      const Boid &b = m_boids[i];
      if (!b.live || b.alpha <= 0.001f)
        continue;

      auto bones = ComputeBoneMatricesInterpolated(m_birdBmd.get(), b.action,
                                                    b.animFrame);

      glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::scale(model, glm::vec3(b.scale));

      float facingRad = glm::radians(b.angle.z + 90.0f);
      float cosF = std::cos(facingRad);
      float sinF = std::sin(facingRad);

      static std::vector<glm::vec3> shadowVerts;
      shadowVerts.clear();
      for (auto &mesh : m_birdBmd->Meshes) {
        for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
          auto &tri = mesh.Triangles[ti];
          for (int v = 0; v < 3; ++v) {
            auto &sv = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = sv.Position;
            int bi = sv.Node;
            if (bi >= 0 && bi < (int)bones.size())
              pos = MuMath::TransformPoint((const float(*)[4])bones[bi].data(), pos);
            pos *= b.scale;
            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;
            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
          if (tri.Polygon == 4) {
            int qi[3] = {0, 2, 3};
            for (int v : qi) {
              auto &sv = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = sv.Position;
              int bi = sv.Node;
              if (bi >= 0 && bi < (int)bones.size())
                pos = MuMath::TransformPoint((const float(*)[4])bones[bi].data(), pos);
              pos *= b.scale;
              float rx = pos.x * cosF - pos.y * sinF;
              float ry = pos.x * sinF + pos.y * cosF;
              pos.x = rx;
              pos.y = ry;
              if (pos.z < sy) {
                float factor = 1.0f / (pos.z - sy);
                pos.x += pos.z * (pos.x + sx) * factor;
                pos.y += pos.z * (pos.y + sx) * factor;
              }
              pos.z = 5.0f;
              shadowVerts.push_back(pos);
            }
          }
        }
      }

      if (!shadowVerts.empty()) {
        bgfx::update(m_birdShadow.vbo, 0,
                      bgfx::copy(shadowVerts.data(), shadowVerts.size() * sizeof(glm::vec3)));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, m_birdShadow.vbo, 0, (uint32_t)shadowVerts.size());
        bgfx::setState(shadowState);
        bgfx::setStencil(shadowStencil);
        bgfx::submit(0, m_shadowShader->program);
      }
    }
  }

  // Fish shadows (similar but using fish mesh)
  if (m_fishBmd && bgfx::isValid(m_fishShadow.vbo)) {
    for (int i = 0; i < MAX_FISHS; ++i) {
      const Fish &f = m_fishs[i];
      if (!f.live || f.alpha <= 0.001f)
        continue;

      auto bones = ComputeBoneMatricesInterpolated(m_fishBmd.get(), f.action,
                                                    f.animFrame);

      glm::mat4 model = glm::translate(glm::mat4(1.0f), f.position);
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::scale(model, glm::vec3(f.scale));

      float facingRad = glm::radians(f.angle.z + 90.0f);
      float cosF = std::cos(facingRad);
      float sinF = std::sin(facingRad);

      static std::vector<glm::vec3> shadowVerts;
      shadowVerts.clear();
      for (auto &mesh : m_fishBmd->Meshes) {
        for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
          auto &tri = mesh.Triangles[ti];
          for (int v = 0; v < 3; ++v) {
            auto &sv = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = sv.Position;
            int bi = sv.Node;
            if (bi >= 0 && bi < (int)bones.size())
              pos = MuMath::TransformPoint((const float(*)[4])bones[bi].data(), pos);
            pos *= f.scale;
            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;
            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
          if (tri.Polygon == 4) {
            int qi[3] = {0, 2, 3};
            for (int v : qi) {
              auto &sv = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = sv.Position;
              int bi = sv.Node;
              if (bi >= 0 && bi < (int)bones.size())
                pos = MuMath::TransformPoint((const float(*)[4])bones[bi].data(), pos);
              pos *= f.scale;
              float rx = pos.x * cosF - pos.y * sinF;
              float ry = pos.x * sinF + pos.y * cosF;
              pos.x = rx;
              pos.y = ry;
              if (pos.z < sy) {
                float factor = 1.0f / (pos.z - sy);
                pos.x += pos.z * (pos.x + sx) * factor;
                pos.y += pos.z * (pos.y + sx) * factor;
              }
              pos.z = 5.0f;
              shadowVerts.push_back(pos);
            }
          }
        }
      }

      if (!shadowVerts.empty()) {
        bgfx::update(m_fishShadow.vbo, 0,
                      bgfx::copy(shadowVerts.data(), shadowVerts.size() * sizeof(glm::vec3)));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, m_fishShadow.vbo, 0, (uint32_t)shadowVerts.size());
        bgfx::setState(shadowState);
        bgfx::setStencil(shadowStencil);
        bgfx::submit(0, m_shadowShader->program);
      }
    }
  }

}

// ── Render Leaves ────────────────────────────────────────────────────

void BoidManager::RenderLeaves(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_leafShader || !TexValid(m_leafTexture) || !bgfx::isValid(m_leafVBO))
    return;

  uint64_t leafState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                     | BGFX_STATE_DEPTH_TEST_LESS
                     | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  // No cull — leaves visible from both sides

  for (int i = 0; i < MAX_LEAVES; ++i) {
    const LeafParticle &leaf = m_leaves[i];
    if (!leaf.live || leaf.alpha <= 0.0f)
      continue;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), leaf.position);
    model = glm::rotate(model, glm::radians(leaf.angle.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(leaf.angle.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(leaf.angle.z), glm::vec3(0, 0, 1));

    bgfx::setTransform(glm::value_ptr(model));
    bgfx::setVertexBuffer(0, m_leafVBO);
    bgfx::setIndexBuffer(m_leafEBO);
    m_leafShader->setTexture(0, "s_texColor", m_leafTexture);
    m_leafShader->setVec4("u_params", glm::vec4(leaf.alpha, 0.0f, 0.0f, 0.0f));
    bgfx::setState(leafState);
    bgfx::submit(0, m_leafShader->program);
  }
}

// ── Cleanup ──────────────────────────────────────────────────────────

void BoidManager::Cleanup() {
  CleanupMeshBuffers(m_birdMeshes);
  CleanupMeshBuffers(m_batMeshes);
  CleanupMeshBuffers(m_fishMeshes);

  if (bgfx::isValid(m_birdShadow.vbo)) bgfx::destroy(m_birdShadow.vbo);
  if (bgfx::isValid(m_batShadow.vbo)) bgfx::destroy(m_batShadow.vbo);
  if (bgfx::isValid(m_fishShadow.vbo)) bgfx::destroy(m_fishShadow.vbo);
  m_birdShadow.vbo = BGFX_INVALID_HANDLE;
  m_batShadow.vbo = BGFX_INVALID_HANDLE;
  m_fishShadow.vbo = BGFX_INVALID_HANDLE;

  if (bgfx::isValid(m_leafVBO)) bgfx::destroy(m_leafVBO);
  if (bgfx::isValid(m_leafEBO)) bgfx::destroy(m_leafEBO);
  m_leafVBO = BGFX_INVALID_HANDLE;
  m_leafEBO = BGFX_INVALID_HANDLE;
  TexDestroy(m_leafTexture);

  m_birdBmd.reset();
  m_batBmd.reset();
  m_fishBmd.reset();
  m_shader.reset();
  m_shadowShader.reset();
  m_leafShader.reset();
}

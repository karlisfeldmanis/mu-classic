#include "BoidManager.hpp"
#include "SoundManager.hpp"
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
  if (!m_terrainData)
    return 0.0f;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
         h01 * (1 - xd) * zd + h11 * xd * zd;
}

glm::vec3 BoidManager::sampleTerrainLight(const glm::vec3 &pos) const {
  const int SIZE = 256;
  if (m_terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);
  float gz = pos.x / 100.0f;
  float gx = pos.z / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);
  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = m_terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = m_terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = m_terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = m_terrainLightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

uint8_t BoidManager::getTerrainLayer1(float worldX, float worldZ) const {
  if (!m_terrainData)
    return 0;
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  gz = std::clamp(gz, 0, S - 1);
  gx = std::clamp(gx, 0, S - 1);
  int idx = gz * S + gx;
  if (idx < 0 || idx >= (int)m_terrainData->mapping.layer1.size())
    return 0;
  return m_terrainData->mapping.layer1[idx];
}

uint8_t BoidManager::getTerrainAttribute(float worldX, float worldZ) const {
  if (!m_terrainData)
    return 0;
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  gz = std::clamp(gz, 0, S - 1);
  gx = std::clamp(gx, 0, S - 1);
  int idx = gz * S + gx;
  if (idx < 0 || idx >= (int)m_terrainData->mapping.attributes.size())
    return 0;
  return m_terrainData->mapping.attributes[idx];
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
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");
  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

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
    glGenVertexArrays(1, &m_birdShadow.vao);
    glGenBuffers(1, &m_birdShadow.vbo);
    glBindVertexArray(m_birdShadow.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_birdShadow.vbo);
    glBufferData(GL_ARRAY_BUFFER, totalVerts * sizeof(glm::vec3), nullptr,
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    m_birdShadow.vertexCount = totalVerts;

    std::cout << "[Boid] Loaded Bird01.bmd (" << m_birdBmd->Bones.size()
              << " bones, " << m_birdBmd->Meshes.size() << " meshes)"
              << std::endl;
  } else {
    std::cerr << "[Boid] Failed to load Bird01.bmd" << std::endl;
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
    glGenVertexArrays(1, &m_fishShadow.vao);
    glGenBuffers(1, &m_fishShadow.vbo);
    glBindVertexArray(m_fishShadow.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_fishShadow.vbo);
    glBufferData(GL_ARRAY_BUFFER, totalVerts * sizeof(glm::vec3), nullptr,
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    m_fishShadow.vertexCount = totalVerts;

    std::cout << "[Boid] Loaded Fish01.bmd (" << m_fishBmd->Bones.size()
              << " bones, " << m_fishBmd->Meshes.size() << " meshes)"
              << std::endl;
  } else {
    std::cerr << "[Boid] Failed to load Fish01.bmd" << std::endl;
  }

  // Initialize all boids/fish as dead with staggered spawn delay
  for (int i = 0; i < MAX_BOIDS; ++i) {
    m_boids[i].live = false;
    m_boids[i].respawnDelay = 2.0f + (float)i * 3.0f; // Stagger: 2s, 5s, 8s, ...
  }
  for (auto &f : m_fishs)
    f.live = false;

  // ── Falling leaves (Main 5.2: ZzzEffectFireLeave.cpp) ──────────────
  m_leafShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/leaf.vert" : "../shaders/leaf.vert",
      shaderTest.good() ? "shaders/leaf.frag" : "../shaders/leaf.frag");

  // Load leaf texture (OZT for alpha, fallback to OZJ)
  std::string leafPath = dataPath + "/World1/leaf01.OZT";
  m_leafTexture = TextureLoader::LoadOZT(leafPath);
  if (!m_leafTexture) {
    leafPath = dataPath + "/World1/leaf01.OZJ";
    m_leafTexture = TextureLoader::LoadOZJ(leafPath);
  }

  if (m_leafTexture) {
    // Create leaf quad VAO (3x3 unit quad in XZ plane, matching Main 5.2
    // RenderPlane3D(3,3))
    float quadVerts[] = {
        // pos(x,y,z),       uv(u,v)
        -3.0f, 0.0f, -3.0f, 0.0f, 0.0f, //
        3.0f,  0.0f, -3.0f, 1.0f, 0.0f,  //
        3.0f,  0.0f, 3.0f,  1.0f, 1.0f,  //
        -3.0f, 0.0f, 3.0f,  0.0f, 1.0f,  //
    };
    unsigned int quadIndices[] = {0, 1, 2, 0, 2, 3};

    glGenVertexArrays(1, &m_leafVAO);
    glGenBuffers(1, &m_leafVBO);
    glGenBuffers(1, &m_leafEBO);
    glBindVertexArray(m_leafVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_leafVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_leafEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    std::cout << "[Boid] Leaf texture loaded" << std::endl;
  }

  for (auto &leaf : m_leaves)
    leaf.live = false;

  std::cout << "[Boid] Ambient creature system initialized" << std::endl;
}

// ── Bird AI ──────────────────────────────────────────────────────────

void BoidManager::moveBird(Boid &b, const glm::vec3 &heroPos, int heroAction) {
  // Main 5.2: MoveBird (GOBoid.cpp:948-1001)
  // Coordinate mapping: MU Position[0]=worldX, Position[1]=worldZ (in our system)
  //   MU Position[2] = height = our worldY

  // Birds always fly — no landing/ground states
  b.ai = BoidAI::FLY;
  b.velocity = 1.0f;
  b.position.y += (float)(rand() % 16 - 8);
  float terrainH = getTerrainHeight(b.position.x, b.position.z);
  float relH = b.position.y - terrainH;
  if (relH < 200.0f)
    b.direction.y = 10.0f;
  else if (relH > 600.0f)
    b.direction.y = -10.0f;
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

    // Animate
    if (b.action >= 0 && b.action < (int)m_birdBmd->Actions.size()) {
      int numKeys = m_birdBmd->Actions[b.action].NumAnimationKeys;
      if (numKeys > 1) {
        b.animFrame += 1.0f * dt * 25.0f; // PlaySpeed=1.0 at 25fps
        if (b.animFrame >= (float)numKeys)
          b.animFrame = std::fmod(b.animFrame, (float)numKeys);
      }
    }

    // Move
    moveBird(b, heroPos, heroAction);
    moveBoidFlock(b, i); // Flocking: steer toward/away from neighbors
    moveBoidGroup(b);    // Apply velocity in facing direction

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
  // MU coords → our coords: Position[0]→X, Position[1]→Z, Position[2]→Y
  leaf.live = true;
  leaf.alpha = 1.0f;
  leaf.onGround = false;

  leaf.position.x = heroPos.x + (float)(rand() % 1600 - 800);
  leaf.position.z = heroPos.z + (float)(rand() % 1400 - 500);
  leaf.position.y = heroPos.y + (float)(rand() % 300 + 50);

  // Wind velocity
  leaf.velocity.x = -(float)(rand() % 64 + 64) * 0.1f;
  // Main 5.2: if behind camera, reverse wind direction
  if (leaf.position.z < heroPos.z + 400.0f)
    leaf.velocity.x = -leaf.velocity.x + 3.2f;
  leaf.velocity.z = (float)(rand() % 32 - 16) * 0.1f;
  leaf.velocity.y = (float)(rand() % 32 - 16) * 0.1f;

  // Angular velocity (degrees per tick)
  leaf.turningForce.x = (float)(rand() % 16 - 8) * 0.1f;
  leaf.turningForce.z = (float)(rand() % 64 - 32) * 0.1f;
  leaf.turningForce.y = (float)(rand() % 16 - 8) * 0.1f;

  leaf.angle = glm::vec3(0.0f);
}

void BoidManager::updateLeaves(float dt, const glm::vec3 &heroPos) {
  if (!m_leafTexture)
    return;

  float ticks = dt * 25.0f; // Convert to tick-based (25fps)

  for (int i = 0; i < MAX_LEAVES; ++i) {
    LeafParticle &leaf = m_leaves[i];

    if (!leaf.live) {
      spawnLeaf(leaf, heroPos);
      continue;
    }

    // Main 5.2: MoveEtcLeaf (ZzzEffectFireLeave.cpp:376-395)
    float terrainH = getTerrainHeight(leaf.position.x, leaf.position.z);

    if (leaf.position.y <= terrainH) {
      // On ground: snap to terrain, fade light (alpha) by 0.05/tick
      leaf.position.y = terrainH;
      leaf.onGround = true;
      leaf.alpha -= 0.05f * ticks;
      if (leaf.alpha <= 0.0f)
        leaf.live = false;
    } else {
      // Airborne: add turbulence, then move
      leaf.velocity.x += (float)(rand() % 16 - 8) * 0.1f;
      leaf.velocity.z += (float)(rand() % 16 - 8) * 0.1f;
      leaf.velocity.y += (float)(rand() % 16 - 8) * 0.1f;
      leaf.position += leaf.velocity * ticks;
    }

    // Rotate by turning force
    leaf.angle += leaf.turningForce * ticks;
  }
}

void BoidManager::Update(float deltaTime, const glm::vec3 &heroPos,
                          int heroAction, float worldTime) {
  m_worldTime = worldTime;
  updateBoids(deltaTime, heroPos, heroAction);
  updateFishs(deltaTime, heroPos);
  updateLeaves(deltaTime, heroPos);
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

  m_shader->setMat4("model", model);
  m_shader->setFloat("objectAlpha", b.alpha);
  m_shader->setVec3("terrainLight", sampleTerrainLight(b.position));

  for (auto &mb : m_birdMeshes) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
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

  m_shader->setMat4("model", model);
  m_shader->setFloat("objectAlpha", f.alpha);
  m_shader->setVec3("terrainLight", sampleTerrainLight(f.position));

  for (auto &mb : m_fishMeshes) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }
}

void BoidManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                          const glm::vec3 &camPos) {
  if (!m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
  m_shader->setFloat("uFogNear", 1500.0f);
  m_shader->setFloat("uFogFar", 3500.0f);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Render birds
  for (int i = 0; i < MAX_BOIDS; ++i) {
    renderBoid(m_boids[i], view, proj);
  }

  // Render fish
  for (int i = 0; i < MAX_FISHS; ++i) {
    renderFish(m_fishs[i], view, proj);
  }

  // Reset objectAlpha
  m_shader->setFloat("objectAlpha", 1.0f);
}

void BoidManager::RenderShadows(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_shadowShader)
    return;

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  // Bird shadows
  if (m_birdBmd && m_birdShadow.vao) {
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
      m_shadowShader->setMat4("model", model);

      float facingRad = glm::radians(b.angle.z + 90.0f);
      float cosF = std::cos(facingRad);
      float sinF = std::sin(facingRad);

      std::vector<glm::vec3> shadowVerts;
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
        glBindBuffer(GL_ARRAY_BUFFER, m_birdShadow.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        shadowVerts.size() * sizeof(glm::vec3),
                        shadowVerts.data());
        glBindVertexArray(m_birdShadow.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
      }
    }
  }

  // Fish shadows (similar but using fish mesh)
  if (m_fishBmd && m_fishShadow.vao) {
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
      m_shadowShader->setMat4("model", model);

      float facingRad = glm::radians(f.angle.z + 90.0f);
      float cosF = std::cos(facingRad);
      float sinF = std::sin(facingRad);

      std::vector<glm::vec3> shadowVerts;
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
        glBindBuffer(GL_ARRAY_BUFFER, m_fishShadow.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        shadowVerts.size() * sizeof(glm::vec3),
                        shadowVerts.data());
        glBindVertexArray(m_fishShadow.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
      }
    }
  }

  glBindVertexArray(0);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

// ── Render Leaves ────────────────────────────────────────────────────

void BoidManager::RenderLeaves(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_leafShader || !m_leafTexture || !m_leafVAO)
    return;

  m_leafShader->use();
  m_leafShader->setMat4("projection", proj);
  m_leafShader->setMat4("view", view);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_leafTexture);
  m_leafShader->setInt("leafTexture", 0);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE); // Leaves visible from both sides

  glBindVertexArray(m_leafVAO);

  for (int i = 0; i < MAX_LEAVES; ++i) {
    const LeafParticle &leaf = m_leaves[i];
    if (!leaf.live || leaf.alpha <= 0.0f)
      continue;

    // Model matrix: translate + rotate by Euler angles
    glm::mat4 model = glm::translate(glm::mat4(1.0f), leaf.position);
    model = glm::rotate(model, glm::radians(leaf.angle.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(leaf.angle.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(leaf.angle.z), glm::vec3(0, 0, 1));

    m_leafShader->setMat4("model", model);
    m_leafShader->setFloat("leafAlpha", leaf.alpha);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  }

  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

// ── Cleanup ──────────────────────────────────────────────────────────

void BoidManager::Cleanup() {
  CleanupMeshBuffers(m_birdMeshes);
  CleanupMeshBuffers(m_fishMeshes);

  if (m_birdShadow.vao)
    glDeleteVertexArrays(1, &m_birdShadow.vao);
  if (m_birdShadow.vbo)
    glDeleteBuffers(1, &m_birdShadow.vbo);
  if (m_fishShadow.vao)
    glDeleteVertexArrays(1, &m_fishShadow.vao);
  if (m_fishShadow.vbo)
    glDeleteBuffers(1, &m_fishShadow.vbo);

  // Leaf resources
  if (m_leafTexture)
    glDeleteTextures(1, &m_leafTexture);
  if (m_leafVAO)
    glDeleteVertexArrays(1, &m_leafVAO);
  if (m_leafVBO)
    glDeleteBuffers(1, &m_leafVBO);
  if (m_leafEBO)
    glDeleteBuffers(1, &m_leafEBO);

  m_birdBmd.reset();
  m_fishBmd.reset();
  m_shader.reset();
  m_shadowShader.reset();
  m_leafShader.reset();
}

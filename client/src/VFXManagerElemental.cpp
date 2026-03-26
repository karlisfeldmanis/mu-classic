#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// Main 5.2: AT_SKILL_BLAST — twin sky-strike bolts at target position
// Creates 2 Blast01.bmd orbs that fall from sky with gravity and explode on impact
void VFXManager::SpawnLightningStrike(const glm::vec3 &targetPos) {
  for (int i = 0; i < 2; ++i) {
    LightningBolt bolt;
    // Main 5.2: Position += (rand%100+200, rand%100-50, rand%500+300)
    // Asymmetric X offset so bolts come from the side matching diagonal fall
    float scatterX = (float)(rand() % 100 + 200); // +200 to +300
    float scatterZ = (float)(rand() % 100 - 50);  // -50 to +50
    float height = (float)(rand() % 500 + 300);   // +300 to +800 above target
    bolt.position = targetPos + glm::vec3(scatterX, height, scatterZ);

    // Main 5.2: Direction=(0,0,-50-rand%50), Angle=(0,20,0) — 20deg diagonal fall
    float fallSpeed = (50.0f + (float)(rand() % 50)) * 25.0f; // 1250-2500 units/sec
    float angleRad = glm::radians(20.0f);
    bolt.velocity = glm::vec3(-fallSpeed * std::sin(angleRad),
                              -fallSpeed * std::cos(angleRad),
                              0.0f);

    // Main 5.2: Scale = (rand()%8+10)*0.1f = 1.0-1.8
    bolt.scale = (float)(rand() % 8 + 10) * 0.1f;
    bolt.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
    bolt.maxLifetime = 1.2f; // 30 ticks at 25fps
    bolt.lifetime = bolt.maxLifetime;
    bolt.impacted = false;
    bolt.impactTimer = 0.0f;
    bolt.numTrail = 0;
    bolt.trailTimer = 0.0f;
    m_lightningBolts.push_back(bolt);
  }
}

// Single vertical sky bolt for Lightning spell monster hit effect.
// Lighter than SpawnLightningStrike (1 bolt straight down vs 2 angled bolts).
void VFXManager::SpawnLightningImpactBolt(const glm::vec3 &targetPos) {
  LightningBolt bolt;
  // Minimal horizontal scatter — bolt comes from directly above
  float scatterX = (float)(rand() % 40 - 20); // -20 to +20
  float scatterZ = (float)(rand() % 40 - 20); // -20 to +20
  float height = (float)(rand() % 200 + 300);  // +300 to +500 above target
  bolt.position = targetPos + glm::vec3(scatterX, height, scatterZ);

  // Straight vertical drop (no 20-degree angle like Cometfall)
  float fallSpeed = (60.0f + (float)(rand() % 40)) * 25.0f; // 1500-2500 units/sec
  bolt.velocity = glm::vec3(0.0f, -fallSpeed, 0.0f);

  bolt.scale = (float)(rand() % 6 + 8) * 0.1f; // 0.8-1.3 (smaller than Cometfall)
  bolt.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
  bolt.maxLifetime = 0.8f; // 20 ticks
  bolt.lifetime = bolt.maxLifetime;
  bolt.impacted = false;
  bolt.impactTimer = 0.0f;
  bolt.numTrail = 0;
  bolt.trailTimer = 0.0f;
  m_lightningBolts.push_back(bolt);
}

// Main 5.2: Meteorite — MODEL_FIRE SubType 0
// Position offset: X += 130+rand%32, Z(height) += 400
// Direction = (0, 0, -50) rotated by Angle(0, 20, 0) — diagonal fall
// Trail: per-tick BITMAP_FIRE SubType 5 billboard sprites (no ribbon)
void VFXManager::SpawnMeteorStrike(const glm::vec3 &targetPos) {
  MeteorBolt m;
  // Main 5.2: Position[0] += 130+rand%32, Position[2] += 400
  float offsetX = 130.0f + (float)(rand() % 32);
  float height = 400.0f;
  m.position = targetPos + glm::vec3(offsetX, height, 0.0f);

  // Main 5.2: Direction=(0,0,-50) rotated by Angle(0,20,0) — diagonal fall
  float fallSpeed = 50.0f * 25.0f; // 1250 units/sec
  float angleRad = glm::radians(20.0f);
  m.velocity = glm::vec3(-std::sin(angleRad) * fallSpeed,
                          -std::cos(angleRad) * fallSpeed,
                          0.0f);

  // Main 5.2: Scale = (rand%8+10)*0.1 = 1.0-1.7
  m.scale = (float)(rand() % 8 + 10) * 0.1f;
  m.maxLifetime = 1.6f; // 40 ticks at 25fps
  m.lifetime = m.maxLifetime;
  m.impacted = false;
  m.impactTimer = 0.0f;
  m.trailTimer = 0.0f;
  m_meteorBolts.push_back(m);
}

void VFXManager::updateLightningBolts(float dt) {
  for (auto &b : m_lightningBolts) {
    if (b.impacted) {
      b.impactTimer += dt;
      continue;
    }
    b.lifetime -= dt;
    b.position += b.velocity * dt;
    // Main 5.2: Angle fixed at (0,20,0), no rotation during flight

    // BITMAP_JOINT_ENERGY trail + scatter particles — update at tick rate (~25fps)
    b.trailTimer += dt;
    if (b.trailTimer >= 0.04f) {
      b.trailTimer -= 0.04f;
      int newCount =
          std::min(b.numTrail + 1, (int)LightningBolt::MAX_TRAIL);
      for (int j = newCount - 1; j > 0; --j)
        b.trail[j] = b.trail[j - 1];
      b.trail[0] = b.position;
      b.numTrail = newCount;
    }

    // Check terrain collision
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(b.position.x, b.position.z)
                         : 0.0f;
    if (b.position.y <= groundH || b.lifetime <= 0.0f) {
      b.position.y = groundH;
      b.velocity = glm::vec3(0.0f);
      b.impacted = true;
      b.impactTimer = 0.0f;
      // Main 5.2: particles at Position[2]+80 (80 above ground)
      glm::vec3 impactAbove = b.position + glm::vec3(0, 80, 0);
      // Subtle impact: small debris + single flash
      SpawnBurst(ParticleType::HIT_SPARK, b.position + glm::vec3(0, 20, 0), 3);
      SpawnBurst(ParticleType::FLARE, impactAbove, 1);
    }
  }
  // Remove expired bolts (impacted + trail fully faded)
  m_lightningBolts.erase(
      std::remove_if(m_lightningBolts.begin(), m_lightningBolts.end(),
                     [](const LightningBolt &b) {
                       return b.impacted && b.impactTimer > 0.8f;
                     }),
      m_lightningBolts.end());
}

void VFXManager::updateMeteorBolts(float dt) {
  for (auto &m : m_meteorBolts) {
    if (m.impacted) {
      m.impactTimer += dt;
      continue;
    }
    m.lifetime -= dt;
    m.position += m.velocity * dt;
    m.rotation += 360.0f * dt; // Spin during flight

    // Animate Fire01.bmd
    if (m_fireBmd && !m_fireBmd->Actions.empty()) {
      int numKeys = m_fireBmd->Actions[0].NumAnimationKeys;
      if (numKeys > 1) {
        m.animFrame += 10.0f * dt;
        if (m.animFrame >= (float)numKeys)
          m.animFrame = std::fmod(m.animFrame, (float)numKeys);
      }
    }

    // Main 5.2: spawn BITMAP_FIRE SubType 5 every tick — fire trail particles
    m.trailTimer += dt;
    if (m.trailTimer >= 0.04f) {
      m.trailTimer -= 0.04f;
      SpawnBurst(ParticleType::SPELL_FIRE, m.position, 2);
    }

    // Terrain collision
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(m.position.x, m.position.z)
                         : 0.0f;
    if (m.position.y <= groundH || m.lifetime <= 0.0f) {
      m.position.y = groundH;
      m.velocity = glm::vec3(0.0f);
      m.impacted = true;
      m.impactTimer = 0.0f;
      // Main 5.2 impact: fire/flare particle bursts
      glm::vec3 impactAbove = m.position + glm::vec3(0, 80, 0);
      SpawnBurst(ParticleType::SPELL_METEOR, impactAbove, 25);
      SpawnBurst(ParticleType::SPELL_FIRE, impactAbove, 15);
      SpawnBurst(ParticleType::FLARE, impactAbove, 5);
      SpawnBurst(ParticleType::HIT_SPARK, m.position + glm::vec3(0, 30, 0), 10);
    }
  }
  m_meteorBolts.erase(
      std::remove_if(m_meteorBolts.begin(), m_meteorBolts.end(),
                     [](const MeteorBolt &m) {
                       return m.impacted && m.impactTimer > 0.5f;
                     }),
      m_meteorBolts.end());
}

void VFXManager::renderMeteorBolts(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_meteorBolts.empty() || m_fireMeshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD;
  for (const auto &m : m_meteorBolts) {
    if (m.impacted) continue;

    // Re-skin fire model with current animation frame
    if (m_fireBmd && !m_fireBmd->Actions.empty()) {
      auto bones = ComputeBoneMatricesInterpolated(m_fireBmd.get(), 0, m.animFrame);
      for (int mi = 0; mi < (int)m_fireMeshes.size() && mi < (int)m_fireBmd->Meshes.size(); ++mi)
        RetransformMeshWithBones(m_fireBmd->Meshes[mi], bones, m_fireMeshes[mi]);
    }

    float alpha = std::min(1.0f, m.lifetime / m.maxLifetime * 4.0f);
    // Main 5.2: BlendMeshLight = 1.0 (default init, not modified for SubType 0)
    m_modelShader->setVec4("u_params", glm::vec4(alpha, 1.0f, 0, 0));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), m.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(20.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(m.rotation), glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(m.scale));
    for (auto &mb : m_fireMeshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
}

// ============================================================
// Ice Spell (AT_SKILL_ICE, skill 7) — Main 5.2 MODEL_ICE + MODEL_ICE_SMALL
// ============================================================

void VFXManager::SpawnIceStrike(const glm::vec3 &targetPos) {
  // Main 5.2: 1x MODEL_ICE crystal at target
  // LifeTime=50 but ~25 ticks visible (5 growth + 20 fade)
  IceCrystal crystal;
  crystal.position = targetPos;
  crystal.scale = 0.8f;       // Main 5.2: Scale = 0.8
  crystal.alpha = 1.0f;
  crystal.maxLifetime = 1.0f; // ~25 ticks at 25fps (5 growth + 20 fade)
  crystal.lifetime = crystal.maxLifetime;
  crystal.fadePhase = false;
  crystal.smokeTimer = 0.0f;
  m_iceCrystals.push_back(crystal);

  // Main 5.2: 5x MODEL_ICE_SMALL debris shards with bouncing physics
  for (int i = 0; i < 5; ++i) {
    IceShard shard;
    shard.position = targetPos + glm::vec3(0, 50.0f, 0);
    // Random outward velocity (Main 5.2: Direction = random horizontal spread)
    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
    float speed = (float)(rand() % 20 + 10); // 10-30 units/tick → scale to per-sec
    shard.velocity = glm::vec3(std::cos(angle) * speed * 25.0f,
                               0.0f,
                               std::sin(angle) * speed * 25.0f);
    // Main 5.2: Gravity = rand()%16+8 (upward initial, decreases by 3/tick)
    shard.gravity = (float)(rand() % 16 + 8) * 25.0f; // Convert to per-second
    shard.angleX = (float)(rand() % 360) * 3.14159f / 180.0f;
    shard.angleZ = (float)(rand() % 360) * 3.14159f / 180.0f; // Main 5.2: Angle[2]
    // Main 5.2: Scale = (rand()%4+8)*0.1 = 0.8-1.1, reduced for subtler debris
    shard.scale = (float)(rand() % 3 + 2) * 0.05f; // 0.10-0.20
    // Main 5.2: LifeTime = rand()%16+32 = 32-47 ticks = 1.28-1.88s
    shard.lifetime = (float)(rand() % 16 + 32) / 25.0f;
    shard.smokeTimer = 0.0f;
    m_iceShards.push_back(shard);
  }

  // Initial impact particles
  SpawnBurst(ParticleType::SPELL_ICE, targetPos + glm::vec3(0, 30, 0), 8);
}

void VFXManager::updateIceCrystals(float dt) {
  for (auto &c : m_iceCrystals) {
    c.lifetime -= dt;

    // Main 5.2: AnimationFrame >= 5 triggers fade phase (5 ticks = 0.2s)
    float elapsed = c.maxLifetime - c.lifetime;
    if (!c.fadePhase && elapsed >= 0.2f) {
      c.fadePhase = true;
    }

    if (c.fadePhase) {
      // Main 5.2: Alpha -= 0.05 per tick (25fps) → 1.25 per second
      c.alpha -= 1.25f * dt;

      // Subtle smoke wisps during crystal fade (~3 per second)
      c.smokeTimer += dt;
      if (c.smokeTimer >= 0.33f) {
        c.smokeTimer -= 0.33f;
        glm::vec3 smokePos = c.position + glm::vec3(
            (float)(rand() % 40 - 20), (float)(rand() % 60 + 20),
            (float)(rand() % 40 - 20));
        SpawnBurst(ParticleType::SMOKE, smokePos, 1);
      }
    }
  }
  m_iceCrystals.erase(
      std::remove_if(m_iceCrystals.begin(), m_iceCrystals.end(),
                     [](const IceCrystal &c) { return c.alpha <= 0.0f; }),
      m_iceCrystals.end());
}

void VFXManager::updateIceShards(float dt) {
  for (auto &s : m_iceShards) {
    s.lifetime -= dt;

    // Main 5.2: Position += Direction, Direction *= 0.9 (decay per tick)
    s.position += s.velocity * dt;
    s.velocity *= (1.0f - 2.5f * dt); // ~0.9 per tick at 25fps

    // Main 5.2: Gravity -= 3 per tick, Position[2] += Gravity
    // Gravity stored as per-second units (initial 200-575 upward)
    s.gravity -= 75.0f * dt; // 3/tick * 25tps = 75/sec
    s.position.y += s.gravity * dt;

    // Main 5.2: Tumble rotation (Angle[0] -= Scale * 32, Angle[2] spins too)
    s.angleX -= s.scale * 800.0f * dt; // 32 * 25fps
    s.angleZ += s.scale * 400.0f * dt; // Z-axis spin

    // Terrain collision — bounce
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(s.position.x, s.position.z)
                         : 0.0f;
    if (s.position.y < groundH) {
      s.position.y = groundH;
      // Main 5.2: Gravity = -Gravity * 0.5 (bounce at 50% restitution)
      s.gravity = -s.gravity * 0.5f;
      // Main 5.2: LifeTime -= 4
      s.lifetime -= 4.0f / 25.0f; // 4 ticks = 0.16s
      // Main 5.2: Angle[0] -= Scale * 128 (faster tumble on bounce)
      s.angleX -= s.scale * 128.0f;
      s.angleZ += s.scale * 64.0f;
    }

    // Main 5.2: ~10% chance per tick (rand()%10==0) to spawn BITMAP_SMOKE
    s.smokeTimer += dt;
    while (s.smokeTimer >= 0.04f) { // tick rate
      s.smokeTimer -= 0.04f;
      if (rand() % 10 == 0) {
        SpawnBurst(ParticleType::SMOKE, s.position, 1);
      }
    }
  }
  m_iceShards.erase(
      std::remove_if(m_iceShards.begin(), m_iceShards.end(),
                     [](const IceShard &s) { return s.lifetime <= 0.0f; }),
      m_iceShards.end());
}

void VFXManager::renderIceCrystals(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_iceCrystals.empty() || m_iceMeshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD;
  for (const auto &c : m_iceCrystals) {
    m_modelShader->setVec4("u_params", glm::vec4(c.alpha, 1.0f, 0, 0));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), c.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(c.scale));
    for (const auto &mb : m_iceMeshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
}

void VFXManager::renderIceShards(const glm::mat4 &view,
                                  const glm::mat4 &projection) {
  if (m_iceShards.empty() || m_iceSmallMeshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  // Main 5.2: BlendMesh=0 → EnableAlphaBlend() (additive), BlendMeshLight=0.3
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD;
  for (const auto &s : m_iceShards) {
    float alpha = std::min(1.0f, s.lifetime * 2.0f);
    // Main 5.2: BlendMeshLight = 0.3 (additive glow)
    m_modelShader->setVec4("u_params", glm::vec4(alpha, 0.5f, 0, 0));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), s.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, s.angleX, glm::vec3(1, 0, 0));
    model = glm::rotate(model, s.angleZ, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(s.scale));
    for (const auto &mb : m_iceSmallMeshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
}

// Main 5.2: MODEL_SKILL_BLAST — render falling Blast01.bmd orbs + vertical beam
void VFXManager::renderLightningBolts(const glm::mat4 &view,
                                       const glm::mat4 &projection) {
  if (m_lightningBolts.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);

  // Pass 1: 3D models (Blast01.bmd)
  if (!m_blastMeshes.empty()) {
    m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
    m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
    m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
    m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
    m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
    m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
    m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
    m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
    uint64_t mState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                    | BGFX_STATE_BLEND_ADD;
    for (const auto &b : m_lightningBolts) {
      if (b.impacted) continue;
      float alpha = std::min(1.0f, b.lifetime / b.maxLifetime * 4.0f);
      m_modelShader->setVec4("u_params", glm::vec4(alpha, 1.0f, 0, 0));
      glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, glm::radians(20.0f), glm::vec3(0, 1, 0));
      model = glm::scale(model, glm::vec3(b.scale));
      for (const auto &mb : m_blastMeshes) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        bgfx::setTransform(glm::value_ptr(model));
        m_modelShader->setTexture(0, "s_texColor", mb.texture);
        bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        bgfx::setState(mState);
        bgfx::submit(0, m_modelShader->program);
      }
    }
  }

  // Pass 2: Energy trail ribbons
  if (TexValid(m_energyTexture) && m_lineShader) {
    uint64_t rState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    m_lineShader->setVec4("u_lineMode", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    for (const auto &b : m_lightningBolts) {
      if (b.numTrail < 2) continue;
      float alpha = b.impacted ? std::max(0.0f, 1.0f - b.impactTimer * 7.0f)
                               : std::min(1.0f, b.lifetime / b.maxLifetime * 4.0f);
      if (alpha <= 0.01f) continue;
      float flicker = 0.7f + 0.3f * ((float)(rand() % 100) / 100.0f);
      glm::vec3 trailColor = glm::vec3(0.3f, 0.5f, 1.0f) * flicker;
      float hw = 50.0f;

      // Build triangle list from trail (convert quad strips to triangles)
      std::vector<RibbonVertex> verts;
      verts.reserve(b.numTrail * 12);
      for (int j = 0; j < b.numTrail - 1; ++j) {
        glm::vec3 p0 = b.trail[j], p1 = b.trail[j + 1];
        float u0 = (float)(b.numTrail - j) / (float)(LightningBolt::MAX_TRAIL);
        float u1 = (float)(b.numTrail - j - 1) / (float)(LightningBolt::MAX_TRAIL);
        glm::vec3 seg = p1 - p0;
        float segLen = glm::length(seg);
        if (segLen < 0.01f) continue;
        glm::vec3 dir = seg / segLen;
        glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
        if (glm::length(glm::cross(dir, glm::vec3(0, 1, 0))) < 0.01f) right = glm::vec3(1, 0, 0);
        glm::vec3 up = glm::normalize(glm::cross(right, dir));
        // Face 1
        verts.push_back({p0 - right * hw, {u0, 0.0f}});
        verts.push_back({p0 + right * hw, {u0, 1.0f}});
        verts.push_back({p1 + right * hw, {u1, 1.0f}});
        verts.push_back({p0 - right * hw, {u0, 0.0f}});
        verts.push_back({p1 + right * hw, {u1, 1.0f}});
        verts.push_back({p1 - right * hw, {u1, 0.0f}});
        // Face 2
        verts.push_back({p0 - up * hw, {u0, 0.0f}});
        verts.push_back({p0 + up * hw, {u0, 1.0f}});
        verts.push_back({p1 + up * hw, {u1, 1.0f}});
        verts.push_back({p0 - up * hw, {u0, 0.0f}});
        verts.push_back({p1 + up * hw, {u1, 1.0f}});
        verts.push_back({p1 - up * hw, {u1, 0.0f}});
      }
      if (verts.empty()) continue;
      if ((int)verts.size() > MAX_RIBBON_VERTS) verts.resize(MAX_RIBBON_VERTS);
      uint32_t nv = (uint32_t)verts.size();
      bgfx::TransientVertexBuffer tvb;
      if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) < nv) continue;
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
      m_lineShader->setVec4("u_lineColor", glm::vec4(trailColor, alpha));
      m_lineShader->setTexture(0, "s_ribbonTex", m_energyTexture);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(rState);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

// Main 5.2: MODEL_POISON — spawn green cloud at target position
// ZzzCharacter.cpp AT_SKILL_POISON: CreateEffect(MODEL_POISON, to->Position, o->Angle, o->Light)
// + 10x BITMAP_SMOKE particles with light (0.4, 0.6, 1.0)
void VFXManager::SpawnPoisonCloud(const glm::vec3 &targetPos) {
  PoisonCloud pc;
  pc.position = targetPos;
  pc.rotation = 0.0f;      // Main 5.2: no rotation set
  pc.lifetime = 1.6f;      // 40 ticks @ 25fps
  pc.maxLifetime = 1.6f;
  pc.alpha = 1.0f;
  pc.scale = 1.0f;         // Main 5.2: Scale = 1.f
  m_poisonClouds.push_back(pc);

  // Main 5.2: 10x BITMAP_SMOKE at target, Light=(0.4, 0.6, 1.0), SubType=1
  SpawnBurst(ParticleType::SMOKE, targetPos + glm::vec3(0, 30, 0), 10);
}

// Main 5.2: BITMAP_FLAME SubType 0 — persistent ground fire at target position
// ZzzEffect.cpp: CreateEffect(BITMAP_FLAME, Position=target, LifeTime=40)
// Every tick: 6 BITMAP_FLAME particles + AddTerrainLight(1.0, 0.4, 0.0, range=3)
void VFXManager::SpawnFlameGround(const glm::vec3 &targetPos) {
  FlameGround fg;
  fg.position = targetPos;
  fg.lifetime = 1.6f;     // 40 ticks @ 25fps
  fg.maxLifetime = 1.6f;
  fg.tickTimer = 0.0f;
  m_flameGrounds.push_back(fg);

  // Initial burst — Main 5.2: first tick spawns particles immediately
  SpawnBurst(ParticleType::SPELL_FLAME, targetPos + glm::vec3(0, 10, 0), 6);
  SpawnBurst(ParticleType::FLARE, targetPos + glm::vec3(0, 30, 0), 2);
}

void VFXManager::updateFlameGrounds(float dt) {
  for (int i = (int)m_flameGrounds.size() - 1; i >= 0; --i) {
    auto &fg = m_flameGrounds[i];
    fg.lifetime -= dt;
    if (fg.lifetime <= 0.0f) {
      m_flameGrounds[i] = m_flameGrounds.back();
      m_flameGrounds.pop_back();
      continue;
    }

    // Tick-based spawning: 6 BITMAP_FLAME particles per tick (25fps = 0.04s)
    fg.tickTimer += dt;
    while (fg.tickTimer >= 0.04f) {
      fg.tickTimer -= 0.04f;

      // Main 5.2: 6 BITMAP_FLAME particles with random +-25 offset
      SpawnBurst(ParticleType::SPELL_FLAME,
                 fg.position + glm::vec3(0, 10, 0), 6);

      // Main 5.2: 1-in-8 chance per tick — stone debris (substitute with sparks)
      if (rand() % 8 == 0) {
        SpawnBurst(ParticleType::HIT_SPARK,
                   fg.position + glm::vec3(0, 20, 0), 3);
      }
    }
  }
}

// Main 5.2: RenderTerrainAlphaBitmap(BITMAP_FLAME, pos, 2.f, 2.f, Light, rotation)
// Flat terrain-projected fire sprite, flickering luminosity 0.8-1.2
void VFXManager::renderFlameGrounds(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_flameGrounds.empty() || !m_lineShader || !TexValid(m_flameTexture)) return;
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  m_lineShader->setVec4("u_lineMode", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  for (const auto &fg : m_flameGrounds) {
    float lum = (float)(rand() % 4 + 8) * 0.1f;
    float alpha = 1.0f;
    if (fg.lifetime < fg.maxLifetime * 0.25f) alpha = fg.lifetime / (fg.maxLifetime * 0.25f);
    float halfSize = 100.0f;
    glm::vec3 right(halfSize, 0.0f, 0.0f), fwd(0.0f, 0.0f, halfSize);
    glm::vec3 pos = fg.position + glm::vec3(0.0f, 2.0f, 0.0f);
    RibbonVertex verts[6];
    verts[0] = {pos - right - fwd, {0, 0}}; verts[1] = {pos + right - fwd, {1, 0}};
    verts[2] = {pos + right + fwd, {1, 1}}; verts[3] = {pos - right - fwd, {0, 0}};
    verts[4] = {pos + right + fwd, {1, 1}}; verts[5] = {pos - right + fwd, {0, 1}};
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(6, m_ribbonLayout) < 6) continue;
    bgfx::allocTransientVertexBuffer(&tvb, 6, m_ribbonLayout);
    memcpy(tvb.data, verts, sizeof(verts));
    m_lineShader->setVec4("u_lineColor", glm::vec4(lum, lum, lum, alpha));
    m_lineShader->setTexture(0, "s_ribbonTex", m_flameTexture);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(state);
    bgfx::submit(0, m_lineShader->program);
  }
}

void VFXManager::updatePoisonClouds(float dt) {
  for (int i = (int)m_poisonClouds.size() - 1; i >= 0; --i) {
    auto &pc = m_poisonClouds[i];
    pc.lifetime -= dt;
    if (pc.lifetime <= 0.0f) {
      m_poisonClouds[i] = m_poisonClouds.back();
      m_poisonClouds.pop_back();
      continue;
    }
    // Main 5.2 MoveEffect: Alpha = LifeTime * 0.1, BlendMeshLight = LifeTime * 0.1
    // LifeTime counts down from 40. Convert: ticksRemaining = lifetime / 0.04
    float ticksRemaining = pc.lifetime / 0.04f;
    pc.alpha = std::min(1.0f, ticksRemaining * 0.1f);
    // No rotation in Main 5.2 — poison cloud is stationary
  }
}

void VFXManager::renderPoisonClouds(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_poisonClouds.empty() || m_poisonMeshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  for (const auto &pc : m_poisonClouds) {
    float ticksRemaining = pc.lifetime / 0.04f;
    float blendMeshLight = ticksRemaining * 0.1f;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), pc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(pc.scale));
    m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
    for (const auto &mb : m_poisonMeshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bool isGlow = (mb.bmdTextureId == 1);
      uint64_t state;
      if (isGlow) {
        m_modelShader->setVec4("u_params", glm::vec4(pc.alpha, blendMeshLight, 0, 0));
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
              | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
      } else {
        m_modelShader->setVec4("u_params", glm::vec4(pc.alpha, 1.0f, 0, 0));
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
              | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
      }
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
}

// Main 5.2: MODEL_STORM SubType 0 — Twister tornado at target
// ZzzEffect.cpp: CreateEffect(MODEL_STORM, Position=caster, LifeTime=59)
// Tornado travels from caster toward target direction
void VFXManager::SpawnTwisterStorm(const glm::vec3 &casterPos,
                                    const glm::vec3 &targetDir) {
  TwisterStorm ts;
  ts.position = casterPos;
  // Snap to terrain height if callback available
  if (m_getTerrainHeight) {
    ts.position.y = m_getTerrainHeight(casterPos.x, casterPos.z);
  }
  // Horizontal direction toward target
  glm::vec3 dir = targetDir;
  dir.y = 0.0f;
  float len = glm::length(dir);
  ts.direction = (len > 0.01f) ? dir / len : glm::vec3(0, 0, 1);
  ts.speed = 200.0f; // Travel speed (world units/sec)
  ts.lifetime = 2.36f;     // 59 ticks @ 25fps
  ts.maxLifetime = 2.36f;
  ts.tickTimer = 0.0f;
  ts.rotation = 0.0f;
  m_twisterStorms.push_back(ts);
}

void VFXManager::updateTwisterStorms(float dt) {
  for (int i = (int)m_twisterStorms.size() - 1; i >= 0; --i) {
    auto &ts = m_twisterStorms[i];
    ts.lifetime -= dt;
    if (ts.lifetime <= 0.0f) {
      m_twisterStorms[i] = m_twisterStorms.back();
      m_twisterStorms.pop_back();
      continue;
    }

    // Move tornado in click direction
    ts.position += ts.direction * ts.speed * dt;
    // Snap to terrain height each tick
    if (m_getTerrainHeight) {
      ts.position.y = m_getTerrainHeight(ts.position.x, ts.position.z);
    }
    // Main 5.2 SubType 0: no explicit model spin — UV scroll creates visual rotation

    // Tick-based particle effects (25fps = 0.04s/tick)
    ts.tickTimer += dt;
    while (ts.tickTimer >= 0.04f) {
      ts.tickTimer -= 0.04f;

      // Main 5.2: BITMAP_SMOKE SubType 3 — LifeTime=10, Scale=(rand()%32+80)*0.01
      // Velocity *= 0.4 per tick (rapid decel), Scale += 0.1/tick (fast expand)
      // Color: Luminosity=LifeTime/8 → (L*0.8, L*0.8, L) slight blue tint
      Particle smoke;
      smoke.type = ParticleType::SMOKE;
      smoke.position = ts.position;
      smoke.velocity = glm::vec3(
          (float)(rand() % 10 - 5) * 0.5f,
          (float)(rand() % 9 + 40) * 2.0f,  // Higher initial → decels fast with our update
          (float)(rand() % 10 - 5) * 0.5f);
      smoke.scale = (float)(rand() % 32 + 80) * 0.15f; // Start smaller, grows with update
      smoke.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      smoke.frame = -1.0f;
      smoke.lifetime = 0.4f;      // Main 5.2: 10 ticks @ 25fps
      smoke.maxLifetime = 0.4f;
      smoke.color = glm::vec3(0.45f, 0.45f, 0.55f); // Slight blue tint (L*0.8, L*0.8, L)
      smoke.alpha = 0.6f;
      m_particles.push_back(smoke);

      // Main 5.2: TWO independent 50% checks for left/right lightning
      // Left bolt: Position[0]-200, Position[2]+700 → Position (tornado center)
      if (rand() % 2 == 0) {
        glm::vec3 skyPos = ts.position + glm::vec3(-200.0f, 700.0f, 0.0f);
        SpawnRibbon(skyPos, ts.position, 10.0f,
                    glm::vec3(0.6f, 0.7f, 1.0f), 0.2f);
        SpawnBurst(ParticleType::SPELL_LIGHTNING, ts.position, 3);
      }
      // Right bolt: Position[0]+200, Position[2]+700 → Position
      if (rand() % 2 == 0) {
        glm::vec3 skyPos = ts.position + glm::vec3(200.0f, 700.0f, 0.0f);
        SpawnRibbon(skyPos, ts.position, 10.0f,
                    glm::vec3(0.6f, 0.7f, 1.0f), 0.2f);
        SpawnBurst(ParticleType::SPELL_LIGHTNING, ts.position, 3);
      }

      // Main 5.2: 25% chance stone debris (MODEL_STONE1/2 SubType 2)
      // 20-35 frame lifetime (0.8-1.4s), scale 0.24-1.04, bouncing upward
      // Substitute with dark particles + sparks (no stone model)
      if (rand() % 4 == 0) {
        for (int d = 0; d < 2; ++d) {
          Particle debris;
          debris.type = ParticleType::SPELL_DARK;
          debris.position = ts.position + glm::vec3(
              (float)(rand() % 80 - 40), 5.0f, (float)(rand() % 80 - 40));
          debris.velocity = glm::vec3(
              (float)(rand() % 40 - 20),
              75.0f + (float)(rand() % 50),  // Upward fling
              (float)(rand() % 40 - 20));
          debris.scale = 6.0f + (float)(rand() % 20); // Main 5.2: 0.24-1.04
          debris.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          debris.frame = -1.0f;
          debris.lifetime = 0.8f + (float)(rand() % 15) * 0.04f; // 20-35 frames
          debris.maxLifetime = debris.lifetime;
          debris.color = glm::vec3(0.4f, 0.35f, 0.3f); // Earthy brown
          debris.alpha = 0.9f;
          m_particles.push_back(debris);
        }
      }
    }
  }
}

void VFXManager::renderTwisterStorms(const glm::mat4 &view,
                                      const glm::mat4 &projection) {
  if (m_twisterStorms.empty() || m_stormMeshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  for (const auto &ts : m_twisterStorms) {
    float ticksRemaining = ts.lifetime / 0.04f;
    float blendMeshLight = std::min(ticksRemaining * 0.1f, 1.0f);
    float uvScrollU = -ticksRemaining * 0.1f;
    float alpha = 0.6f;
    if (ts.lifetime < ts.maxLifetime * 0.2f) alpha = 0.6f * ts.lifetime / (ts.maxLifetime * 0.2f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), ts.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(1.0f));
    for (const auto &mb : m_stormMeshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bool isBlend = (mb.bmdTextureId == 0);
      uint64_t state;
      if (isBlend) {
        m_modelShader->setVec4("u_params", glm::vec4(alpha, blendMeshLight, 0, 0));
        m_modelShader->setVec4("u_texCoordOffset", glm::vec4(uvScrollU, 0, 0, 0));
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
              | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
      } else {
        m_modelShader->setVec4("u_params", glm::vec4(alpha, 1.0f, 0, 0));
        m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
              | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
      }
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
}

// ============================================================
// PowerWave (Skill 11 / MODEL_MAGIC2) — Main 5.2/MuSven ground wave
// ============================================================

void VFXManager::SpawnPowerWave(const glm::vec3 &casterPos, float facing) {
  PowerWave pw;
  pw.position = casterPos;
  // Main 5.2: Direction(0,-60,0) rotated by character's facing angle
  // 60 units/tick * 25fps = 1500 units/sec forward travel speed
  float speed = 60.0f * 25.0f; // 1500 units/sec
  pw.velocity = glm::vec3(std::sin(facing) * speed, 0.0f,
                           std::cos(facing) * speed);
  pw.facing = facing;
  pw.maxLifetime = 0.8f;  // 20 ticks @ 25fps
  pw.lifetime = pw.maxLifetime;
  pw.tickTimer = 0.0f;
  pw.smokeTimer = 0.0f;
  m_powerWaves.push_back(pw);

  // Initial burst particles
  SpawnBurst(ParticleType::SPELL_ENERGY, casterPos + glm::vec3(0, 20, 0), 5);
}

void VFXManager::updatePowerWaves(float dt) {
  for (int i = (int)m_powerWaves.size() - 1; i >= 0; --i) {
    auto &pw = m_powerWaves[i];
    pw.lifetime -= dt;
    if (pw.lifetime <= 0.0f) {
      m_powerWaves[i] = m_powerWaves.back();
      m_powerWaves.pop_back();
      continue;
    }

    // Main 5.2: Position += Direction each tick — wave travels forward
    pw.position += pw.velocity * dt;
    // Snap to terrain height so wave follows ground contour
    if (m_getTerrainHeight) {
      pw.position.y = m_getTerrainHeight(pw.position.x, pw.position.z);
    }

    // Main 5.2: 4x BITMAP_SMOKE SubType 3 unconditionally per tick at wave center
    pw.tickTimer += dt;
    while (pw.tickTimer >= 0.04f) {
      pw.tickTimer -= 0.04f;
      for (int s = 0; s < 4; ++s) {
        Particle smoke;
        smoke.type = ParticleType::SMOKE;
        smoke.position = pw.position + glm::vec3(
            (float)(rand() % 40 - 20), (float)(rand() % 20 + 5),
            (float)(rand() % 40 - 20));
        smoke.velocity = glm::vec3(
            (float)(rand() % 10 - 5) * 0.5f,
            (float)(rand() % 20 + 20) * 1.5f,
            (float)(rand() % 10 - 5) * 0.5f);
        smoke.scale = (float)(rand() % 32 + 80) * 0.15f;
        smoke.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
        smoke.frame = -1.0f;
        smoke.lifetime = 0.4f;
        smoke.maxLifetime = 0.4f;
        // Main 5.2: Light = (Luminosity*0.3, Luminosity*0.6, Luminosity) — blue-dominant
        float L = pw.lifetime / pw.maxLifetime;
        smoke.color = glm::vec3(L * 0.3f, L * 0.6f, L);
        smoke.alpha = 0.6f;
        m_particles.push_back(smoke);
      }
    }
  }
}

void VFXManager::renderPowerWaves(const glm::mat4 &view,
                                   const glm::mat4 &projection) {
  if (m_powerWaves.empty() || m_magic2Meshes.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));

  for (const auto &pw : m_powerWaves) {
    // Main 5.2: BlendMeshLight = LifeTime * 0.1 (starts at 2.0, fades to 0)
    float ticksRemaining = pw.lifetime / 0.04f;
    float blendMeshLight = ticksRemaining * 0.1f; // Uncapped — bright at start
    // Main 5.2: BlendMeshTexCoordU = -LifeTime * 0.2 (UV scroll animation)
    float uvScrollU = -ticksRemaining * 0.2f;
    // Soft alpha: fade in quickly, fade out smoothly for transparency
    float lifeFrac = pw.lifetime / pw.maxLifetime;
    float alpha = std::min(0.7f, lifeFrac * 2.0f); // Max 0.7 — semi-transparent

    glm::mat4 model = glm::translate(glm::mat4(1.0f), pw.position);
    // Standard BMD coordinate transform
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    // Face the wave toward the caster's facing direction
    model = glm::rotate(model, pw.facing, glm::vec3(0, 0, 1));
    // Rotate 90° so model length axis aligns with travel direction
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0, 1, 0));
    // Main 5.2: default effect Scale = 0.9f
    model = glm::scale(model, glm::vec3(0.9f));

    for (const auto &mb : m_magic2Meshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      m_modelShader->setVec4("u_params", glm::vec4(alpha, blendMeshLight, 0, 0));
      m_modelShader->setVec4("u_texCoordOffset", glm::vec4(uvScrollU, 0, 0, 0));
      // SRC_ALPHA blend for semi-transparency (softer than pure additive)
      uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                     | BGFX_STATE_DEPTH_TEST_LESS
                     | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
      bgfx::setTransform(glm::value_ptr(model));
      m_modelShader->setTexture(0, "s_texColor", mb.texture);
      bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(0, m_modelShader->program);
    }
  }
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
}

#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// ── Evil Spirit: 4-directional homing spirit beams ──
// Main 5.2: Beams launch outward then curve back toward caster (MoveHumming),
// creating a spiraling dark energy field. Wobble via random angular drift.

void VFXManager::SpawnEvilSpirit(const glm::vec3 &casterPos, float facing) {
  // Main 5.2: Position[2] += 100 (start 100 above caster)
  glm::vec3 startPos = casterPos + glm::vec3(0, 100, 0);

  // 4 directions: Angle Z = i*90 degrees (Main 5.2: absolute, NOT relative to facing)
  // Each direction: 2 beams (scale 80 primary + scale 20 secondary) = 8 total
  for (int d = 0; d < 4; ++d) {
    // Main 5.2 line 4416: Vector(0, 0, i*90) — absolute cardinal directions
    float yawDeg = (float)d * 90.0f;

    for (int s = 0; s < 2; ++s) {
      SpiritBeam beam;
      beam.position = startPos;
      beam.casterPos = casterPos;
      beam.angle = glm::vec3(0.0f, 0.0f, yawDeg);       // (pitch, unused, yaw)
      beam.direction = glm::vec3(0.0f);                   // Angular drift (wobble)
      beam.scale = (s == 0) ? 80.0f : 20.0f;
      beam.lifetime = 1.96f;          // 49 ticks @ 25fps
      beam.maxLifetime = 1.96f;
      beam.trailTimer = 0.0f;
      beam.numTrail = 0;
      m_spiritBeams.push_back(beam);
    }
  }
}

// Main 5.2 TurnAngle2: rotate current angle toward target by at most maxTurn
// degrees. Takes shortest path around 0-360 wraparound.
static float TurnAngle2(float current, float target, float maxTurn) {
  // Normalize to [0, 360)
  while (current < 0.0f) current += 360.0f;
  while (current >= 360.0f) current -= 360.0f;
  while (target < 0.0f) target += 360.0f;
  while (target >= 360.0f) target -= 360.0f;

  float diff = target - current;
  if (diff > 180.0f) diff -= 360.0f;
  if (diff < -180.0f) diff += 360.0f;

  if (fabsf(diff) <= maxTurn) return target;
  return current + (diff > 0.0f ? maxTurn : -maxTurn);
}

// Main 5.2 MoveHumming: ONLY updates Angle toward target. Does NOT move position.
// Yaw (angle.z) steers toward target in XZ plane.
// Pitch (angle.x) steers toward target height.
// Turn = max degrees per call (per tick).
static void MoveHumming(const glm::vec3 &pos, glm::vec3 &angle,
                        const glm::vec3 &target, float turn) {
  // Yaw: angle from pos to target in XZ plane (our Y-up convention)
  float dx = target.x - pos.x;
  float dz = target.z - pos.z;
  float targetYaw = atan2f(dx, -dz) * (180.0f / 3.14159f);
  if (targetYaw < 0.0f) targetYaw += 360.0f;
  angle.z = TurnAngle2(angle.z, targetYaw, turn);

  // Pitch: vertical angle toward target
  float horizDist = sqrtf(dx * dx + dz * dz);
  float dy = target.y - pos.y;
  float targetPitch = atan2f(dy, std::max(horizDist, 1.0f)) * (180.0f / 3.14159f);
  if (targetPitch < 0.0f) targetPitch += 360.0f;
  angle.x = TurnAngle2(angle.x, targetPitch, turn);
}

void VFXManager::updateSpiritBeams(float dt) {
  for (int i = (int)m_spiritBeams.size() - 1; i >= 0; --i) {
    auto &b = m_spiritBeams[i];
    b.lifetime -= dt;
    if (b.lifetime <= 0.0f) {
      m_spiritBeams[i] = m_spiritBeams.back();
      m_spiritBeams.pop_back();
      continue;
    }

    // Main 5.2: All logic is per-tick (25fps = 0.04s per tick)
    b.trailTimer += dt;
    while (b.trailTimer >= 0.04f) {
      b.trailTimer -= 0.04f;

      // 1. Trail: store current position before moving (Main 5.2: Tails)
      int n = std::min(b.numTrail, SpiritBeam::MAX_TRAIL - 1);
      for (int t = n; t > 0; --t)
        b.trail[t] = b.trail[t - 1];
      b.trail[0] = b.position;
      if (b.numTrail < SpiritBeam::MAX_TRAIL)
        b.numTrail++;

      // 2. MoveHumming: steer angle toward caster (ONLY updates angles, max 10 deg/tick)
      // Main 5.2 line 3801: called BEFORE wobble
      glm::vec3 target = b.casterPos + glm::vec3(0, 80, 0);
      MoveHumming(b.position, b.angle, target, 10.0f);

      // 3. Wobble: random angular drift AFTER MoveHumming (Main 5.2 lines 3812-3817)
      // Deflects beam away from homed trajectory → creates erratic spiral
      b.direction.x += (float)(rand() % 32 - 16) * 0.2f;
      b.direction.z += (float)(rand() % 32 - 16) * 0.8f;
      b.angle.x += b.direction.x;
      b.angle.z += b.direction.z;
      b.direction.x *= 0.6f;
      b.direction.z *= 0.8f;

      // 4. Height clamping: terrain+100 to terrain+400 (Main 5.2 lines 3820-3830)
      // Applied BEFORE position movement so corrected angle affects this tick's move
      // Our convention: positive pitch = sin(pitch) > 0 = beam goes UP
      if (m_getTerrainHeight) {
        float terrainH = m_getTerrainHeight(b.position.x, b.position.z);
        if (b.position.y < terrainH + 100.0f) {
          b.direction.x = 0.0f;
          b.angle.x = 5.0f; // Positive pitch → beam goes UP
        }
        if (b.position.y > terrainH + 400.0f) {
          b.direction.x = 0.0f;
          b.angle.x = 355.0f; // Negative pitch (360-5) → beam goes DOWN
        }
      }

      // 5. Forward movement: velocity vector rotated by current angles (per-tick)
      float yawRad = b.angle.z * (3.14159f / 180.0f);
      float pitchRad = b.angle.x * (3.14159f / 180.0f);
      float cosP = cosf(pitchRad);
      glm::vec3 forward(sinf(yawRad) * cosP, sinf(pitchRad), -cosf(yawRad) * cosP);
      b.position += forward * 70.0f; // 70 units per tick (Main 5.2: Velocity)

      // Main 5.2: Primary beams (scale 80) spawn MODEL_LASER flash at head
      // (handled separately via LaserFlash system)
      if (b.scale > 40.0f) {
        float lifeTicks = b.lifetime / 0.04f;
        float beamLight = std::min(1.0f, lifeTicks * 0.1f);
        LaserFlash lf;
        lf.position = b.position;
        lf.yaw = b.angle.z;
        lf.pitch = b.angle.x;
        lf.light = beamLight;
        lf.lifetime = 0.04f; // 1 tick
        m_laserFlashes.push_back(lf);
      }
    }
  }
}

void VFXManager::renderSpiritBeams(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_spiritBeams.empty() || !m_lineShader) return;
  TexHandle tex = TexValid(m_jointSpiritTexture) ? m_jointSpiritTexture : m_energyTexture;
  bool hasTexture = TexValid(tex);
  m_lineShader->setVec4("u_lineMode", glm::vec4(hasTexture ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));
  // Main 5.2: RENDER_TYPE_ALPHA_BLEND_MINUS — subtractive: dst * (1 - srcColor)
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);

  for (const auto &b : m_spiritBeams) {
    if (b.numTrail < 1) continue;
    glm::vec3 positions[SpiritBeam::MAX_TRAIL + 1];
    positions[0] = b.position;
    int count = 1;
    for (int t = 0; t < b.numTrail && count <= SpiritBeam::MAX_TRAIL; ++t)
      positions[count++] = b.trail[t];
    if (count < 2) continue;

    float lifeTicks = b.lifetime / 0.04f;
    float light = std::min(1.0f, lifeTicks * 0.1f);

    std::vector<RibbonVertex> verts;
    verts.reserve(count * 12);
    for (int j = 0; j < count - 1; ++j) {
      glm::vec3 seg = positions[j] - positions[j + 1];
      float segLen = glm::length(seg);
      if (segLen < 0.01f) continue;
      glm::vec3 fwd = seg / segLen;
      glm::vec3 worldUp(0, 1, 0);
      glm::vec3 right = glm::normalize(glm::cross(fwd, worldUp)) * b.scale;
      glm::vec3 up = glm::normalize(glm::cross(right, fwd)) * b.scale;
      float u0v = (float)j / (float)(count - 1);
      float u1v = (float)(j + 1) / (float)(count - 1);
      RibbonVertex v;
      // Face 1: horizontal
      v.pos = positions[j] - right;     v.uv = glm::vec2(u0v, 0.0f); verts.push_back(v);
      v.pos = positions[j] + right;     v.uv = glm::vec2(u0v, 1.0f); verts.push_back(v);
      v.pos = positions[j+1] + right;   v.uv = glm::vec2(u1v, 1.0f); verts.push_back(v);
      v.pos = positions[j] - right;     v.uv = glm::vec2(u0v, 0.0f); verts.push_back(v);
      v.pos = positions[j+1] + right;   v.uv = glm::vec2(u1v, 1.0f); verts.push_back(v);
      v.pos = positions[j+1] - right;   v.uv = glm::vec2(u1v, 0.0f); verts.push_back(v);
      // Face 2: vertical
      v.pos = positions[j] - up;     v.uv = glm::vec2(u0v, 0.0f); verts.push_back(v);
      v.pos = positions[j] + up;     v.uv = glm::vec2(u0v, 1.0f); verts.push_back(v);
      v.pos = positions[j+1] + up;   v.uv = glm::vec2(u1v, 1.0f); verts.push_back(v);
      v.pos = positions[j] - up;     v.uv = glm::vec2(u0v, 0.0f); verts.push_back(v);
      v.pos = positions[j+1] + up;   v.uv = glm::vec2(u1v, 1.0f); verts.push_back(v);
      v.pos = positions[j+1] - up;   v.uv = glm::vec2(u1v, 0.0f); verts.push_back(v);
    }
    if (verts.empty()) continue;
    if ((int)verts.size() > MAX_RIBBON_VERTS) verts.resize(MAX_RIBBON_VERTS);
    uint32_t nv = (uint32_t)verts.size();
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) >= nv) {
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
      m_lineShader->setVec4("u_lineColor", glm::vec4(glm::vec3(light), light));
      if (hasTexture) m_lineShader->setTexture(0, "s_ribbonTex", tex);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(state);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

void VFXManager::updateLaserFlashes(float dt) {
  for (int i = (int)m_laserFlashes.size() - 1; i >= 0; --i) {
    m_laserFlashes[i].lifetime -= dt;
    if (m_laserFlashes[i].lifetime <= 0.0f) {
      m_laserFlashes[i] = m_laserFlashes.back();
      m_laserFlashes.pop_back();
    }
  }
}

void VFXManager::renderLaserFlashes(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_laserFlashes.empty() || m_laserMeshes.empty() || !m_modelShader) return;
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
  // Main 5.2: RENDER_DARK = reverse subtract (dst - src)
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE)
                 | BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_REVSUB);
  for (const auto &lf : m_laserFlashes) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), lf.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(lf.yaw), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(lf.pitch), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(1.3f));
    m_modelShader->setVec4("u_params", glm::vec4(1.0f, lf.light, 0, 0));
    for (const auto &mb : m_laserMeshes) {
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

bool VFXManager::CheckSpiritBeamHit(uint16_t serverIndex,
                                     const glm::vec3 &monPos,
                                     float radiusSq) {
  for (auto &b : m_spiritBeams) {
    if (b.scale < 40.0f)
      continue; // Only primary beams (scale 80) trigger spin
    if (b.affectedMonsters.count(serverIndex))
      continue;
    float dx = monPos.x - b.position.x;
    float dz = monPos.z - b.position.z;
    if (dx * dx + dz * dz <= radiusSq) {
      b.affectedMonsters.insert(serverIndex);
      return true;
    }
  }
  return false;
}

// ── Hellfire: ground fire circle + fire sprites + debris ──
// Main 5.2: AT_SKILL_HELL creates MODEL_CIRCLE (SubType 0) + MODEL_CIRCLE_LIGHT
// MODEL_CIRCLE: glowing circle mesh, fading over 45 ticks
// MODEL_CIRCLE_LIGHT: scrolling UV, screen shake, stone debris, warm terrain light
// PLAYER_SKILL_HELL animation: 10x BITMAP_FIRE sprites from character bones per frame
// AT_SKILL_BLAST_HELL creates MODEL_CIRCLE SubType 1 with 36 spirit beams

void VFXManager::SpawnHellfire(const glm::vec3 &casterPos) {
  // Ground circle effect at terrain height (MODEL_CIRCLE + MODEL_CIRCLE_LIGHT)
  HellfireEffect hf;
  hf.position = casterPos;
  if (m_getTerrainHeight)
    hf.position.y = m_getTerrainHeight(casterPos.x, casterPos.z);
  hf.lifetime = 1.8f;     // 45 ticks @ 25fps
  hf.maxLifetime = 1.8f;
  hf.tickTimer = 0.0f;
  hf.chargePhase = true;
  m_hellfireEffects.push_back(hf);
}

void VFXManager::SpawnHellfireBeams(const glm::vec3 &casterPos) {
  (void)casterPos; // Not used — Hellfire has no outward beams
}

void VFXManager::EndHellfireCharge() {
  for (auto &hf : m_hellfireEffects)
    hf.chargePhase = false;
}

void VFXManager::SetHeroBonePositions(
    const std::vector<glm::vec3> &positions) {
  m_heroBoneWorldPositions = positions;
}

void VFXManager::updateHellfireBeams(float dt) {
  m_hellfireBeams.clear();
  (void)dt;
}

void VFXManager::renderHellfireBeams(const glm::mat4 &view,
                                      const glm::mat4 &projection) {
  (void)view;
  (void)projection;
}

void VFXManager::updateHellfireEffects(float dt) {
  for (int i = (int)m_hellfireEffects.size() - 1; i >= 0; --i) {
    auto &hf = m_hellfireEffects[i];
    hf.lifetime -= dt;
    if (hf.lifetime <= 0.0f) {
      m_hellfireEffects[i] = m_hellfireEffects.back();
      m_hellfireEffects.pop_back();
      continue;
    }

    hf.tickTimer += dt;
    while (hf.tickTimer >= 0.04f) {
      hf.tickTimer -= 0.04f;
      hf.tickCount++;

      // Main 5.2: BlendMeshTexCoordU = -LifeTime * 0.01 (UV scroll for MODEL_CIRCLE_LIGHT)
      float ticksLeft = hf.lifetime / 0.04f;
      hf.uvScroll = -ticksLeft * 0.01f;

      // ── 1. BITMAP_FIRE SubType 0 from character bones ──
      // Main 5.2: 10x per frame during PLAYER_SKILL_HELL, random bones
      // Init: LifeTime=24, Velocity=(0, -(rand%16+32)*0.1, 0), Scale=(rand%64+128)*0.01
      // Update: Gravity+=0.004, Pos[Z]+=Gravity*10, Scale-=0.04, Frame=(23-Life)/6
      for (int j = 0; j < 10; ++j) {
        // Approximate bone positions: random spread around character body
        float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
        float radius = (float)(rand() % 50 + 10); // ~10-60 units from center (body spread)
        float ox = cosf(angle) * radius;
        float oz = sinf(angle) * radius;
        float oy = (float)(rand() % 80); // 0-80 units height (character body height)

        Particle p;
        p.type = ParticleType::SPELL_FIRE;
        p.position = hf.position + glm::vec3(ox, oy, oz);
        // Main 5.2: initial velocity is mostly zero, gravity causes rise
        // Velocity = (0, -(rand%16+32)*0.1, 0) → lateral drift only
        p.velocity = glm::vec3((float)(rand() % 10 - 5) * 0.5f,
                               (float)(rand() % 4) * 1.0f, // Tiny initial upward
                               (float)(rand() % 10 - 5) * 0.5f);
        // Main 5.2: Scale = (rand%64+128)*0.01 = 1.28-1.92 → world scale ~20-30
        p.scale = (float)(rand() % 64 + 128) * 0.16f; // 20-31 units
        p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
        p.frame = 0.0f; // 4-frame sprite sheet animation
        // Main 5.2: LifeTime = 24 ticks = 0.96s
        p.lifetime = 0.96f;
        p.maxLifetime = 0.96f;
        // Main 5.2: Light is character's light (white), texture provides fire color
        p.color = glm::vec3(1.0f, 0.8f, 0.4f);
        p.alpha = 1.0f;
        m_particles.push_back(p);
      }

      // ── 2. Stone debris — MODEL_CIRCLE_LIGHT spawns these (25% chance per tick) ──
      // Main 5.2: CreateEffect(MODEL_STONE1+rand()%2, Position, Angle, Light, 1)
      // Random position within 300 units from center
      if (rand() % 4 == 0) {
        float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
        float radius = (float)(rand() % 300);
        glm::vec3 debrisPos = hf.position +
            glm::vec3(cosf(angle) * radius, 0.0f, sinf(angle) * radius);
        if (m_getTerrainHeight)
          debrisPos.y = m_getTerrainHeight(debrisPos.x, debrisPos.z);

        // Approximate MODEL_STONE with dark particles (no BMD stone models loaded)
        // Main 5.2: Gravity 3-5, HeadAngle[2]+=15, bounce 0.6x, spin
        for (int s = 0; s < 2; ++s) {
          Particle p;
          p.type = ParticleType::FIRE;
          p.position = debrisPos + glm::vec3((float)(rand()%20-10), 0, (float)(rand()%20-10));
          float upSpeed = (float)(rand() % 64 + 64) * 2.5f;
          p.velocity = glm::vec3((float)(rand()%30-15), upSpeed, (float)(rand()%30-15));
          p.scale = (float)(rand() % 6 + 5); // 5-11 units
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.frame = -1.0f;
          p.lifetime = 0.8f + (float)(rand() % 16) * 0.04f; // 20-35 ticks
          p.maxLifetime = p.lifetime;
          p.color = glm::vec3(0.4f, 0.3f, 0.15f); // Dark brown
          p.alpha = 0.8f;
          m_particles.push_back(p);
        }
      }

      // ── 3. Bone-based BITMAP_LIGHT aura (wing effect) ──
      // Main 5.2 ZzzCharacter.cpp:5584-5614: energy particles from character bones
      // Only during charge phase (~first 15 ticks = HELL_BEGIN+HELL_START duration)
      if (hf.tickCount < 15 && !m_heroBoneWorldPositions.empty()) {
        int maxBone = std::min((int)m_heroBoneWorldPositions.size(), 41);
        for (int bi = 0; bi < maxBone; bi += 2) {
          Particle p;
          p.type = ParticleType::SPELL_ENERGY;
          p.position = m_heroBoneWorldPositions[bi];
          // Main 5.2: outward drift from body
          float outAngle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
          float outSpeed = (float)(rand() % 16 + 32) * 2.5f; // ~80-120 units/sec
          p.velocity = glm::vec3(cosf(outAngle) * outSpeed,
                                  (float)(rand() % 40 + 20), // slight upward
                                  sinf(outAngle) * outSpeed);
          // Main 5.2: Scale = 1.3 + (m_bySkillCount * 0.08) → ~25 world units
          p.scale = 25.0f + (float)(rand() % 6);
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.frame = -1.0f;
          p.lifetime = 0.64f; // 16 ticks
          p.maxLifetime = 0.64f;
          p.color = glm::vec3(0.3f, 0.3f, 1.0f); // Blue-white
          p.alpha = 0.9f;
          m_particles.push_back(p);
        }
      }

      // ── 4. CreateForce spiraling energy (3 beams/tick) ──
      // Main 5.2 ZzzEffect.cpp:215-229: 3 joints from random angles inward
      // Only during charge phase (~first 15 ticks)
      if (hf.tickCount < 15) {
        for (int f = 0; f < 3; ++f) {
          float yaw = ((float)(rand() % 360)) * 3.14159f / 180.0f;
          float pitch = ((float)(rand() % 60 + 15)) * 3.14159f / 180.0f;
          float radius = 300.0f;
          float ox = cosf(yaw) * cosf(pitch) * radius;
          float oy = sinf(pitch) * radius;
          float oz = sinf(yaw) * cosf(pitch) * radius;

          Particle p;
          p.type = ParticleType::SPELL_ENERGY;
          p.position = hf.position + glm::vec3(ox, oy + 120.0f, oz);
          // Velocity toward center (inward spiral)
          glm::vec3 toCenter = hf.position + glm::vec3(0, 120, 0) - p.position;
          float len = glm::length(toCenter);
          if (len > 1.0f)
            p.velocity = (toCenter / len) * 200.0f;
          else
            p.velocity = glm::vec3(0.0f);
          p.scale = 20.0f;
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.frame = -1.0f;
          p.lifetime = 0.6f;
          p.maxLifetime = 0.6f;
          p.color = glm::vec3(0.4f, 0.4f, 1.0f); // Blue-white
          p.alpha = 0.7f;
          m_particles.push_back(p);
        }
      }
    }
  }
}

void VFXManager::renderHellfireEffects(const glm::mat4 &view,
                                        const glm::mat4 &projection) {
  if (m_hellfireEffects.empty() || !m_modelShader) return;
  bool hasCircle = !m_circleMeshes.empty();
  bool hasCircleLight = !m_circleLightMeshes.empty();
  if (!hasCircle && !hasCircleLight) return;

  glm::mat4 invView = glm::inverse(view);
  // u_params2.y = -1.0 activates decal mode: brightness-based alpha fade
  // removes visible square outline from JPEG texture edges on Circle BMD models
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, -1.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  // No depth test — ground decal must always be visible on uneven terrain
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

  for (const auto &hf : m_hellfireEffects) {
    float ticksLeft = hf.lifetime / 0.04f;

    // Layer 1: Circle01.bmd pentagram
    if (hasCircle) {
      float blendLight = ticksLeft * 0.1f;
      glm::mat4 model = glm::translate(glm::mat4(1.0f), hf.position + glm::vec3(0, 10, 0));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      m_modelShader->setVec4("u_params", glm::vec4(1.0f, blendLight, 0, 0));
      m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
      for (const auto &mb : m_circleMeshes) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        bgfx::setTransform(glm::value_ptr(model));
        m_modelShader->setTexture(0, "s_texColor", mb.texture);
        bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        bgfx::setState(state);
        bgfx::submit(0, m_modelShader->program);
      }
    }

    // Layer 2: Circle02.bmd light overlay with UV scroll
    if (hasCircleLight) {
      float lightTicks = std::min(ticksLeft, 40.0f);
      float blendLight2 = (lightTicks >= 30.0f) ? (40.0f - lightTicks) * 0.1f
                                                  : lightTicks * 0.1f;
      if (blendLight2 > 0.01f) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), hf.position + glm::vec3(0, 11, 0));
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
        m_modelShader->setVec4("u_params", glm::vec4(1.0f, blendLight2, 0, 0));
        m_modelShader->setVec4("u_texCoordOffset", glm::vec4(hf.uvScroll, 0, 0, 0));
        for (const auto &mb : m_circleLightMeshes) {
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
  }
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
}

// Main 5.2: CreateInferno — 8 fire explosions in ring, radius 220, 45° apart
void VFXManager::SpawnInferno(const glm::vec3 &casterPos) {
  InfernoEffect inf;
  inf.position = casterPos;
  if (m_getTerrainHeight)
    inf.position.y = m_getTerrainHeight(casterPos.x, casterPos.z);
  inf.lifetime = 1.2f;
  inf.maxLifetime = 1.2f;
  inf.tickTimer = 0.0f;

  // Main 5.2 CreateInferno: 8 explosion points in ring, radius 220, 45° apart
  for (int j = 0; j < 8; ++j) {
    float angle = (float)j * 45.0f * 3.14159f / 180.0f;
    float rx = cosf(angle) * 220.0f;
    float rz = sinf(angle) * 220.0f;
    glm::vec3 ringPos = inf.position + glm::vec3(rx, 0, rz);
    if (m_getTerrainHeight)
      ringPos.y = m_getTerrainHeight(ringPos.x, ringPos.z);
    inf.ringPoints[j] = ringPos;

    // Main 5.2 CreateBomb: Position[2]+=80, massive burst at each ring point
    glm::vec3 bombPos = ringPos + glm::vec3(0, 80, 0);
    // 20x BITMAP_SPARK SubType 2 — heavy arcing sparks with bounce physics
    SpawnBurst(ParticleType::INFERNO_SPARK, bombPos, 20);
    // Main 5.2: BITMAP_EXPLOTION — animated 4x4 explosion sprite at each point
    SpawnBurst(ParticleType::INFERNO_EXPLOSION, bombPos, 2);
    // Dedicated inferno fire (inferno.OZJ) — dense fire columns
    SpawnBurst(ParticleType::INFERNO_FIRE, bombPos, 6);
    SpawnBurst(ParticleType::INFERNO_FIRE, ringPos + glm::vec3(0, 20, 0), 4);
    // Generic fire/flame layers for density
    SpawnBurst(ParticleType::SPELL_FIRE, bombPos, 6);
    SpawnBurst(ParticleType::SPELL_FLAME, ringPos + glm::vec3(0, 20, 0), 4);
    SpawnBurst(ParticleType::SPELL_FLAME, ringPos + glm::vec3(0, 60, 0), 3);
    // Smoke clouds rising from explosions
    SpawnBurst(ParticleType::SMOKE, bombPos + glm::vec3(0, 40, 0), 3);
  }
  m_infernoEffects.push_back(inf);
}

void VFXManager::updateInfernoEffects(float dt) {
  for (int i = (int)m_infernoEffects.size() - 1; i >= 0; --i) {
    auto &inf = m_infernoEffects[i];
    inf.lifetime -= dt;
    if (inf.lifetime <= 0.0f) {
      m_infernoEffects[i] = m_infernoEffects.back();
      m_infernoEffects.pop_back();
      continue;
    }
    // Per-tick fire columns at ring positions — heavy, persistent fire
    inf.tickTimer += dt;
    while (inf.tickTimer >= 0.04f) {
      inf.tickTimer -= 0.04f;
      float intensity = inf.lifetime / inf.maxLifetime; // 1→0
      // Smooth intensity curve — quadratic for gentle ramp-down
      float smoothInt = intensity * intensity;
      for (int j = 0; j < 8; ++j) {
        glm::vec3 base = inf.ringPoints[j];
        // Dedicated inferno fire — probability scales with smooth intensity
        if ((float)(rand() % 100) / 100.0f < smoothInt + 0.2f) {
          SpawnBurst(ParticleType::INFERNO_FIRE,
                     base + glm::vec3((float)(rand() % 40) - 20.0f,
                                      10 + rand() % 80,
                                      (float)(rand() % 40) - 20.0f), 1);
        }
        // Generic flame — always present but thins out smoothly
        if ((float)(rand() % 100) / 100.0f < smoothInt + 0.15f) {
          SpawnBurst(ParticleType::SPELL_FLAME,
                     base + glm::vec3((float)(rand() % 50) - 25.0f,
                                      20 + rand() % 100,
                                      (float)(rand() % 50) - 25.0f), 1);
        }
        // Fire particles — only during strong phase
        if (intensity > 0.35f) {
          SpawnBurst(ParticleType::SPELL_FIRE,
                     base + glm::vec3(0, 40 + rand() % 60, 0), 1);
        }
        // Arcing sparks — sparser as effect fades
        if (rand() % 4 == 0 && intensity > 0.2f) {
          SpawnBurst(ParticleType::INFERNO_SPARK,
                     base + glm::vec3(0, 60 + rand() % 40, 0), 1);
        }
        // Late-phase smoke — rises as fire dies, smooth transition
        if (intensity < 0.45f && (float)(rand() % 100) / 100.0f > intensity) {
          SpawnBurst(ParticleType::SMOKE,
                     base + glm::vec3(0, 30 + rand() % 50, 0), 1);
        }
      }
    }
  }
}

void VFXManager::renderInfernoEffects(const glm::mat4 &view,
                                       const glm::mat4 &projection) {
  if (m_infernoEffects.empty() || m_infernoMeshes.empty() || !m_modelShader) return;
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
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  for (const auto &inf : m_infernoEffects) {
    float t = inf.lifetime / inf.maxLifetime;
    float blendLight = t * t * 3.5f;
    float age = 1.0f - t;
    float growScale = 0.9f + age * (2.0f - age) * 0.075f;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), inf.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(growScale));
    m_modelShader->setVec4("u_params", glm::vec4(1.0f, blendLight, 0, 0));
    for (const auto &mb : m_infernoMeshes) {
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

// Main 5.2: AT_SKILL_FLASH (Aqua Beam) — BITMAP_BOSS_LASER SubType 2
// CalcAddPosition(o, -20, -90, 100, Position) → offset from caster hands
// Flash is SubType 2: Scale=2.5, LifeTime=35, sprite=BITMAP_FIRE+1, color=(0,0.2,1)
// Direction = (0,-50,0) rotated by character angle → 50 units forward per step
void VFXManager::SpawnAquaBeam(const glm::vec3 &casterPos, float facing) {
  AquaBeam ab;

  glm::vec3 forward(-cosf(facing), 0.0f, sinf(facing));

  // Anchor beam start to character's hand bones (no forward offset).
  // At frame 7.0 the hands are fully extended in the release pose, so bone
  // positions already reflect where the beam should originate.
  // Staff is on bone 42 (L Hand), casting hand is bone 33 (R Hand).
  if (m_heroBoneWorldPositions.size() > 42) {
    glm::vec3 rHand = m_heroBoneWorldPositions[33];
    glm::vec3 lHand = m_heroBoneWorldPositions[42];
    ab.startPosition = (rHand + lHand) * 0.5f;
  } else if (m_heroBoneWorldPositions.size() > 33) {
    ab.startPosition = m_heroBoneWorldPositions[33];
  } else {
    glm::vec3 right(sinf(facing), 0.0f, cosf(facing));
    ab.startPosition = casterPos + right * (-20.0f) + forward * 90.0f +
                       glm::vec3(0.0f, 100.0f, 0.0f);
  }

  // Main 5.2: Direction = (0, -50, 0) rotated by character angle
  ab.direction = forward * 50.0f;

  ab.light = glm::vec3(0.5f, 0.7f, 1.0f);
  ab.scale = 120.0f;
  ab.lifetime = 0.8f; // Match remaining animation (~7 frames at 8.25 kf/s)

  m_aquaBeams.push_back(ab);

  // Spawn cast burst particles at origin
  SpawnBurst(ParticleType::SPELL_WATER, ab.startPosition, 8);

  // Impact burst at beam endpoint
  glm::vec3 endpoint = ab.startPosition + ab.direction * (float)(AquaBeam::NUM_SEGMENTS - 1);
  SpawnBurst(ParticleType::SPELL_WATER, endpoint, 15);
  SpawnBurst(ParticleType::FLARE, endpoint, 2);
}

void VFXManager::KillAquaBeams() {
  m_aquaBeams.clear();
}

// Main 5.2: BITMAP_GATHERING SubType 2 — converging lightning + water particles
// Called per-tick during Flash wind-up (frames 1.2-3.0) before beam spawns
void VFXManager::SpawnAquaGathering(const glm::vec3 &handPos) {
  // Use midpoint of both hand bones (staff between hands) if available
  glm::vec3 target = handPos;
  if (m_heroBoneWorldPositions.size() > 42) {
    target = (m_heroBoneWorldPositions[33] + m_heroBoneWorldPositions[42]) * 0.5f;
  } else if (m_heroBoneWorldPositions.size() > 33) {
    target = m_heroBoneWorldPositions[33];
  }

  // 3 converging water particles per tick (Main 5.2: CreateSprite BITMAP_SHINY+1)
  for (int j = 0; j < 3; ++j) {
    float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
    float radius = (float)(rand() % 80 + 50); // 50-130 units out
    glm::vec3 offset(cosf(angle) * radius,
                     (float)(rand() % 60 - 10),
                     sinf(angle) * radius);
    Particle p;
    p.type = ParticleType::SPELL_WATER;
    p.position = target + offset;
    p.velocity = -offset * 4.0f; // Converge toward hand in ~0.25s
    p.lifetime = 0.25f;
    p.maxLifetime = 0.25f;
    p.scale = (float)(rand() % 4 + 5);
    p.alpha = 0.9f;
    p.rotation = (float)(rand() % 360);
    p.color = glm::vec3(0.5f, 0.7f, 1.0f);
    m_particles.push_back(p);
  }

  // 1 lightning arc per tick (Main 5.2: CreateJoint BITMAP_JOINT_THUNDER every other tick)
  {
    float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
    float radius = (float)(rand() % 60 + 80); // 80-140 units out
    glm::vec3 offset(cosf(angle) * radius,
                     (float)(rand() % 80 + 20),
                     sinf(angle) * radius);
    Particle p;
    p.type = ParticleType::SPELL_LIGHTNING;
    p.position = target + offset;
    p.velocity = -offset * 5.0f; // Converge faster
    p.lifetime = 0.2f;
    p.maxLifetime = 0.2f;
    p.scale = (float)(rand() % 6 + 8);
    p.alpha = 1.0f;
    p.rotation = (float)(rand() % 360);
    p.color = glm::vec3(0.6f, 0.8f, 1.2f);
    m_particles.push_back(p);
  }
}

void VFXManager::updateAquaBeams(float dt) {
  for (int i = (int)m_aquaBeams.size() - 1; i >= 0; --i) {
    auto &ab = m_aquaBeams[i];
    ab.lifetime -= dt;
    if (ab.lifetime <= 0.0f) {
      m_aquaBeams[i] = m_aquaBeams.back();
      m_aquaBeams.pop_back();
      continue;
    }

    // Main 5.2: per-tick BITMAP_SPARK+1 particles along beam path
    // 3-4 spark particles scattered along the beam every frame
    ab.sparkTimer += dt;
    while (ab.sparkTimer >= 0.04f) { // 25fps tick rate
      ab.sparkTimer -= 0.04f;
      for (int s = 0; s < 4; ++s) {
        float t = (float)(rand() % (AquaBeam::NUM_SEGMENTS - 1)) +
                  (float)(rand() % 100) / 100.0f;
        glm::vec3 pos = ab.startPosition + ab.direction * t;
        // Random lateral offset within beam width
        glm::vec3 beamDir = glm::normalize(ab.direction);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(beamDir, up));
        float lateralOffset = ((float)(rand() % 200) - 100.0f) * 0.6f; // ±60 units
        float vertOffset = ((float)(rand() % 80) - 20.0f);             // -20 to +60 up
        pos += right * lateralOffset + up * vertOffset;

        Particle p;
        p.type = ParticleType::SPELL_WATER;
        p.position = pos;
        p.velocity = glm::vec3(
            ((float)(rand() % 60) - 30.0f),
            (float)(rand() % 40 + 20),  // drift upward
            ((float)(rand() % 60) - 30.0f));
        p.lifetime = 0.2f + (float)(rand() % 10) * 0.02f;
        p.maxLifetime = p.lifetime;
        p.scale = (float)(rand() % 4 + 3);
        p.alpha = 0.7f;
        p.rotation = (float)(rand() % 360);
        p.color = glm::vec3(0.4f, 0.8f, 1.0f);
        m_particles.push_back(p);
      }
    }
  }
}

void VFXManager::renderAquaBeams(const glm::mat4 &view,
                                  const glm::mat4 &projection) {
  if (m_aquaBeams.empty() || !m_lineShader) return;
  glm::mat4 invView = glm::inverse(view);
  glm::vec3 camPos(invView[3]);
  // beamMode=true, useTexture=false
  m_lineShader->setVec4("u_lineMode", glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD;

  for (const auto &ab : m_aquaBeams) {
    glm::vec3 beamDir = glm::normalize(ab.direction);
    constexpr int EXT = 2;
    constexpr int TOTAL_SEGS = AquaBeam::NUM_SEGMENTS + EXT * 2;
    auto buildStrip = [&](float halfWidth) {
      std::vector<RibbonVertex> verts;
      verts.reserve(TOTAL_SEGS * 6);
      for (int j = -EXT; j < AquaBeam::NUM_SEGMENTS + EXT - 1; ++j) {
        glm::vec3 p0 = ab.startPosition + ab.direction * (float)j;
        glm::vec3 p1 = ab.startPosition + ab.direction * (float)(j + 1);
        glm::vec3 mid = (p0 + p1) * 0.5f;
        glm::vec3 viewDir = glm::normalize(camPos - mid);
        glm::vec3 w = glm::normalize(glm::cross(beamDir, viewDir)) * halfWidth;
        float u0 = (float)(j + EXT) / (float)(TOTAL_SEGS - 1);
        float u1 = (float)(j + EXT + 1) / (float)(TOTAL_SEGS - 1);
        RibbonVertex v;
        v.pos = p0 - w; v.uv = glm::vec2(u0, 0.0f); verts.push_back(v);
        v.pos = p0 + w; v.uv = glm::vec2(u0, 1.0f); verts.push_back(v);
        v.pos = p1 + w; v.uv = glm::vec2(u1, 1.0f); verts.push_back(v);
        v.pos = p0 - w; v.uv = glm::vec2(u0, 0.0f); verts.push_back(v);
        v.pos = p1 + w; v.uv = glm::vec2(u1, 1.0f); verts.push_back(v);
        v.pos = p1 - w; v.uv = glm::vec2(u1, 0.0f); verts.push_back(v);
      }
      return verts;
    };

    // 4-pass layered Gaussian beam
    struct BeamPass { glm::vec3 color; float halfW; };
    BeamPass passes[] = {
      { glm::vec3(0.1f, 0.3f, 0.45f), 160.0f },
      { glm::vec3(0.2f, 0.6f, 0.85f), 90.0f },
      { glm::vec3(0.4f, 0.85f, 1.0f), 45.0f },
      { glm::vec3(0.6f, 1.0f, 1.0f), 18.0f },
    };
    for (const auto &pass : passes) {
      auto verts = buildStrip(pass.halfW);
      if (verts.empty()) continue;
      uint32_t nv = (uint32_t)verts.size();
      bgfx::TransientVertexBuffer tvb;
      if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) >= nv) {
        bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
        memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
        m_lineShader->setVec4("u_lineColor", glm::vec4(pass.color, 1.0f));
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(state);
        bgfx::submit(0, m_lineShader->program);
      }
    }
  }
}

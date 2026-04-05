#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// ── Weapon blur trail (Main 5.2: ZzzEffectBlurSpark.cpp) ────────────────────

void VFXManager::StartWeaponTrail(const glm::vec3 &color, bool isSkill,
                                   uint8_t weaponLevel) {
  m_weaponTrail.active = true;
  m_weaponTrail.fading = false;
  m_weaponTrail.fadeTimer = 0.0f;
  m_weaponTrail.numPoints = 0;
  m_weaponTrail.shrinkAccum = 0.0f;
  m_weaponTrail.color = color;
  m_weaponTrail.isSkill = isSkill;
  m_weaponTrail.weaponLevel = weaponLevel;
}

void VFXManager::StopWeaponTrail() {
  if (!m_weaponTrail.active && !m_weaponTrail.fading)
    return;
  m_weaponTrail.active = false;
  m_weaponTrail.fading = true;
  m_weaponTrail.fadeTimer = WeaponTrail::MAX_FADE_TIME;
}

void VFXManager::AddWeaponTrailPoint(const glm::vec3 &tip,
                                      const glm::vec3 &base) {
  auto &t = m_weaponTrail;
  if (!t.active)
    return;

  // Shift existing points backward (newest at [0])
  int maxIdx = WeaponTrail::MAX_POINTS - 1;
  for (int i = std::min(t.numPoints - 1, maxIdx - 1); i >= 0; --i) {
    t.tip[i + 1] = t.tip[i];
    t.base[i + 1] = t.base[i];
  }
  // Add newest point at [0]
  t.tip[0] = tip;
  t.base[0] = base;
  if (t.numPoints < WeaponTrail::MAX_POINTS)
    t.numPoints++;
}

void VFXManager::updateWeaponTrail(float dt) {
  auto &t = m_weaponTrail;
  if (!t.active && !t.fading)
    return;

  if (t.fading) {
    t.fadeTimer -= dt;
    // Shrink trail by removing oldest points — fast enough to clear within MAX_FADE_TIME
    t.shrinkAccum += dt;
    while (t.shrinkAccum >= 0.01f && t.numPoints > 0) {
      t.numPoints--;
      t.shrinkAccum -= 0.01f;
    }
    if (t.numPoints <= 1 || t.fadeTimer <= 0.0f) {
      t.fading = false;
      t.numPoints = 0;
      t.shrinkAccum = 0.0f;
    }
  }
}

void VFXManager::renderWeaponTrail(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  auto &t = m_weaponTrail;
  if (t.numPoints < 2 || !m_lineShader) return;
  TexHandle blurTex = t.isSkill ? m_motionBlurTexture : m_blurTexture;
  bool hasTexture = TexValid(blurTex);
  m_lineShader->setVec4("u_lineMode", glm::vec4(hasTexture ? 1.0f : 0.0f, 0.0f, 1.0f, 0.0f)); // trailMode=true
  float fadeAlpha = t.fading ? std::max(0.0f, t.fadeTimer / WeaponTrail::MAX_FADE_TIME) : 1.0f;
  // Main 5.2: level 7+ weapon trails stay fully bright, lower levels dim progressively
  float levelMul = (t.weaponLevel >= 7) ? 1.0f : 0.6f;
  float baseAlpha = fadeAlpha * levelMul;
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  std::vector<RibbonVertex> verts;
  verts.reserve(t.numPoints * 6);
  for (int j = 0; j < t.numPoints - 1; ++j) {
    float u0 = (float)j / (float)(t.numPoints - 1);
    float u1 = (float)(j + 1) / (float)(t.numPoints - 1);
    RibbonVertex v;
    v.pos = t.tip[j];      v.uv = glm::vec2(u0, 1.0f); verts.push_back(v);
    v.pos = t.base[j];     v.uv = glm::vec2(u0, 0.0f); verts.push_back(v);
    v.pos = t.base[j + 1]; v.uv = glm::vec2(u1, 0.0f); verts.push_back(v);
    v.pos = t.tip[j];      v.uv = glm::vec2(u0, 1.0f); verts.push_back(v);
    v.pos = t.base[j + 1]; v.uv = glm::vec2(u1, 0.0f); verts.push_back(v);
    v.pos = t.tip[j + 1];  v.uv = glm::vec2(u1, 1.0f); verts.push_back(v);
  }
  if (!verts.empty()) {
    uint32_t nv = (uint32_t)verts.size();
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) >= nv) {
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
      m_lineShader->setVec4("u_lineColor", glm::vec4(t.color, baseAlpha));
      if (hasTexture) m_lineShader->setTexture(0, "s_ribbonTex", blurTex);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(state);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rageful Blow VFX (Main 5.2: MODEL_BLOW_OF_DESTRUCTION ground effects)
// ═══════════════════════════════════════════════════════════════════════════════

void VFXManager::SpawnRagefulBlow(const glm::vec3 &casterPos, float facing) {
  FuryStrikeEffect fs;
  fs.casterPos = casterPos;
  fs.casterFacing = facing;
  fs.tickTimer = 0.0f;
  fs.ticksElapsed = 0;
  fs.phase1Done = false;
  fs.phase2Done = false;
  fs.totalLifetime = 2.0f;
  fs.randomOffset = rand() % 100;

  // Impact at caster's feet — character is the center of the radial cracks
  fs.impactPos.x = casterPos.x;
  fs.impactPos.z = casterPos.z;
  fs.impactPos.y = m_getTerrainHeight
                       ? m_getTerrainHeight(fs.impactPos.x, fs.impactPos.z) - 2.0f
                       : casterPos.y;

  m_furyStrikeEffects.push_back(fs);
}

void VFXManager::updateFuryStrikeEffects(float dt) {
  // Decay camera shake
  m_cameraShake *= 0.85f;
  if (std::abs(m_cameraShake) < 0.01f)
    m_cameraShake = 0.0f;

  for (int i = (int)m_furyStrikeEffects.size() - 1; i >= 0; --i) {
    auto &fs = m_furyStrikeEffects[i];
    fs.totalLifetime -= dt;
    if (fs.totalLifetime <= 0.0f) {
      m_furyStrikeEffects[i] = m_furyStrikeEffects.back();
      m_furyStrikeEffects.pop_back();
      continue;
    }

    fs.tickTimer += dt;
    while (fs.tickTimer >= 0.04f) {
      fs.tickTimer -= 0.04f;
      fs.ticksElapsed++;

      // Tick 7 (= Main 5.2 LifeTime==13): Weapon swing hits ground
      if (fs.ticksElapsed == 7) {
        if (m_playSound) m_playSound(150); // SOUND_FURY_STRIKE2
      }

      // Phase 1: Tick 9 (= Main 5.2 LifeTime==11): Center cluster + 5 radial arms
      if (fs.ticksElapsed == 9 && !fs.phase1Done) {
        fs.phase1Done = true;
        glm::vec3 impact = fs.impactPos;

        // Ground impact burst — centered fire eruption + dust smoke
        for (int j = 0; j < 15; ++j) {
          Particle p;
          p.type = ParticleType::SKILL_FURY;
          p.position = impact + glm::vec3(
              (float)(rand() % 40 - 20), 5.0f, (float)(rand() % 40 - 20));
          // Mostly vertical with slight spread
          p.velocity = glm::vec3(
              (float)(rand() % 40 - 20),
              180.0f + (float)(rand() % 100),
              (float)(rand() % 40 - 20));
          p.scale = 35.0f + (float)(rand() % 25);
          p.maxLifetime = 0.5f + (float)(rand() % 20) / 100.0f;
          p.lifetime = p.maxLifetime;
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.color = glm::vec3(1.0f, 0.5f, 0.15f);
          p.alpha = 1.0f;
          p.frame = 0.0f;
          m_particles.push_back(p);
        }
        // Central flare flash
        for (int j = 0; j < 4; ++j) {
          Particle p;
          p.type = ParticleType::FLARE;
          p.position = impact + glm::vec3(
              (float)(rand() % 30 - 15), 15.0f + (float)(rand() % 20),
              (float)(rand() % 30 - 15));
          p.velocity = glm::vec3(
              (float)(rand() % 20 - 10), 40.0f + (float)(rand() % 30),
              (float)(rand() % 20 - 10));
          p.scale = 30.0f + (float)(rand() % 15);
          p.maxLifetime = 0.3f;
          p.lifetime = p.maxLifetime;
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.color = glm::vec3(1.0f, 0.6f, 0.2f);
          p.alpha = 1.0f;
          p.frame = 0.0f;
          m_particles.push_back(p);
        }
        // Dust/smoke cloud rising from ground
        for (int j = 0; j < 10; ++j) {
          Particle p;
          p.type = ParticleType::SMOKE;
          float ang = (float)(rand() % 360) * 3.14159f / 180.0f;
          float r = (float)(rand() % 60);
          p.position = impact + glm::vec3(cosf(ang) * r, 5.0f, sinf(ang) * r);
          p.velocity = glm::vec3(
              (float)(rand() % 20 - 10),
              30.0f + (float)(rand() % 40),
              (float)(rand() % 20 - 10));
          p.scale = 30.0f + (float)(rand() % 30);
          p.maxLifetime = 1.2f + (float)(rand() % 40) / 100.0f;
          p.lifetime = p.maxLifetime;
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.color = glm::vec3(0.4f, 0.35f, 0.25f); // Brown-gray dust
          p.alpha = 0.5f;
          p.frame = -1.0f;
          m_particles.push_back(p);
        }

        // Helper to spawn an EarthQuake crack instance
        auto spawnEQ = [&](int type, float scale, float lifetime,
                           const glm::vec3 &pos, float angleDeg) {
          EarthQuakeCrack eq;
          eq.position = pos;
          eq.angle = angleDeg;
          eq.scale = scale;
          eq.lifetime = lifetime;
          eq.maxLifetime = lifetime;
          eq.eqType = type;
          eq.blendMeshLight = 1.0f;
          eq.texCoordU = 0.0f;
          eq.addTerrainLight = (type == 2 || type == 5 || type == 8);
          m_earthQuakeCracks.push_back(eq);
        };

        // Center crack cluster: EQ03 + EQ01 + EQ02 (Main 5.2: PKKey=150 → scale 1.5)
        spawnEQ(3, 1.5f, 35.0f, impact, (float)(rand() % 360));
        spawnEQ(1, 1.5f, 35.0f, impact, (float)(rand() % 360));
        spawnEQ(2, 1.5f, 20.0f, impact, (float)(rand() % 360));

        // 5 radial arm pairs (Main 5.2: 72° spacing, SubType offset, distance 100-249)
        for (int arm = 0; arm < 5; ++arm) {
          float armAngleDeg = (float)(fs.randomOffset + arm * 72);
          float armAngleRad = glm::radians(armAngleDeg);
          float dist = (float)(rand() % 150 + 100);

          glm::vec3 armPos = impact;
          armPos.x += sinf(armAngleRad) * dist;
          armPos.z += cosf(armAngleRad) * dist;
          if (m_getTerrainHeight)
            armPos.y = m_getTerrainHeight(armPos.x, armPos.z) + 3.0f;

          float armScale = (float)(rand() % 50 + 40) / 100.0f;
          // Main 5.2: arm angle = 45 + rand()%30-15 = 30-60 degrees
          float armRotation = 45.0f + (float)(rand() % 30 - 15);

          spawnEQ(4, armScale, 35.0f, armPos, armRotation);
          spawnEQ(5, armScale, 40.0f, armPos, armRotation);

          // Smoke puff at each arm endpoint
          for (int s = 0; s < 3; ++s) {
            Particle p;
            p.type = ParticleType::SMOKE;
            p.position = armPos + glm::vec3(
                (float)(rand() % 20 - 10), 3.0f, (float)(rand() % 20 - 10));
            p.velocity = glm::vec3(
                (float)(rand() % 14 - 7),
                20.0f + (float)(rand() % 25),
                (float)(rand() % 14 - 7));
            p.scale = 20.0f + (float)(rand() % 20);
            p.maxLifetime = 0.8f + (float)(rand() % 30) / 100.0f;
            p.lifetime = p.maxLifetime;
            p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
            p.color = glm::vec3(0.45f, 0.4f, 0.3f);
            p.alpha = 0.4f;
            p.frame = -1.0f;
            m_particles.push_back(p);
          }
        }
      }

      // Phase 2: Tick 10 (= Main 5.2 LifeTime==10): Branching crack chains
      if (fs.ticksElapsed == 10 && !fs.phase2Done) {
        fs.phase2Done = true;
        glm::vec3 impact = fs.impactPos;

        // Main 5.2: SOUND_FURY_STRIKE3 — earth cracking/rumble
        if (m_playSound) m_playSound(151); // SOUND_RAGE_BLOW3

        // Main 5.2 parallel branch algorithm:
        // 5 branch positions starting at impact, 4 iterations each
        // Branches diverge with alternating zigzag turns
        glm::vec3 branchPos[5];
        float branchAng[5];
        for (int i = 0; i < 5; ++i) {
          branchPos[i] = impact;
          branchAng[i] = 0.0f;
        }

        int count = 0;
        for (int j = 0; j < 4; ++j) {
          float segDist = (float)(rand() % 15 + 85);
          if (j >= 3) count = rand(); // Randomize on last iteration

          for (int i = 0; i < 5; ++i) {
            float turn;
            if ((count % 2) == 0)
              turn = (float)(rand() % 30 + 50);
            else
              turn = -(float)(rand() % 30 + 50);

            branchAng[i] += turn;
            // Main 5.2: angle = accumulated + (branchIndex * (rand()%10+62))
            float segAngle = branchAng[i] + (float)(i * (rand() % 10 + 62));
            float segAngleRad = glm::radians(segAngle);

            branchPos[i].x += sinf(segAngleRad) * segDist;
            branchPos[i].z += cosf(segAngleRad) * segDist;
            if (m_getTerrainHeight)
              branchPos[i].y = m_getTerrainHeight(branchPos[i].x, branchPos[i].z) + 3.0f;

            float eqAngle = segAngle + 270.0f;

            EarthQuakeCrack eq7;
            eq7.position = branchPos[i];
            eq7.angle = eqAngle;
            eq7.scale = 1.0f;
            eq7.lifetime = 40.0f;
            eq7.maxLifetime = 40.0f;
            eq7.eqType = 7;
            eq7.blendMeshLight = 1.0f;
            eq7.texCoordU = 0.0f;
            eq7.addTerrainLight = false;
            m_earthQuakeCracks.push_back(eq7);

            EarthQuakeCrack eq8;
            eq8.position = branchPos[i];
            eq8.angle = eqAngle;
            eq8.scale = 1.0f;
            eq8.lifetime = 40.0f;
            eq8.maxLifetime = 40.0f;
            eq8.eqType = 8;
            eq8.blendMeshLight = 1.0f;
            eq8.texCoordU = 0.0f;
            eq8.addTerrainLight = true;
            m_earthQuakeCracks.push_back(eq8);

            // Smoke at every other branch segment
            if ((count % 2) == 0) {
              Particle p;
              p.type = ParticleType::SMOKE;
              p.position = branchPos[i] + glm::vec3(0, 3.0f, 0);
              p.velocity = glm::vec3(
                  (float)(rand() % 10 - 5),
                  15.0f + (float)(rand() % 20),
                  (float)(rand() % 10 - 5));
              p.scale = 18.0f + (float)(rand() % 15);
              p.maxLifetime = 0.7f + (float)(rand() % 20) / 100.0f;
              p.lifetime = p.maxLifetime;
              p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
              p.color = glm::vec3(0.45f, 0.4f, 0.3f);
              p.alpha = 0.35f;
              p.frame = -1.0f;
              m_particles.push_back(p);
            }

            count++;
          }
        }
      }
    }
  }
}

void VFXManager::updateEarthQuakeCracks(float dt) {
  float dtTicks = dt / 0.04f;

  for (auto &eq : m_earthQuakeCracks) {
    eq.lifetime -= dtTicks;

    // Update BlendMeshLight based on type (Main 5.2 MoveEffect)
    switch (eq.eqType) {
    case 1: // EQ01: fade out
      eq.blendMeshLight = (eq.lifetime * 0.1f) / 2.0f;
      break;
    case 2: // EQ02: ramp up then down
      if (eq.lifetime >= 10.0f)
        eq.blendMeshLight = (eq.maxLifetime - eq.lifetime) * 0.15f;
      else
        eq.blendMeshLight = eq.lifetime * 0.15f;
      break;
    case 3: // EQ03: center crack — bright
      eq.blendMeshLight = (eq.lifetime * 0.1f) / 4.0f;
      break;
    case 4: // EQ04: radial arms
      eq.blendMeshLight = (eq.lifetime * 0.1f) / 2.0f;
      break;
    case 5: // EQ05: ramp up then down
      if (eq.lifetime >= 30.0f)
        eq.blendMeshLight = (eq.maxLifetime - eq.lifetime) * 0.15f;
      else
        eq.blendMeshLight = eq.lifetime * 0.15f;
      break;
    case 7: // EQ07: branching
      eq.blendMeshLight = (eq.lifetime * 0.1f) / 2.0f;
      break;
    case 8: // EQ08: ramp up then down
      if (eq.lifetime >= 30.0f)
        eq.blendMeshLight = (eq.maxLifetime - eq.lifetime) * 0.15f;
      else
        eq.blendMeshLight = eq.lifetime * 0.15f;
      break;
    }
    eq.blendMeshLight = std::max(0.0f, std::min(1.0f, eq.blendMeshLight));

    // UV scroll for EQ02/05/08
    if (eq.eqType == 2 || eq.eqType == 5 || eq.eqType == 8) {
      eq.texCoordU = -(float)(int)eq.lifetime * 0.01f;
    }

    // Sinking into ground
    bool shouldSink = false;
    switch (eq.eqType) {
    case 1: case 4: case 7: shouldSink = eq.lifetime < 10.0f; break;
    case 2:                 shouldSink = eq.lifetime < 5.0f; break;
    case 3:                 shouldSink = eq.lifetime < 13.0f; break;
    case 5: case 8:         shouldSink = eq.lifetime < 15.0f; break;
    }
    if (shouldSink) {
      eq.position.y -= 0.5f * dtTicks;
    }

  }

  m_earthQuakeCracks.erase(
      std::remove_if(m_earthQuakeCracks.begin(), m_earthQuakeCracks.end(),
                     [](const EarthQuakeCrack &eq) { return eq.lifetime <= 0.0f; }),
      m_earthQuakeCracks.end());
}

void VFXManager::renderEarthQuakeCracks(const glm::mat4 &view,
                                         const glm::mat4 &projection) {
  if (m_earthQuakeCracks.empty() || !m_modelShader) return;
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1, 1, 1, 0));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 1));
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  for (const auto &eq : m_earthQuakeCracks) {
    int t = eq.eqType;
    if (t < 1 || t > 8) continue;
    int meshIdx = (t == 6) ? 4 : t;
    const auto &meshes = m_eqMeshes[meshIdx];
    if (meshes.empty()) continue;
    m_modelShader->setVec4("u_params", glm::vec4(eq.blendMeshLight, eq.blendMeshLight, 0, 0));
    m_modelShader->setVec4("u_texCoordOffset", glm::vec4(eq.texCoordU, 0, 0, 0));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), eq.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(eq.angle), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(eq.scale));
    for (const auto &mb : meshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
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

void VFXManager::updateStoneDebris(float dt) {
  for (auto &s : m_stoneDebris) {
    s.lifetime -= dt;

    // Position += velocity (Main 5.2: Position += Direction)
    s.position += s.velocity * dt;
    // Direction decay ~0.9 per tick (Main 5.2: Direction *= 0.9)
    s.velocity *= (1.0f - 2.5f * dt);

    // Gravity (Main 5.2: Gravity -= 3 per tick, Position[2] += Gravity)
    s.gravity -= 75.0f * dt;
    s.position.y += s.gravity * dt;

    // Tumble rotation
    s.angleX -= s.scale * 800.0f * dt;
    s.angleY += s.scale * 400.0f * dt;

    // Terrain bounce
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(s.position.x, s.position.z)
                         : 0.0f;
    if (s.position.y < groundH) {
      s.position.y = groundH;
      s.gravity = -s.gravity * 0.5f; // 50% restitution
      s.lifetime -= 0.16f; // Lose 4 ticks on bounce
    }
  }
  m_stoneDebris.erase(
      std::remove_if(m_stoneDebris.begin(), m_stoneDebris.end(),
                     [](const StoneDebris &s) { return s.lifetime <= 0.0f; }),
      m_stoneDebris.end());
}

void VFXManager::renderStoneDebris(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_stoneDebris.empty() || !m_modelShader) return;
  if (m_stone1Meshes.empty() && m_stone2Meshes.empty()) return;
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
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  for (const auto &s : m_stoneDebris) {
    float alpha = std::min(1.0f, s.lifetime * 2.0f);
    m_modelShader->setVec4("u_params", glm::vec4(alpha, 1.0f, 0, 0));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), s.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, s.angleX, glm::vec3(1, 0, 0));
    model = glm::rotate(model, s.angleY, glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(s.scale));
    const auto &meshes = s.useStone2 ? m_stone2Meshes : m_stone1Meshes;
    for (const auto &mb : meshes) {
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

// ============================================================================
// Death Stab (AT_SKILL_DEATHSTAB) — 3-phase VFX
// Phase 1: Red spiraling energy vortex converging on weapon (ticks 2-8)
// Phase 2: Forward weapon flare trail (ticks 6-12)
// Phase 3: Cyan lightning electrocution on target (35 ticks)
// ============================================================================

void VFXManager::SpawnDeathStab(const glm::vec3 &heroPos, float facing,
                                const glm::vec3 &targetPos) {
  DeathStabEffect e;
  e.casterPos = heroPos;
  e.targetPos = targetPos;
  e.casterFacing = facing;
  e.totalLifetime = 0.7f; // ~17 ticks of activity
  m_deathStabEffects.push_back(e);
}

void VFXManager::SpawnDeathStabShock(const glm::vec3 &targetPos) {
  DeathStabShock s;
  s.position = targetPos;
  s.lifetime = 1.4f;  // 35 ticks (Main 5.2: m_byHurtByDeathstab = 35)
  s.maxLifetime = 1.4f;
  m_deathStabShocks.push_back(s);

  // Impact flash burst — bright cyan/white flash at hit moment
  glm::vec3 center = targetPos;
  center.y += 50.0f; // Center of target body
  for (int i = 0; i < 4; ++i) {
    Particle p;
    p.type = ParticleType::DEATHSTAB_SPARK;
    p.position = center;
    p.position.x += (float)(rand() % 40) - 20.0f;
    p.position.y += (float)(rand() % 60) - 20.0f;
    p.position.z += (float)(rand() % 40) - 20.0f;
    p.velocity = glm::vec3(
      (float)(rand() % 200 - 100),
      (float)(rand() % 150) + 50.0f,
      (float)(rand() % 200 - 100)
    );
    p.scale = 15.0f + (float)(rand() % 10);
    p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
    p.color = glm::vec3(0.6f, 0.7f, 1.0f); // Cyan-white
    p.alpha = 1.0f;
    p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
    p.lifetime = p.maxLifetime;
    p.frame = 0.0f;
    m_particles.push_back(p);
  }
}

// Main 5.2: GetMagicScrew — spherical-to-Cartesian spiral path
// Exact reproduction from ZzzEffectJoint.cpp lines 7409-7426
static glm::vec3 GetMagicScrewPos(int iParam, float fSpeedRate, int frameOffset) {
  iParam += frameOffset;

  float fSpeed0 = 0.048f * fSpeedRate;
  float fSpeed1 = 0.0613f * fSpeedRate;
  float fSpeed2 = 0.1113f * fSpeedRate;

  // Spherical coordinates → Cartesian
  float sinA = sinf((float)(iParam + 55555) * fSpeed0);
  float cosA = cosf((float)(iParam + 55555) * fSpeed0);
  float sinB = sinf((float)iParam * fSpeed1);
  float cosB = cosf((float)iParam * fSpeed1);

  float dirX = sinA * cosB;
  float dirY = sinA * sinB;
  float dirZ = cosA;

  // Additional rotation
  float fSinAdd = sinf((float)(iParam + 11111) * fSpeed2);
  float fCosAdd = cosf((float)(iParam + 11111) * fSpeed2);

  // Main 5.2 output: [0]=cosAdd*dirY - sinAdd*dirZ, [1]=sinAdd*dirY + cosAdd*dirZ, [2]=dirX
  // Mapped to our Y-up: Main5.2 Z→our Y, Main5.2 X→our X, Main5.2 Y→our Z
  float outX = fCosAdd * dirY - fSinAdd * dirZ; // Main 5.2 result[0]
  float outZ = fSinAdd * dirY + fCosAdd * dirZ; // Main 5.2 result[1]
  float outY = dirX;                             // Main 5.2 result[2]

  return glm::vec3(outX, outY, outZ);
}

void VFXManager::updateDeathStabEffects(float dt) {
  static constexpr float TICK_INTERVAL = 0.04f; // 40ms per tick

  for (int i = (int)m_deathStabEffects.size() - 1; i >= 0; --i) {
    auto &e = m_deathStabEffects[i];
    e.totalLifetime -= dt;
    if (e.totalLifetime <= 0.0f) {
      m_deathStabEffects[i] = m_deathStabEffects.back();
      m_deathStabEffects.pop_back();
      continue;
    }

    e.tickTimer += dt;
    while (e.tickTimer >= TICK_INTERVAL) {
      e.tickTimer -= TICK_INTERVAL;
      e.ticksElapsed++;

      // Phase 1 (ticks 2-8): Energy streaks from behind caster, converging on monster head
      // "Behind" computed from caster→target direction, not stored facing angle
      if (e.ticksElapsed >= 2 && e.ticksElapsed <= 8) {
        // Compute attack direction from caster to target
        glm::vec3 toTarget = e.targetPos - e.casterPos;
        toTarget.y = 0.0f;
        float dist = glm::length(toTarget);
        float sinF, cosF;
        if (dist > 1.0f) {
          sinF = toTarget.x / dist;   // Forward X
          cosF = -toTarget.z / dist;  // Forward Z (our convention: fwd = sin, -cos)
        } else {
          sinF = sinf(e.casterFacing);
          cosF = cosf(e.casterFacing);
        }

        // Convergence target: monster head position
        glm::vec3 swordPos = e.targetPos;
        swordPos.y += 80.0f;

        for (int j = 0; j < 3; ++j) {
          DeathStabSpiral sp;
          // Spawn 1400 units BEHIND caster (opposite attack direction) + random spread
          glm::vec3 spawnBase = e.casterPos;
          spawnBase.y += 120.0f;
          spawnBase.x += (float)(rand() % 601) - 300.0f;
          spawnBase.y += (float)(rand() % 601) - 300.0f;
          spawnBase.z += (float)(rand() % 601) - 300.0f;
          spawnBase.x += -1400.0f * sinF;
          spawnBase.z += 1400.0f * cosF;

          // Bake spiral offset into origin (no per-frame wobble)
          int seed = (e.ticksElapsed * 3 + j) * 17721;
          glm::vec3 spiralDir = GetMagicScrewPos(seed, 1.4f, seed);
          sp.origin = spawnBase + spiralDir * 300.0f;

          sp.swordPos = swordPos;
          sp.position = sp.origin;
          sp.spiralSeed = seed;
          sp.frameCounter = 0;
          sp.maxLifetime = 0.8f;
          sp.lifetime = sp.maxLifetime;
          sp.scale = 60.0f;
          sp.alpha = 1.0f;

          m_deathStabSpirals.push_back(sp);
        }
      }

      // Phase 2 (ticks 6-12): Spear thrust trail
      // Main 5.2: MODEL_SPEAR SubType 1 → BITMAP_FLARE SubType 12
      // SubType 12: WHITE (1,1,1), Scale=100*0.1=10, LifeTime=20 ticks (0.8s)
      if (e.ticksElapsed >= 6 && e.ticksElapsed <= 12) {
        float sinF = sinf(e.casterFacing);
        float cosF = cosf(e.casterFacing);
        float fwdDist = 100.0f + (float)(e.ticksElapsed - 8) * 10.0f;

        for (int j = 0; j < 2; ++j) {
          float randOff = (float)(rand() % 20) - 10.0f;
          float randH = (float)(rand() % 40) + 40.0f;

          glm::vec3 flarePos = e.casterPos;
          flarePos.x += (fwdDist + randOff) * sinF;
          flarePos.y += randH;
          flarePos.z += (fwdDist + randOff) * (-cosF);

          // Main 5.2: BITMAP_FLARE SubType 12 — WHITE, trails backward
          Particle p;
          p.type = ParticleType::FLARE;
          p.position = flarePos;
          p.velocity = glm::vec3(-sinF * 40.0f, 10.0f, cosF * 40.0f);
          p.scale = 10.0f; // Main 5.2: Scale * 0.1 = 100 * 0.1 = 10
          p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          p.color = glm::vec3(1.0f, 1.0f, 1.0f); // Main 5.2: WHITE
          p.alpha = 1.0f;
          p.maxLifetime = 0.8f; // Main 5.2: 20 ticks = 0.8s
          p.lifetime = p.maxLifetime;
          p.frame = 0.0f;
          m_particles.push_back(p);
        }
      }
    }
  }
}

void VFXManager::updateDeathStabSpirals(float dt) {
  for (int i = (int)m_deathStabSpirals.size() - 1; i >= 0; --i) {
    auto &sp = m_deathStabSpirals[i];
    sp.lifetime -= dt;
    if (sp.lifetime <= 0.0f) {
      m_deathStabSpirals[i] = m_deathStabSpirals.back();
      m_deathStabSpirals.pop_back();
      continue;
    }

    // Straight interpolation from origin to sword (no per-frame wobble)
    float lifeTime20 = (sp.lifetime / sp.maxLifetime) * 20.0f;
    float fRate1 = std::max(0.0f, std::min((lifeTime20 - 10.0f) / 10.0f, 1.0f));
    float fRate2 = 1.0f - fRate1;
    sp.position = fRate1 * sp.origin + fRate2 * sp.swordPos;

    // Scale shrinks: Main 5.2 Scale = LifeTime * 3.0
    sp.scale = std::max(4.0f, lifeTime20 * 3.0f);

    // Alpha: Main 5.2 BlendMeshLight = min(LifeTime, 20) * 0.05
    sp.alpha = std::min(1.0f, lifeTime20 * 0.05f);

    // Main 5.2: CreateTail runs at tick rate (25fps = 40ms)
    // Gate tail creation to tick intervals for straight, smooth trails
    static constexpr float TICK_INTERVAL = 0.04f;
    sp.tailTimer += dt;
    while (sp.tailTimer >= TICK_INTERVAL) {
      sp.tailTimer -= TICK_INTERVAL;
      int maxIdx = DeathStabSpiral::MAX_TAILS - 1;
      if (sp.numTails < maxIdx)
        sp.numTails++;
      for (int j = sp.numTails - 1; j > 0; --j)
        sp.tails[j] = sp.tails[j - 1];
      sp.tails[0] = sp.position;
    }
  }
}

void VFXManager::updateDeathStabShocks(float dt) {
  static constexpr float TICK_INTERVAL = 0.04f;

  // Update shocks — spawn new arcs each tick
  for (int i = (int)m_deathStabShocks.size() - 1; i >= 0; --i) {
    auto &s = m_deathStabShocks[i];
    s.lifetime -= dt;
    if (s.lifetime <= 0.0f) {
      m_deathStabShocks[i] = m_deathStabShocks.back();
      m_deathStabShocks.pop_back();
      continue;
    }

    s.tickTimer += dt;
    while (s.tickTimer >= TICK_INTERVAL) {
      s.tickTimer -= TICK_INTERVAL;

      // Main 5.2: iterate ALL bone parent-child pairs on target EVERY frame
      // No skip — creates arcs between every non-dummy bone and its parent
      // Typical monster: 30-50 bone pairs. We approximate with 15-25 arcs.
      // Each arc simulates a parent-child bone connection with ±20 jitter
      int numArcs = 5 + rand() % 4; // 5-8 arcs per tick
      for (int j = 0; j < numArcs; ++j) {
        DeathStabArc arc;

        // Simulate skeleton bone positions:
        // Child bone — random position within monster body volume
        // ±50 horizontal spread, 0-120 height (covers full body)
        arc.start = s.position;
        arc.start.x += (float)(rand() % 100) - 50.0f;
        arc.start.y += (float)(rand() % 120);
        arc.start.z += (float)(rand() % 100) - 50.0f;

        // Parent bone — nearby (connected bone, not completely random)
        // Offset from child by 20-60 units (typical bone segment length)
        arc.end = arc.start;
        arc.end.x += (float)(rand() % 60) - 30.0f;
        arc.end.y += (float)(rand() % 60) - 30.0f;
        arc.end.z += (float)(rand() % 60) - 30.0f;

        // Main 5.2: GetNearRandomPos with range 20 on both endpoints
        arc.start.x += (float)(rand() % 41) - 20.0f;
        arc.start.y += (float)(rand() % 41) - 20.0f;
        arc.start.z += (float)(rand() % 41) - 20.0f;
        arc.end.x += (float)(rand() % 41) - 20.0f;
        arc.end.y += (float)(rand() % 41) - 20.0f;
        arc.end.z += (float)(rand() % 41) - 20.0f;

        // Main 5.2: SubType 7 LifeTime = 1 frame (one-frame flash)
        arc.scale = 12.0f;
        arc.maxLifetime = 0.05f; // ~1 tick — single frame flash
        arc.lifetime = arc.maxLifetime;

        m_deathStabArcs.push_back(arc);
      }

      // 0-1 cyan spark particles per tick
      int numSparks = rand() % 2;
      for (int j = 0; j < numSparks; ++j) {
        Particle p;
        p.type = ParticleType::DEATHSTAB_SPARK;
        p.position = s.position;
        p.position.x += (float)(rand() % 100) - 50.0f;
        p.position.y += (float)(rand() % 120);
        p.position.z += (float)(rand() % 100) - 50.0f;
        p.velocity = glm::vec3(
          (float)(rand() % 100 - 50),
          (float)(rand() % 80) + 20.0f,
          (float)(rand() % 100 - 50)
        );
        p.scale = 8.0f + (float)(rand() % 8);
        p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
        p.color = glm::vec3(0.5f, 0.5f, 1.0f); // Main 5.2: (0.5, 0.5, 1.0)
        p.alpha = 0.9f;
        p.maxLifetime = 0.12f + (float)(rand() % 8) / 100.0f;
        p.lifetime = p.maxLifetime;
        p.frame = 0.0f;
        m_particles.push_back(p);
      }
    }
  }

  // Update arcs — decrement lifetime, remove expired
  for (int i = (int)m_deathStabArcs.size() - 1; i >= 0; --i) {
    m_deathStabArcs[i].lifetime -= dt;
    if (m_deathStabArcs[i].lifetime <= 0.0f) {
      m_deathStabArcs[i] = m_deathStabArcs.back();
      m_deathStabArcs.pop_back();
    }
  }
}

void VFXManager::renderDeathStabShocks(const glm::mat4 &view,
                                        const glm::mat4 &projection) {
  if (m_deathStabArcs.empty() || !m_lineShader) return;
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  m_lineShader->setVec4("u_lineMode", glm::vec4(TexValid(m_lightningTexture) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));

  std::vector<RibbonVertex> verts;
  verts.reserve(m_deathStabArcs.size() * 24);
  for (const auto &arc : m_deathStabArcs) {
    glm::vec3 seg = arc.end - arc.start;
    float segLen = glm::length(seg);
    if (segLen < 5.0f) continue;
    glm::vec3 mid = (arc.start + arc.end) * 0.5f;
    glm::vec3 fwd = seg / segLen;
    glm::vec3 worldUp(0, 1, 0);
    if (std::abs(glm::dot(fwd, worldUp)) > 0.95f) worldUp = glm::vec3(1, 0, 0);
    glm::vec3 perp = glm::normalize(glm::cross(fwd, worldUp));
    glm::vec3 perp2 = glm::normalize(glm::cross(fwd, perp));
    float curveAmt = segLen * 0.3f;
    mid += perp * ((float)(rand() % 200 - 100) / 100.0f * curveAmt);
    mid += perp2 * ((float)(rand() % 200 - 100) / 100.0f * curveAmt * 0.5f);
    glm::vec3 pts[3] = { arc.start, mid, arc.end };
    float scale = arc.scale;
    for (int s = 0; s < 2; ++s) {
      glm::vec3 segDir = pts[s + 1] - pts[s];
      float sLen = glm::length(segDir);
      if (sLen < 1.0f) continue;
      glm::vec3 sFwd = segDir / sLen;
      glm::vec3 wUp(0, 1, 0);
      if (std::abs(glm::dot(sFwd, wUp)) > 0.95f) wUp = glm::vec3(1, 0, 0);
      glm::vec3 right = glm::normalize(glm::cross(sFwd, wUp)) * scale;
      glm::vec3 up = glm::normalize(glm::cross(right, sFwd)) * scale;
      float u0 = (float)s * 0.5f, u1 = (float)(s + 1) * 0.5f;
      RibbonVertex v;
      v.pos = pts[s] - right;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = pts[s] + right;   v.uv = glm::vec2(u0, 1); verts.push_back(v);
      v.pos = pts[s+1] + right; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = pts[s] - right;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = pts[s+1] + right; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = pts[s+1] - right; v.uv = glm::vec2(u1, 0); verts.push_back(v);
      v.pos = pts[s] - up;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = pts[s] + up;   v.uv = glm::vec2(u0, 1); verts.push_back(v);
      v.pos = pts[s+1] + up; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = pts[s] - up;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = pts[s+1] + up; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = pts[s+1] - up; v.uv = glm::vec2(u1, 0); verts.push_back(v);
    }
  }
  if (!verts.empty()) {
    if ((int)verts.size() > MAX_RIBBON_VERTS) verts.resize(MAX_RIBBON_VERTS);
    uint32_t nv = (uint32_t)verts.size();
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) >= nv) {
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
      m_lineShader->setVec4("u_lineColor", glm::vec4(0.5f, 0.5f, 1.0f, 0.7f));
      if (TexValid(m_lightningTexture)) m_lineShader->setTexture(0, "s_ribbonTex", m_lightningTexture);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(state);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

void VFXManager::renderDeathStabSpirals(const glm::mat4 &view,
                                         const glm::mat4 &projection) {
  if (m_deathStabSpirals.empty() || !m_lineShader) return;
  TexHandle tex = TexValid(m_flareForceTexture) ? m_flareForceTexture : m_energyTexture;
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  m_lineShader->setVec4("u_lineMode", glm::vec4(TexValid(tex) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));

  std::vector<RibbonVertex> verts;
  verts.reserve(m_deathStabSpirals.size() * DeathStabSpiral::MAX_TAILS * 12);
  for (const auto &sp : m_deathStabSpirals) {
    if (sp.numTails < 2) continue;
    float hw = sp.scale * 0.5f;
    for (int j = 0; j < sp.numTails - 1; ++j) {
      glm::vec3 segDir = sp.tails[j + 1] - sp.tails[j];
      float sLen = glm::length(segDir);
      if (sLen < 1.0f) continue;
      glm::vec3 sFwd = segDir / sLen;
      glm::vec3 wUp(0, 1, 0);
      if (std::abs(glm::dot(sFwd, wUp)) > 0.95f) wUp = glm::vec3(1, 0, 0);
      glm::vec3 right0 = glm::normalize(glm::cross(sFwd, wUp));
      glm::vec3 up0 = glm::normalize(glm::cross(right0, sFwd));
      float u0 = (float)(sp.numTails - j) / (float)(DeathStabSpiral::MAX_TAILS - 1);
      float u1 = (float)(sp.numTails - (j + 1)) / (float)(DeathStabSpiral::MAX_TAILS - 1);
      RibbonVertex v;
      v.pos = sp.tails[j] - right0 * hw;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = sp.tails[j] + right0 * hw;   v.uv = glm::vec2(u0, 1); verts.push_back(v);
      v.pos = sp.tails[j+1] + right0 * hw; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = sp.tails[j] - right0 * hw;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = sp.tails[j+1] + right0 * hw; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = sp.tails[j+1] - right0 * hw; v.uv = glm::vec2(u1, 0); verts.push_back(v);
      v.pos = sp.tails[j] - up0 * hw;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = sp.tails[j] + up0 * hw;   v.uv = glm::vec2(u0, 1); verts.push_back(v);
      v.pos = sp.tails[j+1] + up0 * hw; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = sp.tails[j] - up0 * hw;   v.uv = glm::vec2(u0, 0); verts.push_back(v);
      v.pos = sp.tails[j+1] + up0 * hw; v.uv = glm::vec2(u1, 1); verts.push_back(v);
      v.pos = sp.tails[j+1] - up0 * hw; v.uv = glm::vec2(u1, 0); verts.push_back(v);
    }
  }
  if (!verts.empty()) {
    if ((int)verts.size() > 4096) verts.resize(4096);
    uint32_t nv = (uint32_t)verts.size();
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) >= nv) {
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
      m_lineShader->setVec4("u_lineColor", glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
      if (TexValid(tex)) m_lineShader->setTexture(0, "s_ribbonTex", tex);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(state);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

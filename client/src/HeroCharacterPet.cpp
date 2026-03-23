#include "HeroCharacter.hpp"
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// ── Pet companion rendering (Guardian Angel / Imp) ──────────────────────────
// Extracted from HeroCharacter::Render — update + render pet each frame.

void HeroCharacter::renderPetCompanion(const glm::mat4 &view, const glm::mat4 &proj,
                                        const glm::vec3 &camPos, float deltaTime,
                                        const glm::vec3 &tLight) {
  // ── Pet companion: update + render (Guardian Angel / Imp) ──
  // Polar orbit system: pet circles character at smooth angular velocity,
  // always facing the character's head. Elegant, stable, no stuttering.
  if (m_pet.active && m_pet.bmd && !m_pet.meshBuffers.empty()) {
    constexpr float TICK_INTERVAL = 0.04f;

    const bool isImp = (m_pet.itemIndex == 1);

    // Orbit parameters per pet type
    const float ORBIT_RADIUS  = isImp ? 100.0f : 140.0f;
    const float BASE_HEIGHT   = isImp ? 90.0f  : 180.0f;
    const float BOB_AMPLITUDE = isImp ? 6.0f   : 12.0f;
    const float BOB_FREQ      = isImp ? 1.5f   : 1.0f;  // Hz
    const float MAX_DIST      = isImp ? 400.0f : 500.0f;

    // ── Teleport if too far (login, teleport, map change) ──
    float petDistX = m_pet.pos.x - m_pos.x;
    float petDistZ = m_pet.pos.z - m_pos.z;
    float petDistSq = petDistX * petDistX + petDistZ * petDistZ;
    if (petDistSq > MAX_DIST * MAX_DIST) {
      m_pet.orbitCenter = m_pos;
      m_pet.orbitAngle = glm::radians((float)(rand() % 360));
      m_pet.orbitSpeed = 0.3f;
      m_pet.orbitSpeedTarget = 0.3f;
      m_pet.orbitRadius = ORBIT_RADIUS;
      m_pet.orbitHeight = BASE_HEIGHT;
      m_pet.orbitHeightTarget = BASE_HEIGHT;
      m_pet.bobPhase = 0.0f;
    }

    // ── Two modes: idle orbit vs following behind ──
    bool charMoving = m_moving;

    // Orbit center trails behind character with natural delay
    // Slow rate when moving = pet visibly lags on direction changes
    float followRate = 1.0f - expf((charMoving ? -1.2f : -3.0f) * deltaTime);
    m_pet.orbitCenter.x += (m_pos.x - m_pet.orbitCenter.x) * followRate;
    m_pet.orbitCenter.z += (m_pos.z - m_pet.orbitCenter.z) * followRate;
    m_pet.orbitCenter.y += (m_pos.y - m_pet.orbitCenter.y) * followRate;

    if (!charMoving) {
      // ── IDLE MODE: orbit around character, face toward them ──
      m_pet.decisionTimer -= deltaTime;
      if (m_pet.decisionTimer <= 0.0f) {
        m_pet.decisionTimer = 3.0f + (float)(rand() % 3000) / 1000.0f;

        int behavior = rand() % 10;
        if (behavior < 4) {
          m_pet.orbitSpeedTarget = 0.12f + (float)(rand() % 150) / 1000.0f;
        } else if (behavior < 8) {
          m_pet.orbitSpeedTarget = -(0.12f + (float)(rand() % 150) / 1000.0f);
        } else {
          m_pet.orbitSpeedTarget = 0.0f;
        }

        float heightVar = isImp ? 10.0f : 25.0f;
        m_pet.orbitHeightTarget = BASE_HEIGHT + (float)(rand() % (int)(heightVar * 2)) - heightVar;
      }

      float smoothRate = 1.0f - expf(-1.5f * deltaTime);
      m_pet.orbitSpeed += (m_pet.orbitSpeedTarget - m_pet.orbitSpeed) * smoothRate;
      m_pet.orbitHeight += (m_pet.orbitHeightTarget - m_pet.orbitHeight) * smoothRate;
      m_pet.orbitRadius += (ORBIT_RADIUS - m_pet.orbitRadius) * smoothRate;

      // Advance orbit angle
      m_pet.orbitAngle += m_pet.orbitSpeed * deltaTime;

    } else {
      // ── FOLLOWING MODE: steer behind character's back ──
      // "Behind" in orbit polar coords = m_facing + PI
      float behindAngle = m_facing + (float)M_PI;
      float angleDiff = behindAngle - m_pet.orbitAngle;
      while (angleDiff > (float)M_PI) angleDiff -= 2.0f * (float)M_PI;
      while (angleDiff < -(float)M_PI) angleDiff += 2.0f * (float)M_PI;
      // Lazy steer — pet swings around slowly on sudden turns
      float steerRate = 1.0f - expf(-1.2f * deltaTime);
      m_pet.orbitAngle += angleDiff * steerRate;

      float smoothRate = 1.0f - expf(-1.5f * deltaTime);
      m_pet.orbitHeight += (BASE_HEIGHT - m_pet.orbitHeight) * smoothRate;
      m_pet.orbitRadius += (ORBIT_RADIUS - m_pet.orbitRadius) * smoothRate;
      m_pet.orbitSpeed = 0.0f;
      m_pet.orbitSpeedTarget = 0.0f;
    }

    // ── Gentle vertical bob (sine wave) ──
    m_pet.bobPhase += deltaTime * BOB_FREQ * 2.0f * (float)M_PI;
    if (m_pet.bobPhase > 2.0f * (float)M_PI) m_pet.bobPhase -= 2.0f * (float)M_PI;
    float bobOffset = sinf(m_pet.bobPhase) * BOB_AMPLITUDE;

    // ── Compute world position from polar orbit around trailing center ──
    m_pet.pos.x = m_pet.orbitCenter.x + cosf(m_pet.orbitAngle) * m_pet.orbitRadius;
    m_pet.pos.z = m_pet.orbitCenter.z + sinf(m_pet.orbitAngle) * m_pet.orbitRadius;
    m_pet.pos.y = m_pet.orbitCenter.y + m_pet.orbitHeight + bobOffset;

    // ── Facing ──
    float targetFacing;
    if (charMoving) {
      // Following: face same direction as character
      targetFacing = m_facing;
    } else {
      // Idle: face toward character
      float toCharX = m_pos.x - m_pet.pos.x;
      float toCharZ = m_pos.z - m_pet.pos.z;
      targetFacing = atan2f(toCharZ, -toCharX);
    }
    float facingDiff = targetFacing - m_pet.facing;
    while (facingDiff > (float)M_PI) facingDiff -= 2.0f * (float)M_PI;
    while (facingDiff < -(float)M_PI) facingDiff += 2.0f * (float)M_PI;
    m_pet.facing += facingDiff * glm::clamp(deltaTime * 6.0f, 0.0f, 1.0f);

    // ── Pitch ──
    float targetPitch;
    if (charMoving) {
      targetPitch = 0.0f; // Level flight when following
    } else {
      // Tilt toward character head when orbiting
      float toCharX = m_pos.x - m_pet.pos.x;
      float toCharZ = m_pos.z - m_pet.pos.z;
      float headHeight = m_pos.y + 120.0f;
      float dy = headHeight - m_pet.pos.y;
      float hDist = sqrtf(toCharX * toCharX + toCharZ * toCharZ);
      targetPitch = atan2f(dy, std::max(hDist, 1.0f));
    }
    float pitchRate = 1.0f - expf(-3.0f * deltaTime);
    m_pet.pitch += (targetPitch - m_pet.pitch) * pitchRate;

    // ── Sparkle particles (tick-rate limited, Imp only) ──
    m_pet.sparkAccum += deltaTime;
    while (m_pet.sparkAccum >= TICK_INTERVAL) {
      m_pet.sparkAccum -= TICK_INTERVAL;
      if (m_vfxManager && isImp) {
        for (int j = 0; j < 2; ++j) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 16 - 8), (float)(rand() % 16 - 8),
              (float)(rand() % 16 - 8));
          m_vfxManager->SpawnBurst(ParticleType::IMP_SPARKLE, sparkPos, 1);
        }
      }
    }

    // ── Alpha: exponential smoothing (Main 5.2: Alpha += (AlphaTarget - Alpha) * 0.1f per tick) ──
    // Guardian Angel is ethereal/translucent; Imp is solid
    float alphaTarget = isImp ? 1.0f : 0.55f;
    if (m_pet.alpha < alphaTarget - 0.01f) {
      float ticksThisFrame = deltaTime / TICK_INTERVAL;
      m_pet.alpha += (alphaTarget - m_pet.alpha) * (1.0f - powf(0.9f, ticksThisFrame));
      if (m_pet.alpha > alphaTarget - 0.01f) m_pet.alpha = alphaTarget;
    }

    // Advance wing flap animation — slow gentle flap (Main 5.2: helpers are graceful)
    int petNumKeys = 1;
    if (!m_pet.bmd->Actions.empty())
      petNumKeys = m_pet.bmd->Actions[0].NumAnimationKeys;
    if (petNumKeys > 1) {
      m_pet.animFrame += 14.0f * deltaTime; // Wing flap speed
      if (m_pet.animFrame >= (float)petNumKeys)
        m_pet.animFrame = std::fmod(m_pet.animFrame, (float)petNumKeys);
    }

    // Compute pet bones — blend toward bind pose to reduce leg/body shake
    // while preserving wing flap at full speed (14fps frequency, reduced amplitude)
    auto petBones = ComputeBoneMatricesInterpolated(m_pet.bmd.get(), 0,
                                                     m_pet.animFrame);
    auto bindBones = ComputeBoneMatricesInterpolated(m_pet.bmd.get(), 0, 0.0f);
    constexpr float ANIM_STRENGTH = 0.6f; // Keep 60% of animation motion
    for (size_t b = 0; b < petBones.size() && b < bindBones.size(); ++b) {
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          petBones[b][r][c] = bindBones[b][r][c] +
              (petBones[b][r][c] - bindBones[b][r][c]) * ANIM_STRENGTH;
    }

    // Re-skin pet mesh vertices
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      RetransformMeshWithBones(m_pet.bmd->Meshes[mi], petBones,
                               m_pet.meshBuffers[mi]);
    }

    // Build pet model matrix: translate + BMD rotations + facing + pitch + scale
    float petScale = isImp ? 0.55f : 0.6f;
    glm::mat4 petModel = glm::translate(glm::mat4(1.0f), m_pet.pos);
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    petModel = glm::rotate(petModel, m_pet.facing, glm::vec3(0, 0, 1));  // yaw
    petModel = glm::rotate(petModel, m_pet.pitch, glm::vec3(1, 0, 0));   // pitch toward head
    petModel = glm::scale(petModel, glm::vec3(petScale));


    // Self-illumination — brighter than surroundings for ethereal glow
    glm::vec3 petTLight = sampleTerrainLightAt(m_pet.pos);
    petTLight = glm::clamp(petTLight * 2.0f, 0.5f, 1.5f);

    auto petDrawMesh = [&](MeshBuffers &mb, float bml, uint64_t state) {
      bgfx::setTransform(glm::value_ptr(petModel));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      m_shader->setTexture(0, "s_texColor", mb.texture);
      m_shader->setVec4("u_params", glm::vec4(m_pet.alpha, bml, 0.0f, 0.0f));
      m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
      m_shader->setVec4("u_terrainLight", glm::vec4(petTLight, 0.0f));
      m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
      m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
      m_shader->setVec4("u_fogParams", glm::vec4(0.0f));
      m_shader->setVec4("u_fogColor", glm::vec4(0.0f));
      bgfx::setState(state);
      bgfx::submit(0, m_shader->program);
    };

    uint64_t normalState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                         | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                         | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    uint64_t additiveState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_DEPTH_TEST_LESS
                           | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);

    // Pass 1: Body meshes (normal blend)
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden) continue;
      bool isBlendMesh = (m_pet.bmd->Meshes[mi].Texture == m_pet.blendMesh) || mb.bright;
      if (isBlendMesh) continue;
      petDrawMesh(mb, 1.0f, normalState);
    }
    // Pass 2: Wing/glow meshes (additive for angel, normal for imp)
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden) continue;
      if (!TexValid(mb.texture)) continue; // Skip missing textures (e.g. x.jpg)
      bool isBlendMesh = (m_pet.bmd->Meshes[mi].Texture == m_pet.blendMesh) || mb.bright;
      if (!isBlendMesh) continue;
      float bml = (m_pet.itemIndex == 0) ? m_pet.alpha : 1.5f * m_pet.alpha;
      uint64_t wingState = (m_pet.itemIndex == 0) ? additiveState : normalState;
      petDrawMesh(mb, bml, wingState);
    }
  }
}

// ── Pet equip / unequip ─────────────────────────────────────────────────────

void HeroCharacter::EquipPet(uint8_t itemIndex) {
  UnequipPet(); // Clear any existing pet

  // Helper01.bmd = Guardian Angel, Helper02.bmd = Imp
  std::string bmdFile = m_dataPath + "/Player/Helper0" +
                         std::to_string(itemIndex + 1) + ".bmd";
  auto bmd = BMDParser::Parse(bmdFile);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load pet model: " << bmdFile << std::endl;
    return;
  }

  m_pet.itemIndex = itemIndex;

  // Main 5.2 GOBoid.cpp: BlendMesh=1 — mesh with Texture==1 renders additive
  // BlendMesh compares against the mesh's Texture INDEX, not the mesh array index
  m_pet.blendMesh = 1; // Standard for all helpers

  // Upload mesh buffers with per-mesh texture resolution
  // Helper BMDs are in Player/ but textures are in Item/
  AABB petAABB{};
  auto petBones = ComputeBoneMatrices(bmd.get());
  for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
    auto &mesh = bmd->Meshes[mi];

    // Try Player/ first, then Item/ fallback for this specific mesh's texture
    UploadMeshWithBones(mesh, m_dataPath + "/Player/", petBones,
                        m_pet.meshBuffers, petAABB, true);

    // Check if the just-uploaded mesh buffer got a valid texture
    auto &mb = m_pet.meshBuffers.back();
    if (!TexValid(mb.texture)) {
      // Fallback: resolve THIS mesh's texture from Item/ directory
      auto texInfo = TextureLoader::ResolveWithInfo(
          m_dataPath + "/Item/", mesh.TextureName);
      if (TexValid(texInfo.textureID)) {
        mb.texture = texInfo.textureID;
        std::cout << "[Hero] Pet mesh " << mi << ": texture '"
                  << mesh.TextureName << "' resolved from Item/" << std::endl;
      }
    }
  }

  // Compute model radius for size-based follow distances
  float petScale = (itemIndex == 1) ? 0.55f : 0.6f;
  m_pet.modelRadius = std::max(petAABB.radius() * petScale, 20.0f);

  m_pet.bmd = std::move(bmd);
  m_pet.active = true;
  m_pet.alpha = 0.0f; // Start transparent, exponential fade in
  m_pet.animFrame = 0.0f;
  m_pet.sparkTimer = 0.0f;
  // Polar orbit init
  float baseH = (itemIndex == 1) ? 90.0f : 180.0f;
  m_pet.orbitCenter = m_pos;
  m_pet.orbitAngle = glm::radians((float)(rand() % 360));
  m_pet.orbitSpeed = 0.3f;
  m_pet.orbitSpeedTarget = 0.3f;
  m_pet.orbitRadius = (itemIndex == 1) ? 100.0f : 140.0f;
  m_pet.orbitHeight = baseH;
  m_pet.orbitHeightTarget = baseH;
  m_pet.bobPhase = 0.0f;
  m_pet.decisionTimer = 1.0f;
  m_pet.sparkAccum = 0.0f;
  m_pet.facing = m_facing;
  m_pet.pitch = 0.0f;
  // Spawn at orbit position
  m_pet.pos.x = m_pos.x + cosf(m_pet.orbitAngle) * m_pet.orbitRadius;
  m_pet.pos.z = m_pos.z + sinf(m_pet.orbitAngle) * m_pet.orbitRadius;
  m_pet.pos.y = m_pos.y + baseH;

  std::cout << "[Hero] Pet companion equipped: Helper0"
            << (int)(itemIndex + 1) << ".bmd ("
            << m_pet.meshBuffers.size() << " meshes, blendMesh="
            << m_pet.blendMesh << ", radius=" << m_pet.modelRadius << ")" << std::endl;
}

void HeroCharacter::UnequipPet() {
  if (!m_pet.active && m_pet.meshBuffers.empty())
    return;
  CleanupMeshBuffers(m_pet.meshBuffers);
  m_pet.bmd.reset();
  m_pet.active = false;
  m_pet.alpha = 0.0f;
  std::cout << "[Hero] Pet companion unequipped" << std::endl;
}

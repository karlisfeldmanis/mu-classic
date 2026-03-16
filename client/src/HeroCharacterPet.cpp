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
  // Main 5.2 GOBoid.cpp: Direction-vector movement with random wandering
  if (m_pet.active && m_pet.bmd && !m_pet.meshBuffers.empty()) {
    constexpr float TICK_INTERVAL = 0.04f;       // 25fps tick rate

    if (m_pet.itemIndex == 1) {
      // ── IMP: sits on character's left shoulder (Main 5.2 bone-attached) ──
      // Main 5.2 GOBoid.cpp: Imp uses BoneTransform[20] for height positioning.
      // Imp sits on the character's shoulder, not flying around like the Angel.
      constexpr float SHOULDER_OFFSET_LEFT = 12.0f;  // Perpendicular to facing (left)
      constexpr float SHOULDER_OFFSET_BACK = -3.0f;  // Slightly behind
      constexpr int   SHOULDER_BONE = 20;             // Main 5.2 BoneTransform[20]
      constexpr float SHOULDER_HEIGHT_FALLBACK = 105.0f;
      constexpr float SHOULDER_HEIGHT_EXTRA = 50.0f;  // Main 5.2: + 50.0f above bone

      // Get shoulder height from character's cached bone matrices (BMD-local Z → GL Y)
      float shoulderHeight = SHOULDER_HEIGHT_FALLBACK;
      if (SHOULDER_BONE < (int)m_cachedBones.size()) {
        // m_cachedBones are BMD-local (MU Z-up). [2][3] = MU Z = height.
        shoulderHeight = m_cachedBones[SHOULDER_BONE][2][3] + SHOULDER_HEIGHT_EXTRA;
      }

      // Position on left shoulder relative to character facing
      float perpAngle = m_facing + (float)M_PI * 0.5f; // 90° left
      float targetX = m_pos.x + cosf(perpAngle) * SHOULDER_OFFSET_LEFT
                               + cosf(m_facing) * SHOULDER_OFFSET_BACK;
      float targetZ = m_pos.z + sinf(perpAngle) * SHOULDER_OFFSET_LEFT
                               + sinf(m_facing) * SHOULDER_OFFSET_BACK;
      float targetY = m_pos.y + shoulderHeight;

      // Smooth lerp to target position (snappy follow, no lag)
      float lerpRate = glm::clamp(deltaTime * 12.0f, 0.0f, 1.0f);
      m_pet.pos.x += (targetX - m_pet.pos.x) * lerpRate;
      m_pet.pos.z += (targetZ - m_pet.pos.z) * lerpRate;
      m_pet.pos.y += (targetY - m_pet.pos.y) * lerpRate;

      // Face same direction as character
      float facingDiff = m_facing - m_pet.facing;
      while (facingDiff > (float)M_PI) facingDiff -= 2.0f * (float)M_PI;
      while (facingDiff < -(float)M_PI) facingDiff += 2.0f * (float)M_PI;
      m_pet.facing += facingDiff * glm::clamp(deltaTime * 8.0f, 0.0f, 1.0f);
      m_pet.pitch = 0.0f; // Level, not looking down

      // Sparkle — imp embers
      m_pet.tickAccum += deltaTime;
      while (m_pet.tickAccum >= TICK_INTERVAL) {
        m_pet.tickAccum -= TICK_INTERVAL;
        if (m_vfxManager && rand() % 6 == 0) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 12 - 6), (float)(rand() % 12 - 6), (float)(rand() % 12 - 6));
          m_vfxManager->SpawnBurst(ParticleType::IMP_SPARKLE, sparkPos, 1);
        }
      }
    } else {
      // ── GUARDIAN ANGEL: flying orbit around owner (Main 5.2 GOBoid.cpp) ──
      constexpr float FLY_RANGE = 100.0f;         // Max wander distance from owner
      constexpr float MAX_DIST = 180.0f;           // Hard leash — teleport back if exceeded
      constexpr float MAX_TURN_PER_TICK = 20.0f;   // Degrees per tick
      constexpr float MIN_HEIGHT = 100.0f;         // Above owner
      constexpr float MAX_HEIGHT = 200.0f;         // Above owner

    // ── Teleport pet to owner if too far (login, teleport, initial spawn at origin) ──
    float petDistX = m_pet.pos.x - m_pos.x;
    float petDistZ = m_pet.pos.z - m_pos.z;
    float petDistSq = petDistX * petDistX + petDistZ * petDistZ;
    if (petDistSq > MAX_DIST * MAX_DIST) {
      m_pet.pos = m_pos + glm::vec3(
          (float)(rand() % 100 - 50), MIN_HEIGHT + (float)(rand() % 50),
          (float)(rand() % 100 - 50));
      m_pet.lastOwnerPos = m_pos;
      m_pet.dirAngle = glm::radians((float)(rand() % 360));
      m_pet.speed = 0.5f;
      m_pet.heightVel = 0.0f;
      m_pet.tickAccum = 0.0f;
      m_pet.followDelay = 0.0f;
      m_pet.wasOwnerMoving = false;
    }

    // ── Detect if owner is moving ──
    glm::vec3 ownerDelta = m_pos - m_pet.lastOwnerPos;
    float ownerMoveSq = ownerDelta.x * ownerDelta.x + ownerDelta.z * ownerDelta.z;
    bool ownerMoving = ownerMoveSq > 0.5f; // Threshold to avoid float noise
    m_pet.lastOwnerPos = m_pos;

    // Accumulate time for tick-based logic
    m_pet.tickAccum += deltaTime;

    // Follow ramp: when character just started moving, angel lerp starts slow and accelerates
    if (ownerMoving && !m_pet.wasOwnerMoving) {
      m_pet.followDelay = 0.0f; // Reset ramp timer on movement start
    }
    // When character just stopped, seed idle wander from current position
    if (!ownerMoving && m_pet.wasOwnerMoving) {
      float edx = m_pet.pos.x - m_pos.x;
      float edz = m_pet.pos.z - m_pos.z;
      m_pet.dirAngle = atan2f(edz, edx); // Continue moving in current direction
      m_pet.speed = 0.3f;                // Slow drift, not a snap
      m_pet.heightVel *= 0.5f;           // Dampen vertical momentum
      m_pet.tickAccum = 0.0f;            // Prevent burst of accumulated ticks
    }
    if (ownerMoving) {
      m_pet.followDelay += deltaTime; // Ramps up from 0
    } else {
      m_pet.followDelay = 0.0f;
    }
    m_pet.wasOwnerMoving = ownerMoving;

    if (ownerMoving) {
      // ── MOVING: angel trails behind character, facing same direction ──
      constexpr float TRAIL_DIST = 60.0f;
      constexpr float RAMP_DURATION = 0.5f; // Time to reach full follow speed
      float behindX = m_pos.x - cosf(m_facing) * TRAIL_DIST;
      float behindZ = m_pos.z - sinf(m_facing) * TRAIL_DIST;

      // Lateral weave: dirAngle is repurposed as smooth lateral offset during MOVING
      // Perpendicular to character facing — adds organic sway to follow path
      float perpX = -sinf(m_facing);
      float perpZ =  cosf(m_facing);
      behindX += perpX * m_pet.dirAngle;
      behindZ += perpZ * m_pet.dirAngle;

      // Lerp rate ramps from ~0.5 to 5.0 over RAMP_DURATION seconds
      float ramp = glm::clamp(m_pet.followDelay / RAMP_DURATION, 0.0f, 1.0f);
      float speed = 0.5f + ramp * 4.5f; // 0.5 → 5.0
      float lerpRate = glm::clamp(deltaTime * speed, 0.0f, 1.0f);
      m_pet.pos.x += (behindX - m_pet.pos.x) * lerpRate;
      m_pet.pos.z += (behindZ - m_pet.pos.z) * lerpRate;

      // Smoothly lerp facing toward character's direction (also ramps)
      float targetFacing = m_facing;
      float facingDiff = targetFacing - m_pet.facing;
      while (facingDiff > M_PI) facingDiff -= 2.0f * M_PI;
      while (facingDiff < -M_PI) facingDiff += 2.0f * M_PI;
      float facingSpeed = 1.0f + ramp * 3.0f; // 1.0 → 4.0
      m_pet.facing += facingDiff * glm::clamp(deltaTime * facingSpeed, 0.0f, 1.0f);
      constexpr float MOVE_HEAD_HEIGHT = 120.0f;
      float mdy = (m_pos.y + MOVE_HEAD_HEIGHT) - m_pet.pos.y;
      float mdx = m_pet.pos.x - m_pos.x;
      float mdz = m_pet.pos.z - m_pos.z;
      float mhDist = sqrtf(mdx * mdx + mdz * mdz);
      m_pet.pitch = atan2f(mdy, std::max(mhDist, 1.0f));

      // Vertical + lateral: smooth toward target height with gentle bobbing
      while (m_pet.tickAccum >= TICK_INTERVAL) {
        m_pet.tickAccum -= TICK_INTERVAL;
        m_pet.pos.y += m_pet.heightVel;
        m_pet.heightVel += ((float)(rand() % 10 - 5)) * 0.1f; // Gentle random nudge
        if (m_pet.pos.y < m_pos.y + MIN_HEIGHT) m_pet.heightVel += 1.0f;
        if (m_pet.pos.y > m_pos.y + MAX_HEIGHT) m_pet.heightVel -= 1.0f;
        m_pet.heightVel *= 0.92f;

        // Lateral weave: random drift ±25 units perpendicular to path
        m_pet.dirAngle += ((float)(rand() % 10 - 5)) * 0.4f;
        m_pet.dirAngle *= 0.93f; // Decay toward center

        // Sparkle — angel gets white dots
        if (m_vfxManager && rand() % 4 == 0) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 16 - 8), (float)(rand() % 16 - 8), (float)(rand() % 16 - 8));
          m_vfxManager->SpawnBurst(ParticleType::PET_SPARKLE, sparkPos, 1);
        }
      }
    } else {
      // ── IDLE: wander smoothly around owner, always face toward character ──
      while (m_pet.tickAccum >= TICK_INTERVAL) {
        m_pet.tickAccum -= TICK_INTERVAL;

        // Smooth turn toward target direction (max 8° per tick)
        constexpr float MAX_TURN = glm::radians(8.0f);
        float angleDiff = m_pet.dirAngle - atan2f(
            sinf(m_pet.dirAngle) * m_pet.speed,
            cosf(m_pet.dirAngle) * m_pet.speed);
        // dirAngle is the target — smoothly steer current movement
        float curMoveAngle = atan2f(sinf(m_pet.dirAngle), cosf(m_pet.dirAngle));

        // Apply smooth movement
        m_pet.pos.x += cosf(m_pet.dirAngle) * m_pet.speed;
        m_pet.pos.z += sinf(m_pet.dirAngle) * m_pet.speed;
        m_pet.pos.y += m_pet.heightVel;

        // Body exclusion: gently push pet away if too close (soft spring, no snap)
        constexpr float MIN_RADIUS = 40.0f;
        float edx = m_pet.pos.x - m_pos.x;
        float edz = m_pet.pos.z - m_pos.z;
        float eDist = sqrtf(edx * edx + edz * edz);
        if (eDist < MIN_RADIUS && eDist > 0.01f) {
          float pushStr = (MIN_RADIUS - eDist) * 0.15f; // Soft push
          m_pet.pos.x += (edx / eDist) * pushStr;
          m_pet.pos.z += (edz / eDist) * pushStr;
        } else if (eDist < 0.01f) {
          m_pet.pos.x += cosf(m_pet.dirAngle) * 0.5f;
          m_pet.pos.z += sinf(m_pet.dirAngle) * 0.5f;
        }

        // Gentle random direction drift: ~1.5% chance per tick, small angle change
        if (rand() % 64 == 0) {
          // New target direction: small random offset from current (±60°)
          float angleOffset = glm::radians((float)(rand() % 120 - 60));
          m_pet.dirAngle += angleOffset;
          m_pet.speed = 0.5f + (float)(rand() % 15) * 0.1f; // 0.5-2.0 units/tick
          m_pet.heightVel += ((float)(rand() % 20 - 10)) * 0.05f; // Gentle nudge
        }

        // Soft wander radius — pull back gradually, never snap
        edx = m_pet.pos.x - m_pos.x;
        edz = m_pet.pos.z - m_pos.z;
        float wanderDist = sqrtf(edx * edx + edz * edz);
        if (wanderDist > FLY_RANGE && wanderDist > 0.01f) {
          float overshoot = wanderDist - FLY_RANGE;
          float pullStr = std::min(overshoot * 0.1f, 2.0f); // Gradual pull
          m_pet.pos.x -= (edx / wanderDist) * pullStr;
          m_pet.pos.z -= (edz / wanderDist) * pullStr;
          // Steer direction back toward owner
          m_pet.dirAngle = atan2f(-edz, -edx) + glm::radians((float)(rand() % 60 - 30));
          m_pet.speed = std::min(m_pet.speed, 1.0f);
        }

        // Height constraints — gentle spring
        float targetY = m_pos.y + (MIN_HEIGHT + MAX_HEIGHT) * 0.5f; // Center of range
        float heightErr = targetY - m_pet.pos.y;
        m_pet.heightVel += heightErr * 0.02f; // Soft spring
        if (m_pet.pos.y < m_pos.y + MIN_HEIGHT) m_pet.heightVel += 0.5f;
        if (m_pet.pos.y > m_pos.y + MAX_HEIGHT) m_pet.heightVel -= 0.5f;
        m_pet.heightVel *= 0.92f; // Strong damping for smooth bobbing

        // Sparkle — angel gets white dots
        if (m_vfxManager && rand() % 4 == 0) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 16 - 8), (float)(rand() % 16 - 8), (float)(rand() % 16 - 8));
          m_vfxManager->SpawnBurst(ParticleType::PET_SPARKLE, sparkPos, 1);
        }
      }
      // Update facing AFTER movement so angel always looks at character head
      constexpr float HEAD_HEIGHT = 120.0f; // Approximate character head height
      float dx = m_pos.x - m_pet.pos.x;
      float dy = (m_pos.y + HEAD_HEIGHT) - m_pet.pos.y;
      float dz = m_pos.z - m_pet.pos.z;
      float hDist = sqrtf(dx * dx + dz * dz);
      float rawAngle = atan2f(dz, dx);
      float targetFacing = rawAngle + glm::half_pi<float>(); // +90° offset for Helper BMD front
      // Smoothly lerp facing toward target
      float facingDiff = targetFacing - m_pet.facing;
      while (facingDiff > M_PI) facingDiff -= 2.0f * M_PI;
      while (facingDiff < -M_PI) facingDiff += 2.0f * M_PI;
      m_pet.facing += facingDiff * glm::clamp(deltaTime * 3.0f, 0.0f, 1.0f);
      m_pet.pitch = atan2f(dy, std::max(hDist, 1.0f));
    }
    } // end Guardian Angel branch

    // ── Alpha: exponential smoothing (Main 5.2: Alpha += (AlphaTarget - Alpha) * 0.1f per tick) ──
    // Adapted for delta-time: alpha approaches 1.0 with ~10% convergence per tick
    if (m_pet.alpha < 0.99f) {
      float ticksThisFrame = deltaTime / TICK_INTERVAL;
      m_pet.alpha += (1.0f - m_pet.alpha) * (1.0f - powf(0.9f, ticksThisFrame));
      if (m_pet.alpha > 0.99f) m_pet.alpha = 1.0f;
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

    // Build pet model matrix: translate to pet.pos + BMD rotations + scale
    // Imp on shoulder is slightly smaller (0.55) than flying Angel (0.6)
    float petScale = (m_pet.itemIndex == 1) ? 0.55f : 0.6f;
    glm::mat4 petModel = glm::translate(glm::mat4(1.0f), m_pet.pos);
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    petModel = glm::rotate(petModel, m_pet.facing, glm::vec3(0, 0, 1));
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
                           | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

    // Pass 1: Body meshes (normal blend)
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden) continue;
      bool isBlendMesh = (m_pet.bmd->Meshes[mi].Texture == m_pet.blendMesh) || mb.bright;
      if (isBlendMesh) continue;
      petDrawMesh(mb, 1.0f, normalState);
    }
    // Pass 2: Wing meshes
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden) continue;
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

  m_pet.bmd = std::move(bmd);
  m_pet.active = true;
  m_pet.alpha = 0.0f; // Start transparent, exponential fade in
  m_pet.animFrame = 0.0f;
  m_pet.sparkTimer = 0.0f;
  // Direction-vector movement init (Main 5.2 GOBoid.cpp)
  m_pet.tickAccum = 0.0f;
  m_pet.lastOwnerPos = m_pos;
  m_pet.facing = m_facing;
  if (itemIndex == 1) {
    // Imp: spawn directly on character's left shoulder
    float perpAngle = m_facing + (float)M_PI * 0.5f;
    m_pet.pos = m_pos + glm::vec3(cosf(perpAngle) * 20.0f, 105.0f,
                                   sinf(perpAngle) * 20.0f);
    m_pet.dirAngle = 0.0f;
    m_pet.speed = 0.0f;
    m_pet.heightVel = 0.0f;
  } else {
    // Angel: spawn at random offset ±256 XY, +128-256 Z from owner
    m_pet.dirAngle = glm::radians((float)(rand() % 360));
    m_pet.speed = (16.0f + (float)(rand() % 64)) * 0.1f;
    m_pet.heightVel = ((float)(rand() % 64 - 32)) * 0.1f;
    m_pet.pos = m_pos + glm::vec3(
        (float)(rand() % 200 - 100), 128.0f + (float)(rand() % 128),
        (float)(rand() % 200 - 100));
  }

  std::cout << "[Hero] Pet companion equipped: Helper0"
            << (int)(itemIndex + 1) << ".bmd ("
            << m_pet.meshBuffers.size() << " meshes, blendMesh="
            << m_pet.blendMesh << ")" << std::endl;
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

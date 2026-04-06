#include "HeroCharacter.hpp"
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ─── Mount Rendering ─────────────────────────────────────────────────────────

void HeroCharacter::renderMountModel(const glm::mat4 &view, const glm::mat4 &proj,
                                      const glm::vec3 &camPos, float deltaTime,
                                      const glm::vec3 &tLight) {
  // ── Mount rendering (Uniria / Dinorant) ──
  // Main 5.2 GOBoid.cpp: mount renders at player position with Z offset.
  // Mount and player have independent animation frames (both ~0.34f/tick).
  if (m_mount.active && m_mount.bmd && !m_mount.meshBuffers.empty()) {
    // Mount always visible when active (riding allowed in safe zone)
    m_mount.alpha = 1.0f;

    // Mount animation: action mapping based on owner's state.
    // Rider01 (Uniria): idle=0, walk=2, attack=3  (4 actions)
    // Rider02 (Dinorant): idle=0, walk=2, attack=4 (8 actions, ground maps)
    bool isRideAttack = (m_action >= ACTION_ATTACK_RIDE_SWORD &&
                         m_action <= ACTION_ATTACK_RIDE_CROSSBOW);
    int mountAction;
    if (isRideAttack) {
      mountAction = (m_mount.itemIndex == 3) ? 4 : 3;
    } else {
      mountAction = m_moving ? 2 : 0;
    }
    if (mountAction >= (int)m_mount.bmd->Actions.size())
      mountAction = 0;

    // Detect mount action transitions for blending (walk→idle only)
    if (mountAction != m_mount.action) {
      bool walkToIdle = (m_mount.action == 2 && mountAction == 0);
      if (walkToIdle) {
        m_mount.priorAction = m_mount.action;
        m_mount.priorAnimFrame = m_mount.animFrame;
        m_mount.isBlending = true;
        m_mount.blendAlpha = 0.0f;
      } else {
        m_mount.isBlending = false;
        m_mount.blendAlpha = 1.0f;
      }
      m_mount.action = mountAction;
    }

    // Advance blend
    if (m_mount.isBlending) {
      m_mount.blendAlpha += deltaTime / BLEND_DURATION;
      if (m_mount.blendAlpha >= 1.0f) {
        m_mount.blendAlpha = 1.0f;
        m_mount.isBlending = false;
      }
    }

    // Sync mount animation frame to player's ride animation frame.
    // Apply stride scale to match visual stride with actual ground speed.
    // >1.0 = faster legs (mount stride < ground speed), <1.0 = slower legs.
    // Sync mount animation frame to player frame (1:1 ratio, no stride scale)
    // Player ride walk and mount walk have matching 7-key patterns (CLAUDE.md)
    int mountNumKeys = m_mount.bmd->Actions[mountAction].NumAnimationKeys;
    bool mountLockPos = m_mount.bmd->Actions[mountAction].LockPositions;
    int mountWrapKeys = mountLockPos ? (mountNumKeys - 1) : mountNumKeys;
    if (mountWrapKeys > 0) {
      int playerNumKeys = m_skeleton->Actions[m_action].NumAnimationKeys;
      bool playerLockPos = m_skeleton->Actions[m_action].LockPositions;
      int playerWrapKeys = playerLockPos ? (playerNumKeys - 1) : playerNumKeys;
      if (playerWrapKeys > 0) {
        float progress = m_animFrame / (float)playerWrapKeys;
        m_mount.animFrame = std::fmod(progress * (float)mountWrapKeys, (float)mountWrapKeys);
      } else {
        m_mount.animFrame = 0.0f;
      }
    }

    // Compute mount bones — blend walk→idle, otherwise interpolate
    std::vector<BoneWorldMatrix> mountBones;
    if (m_mount.isBlending && m_mount.priorAction >= 0) {
      mountBones = ComputeBoneMatricesBlended(m_mount.bmd.get(),
                     m_mount.priorAction, m_mount.priorAnimFrame,
                     mountAction, m_mount.animFrame, m_mount.blendAlpha);
    } else {
      mountBones = ComputeBoneMatricesInterpolated(m_mount.bmd.get(), mountAction,
                                                     m_mount.animFrame);
    }
    // Remove HORIZONTAL root motion only — vertical bounce preserved in bones.
    // Use m_mount.rootBone (0 for Rider01/Uniria, 1 for Rider02/Dinorant)
    // because Rider02's bone 0 is a static Box01 helper, not the animated root.
    int rb = m_mount.rootBone;
    if (!mountBones.empty() && rb < (int)mountBones.size()) {
      glm::vec3 idlePos, curPos;
      glm::vec4 dummyQ;
      GetInterpolatedBoneData(m_mount.bmd.get(), 0, 0.0f, rb, idlePos, dummyQ);
      // During blend: interpolate root position from both actions to match blended bones
      if (m_mount.isBlending && m_mount.priorAction >= 0) {
        glm::vec3 p1, p2;
        glm::vec4 q1, q2;
        GetInterpolatedBoneData(m_mount.bmd.get(), m_mount.priorAction,
                                m_mount.priorAnimFrame, rb, p1, q1);
        GetInterpolatedBoneData(m_mount.bmd.get(), mountAction,
                                m_mount.animFrame, rb, p2, q2);
        curPos = glm::mix(p1, p2, m_mount.blendAlpha);
      } else {
        GetInterpolatedBoneData(m_mount.bmd.get(), mountAction, m_mount.animFrame,
                                rb, curPos, dummyQ);
      }
      float dx = curPos.x - idlePos.x;
      float dy = curPos.y - idlePos.y;
      float dz = curPos.z - idlePos.z;
      // Cache Z bounce — applied as shared world Y offset to both player and mount
      m_mount.zBounce = dz;
      for (auto &bone : mountBones) {
        bone[0][3] -= dx;
        bone[1][3] -= dy;
        bone[2][3] -= dz; // Strip Z bounce from bones (applied via translate instead)
      }
    }
    // Cache bones for shadow rendering
    m_mount.cachedBones = mountBones;
    for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                     mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
      RetransformMeshWithBones(m_mount.bmd->Meshes[mi], mountBones,
                               m_mount.meshBuffers[mi]);
    }

    // Skip GPU rendering when fully faded (safe zone), but animation stays updated
    if (m_mount.alpha > 0.0f) {
      // Main 5.2 GOBoid.cpp: mount at player pos, Dinorant offset -30 Z (ground level)
      // Player is elevated +30 (set above in renderPos), mount stays at terrain.
      glm::vec3 mountPos = m_pos;
      mountPos.y += m_mount.zBounce; // Shared bounce from mount animation
      glm::mat4 mountModel = glm::translate(glm::mat4(1.0f), mountPos);
      mountModel = glm::rotate(mountModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      mountModel = glm::rotate(mountModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      mountModel = glm::rotate(mountModel, m_facing, glm::vec3(0, 0, 1));
      // Main 5.2 RenderMount: overrides creation scale to 1.0 for in-game rendering
      // (0.9 during CreateMountSub, but 1.0 in RenderMount for all non-Fenrir mounts)
      mountModel = glm::scale(mountModel, glm::vec3(1.0f));


      // BGFX: per-submit uniforms and state
      // IMPORTANT: BGFX uniforms persist across submit calls within a frame.
      // Must set ALL uniforms here — any missing uniform inherits stale state
      // from the hero's glow/tint passes, causing invisible mount when items
      // are upgraded (+5/+7+).
      auto mountDrawMesh = [&](MeshBuffers &mb, float bml, uint64_t state) {
        bgfx::setTransform(glm::value_ptr(mountModel));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        m_shader->setTexture(0, "s_texColor", mb.texture);
        m_shader->setVec4("u_params", glm::vec4(m_mount.alpha, bml, 0.0f, 0.0f));
        m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
        m_shader->setVec4("u_viewPos", glm::vec4(camPos, 0.0f));
        m_shader->setVec4("u_lightPos", glm::vec4(camPos + glm::vec3(0, 500, 0), 0.0f));
        m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
        m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
        m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
        m_shader->setVec4("u_fogParams", glm::vec4(0.0f));
        m_shader->setVec4("u_fogColor", glm::vec4(0.0f));
        m_shader->setVec4("u_shadowParams", glm::vec4(0.0f));
        m_shader->uploadPointLights(0, m_pointLights.data());
        bgfx::setState(state);
        bgfx::submit(0, m_shader->program);
      };

      uint64_t normalState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                           | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
      uint64_t additiveState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                             | BGFX_STATE_DEPTH_TEST_LESS
                             | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

      // Pass 1: Normal meshes
      for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                       mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
        auto &mb = m_mount.meshBuffers[mi];
        if (mb.indexCount == 0 || mb.hidden) continue;
        int bmdTex = m_mount.bmd->Meshes[mi].Texture;
        if (bmdTex == m_mount.blendMesh || mb.bright) continue;
        mountDrawMesh(mb, 1.0f, normalState);
      }
      // Pass 2: Additive blend meshes
      if (m_mount.blendMesh >= 0) {
        for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                         mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
          auto &mb = m_mount.meshBuffers[mi];
          if (mb.indexCount == 0 || mb.hidden) continue;
          int bmdTex = m_mount.bmd->Meshes[mi].Texture;
          if (bmdTex != m_mount.blendMesh && !mb.bright) continue;
          mountDrawMesh(mb, 1.5f, additiveState);
        }
      }
    }

    // ── Mount VFX: dust particles while running ──
    // Main 5.2: BITMAP_SMOKE spawned at mount base with random offset while moving
    if (m_moving && !m_inSafeZone && m_vfxManager) {
      m_mount.dustTimer += deltaTime;
      constexpr float DUST_INTERVAL = 0.08f; // ~50% chance per tick at 25fps
      while (m_mount.dustTimer >= DUST_INTERVAL) {
        m_mount.dustTimer -= DUST_INTERVAL;
        // Offset ±32/±32 horizontal, ground level (Y=0)
        glm::vec3 dustPos = m_pos;
        dustPos.x += (float)(rand() % 64 - 32);
        dustPos.z += (float)(rand() % 64 - 32);
        dustPos.y += (float)(rand() % 16);
        m_vfxManager->SpawnBurst(ParticleType::MOUNT_DUST, dustPos, 1);
      }
    } else {
      m_mount.dustTimer = 0.0f;
    }

    // ── Mount sound: hoofbeat steps synced to mount walk animation ──
    // Similar to player footsteps (m_foot), triggered at animation key frames
    if (m_moving && !m_inSafeZone && mountAction == 2) {
      float f = m_mount.animFrame;
      if (f >= 1.5f && !m_mount.hoofFoot[0]) {
        m_mount.hoofFoot[0] = true;
        int snd = SOUND_MOUNT_STEP1 + (m_mount.hoofIndex % 3);
        m_mount.hoofIndex++;
        SoundManager::PlayPitched(snd, 0.85f, 1.15f);
      }
      if (f >= 4.5f && !m_mount.hoofFoot[1]) {
        m_mount.hoofFoot[1] = true;
        int snd = SOUND_MOUNT_STEP1 + (m_mount.hoofIndex % 3);
        m_mount.hoofIndex++;
        SoundManager::PlayPitched(snd, 0.85f, 1.15f);
      }
      if (f < 1.0f) {
        m_mount.hoofFoot[0] = false;
        m_mount.hoofFoot[1] = false;
      }
    } else {
      m_mount.hoofFoot[0] = false;
      m_mount.hoofFoot[1] = false;
    }
  }
}

// ─── Mount Equip / Unequip ───────────────────────────────────────────────────

void HeroCharacter::EquipMount(uint8_t itemIndex) {
  // Pet stays active — mount is from quickslot, pet is from equipment slot 8.
  // They don't conflict: server keeps Imp buff while client shows both models.
  UnequipMount();  // Clear previous mount

  // Main 5.2 ZzzOpenData.cpp: ride models are in Data/Skill/
  //   AccessModel(MODEL_UNICON,  "Data\\Skill\\", "Rider", 1)  → Rider01.bmd (Uniria)
  //   AccessModel(MODEL_PEGASUS, "Data\\Skill\\", "Rider", 2)  → Rider02.bmd (Dinorant)
  int riderIndex = (itemIndex == 2) ? 1 : 2;
  std::string bmdFile = m_dataPath + "/Skill/Rider0" +
                         std::to_string(riderIndex) + ".bmd";
  auto bmd = BMDParser::Parse(bmdFile);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load mount model: " << bmdFile << std::endl;
    return;
  }

  m_mount.itemIndex = itemIndex;
  m_mount.blendMesh = -1;

  // Rider01.bmd (Uniria): bone 0 = Bip01 (animated root)
  // Rider02.bmd (Dinorant): bone 0 = Box01 (STATIC helper), bone 1 = Bip01 (animated root)
  // Find first bone named "Bip01" as the skeleton root for root motion removal.
  m_mount.rootBone = 0;
  for (int bi = 0; bi < (int)bmd->Bones.size(); ++bi) {
    if (std::strcmp(bmd->Bones[bi].Name, "Bip01") == 0) {
      m_mount.rootBone = bi;
      break;
    }
  }

  // Texture directory: Uniria textures in Item/, Dinorant textures in Skill/
  // Main 5.2: OpenTexture(MODEL_UNICON, "Item\\"), OpenTexture(MODEL_PEGASUS, "Skill\\")
  std::string texDir = m_dataPath + ((itemIndex == 2) ? "/Item/" : "/Skill/");

  AABB mountAABB{};
  auto mountBones = ComputeBoneMatrices(bmd.get());
  for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
    auto &mesh = bmd->Meshes[mi];
    UploadMeshWithBones(mesh, texDir, mountBones,
                        m_mount.meshBuffers, mountAABB, true);

    auto &mb = m_mount.meshBuffers.back();
    if (!TexValid(mb.texture)) {
      // Fallback: try the other directory
      std::string fallbackDir = m_dataPath +
          ((itemIndex == 2) ? "/Skill/" : "/Item/");
      auto texInfo = TextureLoader::ResolveWithInfo(fallbackDir, mesh.TextureName);
      if (TexValid(texInfo.textureID)) {
        mb.texture = texInfo.textureID;
      }
    }
  }

  // Create shadow meshes for mount (same pattern as body parts)
  static auto createShadowMeshesMount = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_mount.shadowMeshes = createShadowMeshesMount(bmd.get());

  m_mount.bmd = std::move(bmd);
  m_mount.active = true;
  m_mount.alpha = 1.0f;
  m_mount.animFrame = 0.0f;
  m_mount.zBounce = 0.0f;
  m_mountEquippedIndex = itemIndex;

  // Mount activation sound — hoofbeat burst (mount arrival)
  SoundManager::Play(SOUND_MOUNT_STEP1);
  SoundManager::Play(SOUND_MOUNT_STEP3);

  // Switch player to riding animation (allowed everywhere including safe zone)
  if (m_moving) {
    SetAction(weaponWalkAction());
  } else {
    SetAction(weaponIdleAction());
  }

  std::cout << "[Hero] Mount equipped: Rider0" << riderIndex << ".bmd ("
            << m_mount.meshBuffers.size() << " meshes, "
            << m_mount.bmd->Bones.size() << " bones, "
            << m_mount.bmd->Actions.size() << " actions)" << std::endl;
}

void HeroCharacter::UnequipMount() {
  if (!m_mount.active && m_mount.meshBuffers.empty())
    return;
  // Mount deactivation sound
  SoundManager::Play(SOUND_INTERFACE01);
  CleanupMeshBuffers(m_mount.meshBuffers);
  for (auto &sm : m_mount.shadowMeshes) {
    if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
  }
  m_mount.shadowMeshes.clear();
  m_mount.cachedBones.clear();
  m_mount.bmd.reset();
  m_mount.active = false;
  m_mount.alpha = 0.0f;
  m_mount.zBounce = 0.0f;

  // Switch back to normal animation (safe zone uses unarmed poses since weapon is on back)
  if (m_moving) {
    SetAction((!m_inSafeZone && m_weaponBmd) ? weaponWalkAction() : ACTION_WALK_MALE);
  } else {
    SetAction((!m_inSafeZone && m_weaponBmd) ? weaponIdleAction() : ACTION_STOP_MALE);
  }
  std::cout << "[Hero] Mount unequipped" << std::endl;
}

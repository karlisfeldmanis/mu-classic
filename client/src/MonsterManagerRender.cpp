#include "MonsterManager.hpp"
#include "TerrainUtils.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

void MonsterManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                            const glm::vec3 &camPos, float deltaTime) {
  if (!m_shader || m_monsters.empty())
    return;

  // Extract frustum planes from VP matrix for culling
  glm::mat4 vp = proj * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                          vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                          vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                          vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                          vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                          vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                          vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

  // BGFX: fog params for per-submit uniforms
  glm::vec4 fogParams, fogColor;
  if (m_mapId == 1) {
    fogColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    fogParams = glm::vec4(800.0f, 2500.0f, 1.0f, 0.0f);
  } else {
    fogColor = glm::vec4(0.117f, 0.078f, 0.039f, 0.0f);
    fogParams = glm::vec4(1500.0f, 3500.0f, 1.0f, 0.0f);
  }

  // Point lights: proper upload (matching hero/NPC). The static terrain
  // lightmap (from JPEG) does NOT include fire — fire light comes only from
  // per-pixel point lights. The depth prepass prevents fire world object
  // meshes from rendering on top of monsters.
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);

  // Helper: set all per-submit uniforms and draw a mesh buffer
  auto monDrawMesh = [&](const glm::mat4 &modelMat, MeshBuffers &mb,
                         float objAlpha, float bml, const glm::vec3 &tLight,
                         uint64_t state, const glm::vec3 &baseTint = glm::vec3(1.0f),
                         const glm::vec4 &texCoordOff = glm::vec4(0.0f)) {
    bgfx::setTransform(glm::value_ptr(modelMat));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    m_shader->setTexture(0, "s_texColor", mb.texture);
    m_shader->setVec4("u_params", glm::vec4(objAlpha, bml, 0.0f, 0.0f));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(baseTint, 0.0f));
    m_shader->setVec4("u_fogParams", fogParams);
    m_shader->setVec4("u_fogColor", fogColor);
    m_shader->setVec4("u_texCoordOffset", texCoordOff);
    m_shader->uploadPointLights(plCount, m_pointLights.data());
    // Shadow map
    float shadowEnabled = bgfx::isValid(m_shadowMapTex) ? 1.0f : 0.0f;
    m_shader->setVec4("u_shadowParams", glm::vec4(shadowEnabled, 0.0f, 0.0f, 0.0f));
    if (shadowEnabled > 0.5f) {
      m_shader->setMat4("u_lightMtx", m_lightMtx);
      m_shader->setTexture(1, "s_shadowMap", m_shadowMapTex);
    }
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  };

  // Standard depth test (no prepass). Fire glow meshes are replaced by VFX
  // billboard particles, so normal depth sorting handles everything.
  uint64_t normalState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint64_t additiveState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                          | BGFX_STATE_DEPTH_TEST_LESS
                          | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  uint64_t noneBlendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

  for (auto &mon : m_monsters) {
    // Skip fully faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Frustum culling: skip entities fully outside view frustum
    {
      float cullRadius = mdl.collisionHeight * mon.scale * 2.0f;
      glm::vec3 center = mon.position + glm::vec3(0, cullRadius * 0.4f, 0);
      bool outside = false;
      for (int p = 0; p < 6; ++p) {
        if (frustum[p].x * center.x + frustum[p].y * center.y +
                frustum[p].z * center.z + frustum[p].w <
            -cullRadius) {
          outside = true;
          break;
        }
      }
      if (outside)
        continue;
    }

    // Advance animation (use animBmd + actionMap for skeleton types)
    BMDData *animBmd = mdl.getAnimBmd();
    int mappedAction = (mon.action >= 0 && mon.action < 7)
                           ? mdl.actionMap[mon.action]
                           : mon.action;
    int numKeys = 1;
    bool lockPos = false;
    if (mappedAction >= 0 && mappedAction < (int)animBmd->Actions.size()) {
      numKeys = animBmd->Actions[mappedAction].NumAnimationKeys;
      lockPos = animBmd->Actions[mappedAction].LockPositions;
    }
    if (numKeys > 1) {
      float animSpeed = getAnimSpeed(mon.monsterType, mon.action);

      // Scale walk animation speed to match actual movement speed.
      // refMoveSpeed = the speed the walk animation was designed for.
      // MU Online MoveSpeed=400 means 400ms per grid cell = 100/0.4 = 250 u/s.
      float refMoveSpeed = 250.0f;
      // Skeleton types 14-16 use Player.bmd walk animation (stride at player speed)
      if (mon.monsterType >= 14 && mon.monsterType <= 16)
        refMoveSpeed = 334.0f;

      if (mon.action == ACTION_WALK) {
        bool isOwnSummon = (mon.serverIndex == m_ownSummonIndex &&
                            m_ownSummonIndex != 0);
        if (isOwnSummon && (mon.state == MonsterState::WALKING ||
                            mon.state == MonsterState::IDLE)) {
          // Scale walk anim to actual movement speed (spring-damper velocity)
          float actualSpeed = glm::length(glm::vec2(m_summonVelocity.x, m_summonVelocity.z));
          // For server-driven WALKING, use chase speed as baseline
          if (mon.state == MonsterState::WALKING && actualSpeed < 10.0f)
            actualSpeed = CHASE_SPEED;
          if (actualSpeed > 10.0f)
            animSpeed *= actualSpeed / refMoveSpeed;
          else
            animSpeed *= 0.5f; // Gentle idle sway when nearly stopped
        } else if (mon.state == MonsterState::WALKING) {
          animSpeed *= WANDER_SPEED / refMoveSpeed;
        } else if (mon.state == MonsterState::CHASING) {
          animSpeed *= CHASE_SPEED / refMoveSpeed;
        }
      }

      mon.animFrame += animSpeed * deltaTime;

      // Die and hit animations clamp at last frame (don't loop)
      if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD ||
          mon.state == MonsterState::HIT) {
        if (mon.animFrame >= (float)(numKeys - 1))
          mon.animFrame = (float)(numKeys - 1);
      } else {
        // LockPositions actions wrap at numKeys-1 (last frame == first frame)
        int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
        if (wrapKeys < 1)
          wrapKeys = 1;
        if (mon.animFrame >= (float)wrapKeys)
          mon.animFrame = std::fmod(mon.animFrame, (float)wrapKeys);
      }
    }

    // Advance blending Alpha
    if (mon.isBlending) {
      mon.blendAlpha += deltaTime / mon.BLEND_DURATION;
      if (mon.blendAlpha >= 1.0f) {
        mon.blendAlpha = 1.0f;
        mon.isBlending = false;
      }
    }

    // Compute bone matrices with blending support (animBmd for skeleton types)
    int mappedPrior = (mon.priorAction >= 0 && mon.priorAction < 7)
                          ? mdl.actionMap[mon.priorAction]
                          : mon.priorAction;
    // Reuse pre-allocated bone buffer (no heap alloc after first frame)
    auto &bones = mon.cachedBones;
    if (mon.isBlending && mon.priorAction != -1) {
      ComputeBoneMatricesBlended(animBmd, mappedPrior,
                                 mon.priorAnimFrame, mappedAction,
                                 mon.animFrame, mon.blendAlpha, bones);
    } else {
      ComputeBoneMatricesInterpolated(animBmd, mappedAction, mon.animFrame,
                                      bones);
    }

    // LockPositions: cancel root bone X/Y displacement to prevent animation
    // from physically moving the model. In blending mode, we interpolate the
    // offset.
    if (mdl.rootBone >= 0) {
      int rb = mdl.rootBone;
      float dx = 0.0f, dy = 0.0f;

      if (mon.isBlending && mon.priorAction != -1) {
        bool lock1 = mappedPrior < (int)animBmd->Actions.size() &&
                     animBmd->Actions[mappedPrior].LockPositions;
        bool lock2 = mappedAction < (int)animBmd->Actions.size() &&
                     animBmd->Actions[mappedAction].LockPositions;

        float dx1 = 0.0f, dy1 = 0.0f, dx2 = 0.0f, dy2 = 0.0f;
        if (lock1) {
          auto &bm1 = animBmd->Bones[rb].BoneMatrixes[mappedPrior];
          if (!bm1.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(animBmd, mappedPrior,
                                        mon.priorAnimFrame, rb, p, q)) {
              dx1 = p.x - bm1.Position[0].x;
              dy1 = p.y - bm1.Position[0].y;
            }
          }
        }
        if (lock2) {
          auto &bm2 = animBmd->Bones[rb].BoneMatrixes[mappedAction];
          if (!bm2.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(animBmd, mappedAction, mon.animFrame,
                                        rb, p, q)) {
              dx2 = p.x - bm2.Position[0].x;
              dy2 = p.y - bm2.Position[0].y;
            }
          }
        }
        dx = dx1 * (1.0f - mon.blendAlpha) + dx2 * mon.blendAlpha;
        dy = dy1 * (1.0f - mon.blendAlpha) + dy2 * mon.blendAlpha;
      } else if (mappedAction >= 0 &&
                 mappedAction < (int)animBmd->Actions.size() &&
                 animBmd->Actions[mappedAction].LockPositions) {
        auto &bm = animBmd->Bones[rb].BoneMatrixes[mappedAction];
        if (!bm.Position.empty()) {
          dx = bones[rb][0][3] - bm.Position[0].x;
          dy = bones[rb][1][3] - bm.Position[0].y;
        }
      }

      if (dx != 0.0f || dy != 0.0f) {
        for (int b = 0; b < (int)bones.size(); ++b) {
          bones[b][0][3] -= dx;
          bones[b][1][3] -= dy;
        }
      }
    }

    mon.cachedBones = bones;

    // Monster ambient VFX (Main 5.2: MoveCharacterVisual)
    if (m_vfxManager && mon.state != MonsterState::DYING &&
        mon.state != MonsterState::DEAD) {
      mon.ambientVfxTimer += deltaTime;

      // Budge Dragon (type 2): fire breath during ATTACK1 only (bone 7 = mouth)
      // Main 5.2: 1 particle per tick, frames 0-4, offset (0, 32-64, 0) in bone
      // space
      if (mon.monsterType == 2 && mon.action == ACTION_ATTACK1 &&
          mon.animFrame <= 4.0f) {
        if (7 < (int)bones.size()) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          glm::vec3 localOff(0.0f, (float)(rand() % 32 + 32), 0.0f);

          auto applyFireToBone = [&](int boneIdx) {
            const auto &bm = bones[boneIdx];
            glm::vec3 worldOff;
            worldOff.x = bm[0][0] * localOff.x + bm[0][1] * localOff.y +
                         bm[0][2] * localOff.z;
            worldOff.y = bm[1][0] * localOff.x + bm[1][1] * localOff.y +
                         bm[1][2] * localOff.z;
            worldOff.z = bm[2][0] * localOff.x + bm[2][1] * localOff.y +
                         bm[2][2] * localOff.z;
            glm::vec3 bonePos(bm[0][3], bm[1][3], bm[2][3]);
            glm::vec3 localPos = (bonePos + worldOff);
            glm::vec3 worldPos =
                glm::vec3(modelRot * glm::vec4(localPos, 1.0f));
            glm::vec3 firePos = worldPos * mon.scale + mon.position;
            m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
          };

          applyFireToBone(7);
        }
      }

      // Lich (type 6) / Thunder Lich (type 9): fire VFX along entire staff
      if (mon.monsterType == 6 || mon.monsterType == 9) {
        bool wantAttackFire =
            mon.action == ACTION_ATTACK1 && mon.animFrame <= 4.0f;
        bool wantAmbientFire = mon.ambientVfxTimer >= 0.08f;

        if (wantAttackFire || wantAmbientFire) {
          // Find staff weapon def (bone 41)
          const WeaponDef *staffDef = nullptr;
          for (const auto &wd : mdl.weaponDefs) {
            if (wd.attachBone == 41 && wd.bmd) {
              staffDef = &wd;
              break;
            }
          }

          glm::vec3 staffTopLocal(0.0f), staffBottomLocal(0.0f);
          bool haveStaff = false;

          if (staffDef && staffDef->attachBone < (int)bones.size()) {
            // Weapon transform chain (same as weapon rendering code)
            const auto &parentBone = bones[staffDef->attachBone];
            BoneWorldMatrix weaponLocal =
                MuMath::BuildWeaponOffsetMatrix(staffDef->rot,
                                                staffDef->offset);
            BoneWorldMatrix parentMat;
            MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                                     (const float(*)[4])weaponLocal.data(),
                                     (float(*)[4])parentMat.data());

            const auto &wLocalBones = staffDef->cachedLocalBones;
            std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
            for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
              MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                       (const float(*)[4])wLocalBones[bi].data(),
                                       (float(*)[4])wFinalBones[bi].data());
            }

            // Skin all staff vertices to model-local space
            glm::vec3 handBonePos(bones[staffDef->attachBone][0][3],
                                  bones[staffDef->attachBone][1][3],
                                  bones[staffDef->attachBone][2][3]);
            std::vector<glm::vec3> skinnedVerts;
            for (const auto &mesh : staffDef->bmd->Meshes) {
              for (const auto &vert : mesh.Vertices) {
                int ni = std::clamp((int)vert.Node, 0,
                                    (int)wFinalBones.size() - 1);
                const auto &bm = wFinalBones[ni];
                glm::vec3 vp;
                vp.x = bm[0][0] * vert.Position[0] +
                       bm[0][1] * vert.Position[1] +
                       bm[0][2] * vert.Position[2] + bm[0][3];
                vp.y = bm[1][0] * vert.Position[0] +
                       bm[1][1] * vert.Position[1] +
                       bm[1][2] * vert.Position[2] + bm[1][3];
                vp.z = bm[2][0] * vert.Position[0] +
                       bm[2][1] * vert.Position[1] +
                       bm[2][2] * vert.Position[2] + bm[2][3];
                skinnedVerts.push_back(vp);
              }
            }

            // Find tip (farthest vertex from hand bone)
            float maxDist = 0.0f;
            staffTopLocal = handBonePos;
            for (const auto &vp : skinnedVerts) {
              float d = glm::length(vp - handBonePos);
              if (d > maxDist) {
                maxDist = d;
                staffTopLocal = vp;
              }
            }
            // Find bottom (farthest vertex from tip = opposite end)
            float maxDist2 = 0.0f;
            staffBottomLocal = staffTopLocal;
            for (const auto &vp : skinnedVerts) {
              float d = glm::length(vp - staffTopLocal);
              if (d > maxDist2) {
                maxDist2 = d;
                staffBottomLocal = vp;
              }
            }
            haveStaff = true;
          }

          if (haveStaff) {
            glm::mat4 modelRot = glm::mat4(1.0f);
            modelRot = glm::rotate(modelRot, glm::radians(-90.0f),
                                   glm::vec3(0, 0, 1));
            modelRot = glm::rotate(modelRot, glm::radians(-90.0f),
                                   glm::vec3(0, 1, 0));
            modelRot =
                glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

            // Spawn fire along entire staff (bottom -> top)
            auto spawnFireAt = [&](float t) {
              glm::vec3 p = glm::mix(staffBottomLocal, staffTopLocal, t);
              glm::vec3 scatter((float)(rand() % 12 - 6),
                                (float)(rand() % 12 - 6),
                                (float)(rand() % 12 - 6));
              glm::vec3 worldPos =
                  glm::vec3(modelRot * glm::vec4(p + scatter, 1.0f));
              glm::vec3 firePos = worldPos * mon.scale + mon.position;
              m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
            };

            if (wantAttackFire) {
              for (int i = 0; i < 5; ++i)
                spawnFireAt((float)(rand() % 100) / 100.0f);
            }
            if (wantAmbientFire) {
              for (int i = 0; i < 3; ++i)
                spawnFireAt((float)(rand() % 100) / 100.0f);
              mon.ambientVfxTimer = 0.0f;
            }
          }
        }
      }

      // Gorgon (type 18): ambient fire from random bones + red terrain glow
      // Main 5.2: 10 fire particles per tick from random bones, red light (1,0.2,0)
      if (mon.monsterType == 18 && mon.ambientVfxTimer >= 0.08f) {
        int numBones = (int)bones.size();
        if (numBones > 1) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          for (int i = 0; i < 3; ++i) {
            int boneIdx = rand() % numBones;
            const auto &bm = bones[boneIdx];
            glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
            glm::vec3 scatter((float)(rand() % 20 - 10),
                              (float)(rand() % 20 - 10),
                              (float)(rand() % 20 - 10));
            glm::vec3 worldPos =
                glm::vec3(modelRot * glm::vec4(boneLocal + scatter, 1.0f));
            glm::vec3 firePos = worldPos * mon.scale + mon.position;
            m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
          }
          mon.ambientVfxTimer = 0.0f;
        }
      }

      // Ambient smoke: Hound (1), Budge Dragon (2), Hell Hound (5),
      // Dark Knight (10), Larva (12)
      // Main 5.2: rand()%4 per tick (~25fps) = ~6/sec. At 60fps, use timer.
      if ((mon.monsterType == 1 || mon.monsterType == 2 ||
           mon.monsterType == 5 || mon.monsterType == 10 ||
           mon.monsterType == 12) &&
          mon.ambientVfxTimer >= 0.5f) {
        mon.ambientVfxTimer = 0.0f;
        glm::vec3 smokePos =
            mon.position + glm::vec3((float)(rand() % 64 - 32),
                                     20.0f + (float)(rand() % 30),
                                     (float)(rand() % 64 - 32));
        m_vfxManager->SpawnBurst(ParticleType::SMOKE, smokePos, 1);
      }

      // Poison Bull (type 8): nose smoke from bone 24 during idle/walk
      // Main 5.2: ZzzCharacter.cpp line 5985 — smoke at snout bone
      if (mon.monsterType == 8 && mon.ambientVfxTimer >= 0.3f) {
        if (24 < (int)bones.size()) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          const auto &bm = bones[24];
          glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
          glm::vec3 worldPos =
              glm::vec3(modelRot * glm::vec4(boneLocal, 1.0f));
          glm::vec3 smokePos = worldPos * mon.scale + mon.position;
          m_vfxManager->SpawnBurst(ParticleType::SMOKE, smokePos, 1);
          mon.ambientVfxTimer = 0.0f;
        }
      }
      // ── Dungeon trap VFX (Main 5.2 GMAida.cpp: RenderAidaMonsterVisual) ──
      // Bone→world helper: transform bone-local position to world position
      auto boneToWorld = [&](int boneIdx, glm::vec3 localOff = glm::vec3(0)) {
        if (boneIdx < 0 || boneIdx >= (int)bones.size())
          return mon.position;
        const auto &bm = bones[boneIdx];
        glm::vec3 worldOff(bm[0][0] * localOff.x + bm[0][1] * localOff.y +
                               bm[0][2] * localOff.z,
                           bm[1][0] * localOff.x + bm[1][1] * localOff.y +
                               bm[1][2] * localOff.z,
                           bm[2][0] * localOff.x + bm[2][1] * localOff.y +
                               bm[2][2] * localOff.z);
        glm::vec3 bonePos(bm[0][3], bm[1][3], bm[2][3]);
        glm::mat4 mrot(1.0f);
        mrot = glm::rotate(mrot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
        mrot = glm::rotate(mrot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
        mrot = glm::rotate(mrot, mon.facing, glm::vec3(0, 0, 1));
        glm::vec3 wp =
            glm::vec3(mrot * glm::vec4(bonePos + worldOff, 1.0f));
        return wp * mon.scale + mon.position;
      };

      // Lance Trap (100): continuous rotating lightning sprites at +150 above
      // Main 5.2 ZzzObject.cpp:2784 — two BITMAP_LIGHTNING+1 sprites, counter-rotating
      if (mon.monsterType == 100 && m_vfxManager && mon.ambientVfxTimer >= 0.12f) {
        glm::vec3 lightningPos = mon.position + glm::vec3(0.0f, 150.0f * mon.scale, 0.0f);
        m_vfxManager->SpawnBurst(ParticleType::SPELL_LIGHTNING, lightningPos, 2);
        mon.ambientVfxTimer = 0.0f;
      }
      // Attack VFX handled in TriggerAttackAnimation: 100→lightning, 101→sound, 102→fire.
    }

    // Re-skin meshes
    for (int mi = 0;
         mi < (int)mon.meshBuffers.size() && mi < (int)mdl.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(mdl.bmd->Meshes[mi], bones,
                               mon.meshBuffers[mi]);
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(mon.scale));

    // Terrain lightmap at monster position (static JPEG baseline — no fire).
    // Fire light comes from per-pixel point lights only.
    glm::vec3 tLight = sampleTerrainLightAt(mon.position);
    glm::vec3 monTint(1.0f); // Per-monster color tint (via u_baseTint)
    // Elite Bull Fighter (type 4): darker skin tone (Main 5.2 visual reference)
    if (mon.monsterType == 4)
      tLight *= 0.45f;
    else if (mon.monsterType == 5) // Hell Hound: darker body
      tLight *= 0.3f;
    else if (mon.monsterType == 8) // Poison Bull: green tint
      monTint = glm::vec3(0.55f, 1.0f, 0.55f);
    else if (mon.monsterType == 11) // Ghost: spectral blue tint, darken skin
      monTint = glm::vec3(0.5f, 0.6f, 0.9f);

    // Spawn fade-in (0->1 over ~0.4s)
    if (mon.spawnAlpha < 1.0f) {
      mon.spawnAlpha += deltaTime * 2.5f; // ~0.4s fade-in
      if (mon.spawnAlpha > 1.0f)
        mon.spawnAlpha = 1.0f;
    }

    // Own summon safe zone fade (fast fade out/in ~0.3s)
    if (mon.serverIndex == m_ownSummonIndex && m_ownSummonIndex != 0) {
      float target = m_playerInSafeZone ? 0.0f : 1.0f;
      if (mon.summonFadeAlpha != target) {
        float speed = 3.3f; // ~0.3s
        if (m_playerInSafeZone)
          mon.summonFadeAlpha = std::max(0.0f, mon.summonFadeAlpha - speed * deltaTime);
        else
          mon.summonFadeAlpha = std::min(1.0f, mon.summonFadeAlpha + speed * deltaTime);
      }
    }

    // Combined alpha: corpse fade * spawn fade-in * per-type alpha * summon safe zone fade
    float renderAlpha = mon.corpseAlpha * mon.spawnAlpha * mdl.typeAlpha * mon.summonFadeAlpha;
    if (renderAlpha <= 0.0f)
      continue;

    // BlendMesh UV scroll (Main 5.2: Lich — texCoordV scrolls over time)
    // -(float)((int)(WorldTime)%2000)*0.0005f
    bool hasBlendMesh = (mdl.blendMesh >= 0);
    float blendMeshUVOffset = 0.0f;
    if (hasBlendMesh) {
      int wt = (int)(m_worldTime * 1000.0f) % 2000;
      blendMeshUVOffset = -(float)wt * 0.0005f;
    }
    // Ghost/Gorgon BlendMeshLight flicker (Main 5.2: (float)(rand()%10)*0.1f)
    float blendMeshLightVal = 1.0f;
    if (mon.monsterType == 11 || mon.monsterType == 18) {
      // Smooth flicker instead of pure random to avoid strobing at 60fps
      float phase = m_worldTime * 3.0f + (float)mon.serverIndex * 2.1f;
      blendMeshLightVal = 0.4f + 0.5f * (0.5f + 0.5f * std::sin(phase));
    }

    // Draw all meshes (BGFX path)
    for (int meshIdx = 0; meshIdx < (int)mon.meshBuffers.size(); ++meshIdx) {
      auto &mb = mon.meshBuffers[meshIdx];
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      if (mdl.hiddenMesh >= 0 && mb.bmdTextureId == mdl.hiddenMesh)
        continue;

      bool isBlendMesh = hasBlendMesh && (mb.bmdTextureId == mdl.blendMesh);
      if (isBlendMesh) {
        // Reduce additive glow light to prevent overbright near fire sources
        glm::vec3 blendLight = tLight * 0.4f;
        monDrawMesh(model, mb, renderAlpha, blendMeshLightVal, blendLight, additiveState, monTint);
      } else if (mb.noneBlend) {
        monDrawMesh(model, mb, renderAlpha, 1.0f, tLight, noneBlendState, monTint);
      } else if (mb.bright) {
        monDrawMesh(model, mb, renderAlpha, 1.0f, tLight * 0.5f, additiveState, monTint);
      } else {
        monDrawMesh(model, mb, renderAlpha, 1.0f, tLight, normalState, monTint);
      }
    }

    // Draw weapons (skeleton types)
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponMeshes.size();
         ++wi) {
      auto &wd = mdl.weaponDefs[wi];
      auto &wms = mon.weaponMeshes[wi];
      if (!wd.bmd || wms.meshBuffers.empty())
        continue;
      if (wd.attachBone >= (int)bones.size())
        continue;

      const auto &parentBone = bones[wd.attachBone];
      BoneWorldMatrix weaponLocal =
          MuMath::BuildWeaponOffsetMatrix(wd.rot, wd.offset);
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                               (const float(*)[4])weaponLocal.data(),
                               (float(*)[4])parentMat.data());
      const auto &wLocalBones = wd.cachedLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }
      for (int mi = 0;
           mi < (int)wms.meshBuffers.size() && mi < (int)wd.bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(wd.bmd->Meshes[mi], wFinalBones,
                                 wms.meshBuffers[mi]);
        auto &mb = wms.meshBuffers[mi];
        if (mb.indexCount == 0)
          continue;
        monDrawMesh(model, mb, renderAlpha, 1.0f, tLight, normalState);
      }
    }
  }


  renderDebris(view, proj, camPos);
  renderArrows(view, proj, camPos);
}

void MonsterManager::RenderShadows(const glm::mat4 &view,
                                   const glm::mat4 &proj) {
  if (!m_shadowShader || m_monsters.empty())
    return;

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  // Lambda: project shadow vertices for a mesh using cached bones
  auto buildShadowVerts = [&](const Mesh_t &mesh,
                              const std::vector<BoneWorldMatrix> &cachedBones,
                              float scale, float cosF, float sinF,
                              std::vector<glm::vec3> &out) {
    out.clear();
    for (int i = 0; i < mesh.NumTriangles; ++i) {
      auto &tri = mesh.Triangles[i];
      int steps = (tri.Polygon == 3) ? 3 : 4;
      for (int v = 0; v < 3; ++v) {
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        glm::vec3 pos = srcVert.Position;
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)cachedBones.size())
          pos = MuMath::TransformPoint(
              (const float(*)[4])cachedBones[boneIdx].data(), pos);
        pos *= scale;
        float rx = pos.x * cosF - pos.y * sinF;
        float ry = pos.x * sinF + pos.y * cosF;
        pos.x = rx; pos.y = ry;
        if (pos.z < sy) {
          float factor = 1.0f / (pos.z - sy);
          pos.x += pos.z * (pos.x + sx) * factor;
          pos.y += pos.z * (pos.y + sx) * factor;
        }
        pos.z = 5.0f;
        out.push_back(pos);
      }
      if (steps == 4) {
        int quadIndices[3] = {0, 2, 3};
        for (int v : quadIndices) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)cachedBones.size())
            pos = MuMath::TransformPoint(
                (const float(*)[4])cachedBones[boneIdx].data(), pos);
          pos *= scale;
          float rx = pos.x * cosF - pos.y * sinF;
          float ry = pos.x * sinF + pos.y * cosF;
          pos.x = rx; pos.y = ry;
          if (pos.z < sy) {
            float factor = 1.0f / (pos.z - sy);
            pos.x += pos.z * (pos.x + sx) * factor;
            pos.y += pos.z * (pos.y + sx) * factor;
          }
          pos.z = 5.0f;
          out.push_back(pos);
        }
      }
    }
  };

  // BGFX: stencil-based shadow merging — draw each pixel at most once
  uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                        | BGFX_STATE_DEPTH_TEST_LESS
                        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint32_t shadowStencil = BGFX_STENCIL_TEST_EQUAL
                         | BGFX_STENCIL_FUNC_REF(0)
                         | BGFX_STENCIL_FUNC_RMASK(0xFF)
                         | BGFX_STENCIL_OP_FAIL_S_KEEP
                         | BGFX_STENCIL_OP_FAIL_Z_KEEP
                         | BGFX_STENCIL_OP_PASS_Z_INCR;

  static std::vector<glm::vec3> shadowVerts;

  for (auto &mon : m_monsters) {
    if (mon.cachedBones.empty()) continue;
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f) continue;
    // Main 5.2: KIND_TRAP has no shadow (ZzzCharacter.cpp:8320)
    if (mon.monsterType >= 100 && mon.monsterType <= 102) continue;

    auto &mdl = m_models[mon.modelIdx];
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(mon.scale));

    float cosF = cosf(mon.facing);
    float sinF = sinf(mon.facing);

    // Body mesh shadows
    for (int mi = 0;
         mi < (int)mdl.bmd->Meshes.size() && mi < (int)mon.shadowMeshes.size();
         ++mi) {
      auto &sm = mon.shadowMeshes[mi];
      if (sm.vertexCount == 0 || !bgfx::isValid(sm.vbo)) continue;

      buildShadowVerts(mdl.bmd->Meshes[mi], mon.cachedBones, mon.scale,
                       cosF, sinF, shadowVerts);
      if (shadowVerts.empty()) continue;

      bgfx::update(sm.vbo, 0,
          bgfx::copy(shadowVerts.data(), (uint32_t)(shadowVerts.size() * sizeof(glm::vec3))));
      bgfx::setTransform(glm::value_ptr(model));
      bgfx::setVertexBuffer(0, sm.vbo, 0, (uint32_t)shadowVerts.size());
      bgfx::setState(shadowState);
      bgfx::setStencil(shadowStencil);
      bgfx::submit(0, m_shadowShader->program);
    }

    // Weapon shadows
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponShadowMeshes.size();
         ++wi) {
      auto &wd = mdl.weaponDefs[wi];
      auto &wss = mon.weaponShadowMeshes[wi];
      if (!wd.bmd || wss.meshes.empty()) continue;
      if (wd.attachBone >= (int)mon.cachedBones.size()) continue;

      const auto &parentBone = mon.cachedBones[wd.attachBone];
      BoneWorldMatrix weaponLocal = MuMath::BuildWeaponOffsetMatrix(wd.rot, wd.offset);
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                               (const float(*)[4])weaponLocal.data(),
                               (float(*)[4])parentMat.data());
      const auto &wLocalBones = wd.cachedLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());

      for (int wmi = 0;
           wmi < (int)wd.bmd->Meshes.size() && wmi < (int)wss.meshes.size();
           ++wmi) {
        auto &wsm = wss.meshes[wmi];
        if (wsm.vertexCount == 0 || !bgfx::isValid(wsm.vbo)) continue;

        buildShadowVerts(wd.bmd->Meshes[wmi], wFinalBones, mon.scale,
                         cosF, sinF, shadowVerts);
        if (shadowVerts.empty()) continue;

        bgfx::update(wsm.vbo, 0,
            bgfx::copy(shadowVerts.data(), (uint32_t)(shadowVerts.size() * sizeof(glm::vec3))));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, wsm.vbo, 0, (uint32_t)shadowVerts.size());
        bgfx::setState(shadowState);
        bgfx::setStencil(shadowStencil);
        bgfx::submit(0, m_shadowShader->program);
      }
    }
  }
}

void MonsterManager::SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx) {
  m_shadowMapTex = tex;
  m_lightMtx = lightMtx;
}

void MonsterManager::RenderDepthPrepass(const glm::mat4 &view,
                                         const glm::mat4 &proj,
                                         const glm::vec3 &camPos) {
  if (!m_shader || m_monsters.empty())
    return;

  // Depth-only state: write Z but no color. The fragment shader still runs
  // (for alpha-test discard), but no color reaches the framebuffer.
  uint64_t depthState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

  // Frustum culling reuses the same logic as Render()
  glm::mat4 vp = proj * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3]+vp[0][0], vp[1][3]+vp[1][0], vp[2][3]+vp[2][0], vp[3][3]+vp[3][0]);
  frustum[1] = glm::vec4(vp[0][3]-vp[0][0], vp[1][3]-vp[1][0], vp[2][3]-vp[2][0], vp[3][3]-vp[3][0]);
  frustum[2] = glm::vec4(vp[0][3]+vp[0][1], vp[1][3]+vp[1][1], vp[2][3]+vp[2][1], vp[3][3]+vp[3][1]);
  frustum[3] = glm::vec4(vp[0][3]-vp[0][1], vp[1][3]-vp[1][1], vp[2][3]-vp[2][1], vp[3][3]-vp[3][1]);
  frustum[4] = glm::vec4(vp[0][3]+vp[0][2], vp[1][3]+vp[1][2], vp[2][3]+vp[2][2], vp[3][3]+vp[3][2]);
  frustum[5] = glm::vec4(vp[0][3]-vp[0][2], vp[1][3]-vp[1][2], vp[2][3]-vp[2][2], vp[3][3]-vp[3][2]);
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  for (auto &mon : m_monsters) {
    if (mon.cachedBones.empty()) continue;
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f) continue;
    if (mon.spawnAlpha < 0.5f) continue; // skip mostly-faded spawning monsters

    auto &mdl = m_models[mon.modelIdx];

    // Frustum cull
    float cullRadius = mdl.collisionHeight * mon.scale * 2.0f;
    glm::vec3 center = mon.position + glm::vec3(0, cullRadius * 0.4f, 0);
    bool outside = false;
    for (int p = 0; p < 6; ++p) {
      if (frustum[p].x*center.x + frustum[p].y*center.y +
          frustum[p].z*center.z + frustum[p].w < -cullRadius) {
        outside = true; break;
      }
    }
    if (outside) continue;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(mon.scale));

    // Only opaque meshes (skip BlendMesh, bright/additive)
    bool hasBlendMesh = (mdl.blendMesh >= 0);
    for (auto &mb : mon.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      if (mdl.hiddenMesh >= 0 && mb.bmdTextureId == mdl.hiddenMesh) continue;
      if (hasBlendMesh && mb.bmdTextureId == mdl.blendMesh) continue;
      if (mb.noneBlend || mb.bright) continue;

      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      m_shader->setTexture(0, "s_texColor", mb.texture);
      // Minimal uniforms for shader alpha-test (discard)
      m_shader->setVec4("u_params", glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
      bgfx::setState(depthState);
      bgfx::submit(0, m_shader->program);
    }
  }
}

void MonsterManager::RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram) {
  if (m_monsters.empty()) return;

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW;

  for (auto &mon : m_monsters) {
    if (mon.cachedBones.empty()) continue;
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f) continue;
    // Main 5.2: KIND_TRAP has no shadow
    if (mon.monsterType >= 100 && mon.monsterType <= 102) continue;

    auto &mdl = m_models[mon.modelIdx];
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(mon.scale));

    // Body meshes
    for (auto &mb : mon.meshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
    // Weapon meshes (skeleton/lich types)
    for (auto &wms : mon.weaponMeshes) {
      for (auto &mb : wms.meshBuffers) {
        if (mb.hidden || mb.indexCount == 0) continue;
        bgfx::setTransform(glm::value_ptr(model));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        bgfx::setState(state);
        bgfx::submit(viewId, depthProgram);
      }
    }
  }
}

void MonsterManager::RenderSilhouetteOutline(int monsterIndex,
                                              const glm::mat4 &view,
                                              const glm::mat4 &proj) {
  if (!m_outlineShader || monsterIndex < 0 ||
      monsterIndex >= (int)m_monsters.size())
    return;

  auto &mon = m_monsters[monsterIndex];
  if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
    return;

  auto &mdl = m_models[mon.modelIdx];

  // Build model matrix
  glm::mat4 baseModel = glm::translate(glm::mat4(1.0f), mon.position);
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  baseModel = glm::rotate(baseModel, mon.facing, glm::vec3(0, 0, 1));
  glm::mat4 stencilModel = glm::scale(baseModel, glm::vec3(mon.scale));

  // Per-pass state captured by lambda
  uint32_t stencilFront = 0, stencilBack = 0;
  uint64_t state = 0;
  glm::vec4 outlineParams(0.0f);
  glm::vec4 outlineColor(0.0f);

  // Helper to submit all monster meshes (body + weapons)
  auto submitMonsterMeshes = [&]() {
    for (auto &mb : mon.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      bgfx::setTransform(glm::value_ptr(stencilModel));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setStencil(stencilFront, stencilBack);
      bgfx::setState(state);
      m_outlineShader->setVec4("u_outlineParams", outlineParams);
      m_outlineShader->setVec4("u_outlineColor", outlineColor);
      bgfx::submit(0, m_outlineShader->program);
    }
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponMeshes.size();
         ++wi) {
      for (auto &mb : mon.weaponMeshes[wi].meshBuffers) {
        if (mb.indexCount == 0) continue;
        bgfx::setTransform(glm::value_ptr(stencilModel));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        bgfx::setStencil(stencilFront, stencilBack);
        bgfx::setState(state);
        m_outlineShader->setVec4("u_outlineParams", outlineParams);
        m_outlineShader->setVec4("u_outlineColor", outlineColor);
        bgfx::submit(0, m_outlineShader->program);
      }
    }
  };

  // === Pass 1: Write monster silhouette to stencil ===
  // No color write, no depth write, no depth test — write stencil ref=1
  stencilFront =
      BGFX_STENCIL_TEST_ALWAYS
    | BGFX_STENCIL_FUNC_REF(1)
    | BGFX_STENCIL_FUNC_RMASK(0xFF)
    | BGFX_STENCIL_OP_FAIL_S_KEEP
    | BGFX_STENCIL_OP_FAIL_Z_KEEP
    | BGFX_STENCIL_OP_PASS_Z_REPLACE;
  stencilBack = stencilFront; // double-sided
  state = BGFX_STATE_MSAA; // no color/depth write, no depth test
  outlineParams = glm::vec4(0.0f); // thickness=0 (no extrusion)
  outlineColor = glm::vec4(0.0f);  // invisible (stencil write only)
  submitMonsterMeshes();

  // === Pass 2: Multi-layer soft glow where stencil != 1 ===
  stencilFront =
      BGFX_STENCIL_TEST_NOTEQUAL
    | BGFX_STENCIL_FUNC_REF(1)
    | BGFX_STENCIL_FUNC_RMASK(0xFF)
    | BGFX_STENCIL_OP_FAIL_S_KEEP
    | BGFX_STENCIL_OP_FAIL_Z_KEEP
    | BGFX_STENCIL_OP_PASS_Z_KEEP;
  stencilBack = stencilFront;
  state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                             BGFX_STATE_BLEND_INV_SRC_ALPHA)
    | BGFX_STATE_MSAA;

  constexpr float thicknesses[] = {5.0f, 3.5f, 2.0f};
  constexpr float alphas[] = {0.08f, 0.18f, 0.35f};

  for (int layer = 0; layer < 3; ++layer) {
    outlineParams = glm::vec4(thicknesses[layer], 0.0f, 0.0f, 0.0f);
    outlineColor = glm::vec4(0.8f, 0.4f, 0.15f, alphas[layer]);
    submitMonsterMeshes();
  }
}

// Helper: render a single nameplate at a given monster index
static void renderSingleNameplate(MonsterManager *mgr, ImDrawList *dl,
                                   ImFont *font, const glm::mat4 &view,
                                   const glm::mat4 &proj, int winW, int winH,
                                   int targetIdx, int playerLevel,
                                   bool isSummon, bool isOwnSummon) {
  MonsterInfo mi = mgr->GetMonsterInfo(targetIdx);
  if (mi.state == MonsterState::DEAD || mi.type >= 100)
    return; // Traps (types 100-102) don't show nameplates/HP bars

  // Project monster head position to screen
  glm::vec4 worldPos(mi.position.x, mi.position.y + mi.height + 20.0f,
                     mi.position.z, 1.0f);
  glm::vec4 clip = proj * view * worldPos;
  if (clip.w <= 0.0f)
    return;

  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  float sx = (ndc.x * 0.5f + 0.5f) * winW;
  float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * winH;

  // Nameplate color
  ImU32 threatCol;
  if (isSummon) {
    if (isOwnSummon)
      threatCol = IM_COL32(60, 220, 60, 200);   // Green: own summon
    else
      threatCol = IM_COL32(100, 180, 255, 200);  // Light blue: other's summon
  } else {
    int grayThreshold = 8 + playerLevel / 10;
    if (grayThreshold > 20) grayThreshold = 20;
    int diff = playerLevel - mi.level;
    if (diff > grayThreshold)
      threatCol = IM_COL32(120, 150, 120, 200);
    else if (diff > 5)
      threatCol = IM_COL32(80, 210, 80, 200);
    else if (diff >= -2)
      threatCol = IM_COL32(255, 230, 60, 200);
    else if (diff >= -5)
      threatCol = IM_COL32(255, 140, 40, 200);
    else
      threatCol = IM_COL32(255, 60, 60, 200);
  }

  float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;
  float nameFs = 12.0f * uiScale;
  float lvlFs = 10.0f * uiScale;

  char nameText[64];
  snprintf(nameText, sizeof(nameText), "%s", mi.name.c_str());
  char levelText[16];
  snprintf(levelText, sizeof(levelText), "%d", mi.level);
  ImVec2 nameSize = font->CalcTextSizeA(nameFs, FLT_MAX, 0, nameText);

  float barW = 60.0f * uiScale;
  float barH = 5.0f * uiScale;
  float nameX = sx - nameSize.x * 0.5f;
  float nameY = sy - barH - nameSize.y - 6.0f;
  float barX = sx - barW * 0.5f;
  float barY = sy - barH - 3.0f;

  // Name shadow + text
  dl->AddText(font, nameFs, ImVec2(nameX + 1, nameY + 1),
              IM_COL32(0, 0, 0, 160), nameText);
  dl->AddText(font, nameFs, ImVec2(nameX, nameY), threatCol, nameText);

  // Level badge
  float lvlX = nameX + nameSize.x + 4.0f;
  float lvlY = nameY + 2.0f;
  dl->AddText(font, lvlFs, ImVec2(lvlX + 1, lvlY + 1),
              IM_COL32(0, 0, 0, 140), levelText);
  dl->AddText(font, lvlFs, ImVec2(lvlX, lvlY),
              IM_COL32(200, 200, 200, 160), levelText);

  // HP bar
  float hpFrac = mi.maxHp > 0 ? (float)mi.hp / mi.maxHp : 0.0f;
  hpFrac = std::max(0.0f, std::min(1.0f, hpFrac));

  dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                    IM_COL32(0, 0, 0, 100), 2.0f);
  if (hpFrac > 0.0f) {
    ImU32 hpCol = hpFrac > 0.5f    ? IM_COL32(50, 200, 50, 180)
                  : hpFrac > 0.25f ? IM_COL32(220, 190, 40, 180)
                                   : IM_COL32(210, 50, 50, 180);
    dl->AddRectFilled(ImVec2(barX, barY),
                      ImVec2(barX + barW * hpFrac, barY + barH), hpCol, 2.0f);
  }
  dl->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
              IM_COL32(255, 255, 255, 40), 2.0f);
}

void MonsterManager::RenderNameplates(ImDrawList *dl, ImFont *font,
                                      const glm::mat4 &view,
                                      const glm::mat4 &proj, int winW, int winH,
                                      const glm::vec3 &camPos,
                                      int hoveredMonster,
                                      int attackTarget, int playerLevel) {
  // Always show own summon nameplate (unless faded out in safe zone)
  if (m_ownSummonIndex != 0 && !m_playerInSafeZone) {
    int summonIdx = FindByServerIndex(m_ownSummonIndex);
    if (summonIdx >= 0 && summonIdx != hoveredMonster && summonIdx != attackTarget) {
      renderSingleNameplate(this, dl, font, view, proj, winW, winH,
                            summonIdx, playerLevel, true, true);
    }
  }

  // Show nameplate for hovered monster, or attack target if not hovering
  int targetIdx = hoveredMonster;
  if (targetIdx < 0 || targetIdx >= GetMonsterCount())
    targetIdx = attackTarget;
  if (targetIdx < 0 || targetIdx >= GetMonsterCount())
    return;

  bool isSummon = IsSummon(GetMonsterInfo(targetIdx).serverIndex);
  bool isOwn = isSummon && GetMonsterInfo(targetIdx).serverIndex == m_ownSummonIndex;
  renderSingleNameplate(this, dl, font, view, proj, winW, winH,
                        targetIdx, playerLevel, isSummon, isOwn);
}

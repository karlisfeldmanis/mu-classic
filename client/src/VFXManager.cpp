#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void VFXManager::Init(const std::string &effectDataPath) {
  // Blood texture
  m_bloodTexture =
      TextureLoader::LoadOZT(effectDataPath + "/Effect/blood01.ozt");

  // Main 5.2: BITMAP_SPARK — white star sparks on melee hit
  m_sparkTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark01.OZJ");

  // Main 5.2: BITMAP_SPARK+1 — Aqua Beam laser segments (Spark03.OZJ)
  m_spark3Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark03.OZJ");
  if (!TexValid(m_spark3Texture))
    std::cerr << "[VFX] Failed to load Spark03.OZJ" << std::endl;

  // Aqua Beam outer glow layer
  m_flareBlueTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/flareBlue.OZJ");
  if (!TexValid(m_flareBlueTexture))
    m_flareBlueTexture = m_spark3Texture; // Fallback

  // Main 5.2: BITMAP_FLASH — bright additive impact flare
  m_flareTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/flare01.OZJ");

  // Legacy hit texture (fallback if spark fails)
  m_hitTexture = TextureLoader::LoadOZT(effectDataPath + "/Interface/hit.OZT");

  // Lightning ribbon texture (Main 5.2: BITMAP_JOINT_THUNDER)
  m_lightningTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointThunder01.OZJ");

  // Monster ambient VFX textures
  m_smokeTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/smoke01.OZJ");
  m_fireTexture = TextureLoader::LoadOZJ(effectDataPath + "/Effect/Fire01.OZJ");
  m_energyTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointEnergy01.OZJ");

  // Main 5.2: BITMAP_MAGIC+1 — level-up magic circle ground decal
  m_magicGroundTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Magic_Ground2.OZJ");

  // Main 5.2: ring_of_gradation — golden ring for level-up effect
  m_ringTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/ring_of_gradation.OZJ");

  // Main 5.2: BITMAP_ENERGY — Energy Ball projectile (Effect/Thunder01.jpg)
  m_thunderTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Thunder01.OZJ");

  // Main 5.2: BITMAP_FLARE — level-up orbiting flare texture (Effect/Flare.jpg)
  m_bitmapFlareTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Flare.OZJ");
  if (!TexValid(m_bitmapFlareTexture))
    m_bitmapFlareTexture = m_flareTexture; // Fallback to flare01.OZJ

  // Main 5.2: BITMAP_FLAME — Flame spell ground fire particles
  m_flameTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Flame01.OZJ");
  if (!TexValid(m_flameTexture)) {
    std::cerr << "[VFX] Failed to load Flame01.OZJ — using Fire01 fallback"
              << std::endl;
    m_flameTexture = m_fireTexture;
  }

  // Main 5.2: BITMAP_EXPLOTION — animated 4x4 explosion sprite sheet
  m_explosionTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Explotion01.OZJ");
  if (!TexValid(m_explosionTexture))
    m_explosionTexture = m_flareTexture; // Fallback

  // Main 5.2: Inferno-specific fire texture
  m_infernoFireTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/inferno.OZJ");
  if (!TexValid(m_infernoFireTexture))
    m_infernoFireTexture = m_fireTexture; // Fallback

  // Main 5.2: BITMAP_JOINT_SPIRIT — Evil Spirit beam texture
  m_jointSpiritTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointSpirit01.OZJ");
  if (!TexValid(m_jointSpiritTexture))
    std::cerr << "[VFX] Failed to load JointSpirit01.OZJ" << std::endl;

  // Main 5.2: Circle01.bmd texture (Hellfire ground circle — star/pentagram pattern)
  m_hellfireCircleTex =
      TextureLoader::LoadOZJ(effectDataPath + "/Skill/magic_a01.OZJ");
  if (!TexValid(m_hellfireCircleTex))
    std::cerr << "[VFX] Failed to load Skill/magic_a01.OZJ" << std::endl;

  // Main 5.2: Circle02.bmd texture (Hellfire light circle overlay)
  m_hellfireLightTex =
      TextureLoader::LoadOZJ(effectDataPath + "/Skill/magic_a02.OZJ");
  if (!TexValid(m_hellfireLightTex))
    std::cerr << "[VFX] Failed to load Skill/magic_a02.OZJ" << std::endl;

  // Main 5.2: BITMAP_BLUR — regular attack blur trail (blur01.OZJ)
  m_blurTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/blur01.OZJ");
  if (!TexValid(m_blurTexture))
    std::cerr << "[VFX] Failed to load blur01.OZJ" << std::endl;

  // Main 5.2: BITMAP_BLUR+2 — skill attack blur trail (motion_blur_r.OZJ)
  m_motionBlurTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/motion_blur_r.OZJ");
  if (!TexValid(m_motionBlurTexture))
    std::cerr << "[VFX] Failed to load motion_blur_r.OZJ" << std::endl;

  // Main 5.2: BITMAP_SPARK — hit spark particles (Spark02.OZJ)
  m_spark2Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark02.OZJ");
  if (!TexValid(m_spark2Texture))
    std::cerr << "[VFX] Failed to load Spark02.OZJ" << std::endl;

  // Main 5.2: BITMAP_FLARE_FORCE — Death Stab spiral trail (NSkill.OZJ)
  m_flareForceTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/NSkill.OZJ");
  if (!TexValid(m_flareForceTexture))
    m_flareForceTexture = m_energyTexture; // Fallback

  // MuSven: BITMAP_LIGHTNING+1 — rotating cross sprite for Lance Trap
  m_lightning2Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/lightning2.OZJ");
  if (!TexValid(m_lightning2Texture))
    m_lightning2Texture = m_lightningTexture; // Fallback to JointThunder01

  // MuSven: BITMAP_SHINY — sparkle particle for Lance Trap blade tips
  m_shinyTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Shiny01.OZJ");
  if (!TexValid(m_shinyTexture))
    m_shinyTexture = m_sparkTexture; // Fallback to Spark01

  // Main 5.2: BITMAP_SHINY+1 — Shadow monster white glow aura (Shiny02.OZJ)
  m_shiny2Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Shiny02.OZJ");
  if (!TexValid(m_shiny2Texture))
    m_shiny2Texture = m_shinyTexture; // Fallback to Shiny01

  if (!TexValid(m_bloodTexture))
    std::cerr << "[VFX] Failed to load blood texture" << std::endl;
  if (!TexValid(m_sparkTexture))
    std::cerr << "[VFX] Failed to load spark texture (Spark01.OZJ)"
              << std::endl;
  if (!TexValid(m_flareTexture))
    std::cerr << "[VFX] Failed to load flare texture (flare01.OZJ)"
              << std::endl;
  if (!TexValid(m_lightningTexture))
    std::cerr << "[VFX] Failed to load lightning texture" << std::endl;
  if (!TexValid(m_smokeTexture))
    std::cerr << "[VFX] Failed to load smoke texture" << std::endl;
  if (!TexValid(m_fireTexture))
    std::cerr << "[VFX] Failed to load fire texture" << std::endl;
  if (!TexValid(m_energyTexture))
    std::cerr << "[VFX] Failed to load energy texture" << std::endl;
  if (!TexValid(m_magicGroundTexture))
    std::cerr << "[VFX] Failed to load magic ground texture (Magic_Ground2.OZJ)"
              << std::endl;

  if (!TexValid(m_thunderTexture))
    std::cerr << "[VFX] WARNING: Thunder01.OZJ failed to load — Energy Ball will use "
                 "fallback texture" << std::endl;

  // BGFX shaders for VFX
  m_shader = Shader::Load("vs_billboard.bin", "fs_billboard.bin");
  m_lineShader = Shader::Load("vs_line.bin", "fs_line.bin");
  if (!m_shader)
    std::cerr << "[VFX] Failed to load BGFX billboard shader\n";
  if (!m_lineShader)
    std::cerr << "[VFX] Failed to load BGFX line shader\n";

  // Load Fire Ball 3D model (Main 5.2: MODEL_FIRE = Data/Skill/Fire01.bmd)
  std::string skillPath = effectDataPath + "/Skill/";
  m_fireBmd = BMDParser::Parse(skillPath + "Fire01.bmd");
  if (m_fireBmd) {
    auto bones = ComputeBoneMatrices(m_fireBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_fireBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_fireMeshes, dummyAABB, true);
    }
    std::cout << "[VFX] Fire01.bmd loaded: " << m_fireMeshes.size() << " meshes"
              << std::endl;
  } else {
    std::cerr << "[VFX] Failed to load Fire01.bmd — Fire Ball will use billboard fallback"
              << std::endl;
  }

  // Load Lightning sky-strike model (Main 5.2: MODEL_SKILL_BLAST = Data/Skill/Blast01.bmd)
  m_blastBmd = BMDParser::Parse(skillPath + "Blast01.bmd");
  if (m_blastBmd) {
    auto bones = ComputeBoneMatrices(m_blastBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_blastBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_blastMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Blast01.bmd loaded: " << m_blastMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Poison cloud model (Main 5.2: MODEL_POISON = Data/Skill/Poison01.bmd)
  m_poisonBmd = BMDParser::Parse(skillPath + "Poison01.bmd");
  if (m_poisonBmd) {
    auto bones = ComputeBoneMatrices(m_poisonBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_poisonBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_poisonMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Poison01.bmd loaded: " << m_poisonMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Ice crystal model (Main 5.2: MODEL_ICE = Data/Skill/Ice01.bmd)
  m_iceBmd = BMDParser::Parse(skillPath + "Ice01.bmd");
  if (m_iceBmd) {
    auto bones = ComputeBoneMatrices(m_iceBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_iceBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_iceMeshes, dummyAABB, false);
    }
    std::cout << "[VFX] Ice01.bmd loaded: " << m_iceMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Ice shard model (Main 5.2: MODEL_ICE_SMALL = Data/Skill/Ice02.bmd)
  m_iceSmallBmd = BMDParser::Parse(skillPath + "Ice02.bmd");
  if (m_iceSmallBmd) {
    auto bones = ComputeBoneMatrices(m_iceSmallBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_iceSmallBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_iceSmallMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Ice02.bmd loaded: " << m_iceSmallMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Storm tornado model (Main 5.2: MODEL_STORM = Data/Skill/Storm01.bmd)
  m_stormBmd = BMDParser::Parse(skillPath + "Storm01.bmd");
  if (m_stormBmd) {
    auto bones = ComputeBoneMatrices(m_stormBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stormBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stormMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Storm01.bmd loaded: " << m_stormMeshes.size()
              << " meshes" << std::endl;
  }

  // Load PowerWave model (Main 5.2/MuSven: MODEL_MAGIC2 = Data/Skill/Magic02.bmd)
  m_magic2Bmd = BMDParser::Parse(skillPath + "Magic02.bmd");
  if (m_magic2Bmd) {
    auto bones = ComputeBoneMatrices(m_magic2Bmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_magic2Bmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_magic2Meshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Magic02.bmd loaded: " << m_magic2Meshes.size()
              << " meshes" << std::endl;
  } else {
    std::cerr << "[VFX] Failed to load Magic02.bmd — PowerWave will use billboard fallback"
              << std::endl;
  }

  // Load Hellfire ground circle models (Main 5.2: MODEL_CIRCLE = Circle01.bmd)
  m_circleBmd = BMDParser::Parse(skillPath + "Circle01.bmd");
  if (m_circleBmd) {
    auto bones = ComputeBoneMatrices(m_circleBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_circleBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_circleMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Circle01.bmd loaded: " << m_circleMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Hellfire circle light model (Main 5.2: MODEL_CIRCLE_LIGHT = Circle02.bmd)
  m_circleLightBmd = BMDParser::Parse(skillPath + "Circle02.bmd");
  if (m_circleLightBmd) {
    auto bones = ComputeBoneMatrices(m_circleLightBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_circleLightBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_circleLightMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Circle02.bmd loaded: " << m_circleLightMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Inferno ring model (Main 5.2: MODEL_SKILL_INFERNO = Data/Skill/Inferno01.bmd)
  m_infernoBmd = BMDParser::Parse(skillPath + "Inferno01.bmd");
  if (m_infernoBmd) {
    auto bones = ComputeBoneMatrices(m_infernoBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_infernoBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_infernoMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Inferno01.bmd loaded: " << m_infernoMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Evil Spirit beam head flash (Main 5.2: MODEL_LASER = Data/Skill/Laser01.bmd)
  m_laserBmd = BMDParser::Parse(skillPath + "Laser01.bmd");
  if (m_laserBmd) {
    auto bones = ComputeBoneMatrices(m_laserBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_laserBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_laserMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Laser01.bmd loaded: " << m_laserMeshes.size()
              << " meshes" << std::endl;
  }

  // Rageful Blow: EarthQuake01-08.bmd (Main 5.2: MODEL_SKILL_FURY_STRIKE+1..+8)
  // Types used: 1,2,3 (center), 4,5 (radial arms), 7,8 (branching extensions)
  // Type 6 reuses type 4 meshes (same model)
  {
    const int eqTypes[] = {1, 2, 3, 4, 5, 7, 8};
    for (int t : eqTypes) {
      std::string filename = skillPath + "EarthQuake0" + std::to_string(t) + ".bmd";
      m_eqBmd[t] = BMDParser::Parse(filename);
      if (m_eqBmd[t]) {
        auto bones = ComputeBoneMatrices(m_eqBmd[t].get(), 0, 0);
        AABB dummyAABB{};
        for (auto &mesh : m_eqBmd[t]->Meshes) {
          UploadMeshWithBones(mesh, skillPath, bones, m_eqMeshes[t],
                              dummyAABB, false);
        }
        std::cout << "[VFX] EarthQuake0" << t << ".bmd loaded: "
                  << m_eqMeshes[t].size() << " meshes" << std::endl;
      } else {
        std::cerr << "[VFX] FAILED to parse EarthQuake0" << t << ".bmd" << std::endl;
      }
    }
  }

  // Stone debris (Main 5.2: MODEL_GROUND_STONE)
  m_stone1Bmd = BMDParser::Parse(skillPath + "GroundStone.bmd");
  if (m_stone1Bmd) {
    auto bones = ComputeBoneMatrices(m_stone1Bmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stone1Bmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stone1Meshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] GroundStone.bmd loaded: " << m_stone1Meshes.size()
              << " meshes" << std::endl;
  } else {
    std::cerr << "[VFX] FAILED to parse GroundStone.bmd" << std::endl;
  }

  // Stone debris variant (Main 5.2: MODEL_GROUND_STONE2)
  m_stone2Bmd = BMDParser::Parse(skillPath + "GroundStone2.bmd");
  if (m_stone2Bmd) {
    auto bones = ComputeBoneMatrices(m_stone2Bmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stone2Bmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stone2Meshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] GroundStone2.bmd loaded: " << m_stone2Meshes.size()
              << " meshes" << std::endl;
  } else {
    std::cerr << "[VFX] FAILED to parse GroundStone2.bmd" << std::endl;
  }

  m_modelShader = Shader::Load("vs_model.bin", "fs_model.bin");
  if (!m_modelShader)
    std::cerr << "[VFX] Failed to load BGFX model shader\n";
  initBuffers();
}

void VFXManager::initBuffers() {
  // BGFX billboard quad (4 verts, 6 indices)
  float quadVerts[] = {
      -0.5f, -0.5f, 0.0f,
       0.5f, -0.5f, 0.0f,
       0.5f,  0.5f, 0.0f,
      -0.5f,  0.5f, 0.0f,
  };
  uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

  bgfx::VertexLayout quadLayout;
  quadLayout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .end();
  m_quadVBO = bgfx::createVertexBuffer(
    bgfx::copy(quadVerts, sizeof(quadVerts)), quadLayout);
  m_quadEBO = bgfx::createIndexBuffer(
    bgfx::copy(quadIndices, sizeof(quadIndices)));

  // Ribbon vertex layout: Position(3f) + TexCoord0(2f) = matches vs_line.sc
  m_ribbonLayout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
    .end();

  std::cout << "[VFX] BGFX buffers initialized\n";
}

void VFXManager::SpawnBurst(ParticleType type, const glm::vec3 &position,
                            int count) {
  for (int i = 0; i < count; ++i) {
    if (m_particles.size() >= (size_t)MAX_PARTICLES)
      break;

    Particle p;
    p.type = type;
    p.position = position;
    p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;

    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;

    switch (type) {
    case ParticleType::BLOOD: {
      // Main 5.2: CreateBlood — red spray, gravity-affected
      float speed = 50.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 60),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 15);
      p.maxLifetime = 0.6f + (float)(rand() % 40) / 100.0f;
      p.color = glm::vec3(0.8f, 0.0f, 0.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::HIT_SPARK: {
      // Main 5.2: BITMAP_SPARK — 20 white sparks, gravity, arc trajectory
      // Lifetime 8-15 frames (0.32-0.6s), scale 0.4-0.8 × base ~25
      float speed = 80.0f + (float)(rand() % 120);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 100),
                    std::sin(angle) * speed);
      p.scale = 10.0f + (float)(rand() % 10);
      p.maxLifetime = 0.32f + (float)(rand() % 28) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SMOKE: {
      // Main 5.2: BITMAP_SMOKE — ambient monster smoke, slow rise
      float speed = 10.0f + (float)(rand() % 20);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 20.0f + (float)(rand() % 30),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 20);
      p.maxLifetime = 1.0f + (float)(rand() % 50) / 100.0f;
      p.color = glm::vec3(0.6f, 0.6f, 0.6f);
      p.alpha = 0.6f;
      break;
    }
    case ParticleType::FIRE: {
      // Main 5.2: BITMAP_FIRE_CURSEDLICH — fire breath, upward burst
      // Lifetime 8-20 frames, scale 0.2-0.5, gravity 0.15-0.30
      float speed = 30.0f + (float)(rand() % 40);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.32f + (float)(rand() % 24) / 100.0f;
      p.color = glm::vec3(1.0f, 0.8f, 0.3f);
      p.alpha = 0.55f; // Lower alpha — overlapping additive particles blend softly (no stripes)
      break;
    }
    case ParticleType::ENERGY: {
      // Main 5.2: BITMAP_ENERGY — Lich hand flash, fast fade
      float speed = 40.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 50.0f + (float)(rand() % 30),
                    std::sin(angle) * speed);
      p.scale = 40.0f + (float)(rand() % 20);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.6f, 0.7f, 1.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::FLARE: {
      // Main 5.2: BITMAP_FLASH — bright stationary impact flash
      // Lifetime 8-12 frames (0.3-0.5s), large scale, no movement
      p.velocity = glm::vec3(0.0f);
      p.scale = 80.0f + (float)(rand() % 40);
      p.maxLifetime = 0.3f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f);
      p.alpha = 0.8f;
      break;
    }
    case ParticleType::LEVEL_FLARE: {
      // Main 5.2: BITMAP_FLARE level-up joint — rises upward from ring
      // Scale 40 in original (CreateJoint arg), mapped to our world units
      p.velocity =
          glm::vec3(std::cos(angle) * 30.0f, 80.0f + (float)(rand() % 40),
                    std::sin(angle) * 30.0f);
      p.scale = 50.0f + (float)(rand() % 30);
      p.maxLifetime = 1.2f + (float)(rand() % 40) / 100.0f;
      p.color =
          glm::vec3(1.0f, 0.7f, 0.2f); // Golden-orange (match ground circle)
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_SLASH: {
      // Main 5.2: CreateSpark — BITMAP_SPARK (Spark02.OZJ) white sparks
      // Gravity + terrain bounce, arc trajectory (6-22 per tick² gravity)
      float speed = 100.0f + (float)(rand() % 150);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 80.0f + (float)(rand() % 120),
                    std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 8);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f); // White (Main 5.2: no tint)
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_CYCLONE: {
      // Main 5.2: Spinning ring of cyan sparks (evenly spaced + jitter)
      float ringAngle =
          (float)i / (float)std::max(count, 1) * 6.2832f + angle * 0.3f;
      float speed = 60.0f + (float)(rand() % 40);
      p.velocity =
          glm::vec3(std::cos(ringAngle) * speed,
                    30.0f + (float)(rand() % 40),
                    std::sin(ringAngle) * speed);
      p.scale = 15.0f + (float)(rand() % 12);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.3f, 0.9f, 1.0f); // Cyan-teal
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_FURY: {
      // Main 5.2: CreateEffect(MODEL_SKILL_FURY_STRIKE) — ground burst
      float speed = 40.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 150.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 40.0f + (float)(rand() % 30);
      p.maxLifetime = 0.5f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 0.5f, 0.15f); // Orange-red
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_STAB: {
      // Main 5.2: Piercing directional sparks — narrow cone, fast, dark red
      float spread = 0.4f; // Narrow cone
      float fwdAngle = angle * spread;
      float speed = 150.0f + (float)(rand() % 100);
      p.velocity =
          glm::vec3(std::cos(fwdAngle) * speed,
                    20.0f + (float)(rand() % 30),
                    std::sin(fwdAngle) * speed);
      p.scale = 10.0f + (float)(rand() % 8);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.9f, 0.2f, 0.2f); // Dark red
      p.alpha = 1.0f;
      break;
    }
    // ── DW Spell particles ──
    case ParticleType::SPELL_ENERGY: {
      // Blue-white energy burst — medium speed outward, rising
      float speed = 60.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 80.0f + (float)(rand() % 60),
                    std::sin(angle) * speed);
      p.scale = 25.0f + (float)(rand() % 15);
      p.maxLifetime = 0.35f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.5f, 0.7f, 1.0f); // Blue-white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_FIRE: {
      // Orange-yellow fire burst — fast upward, wide spread
      float speed = 50.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 35.0f + (float)(rand() % 25);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 0.6f, 0.15f); // Orange-yellow
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_FLAME: {
      // Main 5.2: BITMAP_FLAME — 20 tick lifetime (0.8s), white light (texture handles color)
      // Velocity[2] = (rand()%128+128)*0.15 = 19.2-38.4 upward per tick
      // Scale = Scale*(rand()%64+64)*0.01 = 64-128% random
      float offX = (float)(rand() % 50) - 25.0f;
      float offZ = (float)(rand() % 50) - 25.0f;
      float upSpeed = (float)(rand() % 128 + 128) * 0.15f * 25.0f; // Convert per-tick to per-sec
      p.velocity = glm::vec3(offX * 0.3f, upSpeed, offZ * 0.3f);
      p.scale = 30.0f * (float)(rand() % 64 + 64) * 0.01f; // 19.2-38.4
      p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      p.frame = -1.0f; // Full texture UV
      p.maxLifetime = 0.8f; // 20 ticks @ 25fps
      // Main 5.2: Light is white — let Flame01.OZJ texture provide orange color
      float lum = (float)(rand() % 4 + 8) * 0.1f; // 0.8-1.1 flicker
      p.color = glm::vec3(lum, lum, lum);
      p.alpha = 1.0f;
      p.position += glm::vec3(offX, 0.0f, offZ);
      break;
    }
    case ParticleType::SPELL_ICE: {
      // Cyan-white ice shards — fast outward, slight rise, quick fade
      float speed = 100.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 30.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 10);
      p.maxLifetime = 0.3f + (float)(rand() % 15) / 100.0f;
      p.color = glm::vec3(0.7f, 0.95f, 1.0f); // Cyan-white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_LIGHTNING: {
      // Bright white-blue electric sparks — very fast, erratic, short life
      float speed = 180.0f + (float)(rand() % 120);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 10.0f + (float)(rand() % 8);
      p.maxLifetime = 0.15f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.8f, 0.9f, 1.0f); // White-blue
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_POISON: {
      // Green toxic cloud — slow drift, long lifetime, large particles
      float speed = 20.0f + (float)(rand() % 30);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 15.0f + (float)(rand() % 20),
                    std::sin(angle) * speed);
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.6f + (float)(rand() % 30) / 100.0f;
      p.color = glm::vec3(0.2f, 0.8f, 0.15f); // Toxic green
      p.alpha = 0.7f;
      break;
    }
    case ParticleType::SPELL_METEOR: {
      // Dark orange falling sparks — fast downward + outward
      float speed = 80.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 180.0f + (float)(rand() % 100),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 15);
      p.maxLifetime = 0.5f + (float)(rand() % 25) / 100.0f;
      p.color = glm::vec3(1.0f, 0.4f, 0.1f); // Dark orange
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_DARK: {
      // Purple-black dark energy — medium speed, swirling
      float ringAngle =
          (float)i / (float)std::max(count, 1) * 6.2832f + angle * 0.5f;
      float speed = 50.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(ringAngle) * speed,
                    40.0f + (float)(rand() % 50),
                    std::sin(ringAngle) * speed);
      p.scale = 25.0f + (float)(rand() % 20);
      p.maxLifetime = 0.45f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.6f, 0.2f, 0.9f); // Purple
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::SPELL_WATER: {
      // Blue water spray — medium outward, moderate rise
      float speed = 70.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 50.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 18.0f + (float)(rand() % 12);
      p.maxLifetime = 0.35f + (float)(rand() % 15) / 100.0f;
      p.color = glm::vec3(0.2f, 0.5f, 1.0f); // Deep blue
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::SPELL_TELEPORT: {
      // Vertical rising spark column — blue energy
      p.position.y += (float)(rand() % 80); // Vertical spread (column)
      float drift = 15.0f + (float)(rand() % 10);
      p.velocity = glm::vec3(
          std::cos(angle) * drift,
          80.0f + (float)(rand() % 40), // Strong upward rise
          std::sin(angle) * drift);
      p.scale = i == 0 ? 40.0f : (20.0f + (float)(rand() % 15));
      p.maxLifetime = 0.5f;
      p.color = glm::vec3(0.3f, 0.5f, 1.0f); // Blue energy
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_ENERGY_ORB: {
      // Main 5.2: BITMAP_ENERGY (Thunder01.jpg) — energy ball orb/swirl
      // Created directly with specific params in updateSpellProjectiles,
      // but this handles SpawnBurst fallback
      p.velocity = glm::vec3(0.0f);
      p.scale = 80.0f + (float)(rand() % 40);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.4f, 0.6f, 1.0f); // Blue
      p.alpha = 1.0f;
      p.frame = -1.0f; // Full texture UV
      break;
    }
    case ParticleType::INFERNO_SPARK: {
      // Main 5.2: BITMAP_SPARK SubType 2 — 2x scale, 3x velocity, long-lived
      // Lifetime 24-39 ticks (0.96-1.56s), gravity 6-21, bounce on terrain
      float speed = 240.0f + (float)(rand() % 360); // 3x base velocity
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 300.0f + (float)(rand() % 300),
                    std::sin(angle) * speed);
      p.scale = 25.0f + (float)(rand() % 20); // 2x HIT_SPARK scale (25-45)
      p.maxLifetime = 0.96f + (float)(rand() % 60) / 100.0f; // 0.96-1.56s
      p.color = glm::vec3(1.0f, 0.95f, 0.85f); // Warm white (blends into fire)
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::INFERNO_EXPLOSION: {
      // Main 5.2: BITMAP_EXPLOTION — 4x4 animated explosion sprite sheet
      // Lifetime 20 ticks (0.8s), large scale, stationary
      p.velocity = glm::vec3(0.0f, 30.0f, 0.0f); // Slight upward drift
      p.scale = 120.0f + (float)(rand() % 60); // Large explosion flash (120-180)
      p.maxLifetime = 0.8f; // 20 ticks
      p.color = glm::vec3(1.0f, 0.9f, 0.7f); // Warm white-orange
      p.alpha = 1.0f;
      p.frame = 100.0f; // Start at cell 0 of 4x4 grid (100+ encoding in shader)
      break;
    }
    case ParticleType::INFERNO_FIRE: {
      // Dedicated Inferno fire — uses inferno.OZJ texture, rising column
      float offX = (float)(rand() % 60) - 30.0f;
      float offZ = (float)(rand() % 60) - 30.0f;
      float upSpeed = (float)(rand() % 100 + 150) * 1.0f; // 150-250 up
      p.velocity = glm::vec3(offX * 0.4f, upSpeed, offZ * 0.4f);
      p.scale = 40.0f + (float)(rand() % 30); // 40-70 (large fire)
      p.maxLifetime = 0.6f + (float)(rand() % 30) / 100.0f; // 0.6-0.9s
      p.color = glm::vec3(1.0f, 0.8f, 0.5f); // Warm fire tint
      p.alpha = 1.0f;
      p.frame = -1.0f; // Full texture
      p.position += glm::vec3(offX, 0.0f, offZ);
      break;
    }
    case ParticleType::DEATHSTAB_SPARK:
      // This type is spawned directly (not via SpawnBurst)
      break;
    case ParticleType::PET_SPARKLE: {
      // Main 5.2: BITMAP_SPARK SubType 1 — stationary, small, fading white dot
      // Direction=(0,0,0), Velocity=0.3, Light=(0.4,0.4,0.4)
      p.velocity = glm::vec3(0.0f);       // Stationary
      p.scale = 4.0f + (float)(rand() % 4); // Small (4-8 units)
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f; // 0.2-0.3s
      p.color = glm::vec3(0.5f, 0.5f, 0.5f); // Neutral gray-white
      p.alpha = 0.8f;
      break;
    }
    case ParticleType::IMP_SPARKLE: {
      // Imp companion — dark red/orange embers, slight upward drift
      p.velocity = glm::vec3(
          (float)(rand() % 10 - 5) * 0.5f,
          (float)(rand() % 30 + 10) * 1.0f, // Gentle upward float (10-40)
          (float)(rand() % 10 - 5) * 0.5f);
      p.scale = 3.0f + (float)(rand() % 4); // Small (3-7 units)
      p.maxLifetime = 0.3f + (float)(rand() % 15) / 100.0f; // 0.3-0.45s
      p.color = glm::vec3(1.0f, 0.4f + (float)(rand() % 3) * 0.1f, 0.1f); // Red-orange
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::MOUNT_DUST: {
      // Main 5.2: BITMAP_SMOKE hoofbeat dust — low, slow spread, brownish
      // Position offset ±32/±32/±16 applied by caller
      float speed = 8.0f + (float)(rand() % 15);
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             10.0f + (float)(rand() % 15), // Gentle rise
                             std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 20); // 12-32 units (Main 5.2: 0.48-0.80 * 40)
      p.maxLifetime = 0.5f + (float)(rand() % 30) / 100.0f; // 0.5-0.8s
      p.color = glm::vec3(0.7f, 0.65f, 0.55f); // Brownish dust
      p.alpha = 0.5f;
      break;
    }
    case ParticleType::DUNGEON_MIST: {
      // Void edge mist — massive fog billboards covering entire void area
      float drift = 1.0f + (float)(rand() % 2);
      p.velocity = glm::vec3(
          std::cos(angle) * drift, 0.0f, std::sin(angle) * drift);
      p.scale = 800.0f + (float)(rand() % 400); // 800-1200 unit billboards
      p.maxLifetime = 12.0f + (float)(rand() % 600) / 100.0f; // 12-18s
      p.color = glm::vec3(0.15f, 0.13f, 0.12f); // visible dark grey fog
      p.alpha = 0.0f;
      break;
    }
    case ParticleType::SET_WATERFALL: {
      // Main 5.2: BITMAP_WATERFALL_2 SubType 3 — rising columnar energy stream
      // Rises upward, fades out, shrinks. Used for full armor set bonus effect.
      p.velocity = glm::vec3((float)(rand() % 6 - 3),  // slight X drift
                             30.0f + (float)(rand() % 20), // upward rise
                             (float)(rand() % 6 - 3)); // slight Z drift
      p.scale = 8.0f + (float)(rand() % 6);
      p.maxLifetime = 0.8f + (float)(rand() % 4) / 10.0f; // 0.8-1.2s
      // Color set by caller via SetParticleColor or defaults to set glow color
      p.color = glm::vec3(0.5f, 0.7f, 1.0f); // Default blue-white (overridden)
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::BUFF_AURA: {
      // Main 5.2: MODEL_SPEARSKILL SubType 4 — orbiting energy particles
      // Position set by caller (pre-computed orbit position around character)
      // Gentle upward drift + slight outward spread, then fade
      p.velocity = glm::vec3((float)(rand() % 8 - 4),
                             15.0f + (float)(rand() % 10),
                             (float)(rand() % 8 - 4));
      p.scale = 6.0f + (float)(rand() % 8); // 6-14 units
      p.maxLifetime = 0.6f + (float)(rand() % 4) / 10.0f; // 0.6-1.0s
      p.color = glm::vec3(0.4f, 1.0f, 0.6f); // Default green (overridden)
      p.alpha = 0.85f;
      break;
    }
    case ParticleType::TRAP_LIGHTNING: {
      // MuSven: BITMAP_LIGHTNING+1 — rotating cross sprite at trap center
      // Nearly stationary, slow fade, large scale. Rotation set by caller.
      p.velocity = glm::vec3(0.0f);
      p.scale = 80.0f; // Large cross sprite (overridden by caller)
      p.maxLifetime = 0.12f; // Short — redrawn every frame by trap BodyLight
      p.color = glm::vec3(0.4f, 0.8f, 1.0f); // Blue-cyan (overridden by caller)
      p.alpha = 0.9f;
      p.frame = -1.0f; // Full texture UV (not sprite sheet)
      break;
    }
    case ParticleType::TRAP_GLOW: {
      // MuSven: BITMAP_LIGHT — steady glow point at lance blade tips
      // Stationary, quick fade (redrawn each frame), moderate scale.
      p.velocity = glm::vec3(0.0f);
      p.scale = 30.0f; // Glow point size
      p.maxLifetime = 0.12f; // Redrawn each frame
      p.color = glm::vec3(1.0f, 1.0f, 1.0f); // White
      p.alpha = 0.8f;
      break;
    }
    case ParticleType::TRAP_SHINY: {
      // MuSven: BITMAP_SHINY — sparkle particle at blade tips (1/32 chance)
      // Short lived, stationary, small bright flash
      p.velocity = glm::vec3((float)(rand() % 10 - 5),
                             (float)(rand() % 20 + 5),
                             (float)(rand() % 10 - 5));
      p.scale = 12.0f + (float)(rand() % 8); // 12-20
      p.maxLifetime = 0.3f + (float)(rand() % 15) / 100.0f; // 0.3-0.45s
      p.color = glm::vec3(0.8f, 0.9f, 1.0f); // Cool white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::MONSTER_SHINY: {
      // Shadow aura: foggy violet mist per bone.
      // Large scale + long lifetime + Gaussian radial falloff (shader) = soft fog cloud.
      p.velocity = glm::vec3(0.0f);
      p.scale = 100.0f;
      p.maxLifetime = 0.25f;
      p.color = glm::vec3(0.55f, 0.45f, 0.75f);
      p.alpha = 0.25f;
      break;
    }
    case ParticleType::MONSTER_MAGIC: {
      // Poison Shadow aura: foggy green mist per bone.
      p.velocity = glm::vec3(0.0f);
      p.scale = 90.0f;
      p.maxLifetime = 0.25f;
      p.color = glm::vec3(0.2f, 0.7f, 0.1f);
      p.alpha = 0.25f;
      break;
    }
    }

    p.lifetime = p.maxLifetime;
    m_particles.push_back(p);
  }
}

void VFXManager::SpawnBurstColored(ParticleType type, const glm::vec3 &position,
                                    const glm::vec3 &color, int count) {
  // Spawn particles then override their color
  size_t before = m_particles.size();
  SpawnBurst(type, position, count);
  for (size_t i = before; i < m_particles.size(); ++i) {
    m_particles[i].color = color;
  }
}

void VFXManager::SpawnTrapSprite(ParticleType type, const glm::vec3 &position,
                                  const glm::vec3 &color, float rotation,
                                  float scale) {
  // Spawn a single particle with explicit rotation and scale.
  // Used for MuSven BodyLight rotating cross sprites (BITMAP_LIGHTNING+1).
  SpawnBurstColored(type, position, color, 1);
  if (!m_particles.empty()) {
    auto &p = m_particles.back();
    p.rotation = rotation;
    p.scale = scale;
  }
}

void VFXManager::SpawnSkillCast(uint8_t skillId, const glm::vec3 &heroPos,
                                float facing, const glm::vec3 &targetPos) {
  glm::vec3 castPos = heroPos + glm::vec3(0, 50, 0); // Chest height
  switch (skillId) {
  case 19:
  case 20:
  case 21:
  case 23: // Sword skills (Falling Slash, Lunge, Uppercut, Slash)
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    SpawnBurst(ParticleType::HIT_SPARK, castPos, 8);
    break;
  case 22: // Cyclone
    SpawnBurst(ParticleType::SKILL_CYCLONE, heroPos + glm::vec3(0, 30, 0), 20);
    break;
  case 41: // Twisting Slash — ghost weapons handled by HeroCharacter
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 42: // Rageful Blow
    SpawnRagefulBlow(heroPos, facing);
    break;
  case 43: // Death Stab — Main 5.2: MODEL_SPEARSKILL converging + MODEL_SPEAR trail
    SpawnDeathStab(heroPos, facing, targetPos);
    break;
  // DW Spells
  case 17: // Energy Ball
    SpawnBurst(ParticleType::SPELL_ENERGY, castPos, 12);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 4: // Fire Ball — Main 5.2: no caster-side particles, only projectile + sound
    break;
  case 1: // Poison — Main 5.2: no caster-side VFX, cloud spawns at target
    break;
  case 3: // Lightning
    SpawnBurst(ParticleType::SPELL_LIGHTNING, castPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 2: // Meteorite — Main 5.2: no caster-side VFX, fireball spawns at target
    break;
  case 7: // Ice — Main 5.2: no billboard particles on cast, just flare
    SpawnBurst(ParticleType::FLARE, castPos, 1);
    break;
  case 5: // Flame — lighter caster-side glow, main fire at target
    SpawnBurst(ParticleType::SPELL_FLAME, castPos, 8);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 8: // Twister — Main 5.2: cast-side sparkle only
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 6: // Teleport — Main 5.2: BITMAP_SPARK+1 white rising column at feet
    SpawnBurst(ParticleType::SPELL_TELEPORT, heroPos, 18);
    break;
  case 9: // Evil Spirit — beams spawned separately, just a small cast flash
    SpawnBurst(ParticleType::SPELL_DARK, castPos, 10);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 12: // Aqua Beam
    SpawnBurst(ParticleType::SPELL_WATER, castPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 10: // Hellfire — cast flash (main VFX from SpawnHellfire)
    SpawnBurst(ParticleType::SPELL_FIRE, castPos, 15);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 11: // Power Wave — Main 5.2: MODEL_MAGIC2 ground wave, cast glow
    SpawnBurst(ParticleType::SPELL_ENERGY, castPos, 10);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 13: // Cometfall — no cast-side particles (Main 5.2: none for Blast)
    break;
  case 14: // Inferno — no cast-side particles (Main 5.2: ring IS the effect)
    break;
  }
}

void VFXManager::SpawnSkillImpact(uint8_t skillId,
                                  const glm::vec3 &monsterPos) {
  glm::vec3 hitPos = monsterPos + glm::vec3(0, 50, 0);
  switch (skillId) {
  case 19:
  case 20:
  case 21:
  case 22:
  case 23: { // Basic DK sword skills — Main 5.2: CreateSpark (1x BITMAP_SPARK+1 + 20x BITMAP_SPARK)
    SpawnBurst(ParticleType::SKILL_SLASH, hitPos, 20);  // 20x Spark02.OZJ small sparks
    SpawnBurst(ParticleType::FLARE, hitPos, 1);          // 1x bright flash (Spark03 stand-in)
    break;
  }
  case 41: // Twisting Slash
    SpawnBurst(ParticleType::SKILL_CYCLONE, hitPos, 20);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 15);
    break;
  case 42: // Rageful Blow — ground burst + hit sparks
    SpawnBurst(ParticleType::SKILL_FURY, hitPos, 8);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 6);
    break;
  case 43: // Death Stab — Main 5.2: m_byHurtByOneToOne = 35 (target electrocution)
    SpawnDeathStabShock(monsterPos);
    SpawnBurst(ParticleType::FLARE, hitPos, 3); // Impact flash
    break;
  // DW Spell impacts
  case 17: // Energy Ball — Main 5.2: BITMAP_MAGIC impact + radial bursts
    SpawnBurst(ParticleType::SPELL_ENERGY, hitPos, 25);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 10);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 4: // Fire Ball — Main 5.2: impact handled by projectile collision
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 11: // Power Wave — ground burst on impact
    SpawnBurst(ParticleType::SPELL_ENERGY, hitPos, 20);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 8);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 1: // Poison — cloud already spawned at cast time, skip duplicate
    break;
  case 3: // Lightning — electric shock on monster (no sky bolt)
    SpawnBurst(ParticleType::DEATHSTAB_SPARK, hitPos, 15);    // Cyan electric arcs on body
    SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 20);     // Electric sparks
    SpawnBurst(ParticleType::FLARE, hitPos, 2);                // Brief flash
    break;
  case 2: // Meteorite — falling fireball (impact particles handled by bolt system)
    SpawnMeteorStrike(hitPos);
    break;
  case 7: // Ice — Main 5.2: MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
    SpawnIceStrike(hitPos);
    SpawnBurst(ParticleType::FLARE, hitPos, 1);
    break;
  case 5: // Flame — persistent ground fire at target
    SpawnFlameGround(monsterPos);
    break;
  case 8: // Twister — Main 5.2: tornado spawned at caster, just hit sparks here
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 8);
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 5);
    break;
  case 9: // Evil Spirit — hit sparks + dark energy
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 10);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 6: // Teleport — Main 5.2: white rising sparks at destination (feet level)
    SpawnBurst(ParticleType::SPELL_TELEPORT, monsterPos, 18);
    break;
  case 12: // Aqua Beam
    SpawnBurst(ParticleType::SPELL_WATER, hitPos, 25);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 10: // Hellfire (large AoE)
    SpawnBurst(ParticleType::SPELL_FIRE, hitPos, 30);
    SpawnBurst(ParticleType::SPELL_METEOR, hitPos, 15);
    SpawnBurst(ParticleType::FLARE, hitPos, 5);
    break;
  }
}

void VFXManager::SpawnSpellProjectile(uint8_t skillId, const glm::vec3 &start,
                                      const glm::vec3 &target) {
  SpellProjectile proj;
  // Start position comes from bone world pos (already at hand height)
  // Target gets chest-height offset since target pos is at feet
  static constexpr float TARGET_HEIGHT = 120.0f;
  proj.position = start;
  proj.target = target + glm::vec3(0, TARGET_HEIGHT, 0);

  // Purely horizontal direction — Main 5.2: Direction {0,-50,0} rotated by yaw only
  glm::vec3 delta = proj.target - proj.position;
  delta.y = 0.0f; // Horizontal flight
  float dist = glm::length(delta);
  if (dist < 1.0f)
    return;

  proj.direction = delta / dist;
  // Main 5.2: Direction=(0,-50,0) = 50 units/frame × 25fps = 1250 units/sec
  proj.speed = 1250.0f;
  proj.rotation = 0.0f;
  proj.rotSpeed = 0.0f;
  proj.yaw = std::atan2(delta.x, delta.z);
  proj.pitch = 0.0f;
  // Main 5.2: Fire Ball LifeTime=60 ticks (2.4s), distance-based for variable range
  proj.maxLifetime = std::min(2.4f, dist / proj.speed + 0.1f);
  proj.lifetime = proj.maxLifetime;
  proj.trailTimer = 0.0f;
  proj.alpha = 1.0f;
  proj.skillId = skillId;

  // Main 5.2: Only Energy Ball + Fire Ball are actual traveling projectiles.
  // Poison/Ice/Meteorite create instant effects at target.
  switch (skillId) {
  case 17: // Energy Ball — Main 5.2: BITMAP_ENERGY, blue-dominant light
    proj.scale = 40.0f;
    proj.color = glm::vec3(0.4f, 0.6f, 1.0f); // Blue-dominant (0.2R, 0.4G, 1.0B)
    proj.trailType = ParticleType::SPELL_ENERGY;
    break;
  case 11: // Power Wave — Main 5.2: MODEL_MAGIC2, white-blue ground wave
    proj.scale = 50.0f;
    proj.color = glm::vec3(0.6f, 0.8f, 1.0f); // White-blue
    proj.trailType = ParticleType::SPELL_ENERGY;
    break;
  case 4: { // Fire Ball — Main 5.2: MODEL_FIRE 3D model + particle trail
    // Main 5.2: Scale = (rand()%4+8)*0.1f = 0.8-1.1 random per cast
    float rndScale = (float)(rand() % 4 + 8) * 0.1f;
    proj.scale = m_fireMeshes.empty() ? 45.0f : rndScale;
    // Slight color variation: orange shifts between warm/hot
    float rG = 0.5f + (float)(rand() % 20) * 0.01f;  // 0.50-0.69
    float rB = 0.10f + (float)(rand() % 10) * 0.01f;  // 0.10-0.19
    proj.color = glm::vec3(1.0f, rG, rB);
    proj.trailType = ParticleType::SPELL_FIRE;
    // Main 5.2: Fire Ball faces target, no visual spin
    proj.rotSpeed = 0.0f;
    proj.rotation = 0.0f;
    break;
  }
  default:
    proj.scale = 35.0f;
    proj.color = glm::vec3(0.4f, 0.6f, 1.0f);
    proj.trailType = ParticleType::SPELL_ENERGY;
    break;
  }

  m_spellProjectiles.push_back(proj);
}

void VFXManager::SpawnRibbon(const glm::vec3 &start, const glm::vec3 &target,
                             float scale, const glm::vec3 &color,
                             float duration) {
  Ribbon r;
  r.headPos = start;
  r.targetPos = target;
  r.scale = scale;
  r.color = color;
  r.lifetime = duration;
  r.maxLifetime = duration;
  r.velocity = 1500.0f; // Fast travel speed (world units/sec)
  r.uvScroll = 0.0f;

  // Initialize heading toward target
  glm::vec3 dir = target - start;
  float dist = glm::length(dir);
  if (dist > 0.01f) {
    dir /= dist;
    r.headYaw = std::atan2(dir.x, dir.z);
    r.headPitch = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
  }

  m_ribbons.push_back(std::move(r));
}


void VFXManager::updateRibbon(Ribbon &r, float dt) {
  // Steer head toward target (Main 5.2: MoveHumming with max turn = 50 deg)
  glm::vec3 toTarget = r.targetPos - r.headPos;
  float dist = glm::length(toTarget);

  if (dist > 1.0f) {
    glm::vec3 desiredDir = toTarget / dist;
    float desiredYaw = std::atan2(desiredDir.x, desiredDir.z);
    float desiredPitch = std::asin(glm::clamp(desiredDir.y, -1.0f, 1.0f));

    // Max turn rate: 50 degrees/tick * 25 fps = 1250 deg/sec
    float maxTurn = 1250.0f * 3.14159f / 180.0f * dt;

    // Steer yaw
    float yawDiff = desiredYaw - r.headYaw;
    // Normalize to [-pi, pi]
    while (yawDiff > 3.14159f)
      yawDiff -= 2.0f * 3.14159f;
    while (yawDiff < -3.14159f)
      yawDiff += 2.0f * 3.14159f;
    r.headYaw += glm::clamp(yawDiff, -maxTurn, maxTurn);

    // Steer pitch
    float pitchDiff = desiredPitch - r.headPitch;
    r.headPitch += glm::clamp(pitchDiff, -maxTurn, maxTurn);
  }

  // Add random jitter (Main 5.2: rand()%256 - 128 on X and Z per tick)
  float jitterScale = dt * 25.0f; // Scale to tick rate
  float jitterX = ((float)(rand() % 256) - 128.0f) * jitterScale;
  float jitterZ = ((float)(rand() % 256) - 128.0f) * jitterScale;

  // Compute forward direction from yaw/pitch + jitter
  float cy = std::cos(r.headYaw), sy = std::sin(r.headYaw);
  float cp = std::cos(r.headPitch), sp = std::sin(r.headPitch);
  glm::vec3 forward(sy * cp, sp, cy * cp);

  // Move head forward
  r.headPos += forward * r.velocity * dt;
  r.headPos.x += jitterX;
  r.headPos.z += jitterZ;

  // Scroll UV (Main 5.2: WorldTime % 1000 / 1000)
  r.uvScroll += dt;

  // Build cross-section at head position
  // Main 5.2: 4 corners at ±Scale/2 in local X and Z, rotated by heading
  glm::vec3 right(cy, 0.0f, -sy); // Perpendicular to forward in XZ
  glm::vec3 up(0.0f, 1.0f, 0.0f); // Vertical

  RibbonSegment seg;
  seg.center = r.headPos;
  seg.right = right * (r.scale * 0.5f);
  seg.up = up * (r.scale * 0.5f);

  // Prepend new segment (newest at front)
  r.segments.insert(r.segments.begin(), seg);
  if ((int)r.segments.size() > Ribbon::MAX_SEGMENTS)
    r.segments.resize(Ribbon::MAX_SEGMENTS);

  // Decrease lifetime
  r.lifetime -= dt;
}

void VFXManager::updateSpellProjectiles(float dt) {
  for (int i = (int)m_spellProjectiles.size() - 1; i >= 0; --i) {
    auto &p = m_spellProjectiles[i];
    p.lifetime -= dt;

    // Main 5.2: CheckTargetRange — impact when within 100 units XZ of target
    glm::vec3 toTarget = p.target - p.position;
    float distXZ = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (p.lifetime <= 0.0f || distXZ <= 100.0f) {
      glm::vec3 impactPos = p.position; // Explode at fireball's actual position

      if (p.skillId == 4) {
        // Fire Ball — Main 5.2: 2× MODEL_STONE debris on impact
        // Substitute with sparks + fire burst (no stone model)
        SpawnBurst(ParticleType::HIT_SPARK, impactPos, 8);
        SpawnBurst(ParticleType::SPELL_FIRE, impactPos, 6);
        SpawnBurst(ParticleType::FLARE, impactPos, 2);
      } else {
        // Energy Ball — rich energy orb explosion
        for (int j = 0; j < 10; j++) {
          Particle spark;
          spark.type = ParticleType::SPELL_ENERGY_ORB;
          spark.position = impactPos;
          float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
          float speed = 60.0f + (float)(rand() % 150);
          spark.velocity =
              glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 120),
                        std::sin(angle) * speed);
          spark.scale = 35.0f + (float)(rand() % 30);
          spark.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          spark.frame = -1.0f;
          spark.lifetime = 0.35f + (float)(rand() % 15) * 0.01f;
          spark.maxLifetime = spark.lifetime;
          spark.color = p.color;
          spark.alpha = 1.0f;
          m_particles.push_back(spark);
        }
        // Bright impact flash
        {
          Particle flash;
          flash.type = ParticleType::FLARE;
          flash.position = impactPos;
          flash.velocity = glm::vec3(0);
          flash.scale = 150.0f;
          flash.rotation = 0.0f;
          flash.frame = -1.0f;
          flash.lifetime = 0.3f;
          flash.maxLifetime = 0.3f;
          flash.color = p.color;
          flash.alpha = 1.0f;
          m_particles.push_back(flash);
        }
        // Secondary smaller flash
        {
          Particle flash2;
          flash2.type = ParticleType::FLARE;
          flash2.position = impactPos;
          flash2.velocity = glm::vec3(0);
          flash2.scale = 100.0f;
          flash2.rotation = 0.785f;
          flash2.frame = -1.0f;
          flash2.lifetime = 0.2f;
          flash2.maxLifetime = 0.2f;
          flash2.color = glm::vec3(0.6f, 0.8f, 1.0f);
          flash2.alpha = 1.0f;
          m_particles.push_back(flash2);
        }
      }

      m_spellProjectiles[i] = m_spellProjectiles.back();
      m_spellProjectiles.pop_back();
      continue;
    }

    // Move toward target
    p.position += p.direction * p.speed * dt;

    // Visual spin rotation
    p.rotation += p.rotSpeed * dt;

    // Main 5.2: Luminosity = LifeTime * 0.2 (fades from ~4.0 to 0.0 over 20 ticks)
    float ticksRemaining = p.lifetime * 25.0f;
    if (p.skillId == 4) {
      // Fire Ball 3D model: stay bright for most of flight, quick fade at end
      float t = p.lifetime / p.maxLifetime; // 1.0 at start, 0.0 at end
      p.alpha = t < 0.1f ? t * 10.0f : 1.0f;
    } else {
      float luminosity = ticksRemaining * 0.2f;
      p.alpha = std::min(luminosity, 1.0f);
    }

    // Animate Fire01.bmd (Main 5.2: Velocity=0.3 = 0.3 keys/tick = 7.5 keys/sec)
    bool has3DModel = (p.skillId == 4 && !m_fireMeshes.empty() && m_modelShader);
    if (has3DModel && m_fireBmd && !m_fireBmd->Actions.empty()) {
      p.animFrame += 7.5f * dt;
      int numKeys = m_fireBmd->Actions[0].NumAnimationKeys;
      if (numKeys > 1 && p.animFrame >= (float)numKeys)
        p.animFrame = std::fmod(p.animFrame, (float)numKeys);
      // Re-skin fire model with animated bones
      auto bones = ComputeBoneMatricesInterpolated(m_fireBmd.get(), 0, p.animFrame);
      for (int mi = 0; mi < (int)m_fireMeshes.size() && mi < (int)m_fireBmd->Meshes.size(); ++mi)
        RetransformMeshWithBones(m_fireBmd->Meshes[mi], bones, m_fireMeshes[mi]);
    }
    glm::vec3 projVel = p.direction * p.speed;
    // For 3D model: backward drift so particles trail behind the fire ball
    glm::vec3 trailDrift = has3DModel ? -p.direction * (p.speed * 0.15f)
                                       : projVel; // billboard: match projectile

    p.trailTimer += dt;
    if (p.trailTimer >= 0.04f && m_particles.size() < (size_t)MAX_PARTICLES - 4) {
      p.trailTimer -= 0.04f;

      if (has3DModel) {
        // Main 5.2: 1× BITMAP_FIRE SubType 5 per tick at fireball position
        // LifeTime=24 ticks (0.96s), Scale=Scale*(rand%64+128)*0.01
        // Velocity=(0,-(rand%16+32)*0.1,0), Frame=(23-LifeTime)/6 (4-frame sprite)
        {
          Particle trail;
          trail.type = ParticleType::SPELL_FIRE;
          trail.position = p.position;
          // Main 5.2: Velocity=(0,-(rand%16+32)*0.1,0) — slight backward drift
          // In Y-up: backward along projectile direction + slight random spread
          float driftSpeed = (float)(rand() % 16 + 32) * 0.1f * 25.0f; // per-tick → per-sec
          trail.velocity = -p.direction * driftSpeed
                         + glm::vec3((float)(rand()%20-10), 0.0f, (float)(rand()%20-10));
          // Main 5.2: Scale = parentScale * (rand%64+128)*0.01 = 1.28-1.92x
          float scaleMul = (float)(rand() % 64 + 128) * 0.01f;
          trail.scale = 30.0f * scaleMul;
          trail.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          trail.frame = 0.0f; // Main 5.2: 4-frame sprite animation
          trail.lifetime = 0.96f;  // Main 5.2: 24 ticks
          trail.maxLifetime = 0.96f;
          // Main 5.2: Light = (L*1.0, L*0.1, 0.0) — red tint
          float lum = 0.8f + (float)(rand() % 3) * 0.1f;
          trail.color = glm::vec3(lum, lum * 0.1f, 0.0f);
          trail.alpha = 1.0f;
          m_particles.push_back(trail);
        }
      } else {
        // --- Billboard-only trail (Energy Ball, fallback) ---

        // 1) Bright center glow — FLARE (core of the ball)
        {
          Particle glow;
          glow.type = ParticleType::FLARE;
          glow.position = p.position;
          glow.velocity = projVel;
          glow.scale = 70.0f;
          glow.rotation = p.rotation;
          glow.frame = -1.0f;
          glow.lifetime = 0.15f;
          glow.maxLifetime = 0.15f;
          glow.color = glm::vec3(0.5f, 0.7f, 1.0f);
          glow.alpha = 1.0f;
          m_particles.push_back(glow);
        }

        // 2) Thunder01 energy overlay — rotating, full UV
        {
          Particle orb;
          orb.type = ParticleType::SPELL_ENERGY_ORB;
          orb.position = p.position;
          orb.velocity = projVel;
          orb.scale = 80.0f * ((float)(rand() % 8 + 6) * 0.1f);
          orb.rotation = p.rotation;
          orb.frame = -1.0f;
          orb.lifetime = 0.20f;
          orb.maxLifetime = 0.20f;
          orb.color = p.color;
          orb.alpha = 1.0f;
          m_particles.push_back(orb);
        }

        // 3) Second energy overlay at 90-degree offset
        {
          Particle orb2;
          orb2.type = ParticleType::SPELL_ENERGY_ORB;
          orb2.position = p.position;
          orb2.velocity = projVel * 0.8f;
          orb2.scale = 60.0f * ((float)(rand() % 8 + 6) * 0.1f);
          orb2.rotation = p.rotation + 1.57f;
          orb2.frame = -1.0f;
          orb2.lifetime = 0.25f;
          orb2.maxLifetime = 0.25f;
          orb2.color = p.color;
          orb2.alpha = 0.8f;
          m_particles.push_back(orb2);
        }

        // 4) Trailing spark
        {
          Particle sp;
          sp.type = ParticleType::HIT_SPARK;
          sp.position = p.position;
          float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
          float speed = 30.0f + (float)(rand() % 60);
          sp.velocity = glm::vec3(std::cos(angle) * speed,
                                  40.0f + (float)(rand() % 40),
                                  std::sin(angle) * speed);
          sp.scale = 20.0f;
          sp.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          sp.frame = -1.0f;
          sp.lifetime = 0.30f;
          sp.maxLifetime = sp.lifetime;
          sp.color = p.color;
          sp.alpha = 1.0f;
          m_particles.push_back(sp);
        }
      }
    }
  }
}

void VFXManager::Update(float deltaTime) {
  // Update particles
  for (int i = (int)m_particles.size() - 1; i >= 0; --i) {
    auto &p = m_particles[i];
    p.lifetime -= deltaTime;
    if (p.lifetime <= 0.0f) {
      m_particles[i] = m_particles.back();
      m_particles.pop_back();
      continue;
    }

    p.position += p.velocity * deltaTime;

    switch (p.type) {
    case ParticleType::BLOOD:
      // Main 5.2: gravity pull, slight shrink
      p.velocity.y -= 400.0f * deltaTime;
      p.scale *= (1.0f - 0.5f * deltaTime);
      break;
    case ParticleType::HIT_SPARK:
      // Main 5.2: BITMAP_SPARK — gravity 6-22 units/frame² (avg ~350/s²)
      // Sparks arc outward and fall, slight scale shrink
      p.velocity.y -= 400.0f * deltaTime;
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SMOKE:
      p.velocity *= (1.0f - 1.5f * deltaTime); // Slow deceleration
      p.scale *= (1.0f + 0.3f * deltaTime);    // Expand as it rises
      break;
    case ParticleType::FIRE:
      // Main 5.2: BITMAP_FIRE_CURSEDLICH — gravity 0.15-0.30, updraft
      p.velocity.y += 20.0f * deltaTime;
      p.velocity *= (1.0f - 3.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::ENERGY:
      p.velocity *= (1.0f - 5.0f * deltaTime);
      p.scale *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::PET_SPARKLE:
      // Stationary, just gentle scale shrink as it fades
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::IMP_SPARKLE:
      // Embers drift upward, decelerate, shrink
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.8f * deltaTime);
      break;
    case ParticleType::MOUNT_DUST:
      // Hoofbeat dust: slow deceleration, gentle shrink
      p.velocity *= (1.0f - 1.5f * deltaTime);
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::DUNGEON_MIST: {
      // Void edge mist: thick fog bank blanketing terrain edge
      p.velocity *= (1.0f - 0.15f * deltaTime);
      p.scale *= (1.0f + 0.05f * deltaTime); // slowly expand
      float life = 1.0f - p.lifetime / p.maxLifetime; // 0→1
      // Quick fade in, long sustain, slow fade out
      if (life < 0.08f)
        p.alpha = life / 0.08f * 0.9f;
      else if (life > 0.85f)
        p.alpha = (1.0f - life) / 0.15f * 0.9f;
      else
        p.alpha = 0.9f;
      break;
    }
    case ParticleType::SET_WATERFALL:
      // Main 5.2: BITMAP_WATERFALL_2 — rises upward, shrinks, fades
      // Continuous upward drift, gentle deceleration, color fades exponentially
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime); // Shrink over time
      break;
    case ParticleType::BUFF_AURA:
      // Elf buff aura — gentle upward drift, decelerate XZ, shrink
      p.velocity.x *= (1.0f - 3.0f * deltaTime);
      p.velocity.z *= (1.0f - 3.0f * deltaTime);
      p.scale *= (1.0f - 1.2f * deltaTime);
      break;
    case ParticleType::TRAP_LIGHTNING:
      // MuSven: Rotating cross sprite — stationary, no movement, hold scale
      // Rotation is fixed at spawn (WorldTime-based), fades out quickly
      break;
    case ParticleType::TRAP_GLOW:
      // MuSven: BITMAP_LIGHT — steady glow, no movement, hold scale
      break;
    case ParticleType::TRAP_SHINY:
      // MuSven: BITMAP_SHINY — sparkle, gentle drift, shrink
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::MONSTER_SHINY:
    case ParticleType::MONSTER_MAGIC:
      // 1-frame sprites — no animation, constant scale/alpha until expiry
      break;
    case ParticleType::FLARE:
      // Main 5.2: BITMAP_FLASH — stationary, rapid scale shrink + alpha fade
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    case ParticleType::LEVEL_FLARE:
      // Main 5.2: BITMAP_FLARE level-up — gentle rise, slow fade
      p.velocity.y += 10.0f * deltaTime; // Slight updraft
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      // Grow slightly in first half, then shrink
      if (p.lifetime > p.maxLifetime * 0.5f)
        p.scale *= (1.0f + 0.5f * deltaTime);
      else
        p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SKILL_SLASH: {
      // Main 5.2: CreateSpark — gravity pull (avg ~350/s²), terrain bounce
      p.velocity.y -= 350.0f * deltaTime;
      p.scale *= (1.0f - 1.2f * deltaTime);
      // Terrain bounce: if particle falls below ground, reflect Y velocity (damped)
      if (m_getTerrainHeight) {
        float groundY = m_getTerrainHeight(p.position.x, p.position.z);
        if (p.position.y < groundY) {
          p.position.y = groundY;
          p.velocity.y = std::abs(p.velocity.y) * 0.4f; // Bounce with 60% energy loss
          p.velocity.x *= 0.7f;
          p.velocity.z *= 0.7f;
        }
      }
      break;
    }
    case ParticleType::SKILL_CYCLONE:
      // Orbital motion: slight centripetal + updraft
      p.velocity.y += 15.0f * deltaTime;
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SKILL_FURY:
      // Strong gravity pull, large particles fall back down
      p.velocity.y -= 500.0f * deltaTime;
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::SKILL_STAB:
      // Fast directional, rapid fade, slight gravity
      p.velocity.y -= 150.0f * deltaTime;
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    case ParticleType::DEATHSTAB_SPARK:
      // Main 5.2: BITMAP_JOINT_THUNDER bone sparks — cyan, gravity, fast fade
      p.velocity.y -= 300.0f * deltaTime;
      p.velocity *= (1.0f - 3.0f * deltaTime);
      p.scale *= (1.0f - 4.0f * deltaTime);
      break;
    // DW spell update behaviors
    case ParticleType::SPELL_ENERGY:
      // Main 5.2: Gravity=20 is ROTATION speed (Rotation += Gravity per tick)
      // Not actual gravity — particles drift with initial velocity and spin
      p.rotation += 500.0f * deltaTime; // 20 deg/tick × 25fps = 500 deg/sec
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.2f * deltaTime);
      break;
    case ParticleType::SPELL_FIRE:
      // Main 5.2 BITMAP_FIRE SubType 5:
      // Gravity += 0.004/tick → Position[Z] += Gravity*10 (accelerating rise)
      // Scale -= 0.04/tick, Rotation += 5°/tick, Alpha = LifeTime/maxLifeTime
      // Frame = (maxTicks - ticksLeft) / 6 (4-frame sprite animation)
      p.velocity.y += 25.0f * deltaTime; // ~0.004*10*25fps² ≈ 25 u/s²
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      p.scale -= 25.0f * deltaTime; // Main 5.2: -0.04/tick × 25fps = -1.0/s
      if (p.scale < 1.0f) p.scale = 1.0f;
      p.rotation += 2.18f * deltaTime; // 5°/tick × 25fps = 125°/s ≈ 2.18 rad/s
      p.alpha = p.lifetime / p.maxLifetime; // Linear fade
      // 4-frame sprite animation: frame advances as lifetime decreases
      if (p.maxLifetime > 0.0f) {
        float progress = 1.0f - (p.lifetime / p.maxLifetime); // 0→1
        p.frame = std::min(3.0f, std::floor(progress * 4.0f));
      }
      break;
    case ParticleType::SPELL_FLAME:
      // Main 5.2 BITMAP_FLAME: rises with velocity, light fades over lifetime
      // Rotation randomized per frame (Main 5.2: o->Rotation = rand()%360)
      p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      // Light fades: subtract 0.05 per tick → ~1.25/sec
      p.color -= glm::vec3(1.25f * deltaTime);
      p.color = glm::max(p.color, glm::vec3(0.0f));
      p.alpha = p.lifetime / p.maxLifetime;
      break;
    case ParticleType::SPELL_ICE:
      // Moderate gravity, fast shrink (sharp shards)
      p.velocity.y -= 200.0f * deltaTime;
      p.scale *= (1.0f - 2.5f * deltaTime);
      break;
    case ParticleType::SPELL_LIGHTNING:
      // Very fast decay, erratic (already short lifetime)
      p.velocity *= (1.0f - 4.0f * deltaTime);
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    case ParticleType::SPELL_POISON:
      // Slow drift upward, grow slightly then fade
      p.velocity.y += 5.0f * deltaTime;
      p.velocity.x *= (1.0f - 0.5f * deltaTime);
      p.velocity.z *= (1.0f - 0.5f * deltaTime);
      if (p.lifetime > p.maxLifetime * 0.5f)
        p.scale *= (1.0f + 0.5f * deltaTime);
      else
        p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SPELL_METEOR:
      // Main 5.2: rising fire sparks (accelerating upward), shrink + rotate
      p.velocity.y += 150.0f * deltaTime;
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::SPELL_DARK:
      // Orbital swirl, gentle rise
      p.velocity.y += 10.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.5f * deltaTime);
      p.velocity.z *= (1.0f - 1.5f * deltaTime);
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SPELL_WATER:
      // Gravity pull, moderate shrink
      p.velocity.y -= 180.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SPELL_TELEPORT:
      // Rising blue sparks — drift slows
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::SPELL_ENERGY_ORB:
      // Main 5.2: BITMAP_ENERGY — Gravity=20 is rotation speed
      // No velocity decay: core orb particles must track projectile at full speed
      p.rotation += 500.0f * deltaTime;
      break;
    case ParticleType::INFERNO_SPARK: {
      // Main 5.2: BITMAP_SPARK SubType 2 — arcing sparks with terrain bounce
      p.velocity.y -= 500.0f * deltaTime;
      p.velocity.x *= (1.0f - 0.5f * deltaTime);
      p.velocity.z *= (1.0f - 0.5f * deltaTime);
      p.scale *= (1.0f - 0.6f * deltaTime);
      // Smooth color gradient: white-hot → orange → red as spark cools
      float sparkLife = p.lifetime / p.maxLifetime; // 1→0
      p.color = glm::vec3(1.0f,
                           0.4f + 0.6f * sparkLife,   // G: 1.0→0.4
                           0.1f + 0.9f * sparkLife * sparkLife); // B: 1.0→0.1 (fast)
      // Smooth quadratic fade-out (gentle disappearance)
      p.alpha = sparkLife * sparkLife;
      // Bounce on terrain
      if (m_getTerrainHeight) {
        float groundY = m_getTerrainHeight(p.position.x, p.position.z);
        if (p.position.y < groundY + 2.0f && p.velocity.y < 0.0f) {
          p.velocity.y *= -0.4f;
          p.position.y = groundY + 2.0f;
        }
      }
      break;
    }
    case ParticleType::INFERNO_EXPLOSION: {
      // 4x4 animated explosion sprite sheet (100+ encoding)
      // Quick flash-in (first 10%), smooth ease-out fade
      float expLife = p.lifetime / p.maxLifetime; // 1→0
      if (p.maxLifetime > 0.0f) {
        float progress = 1.0f - expLife;
        p.frame = 100.0f + std::min(15.0f, std::floor(progress * 16.0f));
      }
      p.scale *= (1.0f - 0.3f * deltaTime);
      p.velocity.y += 15.0f * deltaTime;
      // Smooth alpha: flash in over first 10%, then ease-out
      if (expLife > 0.9f)
        p.alpha = (1.0f - expLife) * 10.0f; // 0→1 in first 10%
      else
        p.alpha = expLife * expLife * 1.23f; // Quadratic ease-out (1.23 to hit 1.0 at 90%)
      // Color shifts from white-yellow to deep orange as explosion cools
      p.color = glm::vec3(1.0f,
                           0.6f + 0.4f * expLife,  // G: 1.0→0.6
                           0.2f + 0.5f * expLife);  // B: 0.7→0.2
      break;
    }
    case ParticleType::INFERNO_FIRE: {
      // inferno.OZJ — dedicated fire texture, accelerating rise
      float fireLife = p.lifetime / p.maxLifetime; // 1→0
      p.velocity.y += 60.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.5f * deltaTime);
      p.velocity.z *= (1.0f - 1.5f * deltaTime);
      p.scale -= 30.0f * deltaTime;
      if (p.scale < 2.0f) p.scale = 2.0f;
      p.rotation += 3.0f * deltaTime;
      // Smooth ease-out alpha with gentle tail
      p.alpha = fireLife * (2.0f - fireLife); // Parabolic: peaks at 1.0, smooth fade
      // Color: bright yellow-white → deep orange-red
      p.color = glm::vec3(1.0f,
                           0.5f + 0.4f * fireLife,  // G: 0.9→0.5
                           0.15f + 0.45f * fireLife * fireLife); // B: 0.6→0.15 (fast)
      break;
    }
    }

    p.alpha = p.lifetime / p.maxLifetime;
  }

  // Update spell projectiles
  updateSpellProjectiles(deltaTime);

  // Update lightning sky-strike bolts
  updateLightningBolts(deltaTime);
  // Update meteor fireballs
  updateMeteorBolts(deltaTime);
  // Update ice crystals and shards
  updateIceCrystals(deltaTime);
  updateIceShards(deltaTime);
  // Update PowerWave ground waves
  updatePowerWaves(deltaTime);

  // Update ribbons
  for (int i = (int)m_ribbons.size() - 1; i >= 0; --i) {
    updateRibbon(m_ribbons[i], deltaTime);
    if (m_ribbons[i].lifetime <= 0.0f) {
      m_ribbons[i] = std::move(m_ribbons.back());
      m_ribbons.pop_back();
    }
  }

  // Update level-up orbiting sprite effects (tick-based, Main 5.2: 25fps)
  for (int i = (int)m_levelUpEffects.size() - 1; i >= 0; --i) {
    auto &effect = m_levelUpEffects[i];
    effect.tickAccum += deltaTime * 25.0f; // Convert to ticks

    // Process whole ticks
    while (effect.tickAccum >= 1.0f && effect.lifeTime > 0) {
      effect.tickAccum -= 1.0f;
      effect.lifeTime--;

      for (auto &sp : effect.sprites) {
        // Main 5.2: count = (Direction[1] + LifeTime) / PKKey, PKKey=2
        float count = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float ox = std::cos(count) * effect.radius;
        float oz = -std::sin(count) * effect.radius;
        sp.height += sp.riseSpeed; // Direction[2] per tick

        glm::vec3 pos = effect.center + glm::vec3(ox, sp.height, oz);

        // Shift tails down (Main 5.2: CreateTail shifts array)
        int maxT = LEVEL_UP_MAX_TAILS;
        if (sp.numTails < maxT)
          sp.numTails++;
        for (int t = sp.numTails - 1; t > 0; --t)
          sp.tails[t] = sp.tails[t - 1];
        sp.tails[0] = pos;
      }
    }

    if (effect.lifeTime <= 0) {
      m_levelUpEffects[i] = std::move(m_levelUpEffects.back());
      m_levelUpEffects.pop_back();
    }
  }

  // Update ground circles (Main 5.2: spinning magic decals)
  for (int i = (int)m_groundCircles.size() - 1; i >= 0; --i) {
    m_groundCircles[i].lifetime -= deltaTime;
    if (m_groundCircles[i].lifetime <= 0.0f) {
      m_groundCircles[i] = m_groundCircles.back();
      m_groundCircles.pop_back();
      continue;
    }
    // Main 5.2: rotation ~3 rad/sec
    m_groundCircles[i].rotation += 3.0f * deltaTime;
  }

  // Update poison clouds
  updatePoisonClouds(deltaTime);

  // Update flame ground effects
  updateFlameGrounds(deltaTime);

  // Update twister storm tornados
  updateTwisterStorms(deltaTime);

  // Update Evil Spirit beams
  updateSpiritBeams(deltaTime);
  updateLaserFlashes(deltaTime);
  updateHellfireBeams(deltaTime);
  updateHellfireEffects(deltaTime);
  updateInfernoEffects(deltaTime);
  updateAquaBeams(deltaTime);
  updateWeaponTrail(deltaTime);
  updateFuryStrikeEffects(deltaTime);
  updateEarthQuakeCracks(deltaTime);
  updateStoneDebris(deltaTime);
  updateDeathStabEffects(deltaTime);
  updateDeathStabShocks(deltaTime);
  updateDeathStabSpirals(deltaTime);
  updateBuffAuras(deltaTime);
  updateAmbientFires(deltaTime);

}

void VFXManager::renderRibbons(const glm::mat4 &view,
                               const glm::mat4 &projection) {
  if (m_ribbons.empty() || !m_lineShader) return;

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD; // pure additive (ONE, ONE)
  m_lineShader->setVec4("u_lineMode", glm::vec4(TexValid(m_lightningTexture) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f));

  for (const auto &r : m_ribbons) {
    if (r.segments.size() < 2) continue;
    float flicker = 0.7f + 0.3f * ((float)(rand() % 100) / 100.0f);
    glm::vec3 flickerColor = r.color * flicker;
    float ribbonAlpha = r.lifetime / r.maxLifetime;

    std::vector<RibbonVertex> verts;
    verts.reserve(Ribbon::MAX_SEGMENTS * 12);
    float uvScroll = std::fmod(r.uvScroll, 1.0f);
    for (int j = 0; j < (int)r.segments.size() - 1; ++j) {
      const auto &s0 = r.segments[j];
      const auto &s1 = r.segments[j + 1];
      float u0 = ((float)(r.segments.size() - j) / (float)(Ribbon::MAX_SEGMENTS - 1)) * 2.0f - uvScroll;
      float u1 = ((float)(r.segments.size() - (j + 1)) / (float)(Ribbon::MAX_SEGMENTS - 1)) * 2.0f - uvScroll;
      RibbonVertex v;
      v.pos = s0.center - s0.right; v.uv = glm::vec2(u0, 0.0f); verts.push_back(v);
      v.pos = s0.center + s0.right; v.uv = glm::vec2(u0, 1.0f); verts.push_back(v);
      v.pos = s1.center + s1.right; v.uv = glm::vec2(u1, 1.0f); verts.push_back(v);
      v.pos = s0.center - s0.right; v.uv = glm::vec2(u0, 0.0f); verts.push_back(v);
      v.pos = s1.center + s1.right; v.uv = glm::vec2(u1, 1.0f); verts.push_back(v);
      v.pos = s1.center - s1.right; v.uv = glm::vec2(u1, 0.0f); verts.push_back(v);
      float u0b = u0 + uvScroll * 2.0f, u1b = u1 + uvScroll * 2.0f;
      v.pos = s0.center - s0.up; v.uv = glm::vec2(u0b, 0.0f); verts.push_back(v);
      v.pos = s0.center + s0.up; v.uv = glm::vec2(u0b, 1.0f); verts.push_back(v);
      v.pos = s1.center + s1.up; v.uv = glm::vec2(u1b, 1.0f); verts.push_back(v);
      v.pos = s0.center - s0.up; v.uv = glm::vec2(u0b, 0.0f); verts.push_back(v);
      v.pos = s1.center + s1.up; v.uv = glm::vec2(u1b, 1.0f); verts.push_back(v);
      v.pos = s1.center - s1.up; v.uv = glm::vec2(u1b, 0.0f); verts.push_back(v);
    }
    if (verts.empty()) continue;
    if ((int)verts.size() > MAX_RIBBON_VERTS) verts.resize(MAX_RIBBON_VERTS);

    uint32_t nv = (uint32_t)verts.size();
    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) < nv) continue;
    bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
    memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));

    m_lineShader->setVec4("u_lineColor", glm::vec4(flickerColor, ribbonAlpha));
    if (TexValid(m_lightningTexture))
      m_lineShader->setTexture(0, "s_ribbonTex", m_lightningTexture);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(state);
    bgfx::submit(0, m_lineShader->program);
  }
}

void VFXManager::Render(const glm::mat4 &view, const glm::mat4 &projection) {
  if (!m_shader || !bgfx::isValid(m_quadVBO))
    return;

  bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(projection));

  // Disable shadow sampling for all VFX model shader submits
  if (m_modelShader)
    m_modelShader->setVec4("u_shadowParams", glm::vec4(0.0f));

  // BGFX billboard batch draw helper using transient instance data
  // blendMode: 0=alpha, 1=additive (GL_ONE,GL_ONE), 2=subtractive (GL_ZERO,GL_INV_SRC_COLOR)
  auto drawBatchBgfx = [&](ParticleType type, TexHandle texture, int blendMode) {
    if (!TexValid(texture))
      return;
    // Collect particles of this type
    struct BgfxInstance { float data[12]; }; // 3 x vec4 = 48 bytes
    std::vector<BgfxInstance> instances;
    for (const auto &p : m_particles) {
      if (p.type != type) continue;
      BgfxInstance inst;
      // i_data0: worldPos.xyz, scale
      inst.data[0] = p.position.x; inst.data[1] = p.position.y;
      inst.data[2] = p.position.z; inst.data[3] = p.scale;
      // i_data1: rotation, frame, alpha, radialFalloffStrength
      float radialFalloff = (type == ParticleType::MONSTER_SHINY ||
                             type == ParticleType::MONSTER_MAGIC) ? 12.0f : 0.0f;
      inst.data[4] = p.rotation; inst.data[5] = p.frame;
      inst.data[6] = p.alpha; inst.data[7] = radialFalloff;
      // i_data2: color.xyz, 0
      inst.data[8] = p.color.x; inst.data[9] = p.color.y;
      inst.data[10] = p.color.z; inst.data[11] = 0.0f;
      instances.push_back(inst);
      if (instances.size() >= (size_t)MAX_PARTICLES) break;
    }
    if (instances.empty()) return;

    uint32_t numInst = (uint32_t)instances.size();
    if (bgfx::getAvailInstanceDataBuffer(numInst, 48) < numInst) return;
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, numInst, 48);
    memcpy(idb.data, instances.data(), numInst * 48);

    bgfx::setVertexBuffer(0, m_quadVBO);
    bgfx::setIndexBuffer(m_quadEBO);
    bgfx::setInstanceDataBuffer(&idb);
    m_shader->setTexture(0, "s_fireTex", texture);

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_DEPTH_TEST_LESS;
    if (blendMode == 2)
      // Main 5.2: EnableAlphaBlendMinus — subtractive (darkening aura)
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO,
                                       BGFX_STATE_BLEND_INV_SRC_COLOR);
    else if (blendMode == 1)
      state |= BGFX_STATE_BLEND_ADD;
    else
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  };

  // Normal alpha blend particles
  drawBatchBgfx(ParticleType::BLOOD, m_bloodTexture, false);
  drawBatchBgfx(ParticleType::SMOKE, m_smokeTexture, false);
  drawBatchBgfx(ParticleType::DUNGEON_MIST, m_smokeTexture, false);
  drawBatchBgfx(ParticleType::SPELL_POISON,
                TexValid(m_smokeTexture) ? m_smokeTexture : m_flareTexture, false);

  // Additive blend particles
  drawBatchBgfx(ParticleType::HIT_SPARK,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_hitTexture, true);
  drawBatchBgfx(ParticleType::FIRE, m_fireTexture, true);
  drawBatchBgfx(ParticleType::ENERGY, m_energyTexture, true);
  drawBatchBgfx(ParticleType::FLARE,
                TexValid(m_flareTexture) ? m_flareTexture : m_hitTexture, true);

  // DK Skill effect particles (additive)
  drawBatchBgfx(ParticleType::SKILL_SLASH,
                TexValid(m_spark2Texture) ? m_spark2Texture : m_sparkTexture, true);
  drawBatchBgfx(ParticleType::SKILL_CYCLONE,
                TexValid(m_energyTexture) ? m_energyTexture : m_sparkTexture, true);
  drawBatchBgfx(ParticleType::SKILL_FURY,
                TexValid(m_flareTexture) ? m_flareTexture : m_hitTexture, true);
  drawBatchBgfx(ParticleType::SKILL_STAB,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_hitTexture, true);
  drawBatchBgfx(ParticleType::DEATHSTAB_SPARK,
                TexValid(m_lightningTexture) ? m_lightningTexture : m_sparkTexture, true);

  // DW Spell effect particles (additive)
  drawBatchBgfx(ParticleType::SPELL_ENERGY,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_FIRE,
                TexValid(m_fireTexture) ? m_fireTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_FLAME,
                TexValid(m_flameTexture) ? m_flameTexture : m_fireTexture, true);
  drawBatchBgfx(ParticleType::SPELL_ICE,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_LIGHTNING,
                TexValid(m_lightningTexture) ? m_lightningTexture : m_energyTexture, true);
  drawBatchBgfx(ParticleType::SPELL_METEOR,
                TexValid(m_fireTexture) ? m_fireTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_DARK,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_WATER,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_TELEPORT,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::SPELL_ENERGY_ORB,
                TexValid(m_thunderTexture) ? m_thunderTexture : m_energyTexture, true);
  drawBatchBgfx(ParticleType::PET_SPARKLE,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::IMP_SPARKLE,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::MOUNT_DUST,
                TexValid(m_smokeTexture) ? m_smokeTexture : m_flareTexture, false);
  drawBatchBgfx(ParticleType::INFERNO_SPARK,
                TexValid(m_sparkTexture) ? m_sparkTexture : m_hitTexture, true);
  drawBatchBgfx(ParticleType::INFERNO_EXPLOSION,
                TexValid(m_explosionTexture) ? m_explosionTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::INFERNO_FIRE,
                TexValid(m_infernoFireTexture) ? m_infernoFireTexture : m_fireTexture, true);
  drawBatchBgfx(ParticleType::SET_WATERFALL,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);
  drawBatchBgfx(ParticleType::BUFF_AURA,
                TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture, true);

  // Dungeon trap VFX (MuSven BodyLight: additive sprites)
  drawBatchBgfx(ParticleType::TRAP_LIGHTNING,
                TexValid(m_lightning2Texture) ? m_lightning2Texture : m_energyTexture, true);
  drawBatchBgfx(ParticleType::TRAP_GLOW,
                TexValid(m_flareTexture) ? m_flareTexture : m_energyTexture, true);
  drawBatchBgfx(ParticleType::TRAP_SHINY,
                TexValid(m_shinyTexture) ? m_shinyTexture : m_sparkTexture, true);

  // Lost Tower monster ambient auras (both additive)
  // Shadow: dim ghostly violet glow (original subtractive is invisible on dark floors)
  drawBatchBgfx(ParticleType::MONSTER_SHINY,
                TexValid(m_shiny2Texture) ? m_shiny2Texture : m_shinyTexture, 1);
  // Poison Shadow: bright green glow (Main 5.2 SubType=0 additive)
  drawBatchBgfx(ParticleType::MONSTER_MAGIC,
                TexValid(m_magicGroundTexture) ? m_magicGroundTexture : m_flareTexture, 1);

  // 3D spell models — use model shader for MeshBuffers with BGFX handles
  renderSpellProjectiles(view, projection);
  renderLightningBolts(view, projection);
  renderMeteorBolts(view, projection);
  renderIceCrystals(view, projection);
  renderIceShards(view, projection);
  renderPowerWaves(view, projection);
  renderPoisonClouds(view, projection);
  renderTwisterStorms(view, projection);
  renderStoneDebris(view, projection);

  // Billboard-based effects
  renderLevelUpEffects(view, projection);
  renderGroundCircles(view, projection);
  renderFlameGrounds(view, projection);

  // Ribbon/trail effects
  renderSpiritBeams(view, projection);
  renderLaserFlashes(view, projection);
  renderHellfireBeams(view, projection);
  renderHellfireEffects(view, projection);
  renderInfernoEffects(view, projection);
  renderEarthQuakeCracks(view, projection);
  renderAquaBeams(view, projection);
  renderRibbons(view, projection);
  renderWeaponTrail(view, projection);
  renderDeathStabShocks(view, projection);
  renderDeathStabSpirals(view, projection);
  renderBuffAuras(view, projection);
}

// ── Elf buff aura ribbon trails (Main 5.2: MODEL_SPEARSKILL SubType 3/4) ──

void VFXManager::SetBuffAura(int type, bool active, const glm::vec3 &center) {
  int idx = (type == 1) ? 0 : 1; // 0=defense, 1=damage
  auto &aura = m_buffAuras[idx];
  if (active && !aura.active) {
    aura.active = true;
    aura.type = type;
    aura.center = center;
    // Main 5.2: Defense = blue-green, Damage = orange-red
    if (type == 1) {
      aura.radius = 40.0f;
      aura.trailWidth = 8.0f;
      aura.color = glm::vec3(0.4f, 1.0f, 0.6f);
    } else {
      aura.radius = 35.0f;
      aura.trailWidth = 10.0f;
      aura.color = glm::vec3(1.0f, 0.6f, 0.2f);
    }
    // Initialize 5 trails with evenly distributed phases
    for (int i = 0; i < BUFF_AURA_TRAILS; ++i) {
      auto &trail = aura.trails[i];
      trail.phase = (float)i * (2.0f * 3.14159f / BUFF_AURA_TRAILS);
      trail.orbitSpeed = 2.5f + (float)(rand() % 10) * 0.1f; // 2.5-3.5 rad/s
      trail.height = 20.0f + (float)(rand() % 120); // 20-140 initial height
      trail.heightDir = (i % 2 == 0) ? 1.0f : -1.0f;
      trail.numTails = 0;
    }
  } else if (!active) {
    aura.active = false;
  }
}

void VFXManager::UpdateBuffAuraCenter(int type, const glm::vec3 &center) {
  int idx = (type == 1) ? 0 : 1;
  auto &aura = m_buffAuras[idx];
  if (!aura.active) return;
  glm::vec3 delta = center - aura.center;
  aura.center = center;
  // Shift all tail positions so trails follow character movement
  for (int i = 0; i < BUFF_AURA_TRAILS; ++i) {
    for (int t = 0; t < aura.trails[i].numTails; ++t)
      aura.trails[i].tails[t] += delta;
  }
}

void VFXManager::SpawnWeaponSparkle(const glm::vec3 &bonePos, const glm::vec3 &color) {
  // Main 5.2: BITMAP_SHINY+1 at weapon bone — small bright sparkle
  SpawnBurstColored(ParticleType::BUFF_AURA, bonePos, color, 1);
}

void VFXManager::updateBuffAuras(float deltaTime) {
  for (int idx = 0; idx < 2; ++idx) {
    auto &aura = m_buffAuras[idx];
    if (!aura.active) continue;

    // Tick-based update (25fps like Main 5.2)
    float tickDt = deltaTime * 25.0f;

    for (int i = 0; i < BUFF_AURA_TRAILS; ++i) {
      auto &trail = aura.trails[i];

      // Advance orbit phase
      trail.phase += trail.orbitSpeed * deltaTime;
      if (trail.phase > 6.28318f) trail.phase -= 6.28318f;

      // Oscillate height — full body coverage (feet to above head)
      trail.height += trail.heightDir * 60.0f * deltaTime;
      if (trail.height > 150.0f) { trail.height = 150.0f; trail.heightDir = -1.0f; }
      if (trail.height < 10.0f) { trail.height = 10.0f; trail.heightDir = 1.0f; }

      // Compute current head position
      float ox = std::cos(trail.phase) * aura.radius;
      float oz = -std::sin(trail.phase) * aura.radius;
      glm::vec3 headPos = aura.center + glm::vec3(ox, trail.height, oz);

      // Shift tails down and insert new head (tick-rate)
      trail.tailAccum += tickDt;
      while (trail.tailAccum >= 1.0f) {
        trail.tailAccum -= 1.0f;
        if (trail.numTails < BUFF_AURA_MAX_TAILS)
          trail.numTails++;
        for (int t = trail.numTails - 1; t > 0; --t)
          trail.tails[t] = trail.tails[t - 1];
        trail.tails[0] = headPos;
      }
    }
  }
}

void VFXManager::renderBuffAuras(const glm::mat4 &view, const glm::mat4 &projection) {
  bool anyActive = false;
  for (int idx = 0; idx < 2; ++idx)
    if (m_buffAuras[idx].active) anyActive = true;
  if (!anyActive || !m_lineShader) return;

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  m_lineShader->setVec4("u_lineMode", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

  for (int idx = 0; idx < 2; ++idx) {
    const auto &aura = m_buffAuras[idx];
    if (!aura.active) continue;

    // Select texture: defense=flareBlue, damage=jointSpirit
    TexHandle tex = (aura.type == 1)
      ? (TexValid(m_flareBlueTexture) ? m_flareBlueTexture : m_energyTexture)
      : (TexValid(m_jointSpiritTexture) ? m_jointSpiritTexture : m_energyTexture);
    if (!TexValid(tex)) continue;

    for (int i = 0; i < BUFF_AURA_TRAILS; ++i) {
      const auto &trail = aura.trails[i];
      if (trail.numTails < 2) continue;

      int nSegs = trail.numTails - 1;
      std::vector<RibbonVertex> verts;
      verts.reserve(nSegs * 12);

      float hw = aura.trailWidth;
      for (int j = 0; j < nSegs; ++j) {
        glm::vec3 p0 = trail.tails[j];
        glm::vec3 p1 = trail.tails[j + 1];

        // Taper from head (full width) to tail (thin)
        float L0 = (float)(trail.numTails - j) / (float)BUFF_AURA_MAX_TAILS;
        float L1 = (float)(trail.numTails - (j + 1)) / (float)BUFF_AURA_MAX_TAILS;
        float taper0 = 0.2f + 0.8f * L0;
        float taper1 = 0.2f + 0.8f * L1;
        float hw0 = hw * taper0, hw1 = hw * taper1;

        // Horizontal face (XZ plane ribbon)
        verts.push_back({p0 + glm::vec3(-hw0, 0, 0), {L0, 0.0f}});
        verts.push_back({p0 + glm::vec3(+hw0, 0, 0), {L0, 1.0f}});
        verts.push_back({p1 + glm::vec3(+hw1, 0, 0), {L1, 1.0f}});
        verts.push_back({p0 + glm::vec3(-hw0, 0, 0), {L0, 0.0f}});
        verts.push_back({p1 + glm::vec3(+hw1, 0, 0), {L1, 1.0f}});
        verts.push_back({p1 + glm::vec3(-hw1, 0, 0), {L1, 0.0f}});
        // Vertical face (Y axis ribbon)
        verts.push_back({p0 + glm::vec3(0, -hw0, 0), {L0, 1.0f}});
        verts.push_back({p0 + glm::vec3(0, +hw0, 0), {L0, 0.0f}});
        verts.push_back({p1 + glm::vec3(0, +hw1, 0), {L1, 0.0f}});
        verts.push_back({p0 + glm::vec3(0, -hw0, 0), {L0, 1.0f}});
        verts.push_back({p1 + glm::vec3(0, +hw1, 0), {L1, 0.0f}});
        verts.push_back({p1 + glm::vec3(0, -hw1, 0), {L1, 1.0f}});
      }
      if (verts.empty()) continue;

      uint32_t nv = (uint32_t)verts.size();
      bgfx::TransientVertexBuffer tvb;
      if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) < nv) continue;
      bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
      memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));

      m_lineShader->setVec4("u_lineColor", glm::vec4(aura.color, 0.7f));
      m_lineShader->setTexture(0, "s_ribbonTex", tex);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setState(state);
      bgfx::submit(0, m_lineShader->program);
    }
  }
}

void VFXManager::UpdateLevelUpCenter(const glm::vec3 &position) {
  for (auto &effect : m_levelUpEffects) {
    glm::vec3 delta = position - effect.center;
    effect.center = position;
    // Shift all existing tail positions so the trail follows the character
    for (auto &sp : effect.sprites) {
      for (int t = 0; t < sp.numTails; ++t)
        sp.tails[t] += delta;
    }
  }
  // Ground circles also follow the character
  for (auto &gc : m_groundCircles) {
    gc.position = position;
  }
}

void VFXManager::SpawnLevelUpEffect(const glm::vec3 &position) {
  // Main 5.2 WSclient.cpp: 15 CreateJoint(BITMAP_FLARE, ..., 0, Target, 40, 2)
  // ZzzEffectJoint.cpp SubType=0: random phase, random upward speed, orbit=40

  LevelUpEffect effect;
  effect.center = position;
  effect.lifeTime = 50;        // Main 5.2: LifeTime = 50 (when Scale > 10)
  effect.tickAccum = 0.0f;
  effect.radius = 40.0f;       // Main 5.2: Velocity = 40
  effect.spriteScale = 40.0f;  // Main 5.2: Scale = 40

  // 15 sprites with random phases and rise speeds (Main 5.2)
  for (int i = 0; i < 15; ++i) {
    LevelUpSprite sp;
    sp.phase = (float)(rand() % 500 - 250); // Main 5.2: Direction[1]
    // Main 5.2: When Scale > 10: Direction[2] = (rand()%250+200)/100.f = 2.0-4.49
    sp.riseSpeed = (float)(rand() % 250 + 200) / 100.0f;
    sp.height = 0.0f;
    sp.numTails = 0;
    effect.sprites.push_back(sp);
  }

  // Pre-process initial ticks so trails render immediately (no stutter)
  for (int t = 0; t < 4 && effect.lifeTime > 0; ++t) {
    effect.lifeTime--;
    for (auto &sp : effect.sprites) {
      float count = (sp.phase + (float)effect.lifeTime) / 2.0f;
      float ox = std::cos(count) * effect.radius;
      float oz = -std::sin(count) * effect.radius;
      sp.height += sp.riseSpeed;
      glm::vec3 pos = effect.center + glm::vec3(ox, sp.height, oz);
      if (sp.numTails < LEVEL_UP_MAX_TAILS)
        sp.numTails++;
      for (int j = sp.numTails - 1; j > 0; --j)
        sp.tails[j] = sp.tails[j - 1];
      sp.tails[0] = pos;
    }
  }

  m_levelUpEffects.push_back(std::move(effect));

  // Main 5.2: CreateEffect(BITMAP_MAGIC+1, ...) — ground magic circle
  GroundCircle gc;
  gc.position = position;
  gc.rotation = 0.0f;
  gc.maxLifetime = 2.0f; // Main 5.2: LifeTime=20 ticks at 25fps = 0.8s, extended for visual
  gc.lifetime = gc.maxLifetime;
  gc.color = glm::vec3(1.0f, 0.75f, 0.2f); // Golden-orange (regular level-up)
  m_groundCircles.push_back(gc);
}

void VFXManager::SpawnBuffCastFlash(const glm::vec3 &position,
                                     const glm::vec3 &color) {
  // Main 5.2: BITMAP_MAGIC+1 SubType 1/2/3 — expanding colored ground circle
  // Same rendering as level-up ground circle but with buff-specific color
  GroundCircle gc;
  gc.position = position;
  gc.rotation = 0.0f;
  gc.maxLifetime = 1.2f; // Main 5.2: LifeTime=20 ticks = 0.8s, slight extend
  gc.lifetime = gc.maxLifetime;
  gc.color = color;
  m_groundCircles.push_back(gc);
}

void VFXManager::renderLevelUpEffects(const glm::mat4 &view,
                                      const glm::mat4 &projection) {
  if (m_levelUpEffects.empty()) return;

  TexHandle flareTex = TexValid(m_bitmapFlareTexture) ? m_bitmapFlareTexture : m_flareTexture;

  // Pass 1: Trail ribbons
  if (m_lineShader && TexValid(flareTex)) {
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    m_lineShader->setVec4("u_lineMode", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    for (const auto &effect : m_levelUpEffects) {
      float effectAlpha = 1.0f;
      if (effect.lifeTime < 10)
        effectAlpha = std::pow(1.0f / 1.3f, (float)(10 - effect.lifeTime));
      float hw = effect.spriteScale * 0.5f;

      for (const auto &sp : effect.sprites) {
        if (sp.numTails < 2) continue;
        float frac = effect.tickAccum;
        float curCount = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float interpCount = curCount - 0.5f * frac;
        float interpHeight = sp.height + sp.riseSpeed * frac;
        glm::vec3 interpHead = effect.center + glm::vec3(std::cos(interpCount) * effect.radius, interpHeight, -std::sin(interpCount) * effect.radius);

        int nSegs = sp.numTails - 1;
        std::vector<RibbonVertex> verts;
        verts.reserve(nSegs * 12);
        for (int j = 0; j < nSegs; ++j) {
          glm::vec3 p0 = (j == 0) ? interpHead : sp.tails[j];
          glm::vec3 p1 = sp.tails[j + 1];
          float L1 = (float)(sp.numTails - j) / (float)(LEVEL_UP_MAX_TAILS - 1);
          float L2 = (float)(sp.numTails - (j + 1)) / (float)(LEVEL_UP_MAX_TAILS - 1);
          float taper0 = 0.3f + 0.7f * L1, taper1 = 0.3f + 0.7f * L2;
          float hw0 = hw * taper0, hw1 = hw * taper1;
          verts.push_back({p0 + glm::vec3(-hw0, 0, 0), {L1, 0.0f}});
          verts.push_back({p0 + glm::vec3(+hw0, 0, 0), {L1, 1.0f}});
          verts.push_back({p1 + glm::vec3(+hw1, 0, 0), {L2, 1.0f}});
          verts.push_back({p0 + glm::vec3(-hw0, 0, 0), {L1, 0.0f}});
          verts.push_back({p1 + glm::vec3(+hw1, 0, 0), {L2, 1.0f}});
          verts.push_back({p1 + glm::vec3(-hw1, 0, 0), {L2, 0.0f}});
          verts.push_back({p0 + glm::vec3(0, -hw0, 0), {L1, 1.0f}});
          verts.push_back({p0 + glm::vec3(0, +hw0, 0), {L1, 0.0f}});
          verts.push_back({p1 + glm::vec3(0, +hw1, 0), {L2, 0.0f}});
          verts.push_back({p0 + glm::vec3(0, -hw0, 0), {L1, 1.0f}});
          verts.push_back({p1 + glm::vec3(0, +hw1, 0), {L2, 0.0f}});
          verts.push_back({p1 + glm::vec3(0, -hw1, 0), {L2, 1.0f}});
        }
        if (verts.empty()) continue;
        uint32_t nv = (uint32_t)verts.size();
        bgfx::TransientVertexBuffer tvb;
        if (bgfx::getAvailTransientVertexBuffer(nv, m_ribbonLayout) < nv) continue;
        bgfx::allocTransientVertexBuffer(&tvb, nv, m_ribbonLayout);
        memcpy(tvb.data, verts.data(), nv * sizeof(RibbonVertex));
        m_lineShader->setVec4("u_lineColor", glm::vec4(1.0f, 0.85f, 0.35f, effectAlpha));
        m_lineShader->setTexture(0, "s_ribbonTex", flareTex);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(state);
        bgfx::submit(0, m_lineShader->program);
      }
    }
  }

  // Pass 2: Head glow billboards
  if (m_shader && bgfx::isValid(m_quadVBO) && TexValid(flareTex)) {
    struct BgfxInstance { float data[12]; };
    std::vector<BgfxInstance> heads;
    for (const auto &effect : m_levelUpEffects) {
      float effectAlpha = 1.0f;
      if (effect.lifeTime < 10) effectAlpha = std::pow(1.0f / 1.3f, (float)(10 - effect.lifeTime));
      float frac = effect.tickAccum;
      for (const auto &sp : effect.sprites) {
        if (sp.numTails < 1) continue;
        float curCount = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float interpCount = curCount - 0.5f * frac;
        float interpHeight = sp.height + sp.riseSpeed * frac;
        glm::vec3 headPos = effect.center + glm::vec3(std::cos(interpCount) * effect.radius, interpHeight, -std::sin(interpCount) * effect.radius);
        BgfxInstance inst;
        inst.data[0] = headPos.x; inst.data[1] = headPos.y;
        inst.data[2] = headPos.z; inst.data[3] = effect.spriteScale * 1.2f;
        inst.data[4] = interpCount; inst.data[5] = 0.0f;
        inst.data[6] = effectAlpha * 0.8f; inst.data[7] = 0.0f;
        inst.data[8] = 1.0f; inst.data[9] = 0.9f;
        inst.data[10] = 0.5f; inst.data[11] = 0.0f;
        heads.push_back(inst);
      }
    }
    if (!heads.empty()) {
      uint32_t n = (uint32_t)heads.size();
      if (bgfx::getAvailInstanceDataBuffer(n, 48) >= n) {
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, n, 48);
        memcpy(idb.data, heads.data(), n * 48);
        bgfx::setVertexBuffer(0, m_quadVBO);
        bgfx::setIndexBuffer(m_quadEBO);
        bgfx::setInstanceDataBuffer(&idb);
        m_shader->setTexture(0, "s_fireTex", flareTex);
        uint64_t bs = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
        bgfx::setState(bs);
        bgfx::submit(0, m_shader->program);
      }
    }
  }
}

void VFXManager::renderGroundCircles(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_groundCircles.empty() || !m_lineShader || !TexValid(m_magicGroundTexture)) return;

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
  m_lineShader->setVec4("u_lineMode", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

  for (const auto &gc : m_groundCircles) {
    float t = 1.0f - gc.lifetime / gc.maxLifetime;
    float halfSize = t * 150.0f;
    float alpha = 1.0f;
    if (gc.lifetime < gc.maxLifetime * 0.25f)
      alpha = gc.lifetime / (gc.maxLifetime * 0.25f);

    float c = std::cos(gc.rotation), s = std::sin(gc.rotation);
    glm::vec3 right(c * halfSize, 0.0f, s * halfSize);
    glm::vec3 fwd(-s * halfSize, 0.0f, c * halfSize);
    glm::vec3 pos = gc.position + glm::vec3(0.0f, 2.0f, 0.0f);

    RibbonVertex verts[6];
    verts[0] = {pos - right - fwd, {0.0f, 0.0f}};
    verts[1] = {pos + right - fwd, {1.0f, 0.0f}};
    verts[2] = {pos + right + fwd, {1.0f, 1.0f}};
    verts[3] = {pos - right - fwd, {0.0f, 0.0f}};
    verts[4] = {pos + right + fwd, {1.0f, 1.0f}};
    verts[5] = {pos - right + fwd, {0.0f, 1.0f}};

    bgfx::TransientVertexBuffer tvb;
    if (bgfx::getAvailTransientVertexBuffer(6, m_ribbonLayout) < 6) continue;
    bgfx::allocTransientVertexBuffer(&tvb, 6, m_ribbonLayout);
    memcpy(tvb.data, verts, sizeof(verts));

    m_lineShader->setVec4("u_lineColor", glm::vec4(gc.color, alpha));
    m_lineShader->setTexture(0, "s_ribbonTex", m_magicGroundTexture);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(state);
    bgfx::submit(0, m_lineShader->program);
  }
}

void VFXManager::renderSpellProjectiles(const glm::mat4 &view,
                                        const glm::mat4 &projection) {
  if (m_spellProjectiles.empty()) return;

  // Pass 1: 3D fire model
  bool hasFireModel = !m_fireMeshes.empty() && m_modelShader;
  for (const auto &p : m_spellProjectiles) {
    if (p.skillId == 4 && hasFireModel)
      renderFireModel(p, view, projection);
  }

  // Pass 2: Billboard orbs + halos
  if (!m_shader || !bgfx::isValid(m_quadVBO)) return;

  struct BgfxInstance { float data[12]; };
  auto submitBillboards = [&](const std::vector<BgfxInstance> &instances, TexHandle tex, uint64_t blendState) {
    if (instances.empty() || !TexValid(tex)) return;
    uint32_t n = (uint32_t)instances.size();
    if (bgfx::getAvailInstanceDataBuffer(n, 48) < n) return;
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, n, 48);
    memcpy(idb.data, instances.data(), n * 48);
    bgfx::setVertexBuffer(0, m_quadVBO);
    bgfx::setIndexBuffer(m_quadEBO);
    bgfx::setInstanceDataBuffer(&idb);
    m_shader->setTexture(0, "s_fireTex", tex);
    bgfx::setState(blendState);
    bgfx::submit(0, m_shader->program);
  };

  uint64_t addState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

  TexHandle orbTex = TexValid(m_thunderTexture) ? m_thunderTexture
                   : (TexValid(m_energyTexture) ? m_energyTexture : m_flareTexture);

  // Core orbs
  std::vector<BgfxInstance> orbData;
  for (const auto &p : m_spellProjectiles) {
    if (p.skillId == 4 && hasFireModel) continue;
    BgfxInstance inst;
    inst.data[0] = p.position.x; inst.data[1] = p.position.y;
    inst.data[2] = p.position.z; inst.data[3] = p.scale;
    inst.data[4] = p.rotation; inst.data[5] = -1.0f;
    inst.data[6] = p.alpha * 0.9f; inst.data[7] = 0.0f;
    inst.data[8] = p.color.x; inst.data[9] = p.color.y;
    inst.data[10] = p.color.z; inst.data[11] = 0.0f;
    orbData.push_back(inst);
  }
  submitBillboards(orbData, orbTex, addState);

  // Outer halos
  TexHandle haloTex = TexValid(m_flareTexture) ? m_flareTexture : m_energyTexture;
  std::vector<BgfxInstance> haloData;
  for (const auto &p : m_spellProjectiles) {
    if (p.skillId == 4 && hasFireModel) continue;
    BgfxInstance inst;
    inst.data[0] = p.position.x; inst.data[1] = p.position.y;
    inst.data[2] = p.position.z; inst.data[3] = p.scale * 1.8f;
    inst.data[4] = -p.rotation * 0.5f; inst.data[5] = -1.0f;
    inst.data[6] = p.alpha * 0.4f; inst.data[7] = 0.0f;
    inst.data[8] = p.color.x; inst.data[9] = p.color.y;
    inst.data[10] = p.color.z; inst.data[11] = 0.0f;
    haloData.push_back(inst);
  }
  submitBillboards(haloData, haloTex, addState);
}

void VFXManager::renderFireModel(const SpellProjectile &p,
                                  const glm::mat4 &view,
                                  const glm::mat4 &projection) {
  if (!m_modelShader) return;

  // Model matrix: translate → BMD base rotation → heading → pitch → scale
  glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, p.yaw, glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::scale(model, glm::vec3(p.scale));

  m_modelShader->setVec4("u_params", glm::vec4(p.alpha, 1.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  m_modelShader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
  m_modelShader->setVec4("u_lightCount", glm::vec4(0.0f));
  m_modelShader->setVec4("u_fogParams", glm::vec4(0.0f));
  m_modelShader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  m_modelShader->setVec4("u_glowColor", glm::vec4(0.0f));
  m_modelShader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec4("u_viewPos", glm::vec4(glm::vec3(invView[3]), 0.0f));
  m_modelShader->setVec4("u_lightPos", glm::vec4(0, 5000, 0, 0));
  m_modelShader->setVec4("u_lightColor", glm::vec4(1, 1, 1, 0));

  uint64_t stAlpha = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint64_t stAdd = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_ADD;

  m_modelShader->setVec4("u_shadowParams", glm::vec4(0.0f));
  for (const auto &mb : m_fireMeshes) {
    if (mb.indexCount == 0 || mb.hidden) continue;
    bgfx::setTransform(glm::value_ptr(model));
    m_modelShader->setTexture(0, "s_texColor", mb.texture);
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    // Main 5.2 BlendMesh=1: mesh with textureId 1 renders additive (fire glow)
    bgfx::setState(mb.bmdTextureId == 1 ? stAdd : stAlpha);
    bgfx::submit(0, m_modelShader->program);
  }
}


// Main 5.2: AddTerrainLight — each spell projectile emits a dynamic point light
// Fire Ball: color (L*1.0, L*0.1, 0.0), range=300 world units (3 grid cells)
// Energy Ball: color (0.0, L*0.3, L*1.0), range=200 world units (2 grid cells)
// Other spells: color derived from projectile color, range=200
void VFXManager::GetActiveSpellLights(std::vector<glm::vec3> &positions,
                                      std::vector<glm::vec3> &colors,
                                      std::vector<float> &ranges,
                                      std::vector<int> &objectTypes) const {
  for (const auto &p : m_spellProjectiles) {
    if (p.alpha <= 0.01f)
      continue;

    // Luminosity flicker, toned down from Main 5.2 to avoid overly bright terrain glow
    float L = 0.4f + (float)(rand() % 4) * 0.05f; // 0.4-0.55 (was 0.7-1.0)
    L *= p.alpha; // Fade with projectile

    glm::vec3 lightColor;
    float lightRange;

    switch (p.skillId) {
    case 4: // Fire Ball
      lightColor = glm::vec3(L * 1.0f, L * 0.1f, 0.0f);
      lightRange = 150.0f;
      break;
    case 1: // Poison — green
      lightColor = glm::vec3(L * 0.3f, L * 1.0f, L * 0.6f);
      lightRange = 150.0f;
      break;
    case 3: // Lightning — blue-white
      lightColor = glm::vec3(L * 0.2f, L * 0.4f, L * 1.0f);
      lightRange = 150.0f;
      break;
    default: // Generic: use projectile color scaled by luminosity
      lightColor = p.color * L * 0.5f;
      lightRange = 150.0f;
      break;
    }

    positions.push_back(p.position);
    colors.push_back(lightColor);
    ranges.push_back(lightRange);
    objectTypes.push_back(-1); // -1 = spell light (no object type flicker)
  }

  // Ribbon lights (Lightning beams, Lich bolts)
  // Main 5.2: Lightning terrain light = (L*0.2, L*0.4, L*1.0), range=2 cells
  for (const auto &r : m_ribbons) {
    if (r.lifetime <= 0.0f || r.segments.empty())
      continue;
    float t = r.lifetime / r.maxLifetime;
    float L = (0.7f + (float)(rand() % 4) * 0.1f) * t;
    // Light at ribbon head position
    glm::vec3 lightColor = r.color * L;
    positions.push_back(r.headPos);
    colors.push_back(lightColor);
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Lightning sky-strike bolt lights
  // Main 5.2: AddTerrainLight(pos, (L*0.2, L*0.4, L*1.0), 2, PrimaryTerrainLight)
  for (const auto &b : m_lightningBolts) {
    float L = 0.7f + (float)(rand() % 4) * 0.1f;
    // Main 5.2: Luminosity dims in last 5 ticks (0.2s) before impact
    if (!b.impacted && b.lifetime < 0.2f) {
      float ticksLeft = b.lifetime / 0.04f;
      L -= (5.0f - ticksLeft) * 0.2f;
      if (L < 0.0f) L = 0.0f;
    }
    if (b.impacted)
      L *= std::max(0.0f, 1.0f - b.impactTimer * 4.0f); // Quick fade after impact
    if (L <= 0.01f)
      continue;
    positions.push_back(b.position);
    colors.push_back(glm::vec3(L * 0.2f, L * 0.4f, L * 1.0f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Poison cloud lights — Main 5.2 MoveEffect:
  // Luminosity = Luminosity (from o->Luminosity, set elsewhere as LifeTime*0.2f typically)
  // Vector(Luminosity*0.3f, Luminosity*1.f, Luminosity*0.6f, Light);
  // AddTerrainLight(pos, Light, 2, PrimaryTerrainLight);
  for (const auto &pc : m_poisonClouds) {
    float ticksRemaining = pc.lifetime / 0.04f;
    float L = ticksRemaining * 0.1f; // Matches BlendMeshLight fade
    if (L <= 0.01f)
      continue;
    L = std::min(L, 1.5f); // Clamp so it doesn't overpower
    positions.push_back(pc.position);
    colors.push_back(glm::vec3(L * 0.3f, L * 1.0f, L * 0.6f));
    ranges.push_back(200.0f); // range=2 grid cells
    objectTypes.push_back(-1);
  }

  // PowerWave terrain light — MuSven: blue-cyan (L*0.3, L*0.6, L*1.0)
  for (const auto &pw : m_powerWaves) {
    float ticksRemaining = pw.lifetime / 0.04f;
    float L = std::min(ticksRemaining * 0.1f, 1.0f);
    if (L <= 0.01f) continue;
    positions.push_back(pw.position);
    colors.push_back(glm::vec3(L * 0.3f, L * 0.6f, L * 1.0f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Twister storm terrain darkening — Main 5.2: Luminosity = (rand()%4+7)*0.1f
  // Fades when LifeTime < 5: Luminosity -= (5-LifeTime)*0.2f
  // AddTerrainLight(-L*0.4, -L*0.3, -L*0.2), range=5
  for (const auto &ts : m_twisterStorms) {
    float ticksRemaining = ts.lifetime / 0.04f;
    float L = (float)(rand() % 4 + 7) * 0.1f; // Main 5.2: 0.7-1.0 randomized
    if (ticksRemaining < 5.0f) {
      L -= (5.0f - ticksRemaining) * 0.2f;
      if (L < 0.0f) L = 0.0f;
    }
    if (L <= 0.01f)
      continue;
    positions.push_back(ts.position);
    colors.push_back(glm::vec3(-L * 0.4f, -L * 0.3f, -L * 0.2f));
    ranges.push_back(500.0f); // range=5 grid cells
    objectTypes.push_back(-1);
  }

  // Flame ground fire lights
  for (const auto &fg : m_flameGrounds) {
    float ticksRemaining = fg.lifetime / 0.04f;
    float L = std::min(1.0f, ticksRemaining * 0.05f);
    L *= (0.4f + (float)(rand() % 4) * 0.05f); // Toned down flicker
    if (L <= 0.01f)
      continue;
    positions.push_back(fg.position);
    colors.push_back(glm::vec3(L * 1.0f, L * 0.4f, L * 0.0f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Hellfire ground circle — warm orange terrain light
  for (const auto &hf : m_hellfireEffects) {
    float ticksRemaining = hf.lifetime / 0.04f;
    float L = std::min(1.0f, ticksRemaining * 0.1f);
    L *= (0.4f + (float)(rand() % 4) * 0.05f); // Toned down
    if (L <= 0.01f)
      continue;
    positions.push_back(hf.position);
    colors.push_back(glm::vec3(L, L * 0.8f, L * 0.2f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Inferno ring — smooth warm fire glow at ring points + darkening at center
  for (const auto &inf : m_infernoEffects) {
    float t = inf.lifetime / inf.maxLifetime; // 1→0
    // Smooth quadratic falloff — no random flicker for clean blending
    float L = t * t;
    if (L <= 0.005f)
      continue;
    // Center: Main 5.2 negative light (darkening), smooth fade
    positions.push_back(inf.position);
    colors.push_back(glm::vec3(-L * 0.4f, -L * 0.4f, -L * 0.4f));
    ranges.push_back(500.0f);
    objectTypes.push_back(-1);
    // Ring points: warm orange glow, smooth fade with color shift
    // Fire color cools from bright orange to deep red as effect fades
    for (int rp = 0; rp < 8; ++rp) {
      positions.push_back(inf.ringPoints[rp]);
      colors.push_back(glm::vec3(L * 0.8f,
                                  L * (0.25f + 0.2f * t),  // G fades faster
                                  L * 0.08f));              // Minimal blue
      ranges.push_back(200.0f);
      objectTypes.push_back(-1);
    }
  }

  // Evil Spirit beams — terrain darkening (negative light)
  // Main 5.2: Luminosity = -(rand()%4+4)*0.01, AddTerrainLight(pos, (L,L,L), range=4)
  for (const auto &b : m_spiritBeams) {
    float L = -(float)(rand() % 4 + 4) * 0.01f; // -0.04 to -0.08
    positions.push_back(b.position);
    colors.push_back(glm::vec3(L));
    ranges.push_back(400.0f); // range=4 grid cells
    objectTypes.push_back(-1);
  }

  // EarthQuake cracks — red terrain light
  // Only EQ02, EQ05, EQ08 emit light
  for (const auto &eq : m_earthQuakeCracks) {
    if (!eq.addTerrainLight || eq.blendMeshLight <= 0.01f)
      continue;
    float L = eq.blendMeshLight;
    positions.push_back(eq.position);
    colors.push_back(glm::vec3(L * 1.2f, L * 0.15f, L * 0.0f)); // Warm red-orange
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Meteorite bolts — orange terrain glow during flight
  for (const auto &m : m_meteorBolts) {
    if (m.impacted) continue;
    float L = (float)(rand() % 4 + 4) * 0.1f; // 0.4-0.7 (toned down from 0.7-1.0)
    positions.push_back(m.position);
    colors.push_back(glm::vec3(L * 1.0f, L * 0.3f, 0.0f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Aqua Beam — blue terrain light at all 20 segments (Main 5.2: AddTerrainLight, no fade)
  for (const auto &ab : m_aquaBeams) {
    for (int j = 0; j < AquaBeam::NUM_SEGMENTS; ++j) {
      glm::vec3 pos = ab.startPosition + ab.direction * (float)j;
      positions.push_back(pos);
      colors.push_back(ab.light); // Full brightness, no fade
      ranges.push_back(200.0f);   // range=2 grid cells
      objectTypes.push_back(-1);
    }
  }

}


void VFXManager::Cleanup() {
  // Buffer cleanup (backend-specific)
  if (bgfx::isValid(m_quadVBO))   bgfx::destroy(m_quadVBO);
  if (bgfx::isValid(m_quadEBO))   bgfx::destroy(m_quadEBO);
  if (bgfx::isValid(m_instanceVBO)) bgfx::destroy(m_instanceVBO);
  if (bgfx::isValid(m_ribbonVBO)) bgfx::destroy(m_ribbonVBO);
  m_quadVBO = BGFX_INVALID_HANDLE;
  m_quadEBO = BGFX_INVALID_HANDLE;
  m_instanceVBO = BGFX_INVALID_HANDLE;
  m_ribbonVBO = BGFX_INVALID_HANDLE;

  // Texture cleanup (TexValid/TexDestroy are dual-path)
  TexHandle textures[] = {m_bloodTexture,       m_hitTexture,
                          m_sparkTexture,       m_flareTexture,
                          m_smokeTexture,       m_fireTexture,
                          m_energyTexture,      m_lightningTexture,
                          m_magicGroundTexture, m_ringTexture,
                          m_bitmapFlareTexture, m_thunderTexture,
                          m_flameTexture,       m_jointSpiritTexture,
                          m_hellfireCircleTex,  m_hellfireLightTex,
                          m_explosionTexture,   m_infernoFireTexture,
                          m_spark3Texture,      m_flareBlueTexture,
                          m_blurTexture,        m_motionBlurTexture,
                          m_spark2Texture,      m_flareForceTexture,
                          m_shiny2Texture};
  for (auto t : textures) {
    if (TexValid(t))
      TexDestroy(t);
  }

  // Mesh cleanup (CleanupMeshBuffers is dual-path)
  CleanupMeshBuffers(m_fireMeshes);
  m_fireBmd.reset();
  m_modelShader.reset();

  CleanupMeshBuffers(m_blastMeshes);
  m_blastBmd.reset();

  CleanupMeshBuffers(m_poisonMeshes);
  m_poisonBmd.reset();

  CleanupMeshBuffers(m_stormMeshes);
  m_stormBmd.reset();

  CleanupMeshBuffers(m_circleMeshes);
  m_circleBmd.reset();
  CleanupMeshBuffers(m_circleLightMeshes);
  m_circleLightBmd.reset();
  CleanupMeshBuffers(m_laserMeshes);
  m_laserBmd.reset();

  CleanupMeshBuffers(m_iceMeshes);
  m_iceBmd.reset();
  CleanupMeshBuffers(m_iceSmallMeshes);
  m_iceSmallBmd.reset();
  CleanupMeshBuffers(m_infernoMeshes);
  m_infernoBmd.reset();
  for (int i = 1; i <= 8; ++i) {
    CleanupMeshBuffers(m_eqMeshes[i]);
    m_eqBmd[i].reset();
  }
  CleanupMeshBuffers(m_stone1Meshes);
  m_stone1Bmd.reset();
  CleanupMeshBuffers(m_stone2Meshes);
  m_stone2Bmd.reset();

  m_particles.clear();
  m_ribbons.clear();
  m_groundCircles.clear();
  m_levelUpEffects.clear();
  m_spellProjectiles.clear();
  m_lightningBolts.clear();
  m_poisonClouds.clear();
  m_flameGrounds.clear();
  m_twisterStorms.clear();
  m_aquaBeams.clear();
  m_infernoEffects.clear();
  m_ambientFires.clear();
}

// ── Ambient fire billboard system ──────────────────────────────────────────
// Replaces BlendMesh fire glow meshes on world objects. Spawns FIRE particles
// at registered positions each frame. Particles render as additive billboards
// AFTER monsters, so they layer naturally without clipping through 3D geometry.

void VFXManager::ClearAmbientFires() {
  // Keep existing emitters' timers but mark for re-registration.
  // Positions are re-registered each frame from main.cpp.
  m_ambientFires.clear();
}

void VFXManager::AddAmbientFire(const glm::vec3 &pos, float intensity) {
  // Distance cull: skip fires far from camera (>3000 units)
  float dx = pos.x - m_cameraPos.x;
  float dz = pos.z - m_cameraPos.z;
  if (dx * dx + dz * dz > 3000.0f * 3000.0f) return;

  AmbientFireEmitter e;
  e.position = pos;
  e.intensity = intensity;
  e.spawnTimer = 0.0f;
  m_ambientFires.push_back(e);
}

void VFXManager::updateAmbientFires(float dt) {
  // Spawn rate: ~6 particles/sec per intensity unit
  const float spawnInterval = 1.0f / 6.0f;

  for (auto &e : m_ambientFires) {
    e.spawnTimer += dt * e.intensity;
    while (e.spawnTimer >= spawnInterval) {
      e.spawnTimer -= spawnInterval;

      // Spawn a fire particle at the emitter position with some randomness
      if (m_particles.size() >= MAX_PARTICLES) break;

      Particle p;
      p.type = ParticleType::FIRE;
      float rx = ((float)(rand() % 40) - 20.0f); // -20..+20 horizontal spread
      float rz = ((float)(rand() % 40) - 20.0f);
      p.position = e.position + glm::vec3(rx, 0.0f, rz);

      // Rise upward with slight drift (Main 5.2 BITMAP_FIRE style)
      float speed = 15.0f + (float)(rand() % 20);
      float angle = (float)(rand() % 360) * 0.01745f;
      p.velocity = glm::vec3(std::cos(angle) * speed * 0.3f,
                              40.0f + (float)(rand() % 30),
                              std::sin(angle) * speed * 0.3f);
      p.scale = (20.0f + (float)(rand() % 25)) * e.intensity;
      p.maxLifetime = 0.35f + (float)(rand() % 20) / 100.0f;
      p.lifetime = p.maxLifetime;
      p.color = glm::vec3(1.0f, 0.7f, 0.25f); // Warm orange fire
      p.alpha = 0.9f;
      p.rotation = (float)(rand() % 360) * 0.01745f;
      p.frame = 0.0f;
      m_particles.push_back(p);
    }
  }
}


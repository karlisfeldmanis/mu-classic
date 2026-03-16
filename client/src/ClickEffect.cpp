#include "ClickEffect.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

float ClickEffect::getTerrainHeight(float worldX, float worldZ) const {
  return TerrainUtils::GetHeight(m_terrainData, worldX, worldZ);
}

void ClickEffect::drawGroundQuad(float cx, float cz, float halfSize,
                                 float hOff) {
  ViewerVertex verts[4];
  verts[0].pos = {cx - halfSize,
                  getTerrainHeight(cx - halfSize, cz - halfSize) + hOff,
                  cz - halfSize};
  verts[1].pos = {cx + halfSize,
                  getTerrainHeight(cx + halfSize, cz - halfSize) + hOff,
                  cz - halfSize};
  verts[2].pos = {cx + halfSize,
                  getTerrainHeight(cx + halfSize, cz + halfSize) + hOff,
                  cz + halfSize};
  verts[3].pos = {cx - halfSize,
                  getTerrainHeight(cx - halfSize, cz + halfSize) + hOff,
                  cz + halfSize};
  for (int i = 0; i < 4; ++i)
    verts[i].normal = glm::vec3(0, 1, 0);
  verts[0].tex = {0, 0};
  verts[1].tex = {1, 0};
  verts[2].tex = {1, 1};
  verts[3].tex = {0, 1};

  bgfx::update(m_dynVbo, 0, bgfx::copy(verts, sizeof(verts)));
}

void ClickEffect::Init() {
  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  m_dynVbo = bgfx::createDynamicVertexBuffer(4, layout, BGFX_BUFFER_ALLOW_RESIZE);
  uint16_t indices[] = {0, 1, 2, 0, 2, 3};
  m_quadEbo = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
}

void ClickEffect::LoadAssets(const std::string &dataPath) {
  std::string effectDir = dataPath + "/Effect/";
  m_ringTex = TextureLoader::Resolve(effectDir, "cursorpin02.OZJ");
  m_waveTex = TextureLoader::Resolve(effectDir, "cursorpin01.OZJ");
  m_glowTex = TextureLoader::Resolve(effectDir, "Magic_Ground1.OZJ");
  m_bmd = BMDParser::Parse(effectDir + "MoveTargetPosEffect.bmd");
  if (m_bmd && !m_bmd->Meshes.empty()) {
    std::vector<BoneWorldMatrix> idBones(m_bmd->Bones.size());
    for (auto &b : idBones)
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          b[r][c] = (r == c) ? 1.0f : 0.0f;
    AABB aabb;
    UploadMeshWithBones(m_bmd->Meshes[0], effectDir, idBones,
                        m_modelBuffers, aabb, true);
    std::cout << "[ClickEffect] Loaded: BMD + "
              << (TexValid(m_ringTex) ? 1 : 0) << " ring, "
              << (TexValid(m_waveTex) ? 1 : 0) << " wave, "
              << (TexValid(m_glowTex) ? 1 : 0) << " glow textures"
              << std::endl;
  } else {
    std::cerr << "[ClickEffect] Failed to load MoveTargetPosEffect.bmd"
              << std::endl;
  }
}

void ClickEffect::Show(const glm::vec3 &pos) {
  m_pos = pos;
  m_visible = true;
  m_lifetime = 1.2f;
  m_scale = 1.8f;
  m_shrinking = true;
  m_animFrame = 0.0f;
  m_glowAngle = 0.0f;
  m_waves.clear();
  m_waveTimer = 0.0f;
  m_waves.push_back({1.2f, 1.0f});
}

void ClickEffect::Hide() {
  m_visible = false;
  m_waves.clear();
}

void ClickEffect::Render(const glm::mat4 &view, const glm::mat4 &proj,
                         float deltaTime, Shader *shader) {
  if (!m_visible || !shader || !m_terrainData)
    return;

  // Lifetime countdown
  m_lifetime -= deltaTime;
  if (m_lifetime <= 0.0f) {
    m_visible = false;
    m_waves.clear();
    return;
  }

  // Fade multiplier: fade over last 0.4s
  float fadeMul =
      (m_lifetime < 0.4f) ? (m_lifetime / 0.4f) : 1.0f;

  // Animate pulsing ring
  float pulseSpeed = 0.15f * 25.0f;
  if (m_shrinking) {
    m_scale -= pulseSpeed * deltaTime;
    if (m_scale <= 0.8f) {
      m_scale = 0.8f;
      m_shrinking = false;
    }
  } else {
    m_scale += pulseSpeed * deltaTime;
    if (m_scale >= 1.8f) {
      m_scale = 1.8f;
      m_shrinking = true;
    }
  }

  // Spawn expanding wave rings
  m_waveTimer += deltaTime;
  if (m_waveTimer >= 0.6f) {
    m_waves.push_back({1.2f, 1.0f});
    m_waveTimer -= 0.6f;
  }

  // Update wave rings
  float waveShrink = 0.04f * 25.0f;
  float waveFade = 0.05f * 25.0f;
  for (auto &w : m_waves) {
    w.scale -= waveShrink * deltaTime;
    if (w.scale < 0.6f)
      w.alpha -= waveFade * deltaTime;
  }
  m_waves.erase(
      std::remove_if(m_waves.begin(), m_waves.end(),
                     [](const Wave &w) {
                       return w.scale <= 0.2f || w.alpha <= 0.0f;
                     }),
      m_waves.end());

  // Animate ground glow rotation
  m_glowAngle += 1.5f * deltaTime;

  // Animate BMD model
  if (m_bmd && !m_bmd->Actions.empty()) {
    int numKeys = m_bmd->Actions[0].NumAnimationKeys;
    m_animFrame += ANIM_SPEED * deltaTime;
    if (m_animFrame >= (float)numKeys)
      m_animFrame = std::fmod(m_animFrame, (float)numKeys);
  }

  float cx = m_pos.x, cz = m_pos.z;
  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

  glm::mat4 identity(1.0f);
  uint64_t groundState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_ADD;
  // No WRITE_Z (depth mask false), no CULL (two-sided)

  // Helper: submit a ground quad pass with BGFX
  auto submitGroundQuad = [&](TexHandle tex, float halfSize, float hOff,
                              const glm::vec3 &lightColor, float blendLight) {
    drawGroundQuad(cx, cz, halfSize, hOff);
    bgfx::setTransform(glm::value_ptr(identity));
    bgfx::setVertexBuffer(0, m_dynVbo);
    bgfx::setIndexBuffer(m_quadEbo);
    shader->setTexture(0, "s_texColor", tex);
    shader->setVec4("u_params", glm::vec4(1.0f, blendLight, 0.0f, 0.0f));
    shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(lightColor, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    bgfx::setState(groundState);
    bgfx::submit(0, shader->program);
  };

  // Pass 1: Ground glow (Magic_Ground1)
  if (TexValid(m_glowTex))
    submitGroundQuad(m_glowTex, 50.0f, 1.5f,
                     glm::vec3(0.7f, 0.5f, 0.2f) * fadeMul, fadeMul);

  // Pass 2: Pulsing ring (cursorpin02)
  if (TexValid(m_ringTex))
    submitGroundQuad(m_ringTex, m_scale * 30.0f, 2.0f,
                     glm::vec3(1.0f, 0.7f, 0.3f) * fadeMul, fadeMul);

  // Pass 3: Expanding wave rings (cursorpin01)
  if (TexValid(m_waveTex) && !m_waves.empty()) {
    for (auto &w : m_waves) {
      float a = w.alpha * fadeMul;
      submitGroundQuad(m_waveTex, w.scale * 30.0f, 2.5f,
                       glm::vec3(1.0f, 0.7f, 0.3f) * a, a);
    }
  }

  // Pass 4: BMD spinning cone model
  if (m_bmd && !m_modelBuffers.empty() &&
      m_modelBuffers[0].indexCount > 0) {
    auto bones = ComputeBoneMatricesInterpolated(m_bmd.get(), 0, m_animFrame);
    RetransformMeshWithBones(m_bmd->Meshes[0], bones, m_modelBuffers[0]);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(0.35f));

    auto &mb = m_modelBuffers[0];
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    shader->setTexture(0, "s_texColor", mb.texture);
    shader->setVec4("u_params", glm::vec4(1.0f, fadeMul, 0.0f, 0.0f));
    shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(fadeMul, 0.7f * fadeMul, 0.3f * fadeMul, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    bgfx::setState(groundState);
    bgfx::submit(0, shader->program);
  }
}

void ClickEffect::Cleanup() {
  if (bgfx::isValid(m_dynVbo)) bgfx::destroy(m_dynVbo);
  if (bgfx::isValid(m_quadEbo)) bgfx::destroy(m_quadEbo);
  m_dynVbo = BGFX_INVALID_HANDLE;
  m_quadEbo = BGFX_INVALID_HANDLE;
  CleanupMeshBuffers(m_modelBuffers);
  m_bmd.reset();
  TexDestroy(m_ringTex);
  TexDestroy(m_waveTex);
  TexDestroy(m_glowTex);
}

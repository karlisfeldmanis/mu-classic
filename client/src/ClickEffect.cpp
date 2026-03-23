#include "ClickEffect.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// Main 5.2 tick-based constants (ZzzEffect.cpp):
// Parent (MODEL_MOVE_TARGETPOSITION_EFFECT): LifeTime=30, Scale=0.6, BlendMesh=0
// EFFECT1 (cursorpin01): scale 1.2→0.2 at -0.04/tick, LifeTime=20, spawned every 15 ticks
// EFFECT2 (cursorpin02): scale oscillates 0.8-1.8 at ±0.15/tick, LifeTime=30
// BITMAP_MAGIC SubType 11: LifeTime=24, Scale=0.8, HeadAngle += (2,-2,2)/tick
// BITMAP_SPARK+1 SubType 24: 1 ascending spark column per tick at +110 height
// Light color: (1.0, 0.7, 0.3)
static constexpr float MARKER_HALF_SIZE = 30.0f;
static constexpr float HEIGHT_OFFSET = 2.0f;
static constexpr int   PARENT_LIFE = 30;       // ticks
static constexpr int   SHRINK_LIFE = 20;       // ticks per shrink ring
static constexpr float SHRINK_SPAWN_INTERVAL = 15.0f; // ticks between spawns
static constexpr float TICK_RATE = 25.0f;      // Main 5.2: 25fps tick engine
static constexpr float BMD_SCALE = 0.35f;      // Visual scale for BMD cone
static constexpr float BMD_ANIM_SPEED = 8.0f;  // Animation playback speed

// Main 5.2 orange tint for cursor effect
static const glm::vec3 CURSOR_TINT(1.0f, 0.7f, 0.3f);

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

void ClickEffect::submitQuad(Shader *shader, const glm::vec3 &eye,
                              TexHandle tex, float alpha,
                              const glm::vec3 &tint, uint64_t state) {
  glm::mat4 identity(1.0f);
  bgfx::setTransform(glm::value_ptr(identity));
  bgfx::setVertexBuffer(0, m_dynVbo);
  bgfx::setIndexBuffer(m_quadEbo);
  shader->setTexture(0, "s_texColor", tex);
  shader->setVec4("u_params", glm::vec4(1.0f, alpha, 0.0f, 0.0f));
  shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
  shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
  shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
  shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
  shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
  shader->setVec4("u_glowColor", glm::vec4(0.0f));
  shader->setVec4("u_baseTint", glm::vec4(tint, 0.0f));
  shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
  shader->setVec4("u_fogParams", glm::vec4(0.0f));
  shader->setVec4("u_fogColor", glm::vec4(0.0f));
  bgfx::setState(state);
  bgfx::submit(0, shader->program);
}

void ClickEffect::Init() {
  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  m_dynVbo =
      bgfx::createDynamicVertexBuffer(4, layout, BGFX_BUFFER_ALLOW_RESIZE);
  uint16_t indices[] = {0, 1, 2, 0, 2, 3};
  m_quadEbo = bgfx::createIndexBuffer(bgfx::copy(indices, sizeof(indices)));
}

void ClickEffect::LoadAssets(const std::string &dataPath) {
  std::string effectDir = dataPath + "/Effect/";

  // Bitmap textures
  m_shrinkTex = TextureLoader::Resolve(effectDir, "cursorpin01.OZJ");
  m_pulseTex = TextureLoader::Resolve(effectDir, "cursorpin02.OZJ");
  m_glowTex = TextureLoader::Resolve(effectDir, "Magic_Ground1.OZJ");

  // BMD spinning cone model (MODEL_MOVE_TARGETPOSITION_EFFECT)
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
    std::cout << "[ClickEffect] Loaded MoveTargetPosEffect.bmd + "
              << (int)m_modelBuffers.size() << " mesh buffers" << std::endl;
  }

  if (TexValid(m_shrinkTex))
    std::cout << "[ClickEffect] Loaded cursorpin01.OZJ" << std::endl;
  if (TexValid(m_pulseTex))
    std::cout << "[ClickEffect] Loaded cursorpin02.OZJ" << std::endl;
  if (TexValid(m_glowTex))
    std::cout << "[ClickEffect] Loaded Magic_Ground1.OZJ (glow)" << std::endl;
}

void ClickEffect::Show(const glm::vec3 &pos) {
  m_pos = pos;
  m_visible = true;

  // Parent effect: 30 ticks total
  m_parentLife = PARENT_LIFE;
  m_spawnTimer = SHRINK_SPAWN_INTERVAL; // spawn first ring immediately

  // Spawn first shrink ring (EFFECT1)
  for (auto &ring : m_shrinkRings)
    ring.active = false;
  m_shrinkRings[0] = {1.2f, 1.0f, SHRINK_LIFE, true};

  // Init pulse ring (EFFECT2): starts at 1.8, shrinking
  m_pulseScale = 1.8f;
  m_pulseAlpha = 1.0f;
  m_pulseDir = 0; // shrinking first

  // Init rotating glow (BITMAP_MAGIC SubType 11): LifeTime=24, Scale=0.8
  m_glowRotation = 0.0f;
  m_glowAlpha = 1.0f;

  // Init BMD cone model
  m_animFrame = 0.0f;
  m_bmdAlpha = 1.0f;

}

void ClickEffect::Hide() { m_visible = false; }

void ClickEffect::Update(float deltaTime) {
  if (!m_visible)
    return;

  float ticks = deltaTime * TICK_RATE;

  // === Parent lifetime ===
  m_parentLife -= ticks;

  // === Spawn new shrink rings every 15 ticks ===
  m_spawnTimer += ticks;
  if (m_spawnTimer >= SHRINK_SPAWN_INTERVAL && m_parentLife > 0.0f) {
    m_spawnTimer -= SHRINK_SPAWN_INTERVAL;
    for (auto &ring : m_shrinkRings) {
      if (!ring.active) {
        ring = {1.2f, 1.0f, SHRINK_LIFE, true};
        break;
      }
    }
  }

  // === Update shrink rings (EFFECT1) ===
  for (auto &ring : m_shrinkRings) {
    if (!ring.active)
      continue;
    ring.scale -= 0.04f * ticks;
    ring.life -= (int)std::max(1.0f, ticks);

    if (ring.life <= 10)
      ring.alpha -= 0.05f * ticks;

    if (ring.scale <= 0.2f || ring.alpha <= 0.0f || ring.life <= 0)
      ring.active = false;
  }

  // === Update pulse ring (EFFECT2) ===
  if (m_pulseAlpha > 0.0f) {
    if (m_pulseDir == 1) {
      m_pulseScale += 0.15f * ticks;
      if (m_pulseScale >= 1.8f)
        m_pulseDir = 0;
    } else {
      m_pulseScale -= 0.15f * ticks;
      if (m_pulseScale <= 0.8f)
        m_pulseDir = 1;
    }

    if (m_parentLife <= 10.0f)
      m_pulseAlpha -= 0.05f * ticks;
  }

  // === Update rotating glow (BITMAP_MAGIC SubType 11) ===
  m_glowRotation += 2.0f * ticks;
  if (m_parentLife <= 10.0f)
    m_glowAlpha -= 0.05f * ticks;

  // === Update BMD cone model ===
  if (m_bmd && !m_bmd->Actions.empty()) {
    int numKeys = m_bmd->Actions[0].NumAnimationKeys;
    m_animFrame += BMD_ANIM_SPEED * deltaTime;
    if (m_animFrame >= (float)numKeys)
      m_animFrame = std::fmod(m_animFrame, (float)numKeys);
  }
  // Main 5.2: BlendMeshLight -= 0.05f in last 10 ticks
  if (m_parentLife <= 10.0f)
    m_bmdAlpha -= 0.05f * ticks;

  // All done when parent expires and all effects dead
  bool anyShrinkActive = false;
  for (const auto &ring : m_shrinkRings)
    if (ring.active)
      anyShrinkActive = true;

  if (m_parentLife <= 0.0f && !anyShrinkActive && m_pulseAlpha <= 0.0f)
    m_visible = false;
}

void ClickEffect::Render(const glm::mat4 &view, const glm::mat4 &proj,
                         float deltaTime, Shader *shader) {
  if (!m_visible || !shader || !m_terrainData)
    return;

  float cx = m_pos.x, cz = m_pos.z;
  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

  // Ground layers: no depth test so marker is always visible on terrain
  uint64_t additiveState =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_ALWAYS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
  // 3D cone model keeps depth test
  uint64_t additiveStateDepth =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_LESS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);

  // === Layer 1: Rotating glow (BITMAP_MAGIC SubType 11) ===
  // Main 5.2: two overlapping layers at Scale and Scale*1.2
  if (m_glowAlpha > 0.0f && TexValid(m_glowTex)) {
    float glowScale = 0.8f; // Main 5.2: BITMAP_MAGIC SubType 11 Scale = 0.8
    float glowSize1 = MARKER_HALF_SIZE * glowScale * 1.5f;
    float glowSize2 = glowSize1 * 1.2f;
    float rot = glm::radians(m_glowRotation);
    float cosR = cosf(rot), sinR = sinf(rot);

    auto rotUV = [&](float u, float v) -> glm::vec2 {
      float du = u - 0.5f, dv = v - 0.5f;
      return {0.5f + du * cosR - dv * sinR, 0.5f + du * sinR + dv * cosR};
    };

    // Inner glow layer
    {
      float h = HEIGHT_OFFSET * 0.5f;
      ViewerVertex verts[4];
      verts[0].pos = {cx - glowSize1, getTerrainHeight(cx - glowSize1, cz - glowSize1) + h, cz - glowSize1};
      verts[1].pos = {cx + glowSize1, getTerrainHeight(cx + glowSize1, cz - glowSize1) + h, cz - glowSize1};
      verts[2].pos = {cx + glowSize1, getTerrainHeight(cx + glowSize1, cz + glowSize1) + h, cz + glowSize1};
      verts[3].pos = {cx - glowSize1, getTerrainHeight(cx - glowSize1, cz + glowSize1) + h, cz + glowSize1};
      for (int i = 0; i < 4; ++i) verts[i].normal = glm::vec3(0, 1, 0);
      verts[0].tex = rotUV(0, 0);
      verts[1].tex = rotUV(1, 0);
      verts[2].tex = rotUV(1, 1);
      verts[3].tex = rotUV(0, 1);
      bgfx::update(m_dynVbo, 0, bgfx::copy(verts, sizeof(verts)));
      submitQuad(shader, eye, m_glowTex,
                 std::clamp(m_glowAlpha * 0.5f, 0.0f, 1.0f), CURSOR_TINT,
                 additiveState);
    }

    // Outer glow layer (1.2x scale, counter-rotating)
    {
      float cosR2 = cosf(-rot * 0.7f), sinR2 = sinf(-rot * 0.7f);
      auto rotUV2 = [&](float u, float v) -> glm::vec2 {
        float du = u - 0.5f, dv = v - 0.5f;
        return {0.5f + du * cosR2 - dv * sinR2, 0.5f + du * sinR2 + dv * cosR2};
      };
      float h = HEIGHT_OFFSET * 0.5f;
      ViewerVertex verts[4];
      verts[0].pos = {cx - glowSize2, getTerrainHeight(cx - glowSize2, cz - glowSize2) + h, cz - glowSize2};
      verts[1].pos = {cx + glowSize2, getTerrainHeight(cx + glowSize2, cz - glowSize2) + h, cz - glowSize2};
      verts[2].pos = {cx + glowSize2, getTerrainHeight(cx + glowSize2, cz + glowSize2) + h, cz + glowSize2};
      verts[3].pos = {cx - glowSize2, getTerrainHeight(cx - glowSize2, cz + glowSize2) + h, cz + glowSize2};
      for (int i = 0; i < 4; ++i) verts[i].normal = glm::vec3(0, 1, 0);
      verts[0].tex = rotUV2(0, 0);
      verts[1].tex = rotUV2(1, 0);
      verts[2].tex = rotUV2(1, 1);
      verts[3].tex = rotUV2(0, 1);
      bgfx::update(m_dynVbo, 0, bgfx::copy(verts, sizeof(verts)));
      submitQuad(shader, eye, m_glowTex,
                 std::clamp(m_glowAlpha * 0.3f, 0.0f, 1.0f), CURSOR_TINT,
                 additiveState);
    }
  }

  // === Layer 2: Shrink rings (BITMAP_TARGET_POSITION_EFFECT1: cursorpin01) ===
  if (TexValid(m_shrinkTex)) {
    for (const auto &ring : m_shrinkRings) {
      if (!ring.active || ring.scale <= 0.0f)
        continue;
      float sz = MARKER_HALF_SIZE * std::max(0.1f, ring.scale);
      drawGroundQuad(cx, cz, sz, HEIGHT_OFFSET);
      submitQuad(shader, eye, m_shrinkTex,
                 std::clamp(ring.alpha, 0.0f, 1.0f), CURSOR_TINT,
                 additiveState);
    }
  }

  // === Layer 3: Pulsing ring (BITMAP_TARGET_POSITION_EFFECT2: cursorpin02) ===
  if (m_pulseAlpha > 0.0f && TexValid(m_pulseTex)) {
    float sz = MARKER_HALF_SIZE * std::clamp(m_pulseScale, 0.5f, 2.0f);
    drawGroundQuad(cx, cz, sz, HEIGHT_OFFSET);
    submitQuad(shader, eye, m_pulseTex,
               std::clamp(m_pulseAlpha, 0.0f, 1.0f), CURSOR_TINT,
               additiveState);
  }

  // === Layer 4: BMD spinning cone (MODEL_MOVE_TARGETPOSITION_EFFECT) ===
  if (m_bmd && !m_modelBuffers.empty() && m_modelBuffers[0].indexCount > 0 &&
      m_bmdAlpha > 0.0f) {
    auto bones = ComputeBoneMatricesInterpolated(m_bmd.get(), 0, m_animFrame);
    RetransformMeshWithBones(m_bmd->Meshes[0], bones, m_modelBuffers[0]);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(BMD_SCALE));

    auto &mb = m_modelBuffers[0];
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    shader->setTexture(0, "s_texColor", mb.texture);
    float alpha = std::clamp(m_bmdAlpha, 0.0f, 1.0f);
    shader->setVec4("u_params", glm::vec4(alpha, alpha, 0.0f, 0.0f));
    shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_baseTint", glm::vec4(CURSOR_TINT, 0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    bgfx::setState(additiveStateDepth);
    bgfx::submit(0, shader->program);
  }
}

void ClickEffect::Cleanup() {
  if (bgfx::isValid(m_dynVbo))
    bgfx::destroy(m_dynVbo);
  if (bgfx::isValid(m_quadEbo))
    bgfx::destroy(m_quadEbo);
  m_dynVbo = BGFX_INVALID_HANDLE;
  m_quadEbo = BGFX_INVALID_HANDLE;
  CleanupMeshBuffers(m_modelBuffers);
  m_bmd.reset();
  TexDestroy(m_shrinkTex);
  TexDestroy(m_pulseTex);
  TexDestroy(m_glowTex);
}

#include "Sky.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

// ─── BGFX path ──────────────────────────────────────────────────────────

// Sky vertex layout: pos(3f) + uv(2f) + alpha(1f packed as texcoord1.x)
static bgfx::VertexLayout s_skyLayout;
static bool s_skyLayoutInit = false;

static void initSkyLayout() {
  if (s_skyLayoutInit)
    return;
  s_skyLayout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 1, bgfx::AttribType::Float)
      .end();
  s_skyLayoutInit = true;
}

// Generate a soft radial glow texture for the sun (BGFX version)
static TexHandle createSunTextureBgfx() {
  const int SIZE = 64;
  std::vector<uint8_t> pixels(SIZE * SIZE * 4);
  float center = SIZE * 0.5f;
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      float dx = (x + 0.5f - center) / center;
      float dy = (y + 0.5f - center) / center;
      float dist = sqrtf(dx * dx + dy * dy);
      float core = std::max(0.0f, 1.0f - dist * 2.5f);
      float halo = std::max(0.0f, 1.0f - dist);
      halo *= halo;
      float brightness = core * 0.8f + halo * 0.4f;
      brightness = std::min(brightness, 1.0f);
      int idx = (y * SIZE + x) * 4;
      pixels[idx + 0] = (uint8_t)(255 * brightness);
      pixels[idx + 1] = (uint8_t)(240 * brightness);
      pixels[idx + 2] = (uint8_t)(200 * brightness);
      pixels[idx + 3] = (uint8_t)(255 * brightness);
    }
  }
  const bgfx::Memory *mem =
      bgfx::copy(pixels.data(), (uint32_t)pixels.size());
  return bgfx::createTexture2D(SIZE, SIZE, false, 1,
                               bgfx::TextureFormat::RGBA8,
                               BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                               mem);
}

void Sky::Init(const std::string &dataPath) {
  initSkyLayout();

  // Load sky texture from Object63/sky.OZJ
  std::string skyTexPath = dataPath + "Object63/sky.OZJ";
  texture = TextureLoader::LoadOZJ(skyTexPath);
  if (!TexValid(texture)) {
    std::cerr << "[Sky] Failed to load sky texture: " << skyTexPath
              << std::endl;
    return;
  }

  // Build cylinder + bottom cap geometry
  struct SkyVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float alpha;
  };

  std::vector<SkyVertex> vertices;
  std::vector<uint16_t> indices;

  // Cylinder band
  for (int i = 0; i <= SEGMENTS; ++i) {
    float angle = (float)i / SEGMENTS * 2.0f * (float)M_PI;
    float x = cosf(angle) * RADIUS;
    float z = sinf(angle) * RADIUS;
    float u = (float)i / SEGMENTS * 2.0f; // Repeat texture twice around

    // Bottom vertex (full opacity)
    vertices.push_back({{x, BAND_BOTTOM, z}, {u, 0.0f}, 1.0f});
    // Top vertex (fade out to transparent)
    vertices.push_back({{x, BAND_TOP, z}, {u, 1.0f}, 0.0f});
  }

  for (int i = 0; i < SEGMENTS; ++i) {
    uint16_t base = (uint16_t)(i * 2);
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 1);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
  }

  // Bottom cap disc: separate vertices with alpha=2.0 (shader renders as fog)
  uint16_t capStart = (uint16_t)vertices.size();
  vertices.push_back(
      {{0.0f, BAND_BOTTOM, 0.0f}, {0.5f, 0.0f}, 2.0f}); // center
  for (int i = 0; i < SEGMENTS; ++i) {
    float angle = (float)i / SEGMENTS * 2.0f * (float)M_PI;
    float x = cosf(angle) * RADIUS;
    float z = sinf(angle) * RADIUS;
    vertices.push_back({{x, BAND_BOTTOM, z}, {0.5f, 0.0f}, 2.0f});
  }
  for (int i = 0; i < SEGMENTS; ++i) {
    indices.push_back(capStart);
    indices.push_back(capStart + 1 + ((i + 1) % SEGMENTS));
    indices.push_back(capStart + 1 + i);
  }

  indexCount = (int)indices.size();

  // Create vertex/index buffers
  const bgfx::Memory *vMem = bgfx::copy(
      vertices.data(), (uint32_t)(vertices.size() * sizeof(SkyVertex)));
  vbo = bgfx::createVertexBuffer(vMem, s_skyLayout);

  const bgfx::Memory *iMem = bgfx::copy(
      indices.data(), (uint32_t)(indices.size() * sizeof(uint16_t)));
  ebo = bgfx::createIndexBuffer(iMem);

  // Load sky shader
  shader = Shader::Load("vs_sky.bin", "fs_sky.bin");
  if (!shader)
    std::cerr << "[Sky] Failed to load sky shader" << std::endl;

  // Sun billboard
  sunTexture = createSunTextureBgfx();

  struct SkyVertex sunVerts[] = {
      {{-1, -1, 0}, {0, 0}, 1.0f},
      {{1, -1, 0}, {1, 0}, 1.0f},
      {{1, 1, 0}, {1, 1}, 1.0f},
      {{-1, 1, 0}, {0, 1}, 1.0f},
  };
  uint16_t sunIdx[] = {0, 1, 2, 0, 2, 3};

  const bgfx::Memory *svMem = bgfx::copy(sunVerts, sizeof(sunVerts));
  sunVbo = bgfx::createVertexBuffer(svMem, s_skyLayout);
  const bgfx::Memory *siMem = bgfx::copy(sunIdx, sizeof(sunIdx));
  sunEbo = bgfx::createIndexBuffer(siMem);

  std::cout << "[Sky] Initialized with " << SEGMENTS << " segments, radius "
            << RADIUS << " (BGFX)" << std::endl;
}

void Sky::Render(const glm::mat4 &view, const glm::mat4 &projection,
                 const glm::vec3 &cameraPos, float luminosity) {
  if (!TexValid(texture) || !shader || indexCount == 0)
    return;

  // Set view transform (idempotent if caller already set it)
  bgfx::setViewTransform(0, glm::value_ptr(view),
                         glm::value_ptr(projection));

  // Sky cylinder centered on camera (horizontal only)
  glm::mat4 model = glm::translate(
      glm::mat4(1.0f), glm::vec3(cameraPos.x, 0.0f, cameraPos.z));
  bgfx::setTransform(glm::value_ptr(model));

  // Uniforms
  shader->setFloat("u_skyParams", luminosity);
  shader->setVec3("u_skyFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
  shader->setTexture(0, "s_skyTexture", texture);

  // Render state: depth test, no depth write, alpha blend
  uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_LESS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                             BGFX_STATE_BLEND_INV_SRC_ALPHA);
  bgfx::setState(state);

  bgfx::setVertexBuffer(0, vbo);
  bgfx::setIndexBuffer(ebo);
  bgfx::submit(0, shader->program);

  // ─── Sun sprite ─────────────────────────────────────────────────
  if (bgfx::isValid(sunVbo) && TexValid(sunTexture)) {
    float sunScale = 400.0f;
    glm::vec3 sunOffset(-900.0f, 550.0f, RADIUS * 0.85f);
    glm::vec3 sunPos = cameraPos + sunOffset;

    // Billboard: face camera by extracting right/up from view matrix
    glm::vec3 camRight =
        glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 camUp =
        glm::vec3(view[0][1], view[1][1], view[2][1]);

    glm::mat4 sunModel(1.0f);
    sunModel[0] = glm::vec4(camRight * sunScale, 0.0f);
    sunModel[1] = glm::vec4(camUp * sunScale, 0.0f);
    sunModel[2] = glm::vec4(glm::cross(camRight, camUp) * sunScale, 0.0f);
    sunModel[3] = glm::vec4(sunPos, 1.0f);

    bgfx::setTransform(glm::value_ptr(sunModel));

    // Black fog so sun renders as pure additive glow
    shader->setVec3("u_skyFogColor", glm::vec3(0.0f));
    shader->setTexture(0, "s_skyTexture", sunTexture);

    // Additive blend state
    uint64_t sunState =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                               BGFX_STATE_BLEND_ONE);
    bgfx::setState(sunState);

    bgfx::setVertexBuffer(0, sunVbo);
    bgfx::setIndexBuffer(sunEbo);
    bgfx::submit(0, shader->program);
  }
}

void Sky::Cleanup() {
  if (bgfx::isValid(vbo))
    bgfx::destroy(vbo);
  if (bgfx::isValid(ebo))
    bgfx::destroy(ebo);
  TexDestroy(texture);
  if (bgfx::isValid(sunVbo))
    bgfx::destroy(sunVbo);
  if (bgfx::isValid(sunEbo))
    bgfx::destroy(sunEbo);
  TexDestroy(sunTexture);
  vbo = BGFX_INVALID_HANDLE;
  ebo = BGFX_INVALID_HANDLE;
  sunVbo = BGFX_INVALID_HANDLE;
  sunEbo = BGFX_INVALID_HANDLE;
  shader.reset();
}

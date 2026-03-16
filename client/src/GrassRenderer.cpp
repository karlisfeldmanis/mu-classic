#include "GrassRenderer.hpp"
#include "TextureLoader.hpp"
#include <cstdio>
#include <cstdlib>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

// --- Constants ---

// Reference: Height = pBitmap->Height * 2.0f; TileGrass01/02 are 256x64, so
// 64*2=128
static constexpr float GRASS_HEIGHT_DEFAULT = 128.0f;
static constexpr float GRASS_HEIGHT_DEVIAS = 70.0f; // Shorter snowy grass
static constexpr float GRASS_ALPHA_DEVIAS = 0.9f;   // Slightly transparent
// Reference: Width = 64.0f / 256.0f = 0.25 (four grass blade variants per
// texture)
static constexpr float UV_WIDTH = 64.0f / 256.0f;

// ============================================================
// BGFX implementation
// ============================================================

// BGFX vertex layout for grass:
//   a_position  (3f) — world position
//   a_texcoord0 (2f) — UV coords
//   a_color0    (3f) — vertex lightmap color (not normalized)
//   a_texcoord1 (4f) — windWeight, gridX, texLayer, pad
struct BgfxGrassVertex {
  glm::vec3 position;
  glm::vec2 texCoord;
  glm::vec3 color;
  float windWeight;
  float gridX;
  float texLayer;
  float _pad;
};
static_assert(sizeof(BgfxGrassVertex) == 48, "BgfxGrassVertex must be 48 bytes");

static bgfx::VertexLayout s_grassLayout;
static bool s_grassLayoutInit = false;

static void ensureGrassLayout() {
  if (!s_grassLayoutInit) {
    s_grassLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    3, bgfx::AttribType::Float, false)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .end();
    s_grassLayoutInit = true;
  }
}

void GrassRenderer::Init() {
  shader = Shader::Load("vs_grass.bin", "fs_grass.bin");
  if (!shader) {
    fprintf(stderr, "[GrassRenderer] Failed to load BGFX grass shader\n");
  }
}

void GrassRenderer::Load(const TerrainData &data, int worldID,
                          const std::string &dataPath,
                          const std::vector<bool> *objectOccupancy) {
  m_worldID = worldID;
  const int SIZE = TerrainParser::TERRAIN_SIZE; // 256

  // Per-world grass configuration
  bool isSnowWorld = (worldID == 3); // Devias
  float grassHeight = isSnowWorld ? GRASS_HEIGHT_DEVIAS : GRASS_HEIGHT_DEFAULT;
  m_alphaMult = isSnowWorld ? GRASS_ALPHA_DEVIAS : 1.0f;

  // Load grass textures (OZT for alpha)
  std::string worldDir = dataPath + "/World" + std::to_string(worldID);
  std::string fallbackDir = dataPath + "/World1";
  const char *names[3] = {"TileGrass01", "TileGrass02", "TileGrass03"};
  const char *snowyPreferred = "TileGrass02";

  for (int i = 0; i < 3; ++i) {
    std::string ozt = worldDir + "/" + names[i] + ".OZT";
    grassTextures[i] = TextureLoader::LoadOZT(ozt);
    if (!TexValid(grassTextures[i])) {
      if (isSnowWorld) {
        std::string snowOzt = worldDir + "/" + snowyPreferred + ".OZT";
        grassTextures[i] = TextureLoader::LoadOZT(snowOzt);
        if (TexValid(grassTextures[i]))
          std::cout << "[GrassRenderer] " << names[i] << " using snowy "
                    << snowyPreferred << std::endl;
      }
      for (int alt = 0; alt < 3 && !TexValid(grassTextures[i]); ++alt) {
        if (alt == i) continue;
        std::string altOzt = worldDir + "/" + names[alt] + ".OZT";
        grassTextures[i] = TextureLoader::LoadOZT(altOzt);
        if (TexValid(grassTextures[i]))
          std::cout << "[GrassRenderer] " << names[i] << " using " << names[alt]
                    << " from same world" << std::endl;
      }
    }
    if (!TexValid(grassTextures[i]) && worldID != 1 && !isSnowWorld) {
      std::string fallbackOzt = fallbackDir + "/" + names[i] + ".OZT";
      grassTextures[i] = TextureLoader::LoadOZT(fallbackOzt);
      if (TexValid(grassTextures[i]))
        std::cout << "[GrassRenderer] " << names[i] << " fallback from World1"
                  << std::endl;
    }
    if (TexValid(grassTextures[i])) {
      std::cout << "[GrassRenderer] Loaded " << names[i] << std::endl;
    } else {
      std::cerr << "[GrassRenderer] Failed to load " << names[i] << std::endl;
    }
  }

  // Generate grass billboard quads
  std::vector<BgfxGrassVertex> vertices;
  std::vector<uint32_t> indices;

  srand(42);
  float rowUVOffsets[256];
  for (int z = 0; z < SIZE; ++z) {
    rowUVOffsets[z] = (float)(rand() % 4) / 4.0f;
  }

  for (int z = 0; z < SIZE - 1; ++z) {
    for (int x = 0; x < SIZE - 1; ++x) {
      int idx = z * SIZE + x;

      uint8_t layer1 = data.mapping.layer1[idx];
      if (layer1 != 0 && layer1 != 1) continue;
      if (data.mapping.alpha[idx] > 0.0f) continue;

      if (idx < (int)data.mapping.attributes.size() &&
          (data.mapping.attributes[idx] & 0x08) != 0)
        continue;

      {
        bool nearRift = false;
        const int GRASS_RIFT_MARGIN = 3;
        for (int dz2 = -GRASS_RIFT_MARGIN;
             dz2 <= GRASS_RIFT_MARGIN && !nearRift; ++dz2) {
          for (int dx2 = -GRASS_RIFT_MARGIN;
               dx2 <= GRASS_RIFT_MARGIN && !nearRift; ++dx2) {
            int nz = z + dz2, nx = x + dx2;
            if (nz >= 0 && nz < SIZE && nx >= 0 && nx < SIZE) {
              int ni = nz * SIZE + nx;
              if (ni < (int)data.mapping.attributes.size() &&
                  (data.mapping.attributes[ni] & 0x08) != 0) {
                nearRift = true;
              }
            }
          }
        }
        if (nearRift) continue;
      }

      float h_sw = data.heightmap[z * SIZE + x];
      float h_ne = data.heightmap[(z + 1) * SIZE + (x + 1)];

      float su = (float)x * UV_WIDTH + rowUVOffsets[z];
      su = su - floorf(su);
      float uLeft = su;
      float uRight = su + UV_WIDTH;

      glm::vec3 lightColor(1.0f);
      if (idx < (int)data.lightmap.size()) {
        lightColor = data.lightmap[idx];
      }

      float texLayer = (float)layer1;

      uint32_t baseIdx = (uint32_t)vertices.size();

      glm::vec3 posSW((float)z * 100.0f, h_sw, (float)x * 100.0f);
      glm::vec3 posNE((float)(z + 1) * 100.0f, h_ne, (float)(x + 1) * 100.0f);

      // Bottom-left (SW, anchored)
      BgfxGrassVertex bl;
      bl.position = posSW;
      bl.texCoord = glm::vec2(uLeft, 1.0f);
      bl.color = lightColor;
      bl.windWeight = 0.0f;
      bl.gridX = (float)x;
      bl.texLayer = texLayer;
      bl._pad = 0.0f;
      vertices.push_back(bl);

      // Bottom-right (NE, anchored)
      BgfxGrassVertex br;
      br.position = posNE;
      br.texCoord = glm::vec2(uRight, 1.0f);
      br.color = lightColor;
      br.windWeight = 0.0f;
      br.gridX = (float)(x + 1);
      br.texLayer = texLayer;
      br._pad = 0.0f;
      vertices.push_back(br);

      // Top-right (NE, elevated + wind)
      BgfxGrassVertex tr;
      tr.position = glm::vec3(posNE.x, posNE.y + grassHeight, posNE.z - 50.0f);
      tr.texCoord = glm::vec2(uRight, 0.0f);
      tr.color = lightColor;
      tr.windWeight = 1.0f;
      tr.gridX = (float)(x + 1);
      tr.texLayer = texLayer;
      tr._pad = 0.0f;
      vertices.push_back(tr);

      // Top-left (SW, elevated + wind)
      BgfxGrassVertex tl;
      tl.position = glm::vec3(posSW.x, posSW.y + grassHeight, posSW.z - 50.0f);
      tl.texCoord = glm::vec2(uLeft, 0.0f);
      tl.color = lightColor;
      tl.windWeight = 1.0f;
      tl.gridX = (float)x;
      tl.texLayer = texLayer;
      tl._pad = 0.0f;
      vertices.push_back(tl);

      indices.push_back(baseIdx + 0);
      indices.push_back(baseIdx + 1);
      indices.push_back(baseIdx + 2);
      indices.push_back(baseIdx + 0);
      indices.push_back(baseIdx + 2);
      indices.push_back(baseIdx + 3);
    }
  }

  indexCount = (int)indices.size();
  std::cout << "[GrassRenderer] Generated " << (indexCount / 6)
            << " grass billboards (" << vertices.size() << " vertices)"
            << std::endl;

  if (indexCount == 0) return;

  // Upload to BGFX
  ensureGrassLayout();

  const bgfx::Memory *vMem = bgfx::copy(vertices.data(),
      (uint32_t)(vertices.size() * sizeof(BgfxGrassVertex)));
  vbo = bgfx::createVertexBuffer(vMem, s_grassLayout);

  const bgfx::Memory *iMem = bgfx::copy(indices.data(),
      (uint32_t)(indices.size() * sizeof(uint32_t)));
  ebo = bgfx::createIndexBuffer(iMem, BGFX_BUFFER_INDEX32);
}

void GrassRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                            float time, const glm::vec3 &viewPos,
                            const std::vector<PushSource> &pushSources) {
  if (indexCount == 0 || !shader) return;

  // View transform (may already be set by terrain, but set for safety)
  bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(projection));

  // Pack params: x=time, y=numPushers, z=luminosity, w=alphaMult
  int count = std::min((int)pushSources.size(), 17);
  shader->setVec4("u_grassParams",
      glm::vec4(time, (float)count, m_luminosity, m_alphaMult));

  // Pack fog: x=fogNear, y=fogFar
  shader->setVec4("u_grassFog",
      glm::vec4(fogNear, fogFar, 0.0f, 0.0f));

  shader->setVec3("u_fogColor", fogColor);
  shader->setVec3("u_viewPos", viewPos);

  // Upload push sources as vec4 array (xyz=pos, w=radius)
  float pushData[17 * 4] = {};
  for (int i = 0; i < count; ++i) {
    pushData[i * 4 + 0] = pushSources[i].pos.x;
    pushData[i * 4 + 1] = pushSources[i].pos.y;
    pushData[i * 4 + 2] = pushSources[i].pos.z;
    pushData[i * 4 + 3] = pushSources[i].radius;
  }
  // BGFX requires num >= 1 for array uniforms
  int pushUploadCount = (count > 0) ? count : 1;
  if (!bgfx::isValid(u_pushPosRadius)) {
    u_pushPosRadius = bgfx::createUniform("u_pushPosRadius",
        bgfx::UniformType::Vec4, 17);
  }
  bgfx::setUniform(u_pushPosRadius, pushData, pushUploadCount);

  // Bind textures
  shader->setTexture(0, "s_grassTex0", grassTextures[0]);
  shader->setTexture(1, "s_grassTex1", grassTextures[1]);
  shader->setTexture(2, "s_grassTex2", grassTextures[2]);

  // State: alpha blend, depth test, no face culling (both sides visible)
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA);
  // No BGFX_STATE_CULL_* = no face culling
  bgfx::setState(state);

  bgfx::setVertexBuffer(0, vbo);
  bgfx::setIndexBuffer(ebo);
  bgfx::submit(0, shader->program);
}

void GrassRenderer::Cleanup() {
  if (bgfx::isValid(vbo)) { bgfx::destroy(vbo); vbo = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(ebo)) { bgfx::destroy(ebo); ebo = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(u_pushPosRadius)) {
    bgfx::destroy(u_pushPosRadius);
    u_pushPosRadius = BGFX_INVALID_HANDLE;
  }
  for (int i = 0; i < 3; ++i) TexDestroy(grassTextures[i]);
  if (shader) { shader->destroy(); shader.reset(); }
  indexCount = 0;
}

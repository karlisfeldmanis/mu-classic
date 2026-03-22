#include "Terrain.hpp"
#include "TextureLoader.hpp"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <set>

// ============================================================================
// BGFX Implementation
// ============================================================================

static bgfx::VertexLayout s_terrainLayout;
static bool s_terrainLayoutInit = false;

static void initTerrainLayout() {
  if (s_terrainLayoutInit)
    return;
  s_terrainLayout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float, false)
      .end();
  s_terrainLayoutInit = true;
}

Terrain::Terrain() {}

Terrain::~Terrain() {
  // Do NOT call Cleanup() here — global/static destructors run after
  // bgfx::shutdown() via __cxa_finalize_ranges, so BGFX context is gone.
  // All BGFX resources are cleaned up via explicit Cleanup() in main.cpp
  // shutdown path before bgfx::shutdown().
}

void Terrain::Cleanup() {
  if (bgfx::isValid(vbo))  { bgfx::destroy(vbo);  vbo = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(ebo))  { bgfx::destroy(ebo);  ebo = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(voidVbo)) { bgfx::destroy(voidVbo); voidVbo = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(voidEbo)) { bgfx::destroy(voidEbo); voidEbo = BGFX_INVALID_HANDLE; }
  voidIndexCount = 0;
  TexDestroy(tileTextureArray);
  TexDestroy(layer1InfoMap);
  TexDestroy(layer2InfoMap);
  TexDestroy(alphaMap);
  TexDestroy(attributeMap);
  TexDestroy(symmetryMap);
  TexDestroy(lightmapTex);
  if (shader)
    shader->destroy();
  shader.reset();
}

void Terrain::Init() {
  initTerrainLayout();
  shader = Shader::Load("vs_terrain.bin", "fs_terrain.bin");
  if (!shader) {
    std::cerr << "[Terrain] Failed to load BGFX terrain shader" << std::endl;
  }
}

void Terrain::Load(const TerrainData &data, int worldID,
                   const std::string &data_path,
                   const std::vector<uint8_t> &rawAttributes,
                   const std::vector<bool> &bridgeMask) {
  this->worldID = worldID;
  const auto &meshAttrs =
      rawAttributes.empty() ? data.mapping.attributes : rawAttributes;
  const int S = TerrainParser::TERRAIN_SIZE;

  m_heightmap = data.heightmap;

  // Pre-compute void/bridge distance fields (needed by both setupMesh for
  // vertex darkening and by lightmap processing below).
  bool hasBridge = (bridgeMask.size() >= (size_t)(S * S));
  const float BRIDGE_PROTECT = 10.0f;

  std::vector<float> bridgeDist(S * S, 999.0f);
  if (hasBridge) {
    for (int i = 0; i < S * S; i++)
      if (bridgeMask[i]) bridgeDist[i] = 0.0f;
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++) {
        int i = y * S + x;
        if (x > 0) bridgeDist[i] = std::min(bridgeDist[i], bridgeDist[i-1] + 1.0f);
        if (y > 0) bridgeDist[i] = std::min(bridgeDist[i], bridgeDist[i-S] + 1.0f);
      }
    for (int y = S-1; y >= 0; y--)
      for (int x = S-1; x >= 0; x--) {
        int i = y * S + x;
        if (x < S-1) bridgeDist[i] = std::min(bridgeDist[i], bridgeDist[i+1] + 1.0f);
        if (y < S-1) bridgeDist[i] = std::min(bridgeDist[i], bridgeDist[i+S] + 1.0f);
      }
  }

  std::vector<float> voidDist(S * S, 999.0f);
  if (!meshAttrs.empty()) {
    for (int i = 0; i < S * S; i++) {
      if ((meshAttrs[i] & 0x08) && !(hasBridge && bridgeMask[i]))
        voidDist[i] = 0.0f;
    }
    for (int pass = 0; pass < 2; pass++) {
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int i = y * S + x;
          if (x > 0) voidDist[i] = std::min(voidDist[i], voidDist[i-1] + 1.0f);
          if (y > 0) voidDist[i] = std::min(voidDist[i], voidDist[i-S] + 1.0f);
        }
      for (int y = S-1; y >= 0; y--)
        for (int x = S-1; x >= 0; x--) {
          int i = y * S + x;
          if (x < S-1) voidDist[i] = std::min(voidDist[i], voidDist[i+1] + 1.0f);
          if (y < S-1) voidDist[i] = std::min(voidDist[i], voidDist[i+S] + 1.0f);
        }
    }
  }

  // terrainDist: for void cells, distance to nearest non-void cell.
  // Used by void floor mesh to gradient from terrain edge into deep void.
  std::vector<float> terrainDist(S * S, 999.0f);
  if (!meshAttrs.empty()) {
    for (int i = 0; i < S * S; i++) {
      bool isVoidCell = (meshAttrs[i] & 0x08) && !(hasBridge && bridgeMask[i]);
      if (!isVoidCell) terrainDist[i] = 0.0f;
    }
    for (int pass = 0; pass < 2; pass++) {
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int i = y * S + x;
          if (x > 0) terrainDist[i] = std::min(terrainDist[i], terrainDist[i-1] + 1.0f);
          if (y > 0) terrainDist[i] = std::min(terrainDist[i], terrainDist[i-S] + 1.0f);
        }
      for (int y = S-1; y >= 0; y--)
        for (int x = S-1; x >= 0; x--) {
          int i = y * S + x;
          if (x < S-1) terrainDist[i] = std::min(terrainDist[i], terrainDist[i+1] + 1.0f);
          if (y < S-1) terrainDist[i] = std::min(terrainDist[i], terrainDist[i+S] + 1.0f);
        }
    }
  }

  // nearTerrainH: for void cells, propagate the nearest terrain cell's height.
  // Void cells' own heights are valley-floor values (too low for height-based
  // fade). We need the cliff-top height so the shader can fade from cliff face
  // down to black.
  std::vector<float> nearTerrainH(S * S, 0.0f);
  if (!m_heightmap.empty()) {
    for (int i = 0; i < S * S; i++)
      nearTerrainH[i] = m_heightmap[i];
    // Multi-pass sweep: void cells take height from closest-to-terrain neighbor
    for (int pass = 0; pass < 4; pass++) {
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int i = y * S + x;
          bool isVoid = (meshAttrs[i] & 0x08) && !(hasBridge && bridgeMask[i]);
          if (!isVoid) continue;
          float bestDist = terrainDist[i];
          if (x > 0 && terrainDist[i-1] < bestDist)   { bestDist = terrainDist[i-1]; nearTerrainH[i] = nearTerrainH[i-1]; }
          if (y > 0 && terrainDist[i-S] < bestDist)    { bestDist = terrainDist[i-S]; nearTerrainH[i] = nearTerrainH[i-S]; }
        }
      for (int y = S-1; y >= 0; y--)
        for (int x = S-1; x >= 0; x--) {
          int i = y * S + x;
          bool isVoid = (meshAttrs[i] & 0x08) && !(hasBridge && bridgeMask[i]);
          if (!isVoid) continue;
          float bestDist = terrainDist[i];
          if (x > 0 && terrainDist[i-1] < bestDist)     { bestDist = terrainDist[i-1]; nearTerrainH[i] = nearTerrainH[i-1]; }
          if (x < S-1 && terrainDist[i+1] < bestDist)   { bestDist = terrainDist[i+1]; nearTerrainH[i] = nearTerrainH[i+1]; }
          if (y > 0 && terrainDist[i-S] < bestDist)      { bestDist = terrainDist[i-S]; nearTerrainH[i] = nearTerrainH[i-S]; }
          if (y < S-1 && terrainDist[i+S] < bestDist)    { bestDist = terrainDist[i+S]; nearTerrainH[i] = nearTerrainH[i+S]; }
        }
    }
  }

  setupMesh(data.heightmap, data.lightmap, meshAttrs, bridgeMask, voidDist, terrainDist, bridgeDist);
  std::string worldDir = data_path + "/World" + std::to_string(worldID);
  setupTextures(data, worldDir);

  // Lightmap alpha: -1.0 for terrain cells (disables height fade in shader),
  // nearTerrainH for void cells (cliff-top height for vertical fade gradient).
  if ((int)m_baselineLightRGBA.size() >= S * S * 4) {
    for (int i = 0; i < S * S; i++) {
      bool isVoid = !meshAttrs.empty() && (meshAttrs[i] & 0x08) && !(hasBridge && bridgeMask[i]);
      m_baselineLightRGBA[i * 4 + 3] = isVoid ? nearTerrainH[i] : -1.0f;
    }
  }

  // Upload voidDist as R8 texture for shader-based edge fade.
  // Normalize: 0=void, 1=dist>=6 (fully lit). Bridge-adjacent cells forced to 1.
  {
    std::vector<uint8_t> vdData(S * S);
    int voidCount = 0;
    for (int i = 0; i < S * S; i++) {
      // Protect bridge area: cells near bridges stay fully lit
      if (bridgeDist[i] < BRIDGE_PROTECT) {
        vdData[i] = 255;
        continue;
      }
      float d = std::min(voidDist[i] / 6.0f, 1.0f); // 0-6 cells → 0.0-1.0
      vdData[i] = (uint8_t)(d * 255.0f);
      if (voidDist[i] < 0.5f) voidCount++;
    }
    printf("[Terrain] voidDist texture: %d void cells, bridgeProtect=%.0f\n", voidCount, BRIDGE_PROTECT);
    const bgfx::Memory *mem = bgfx::copy(vdData.data(), S * S);
    voidDistMap = bgfx::createTexture2D(
        S, S, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Collect void edge positions (cells adjacent to void) for mist spawning.
  // Sample every 2nd cell to keep the count reasonable.
  m_voidEdgePositions.clear();
  for (int y = 0; y < S; y += 2) {
    for (int x = 0; x < S; x += 2) {
      int i = y * S + x;
      if (voidDist[i] >= 0.5f && voidDist[i] <= 2.5f) {
        float wx = (float)y * 100.0f;
        float wz = (float)x * 100.0f;
        float wy = data.heightmap[i];
        m_voidEdgePositions.push_back(glm::vec3(wx, wy, wz));
      }
    }
  }

  // Darken lightmap near void edges (Main 5.2: pre-baked black lightmap at
  // void areas). Cells within the fade radius get progressively darker.
  // Bridge cells and their neighbors are fully protected.
  if (!meshAttrs.empty() && (int)m_baselineLightRGBA.size() >= S * S * 4) {
    const float FADE_RADIUS = 8.0f;

    // Compute per-cell depression: how far below its highest neighbor this cell
    // sits. Cells at cliff tops / flat terrain have depression~0 (stay bright).
    // Cells on cliff faces going down into voids have high depression (get dark).
    // This ensures only the vertical cliff geometry gets void darkening, not the
    // walkable terrain at cliff edges or bridge approaches.
    std::vector<float> depression(S * S, 0.0f);
    if (!m_heightmap.empty()) {
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int i = y * S + x;
          float h = m_heightmap[i];
          float maxNeighborH = h;
          if (x > 0)     maxNeighborH = std::max(maxNeighborH, m_heightmap[i - 1]);
          if (x < S - 1) maxNeighborH = std::max(maxNeighborH, m_heightmap[i + 1]);
          if (y > 0)     maxNeighborH = std::max(maxNeighborH, m_heightmap[i - S]);
          if (y < S - 1) maxNeighborH = std::max(maxNeighborH, m_heightmap[i + S]);
          depression[i] = maxNeighborH - h; // 0 = at top/flat, positive = sunken
        }
    }

    // Lightmap floor: prevent pitch-black walkable terrain at steep edges.
    for (int i = 0; i < S * S; i++) {
      if (meshAttrs[i] & 0x08) continue;
      if (hasBridge && bridgeMask[i]) continue;
      for (int c = 0; c < 3; c++)
        m_baselineLightRGBA[i * 4 + c] = std::max(m_baselineLightRGBA[i * 4 + c], 0.30f);
    }

    // Subtle terrain-side darkening near void edges (voidDist 1-2).
    // Creates a smooth transition from bright terrain to dark cliff face.
    for (int i = 0; i < S * S; i++) {
      if (meshAttrs[i] & 0x08) continue;
      if (hasBridge && bridgeMask[i]) continue;
      if (bridgeDist[i] < BRIDGE_PROTECT) continue;

      float vd = voidDist[i];
      if (vd > 2.0f) continue;
      // vd=1 → 85%, vd=2 → 95%
      float darken = 0.75f + 0.125f * vd;
      darken = std::min(darken, 1.0f);
      for (int c = 0; c < 3; c++)
        m_baselineLightRGBA[i * 4 + c] *= darken;
    }

    // SET void cell lightmap values based on terrainDist.
    // Cliff face cells inherit brightness from nearest terrain lightmap.
    // Deep void cells fade to dark.
    for (int i = 0; i < S * S; i++) {
      if (!(meshAttrs[i] & 0x08)) continue;
      if (hasBridge && bridgeMask[i]) continue;

      float td = terrainDist[i];
      // Find nearest terrain cell's lightmap brightness
      int cy = i / S, cx = i % S;
      float nearBright = 0.40f;
      bool foundTerr = false;
      for (int r = 1; r <= 6; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
          for (int dx = -r; dx <= r; ++dx) {
            if (abs(dy) != r && abs(dx) != r) continue;
            int ny = cy + dy, nx = cx + dx;
            if (ny < 0 || ny >= S || nx < 0 || nx >= S) continue;
            int ni = ny * S + nx;
            if (!(meshAttrs[ni] & 0x08)) {
              float b = std::max({m_baselineLightRGBA[ni * 4 + 0],
                                  m_baselineLightRGBA[ni * 4 + 1],
                                  m_baselineLightRGBA[ni * 4 + 2]});
              nearBright = std::max(nearBright, b);
              foundTerr = true;
            }
          }
        }
        if (foundTerr) break;
      }

      // XZ distance fade: bright near terrain, dimming deeper in void
      float t = std::min(td / 6.0f, 1.0f);
      float ss = t * t * (3.0f - 2.0f * t);
      float brightness = nearBright * (1.0f - ss * 0.6f);

      for (int c = 0; c < 3; c++)
        m_baselineLightRGBA[i * 4 + c] = brightness;
    }

    // Map-edge brightness floor: JPEG lightmap has pre-baked dark values
    // at map boundaries. Apply minimum brightness near edges to prevent
    // excessive darkness on walkable terrain at the map perimeter.
    {
      const int EDGE_RADIUS = 12; // cells from edge that get brightened
      const float EDGE_FLOOR = 0.55f;
      for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
          int i = y * S + x;
          if (meshAttrs[i] & 0x08) continue; // skip void
          int edgeDist = std::min({x, y, S - 1 - x, S - 1 - y});
          if (edgeDist >= EDGE_RADIUS) continue;
          float t = (float)edgeDist / (float)EDGE_RADIUS; // 0=edge, 1=interior
          float floor = EDGE_FLOOR * (1.0f - t);
          for (int c = 0; c < 3; c++)
            m_baselineLightRGBA[i * 4 + c] = std::max(m_baselineLightRGBA[i * 4 + c], floor);
        }
      }
    }

    // Re-upload lightmap texture with modified void edges
    {
      TexDestroy(lightmapTex);
      const bgfx::Memory *mem = bgfx::copy(
          m_baselineLightRGBA.data(), m_baselineLightRGBA.size() * sizeof(float));
      lightmapTex = bgfx::createTexture2D(
          S, S, false, 1, bgfx::TextureFormat::RGBA32F,
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
    }
  }
}

float Terrain::GetHeight(float x, float y) {
  if (m_heightmap.empty())
    return 0.0f;
  float gridZ = x / 100.0f;
  float gridX = y / 100.0f;
  int z = (int)gridZ;
  int x_idx = (int)gridX;
  if (z < 0 || z >= 255 || x_idx < 0 || x_idx >= 255)
    return 0.0f;
  float fx = gridX - x_idx;
  float fz = gridZ - z;
  int size = 256;
  float h00 = m_heightmap[z * size + x_idx];
  float h10 = m_heightmap[z * size + (x_idx + 1)];
  float h01 = m_heightmap[(z + 1) * size + x_idx];
  float h11 = m_heightmap[(z + 1) * size + (x_idx + 1)];
  float h0 = h00 * (1.0f - fx) + h10 * fx;
  float h1 = h01 * (1.0f - fx) + h11 * fx;
  return h0 * (1.0f - fz) + h1 * fz;
}

void Terrain::SetPointLights(const std::vector<glm::vec3> &positions,
                             const std::vector<glm::vec3> &colors,
                             const std::vector<float> &ranges,
                             const std::vector<int> &objectTypes) {
  plCount = (int)positions.size();
  plPositions.assign(positions.begin(), positions.begin() + plCount);
  plColors.assign(colors.begin(), colors.begin() + plCount);
  plRanges.assign(ranges.begin(), ranges.begin() + plCount);
  if ((int)objectTypes.size() >= plCount)
    plObjectTypes.assign(objectTypes.begin(), objectTypes.begin() + plCount);
  else
    plObjectTypes.assign(plCount, 0);
}

void Terrain::Render(const glm::mat4 &view, const glm::mat4 &projection,
                     float time, const glm::vec3 &viewPos) {
  if (indexCount == 0 || !shader)
    return;

  // View transform (view 0)
  bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(projection));

  // Model transform (identity for terrain)
  glm::mat4 model(1.0f);
  bgfx::setTransform(glm::value_ptr(model));

  // Packed uniforms
  shader->setVec4("u_terrainParams",
                  glm::vec4(time, (float)debugMode, m_luminosity, 0.0f));
  shader->setVec4("u_fogParams",
                  glm::vec4(m_fogNear, m_fogFar, m_fogHeightBase, m_fogHeightFade));
  shader->setVec3("u_fogColor", m_fogColor);
  shader->setVec3("u_viewPos", viewPos);

  // Apply dynamic point lights to lightmap (CPU-side)
  applyDynamicLights();

  // Bind textures
  shader->setTexture(0, "s_tileTextures", tileTextureArray);
  shader->setTexture(1, "s_layer1Map", layer1InfoMap);
  shader->setTexture(2, "s_layer2Map", layer2InfoMap);
  shader->setTexture(3, "s_alphaMap", alphaMap);
  shader->setTexture(4, "s_attributeMap", attributeMap);
  shader->setTexture(5, "s_symmetryMap", symmetryMap);
  shader->setTexture(6, "s_lightMap", lightmapTex);
  shader->setTexture(8, "s_voidDistMap", voidDistMap);

  // Shadow map
  float shadowEnabled = m_shadowEnabled ? 1.0f : 0.0f;
  float shadowDebug = m_shadowDebug ? 1.0f : 0.0f;
  shader->setVec4("u_shadowParams", glm::vec4(shadowEnabled, shadowDebug, 0.0f, 0.0f));
  if (m_shadowEnabled) {
    shader->setMat4("u_lightMtx", m_lightMtx);
    shader->setTexture(7, "s_shadowMap", m_shadowMapTex);
  }
  { static bool loggedOnce = false;
    if (!loggedOnce) {
      printf("[Terrain] Render: shadow=%d debug=%d tex_valid=%d\n",
             m_shadowEnabled, m_shadowDebug, bgfx::isValid(m_shadowMapTex));
      loggedOnce = true;
    }
  }

  // State: opaque, depth test+write
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
  bgfx::setState(state);

  bgfx::setVertexBuffer(0, vbo);
  bgfx::setIndexBuffer(ebo);
  bgfx::submit(0, shader->program);

  // Void mesh: cliff walls — no backface culling, vertex color gradient
  if (voidIndexCount > 0 && bgfx::isValid(voidVbo)) {
    // Re-set all uniforms/textures for this draw call (BGFX requires per-submit)
    shader->setVec4("u_terrainParams",
                    glm::vec4(time, (float)debugMode, m_luminosity, 1.0f)); // w=1 enables void fade
    shader->setVec4("u_fogParams",
                    glm::vec4(m_fogNear, m_fogFar, m_fogHeightBase, m_fogHeightFade));
    shader->setVec3("u_fogColor", m_fogColor);
    shader->setVec3("u_viewPos", viewPos);
    shader->setVec4("u_shadowParams", glm::vec4(0.0f)); // no shadows on cliff
    shader->setTexture(0, "s_tileTextures", tileTextureArray);
    shader->setTexture(1, "s_layer1Map", layer1InfoMap);
    shader->setTexture(2, "s_layer2Map", layer2InfoMap);
    shader->setTexture(3, "s_alphaMap", alphaMap);
    shader->setTexture(4, "s_attributeMap", attributeMap);
    shader->setTexture(5, "s_symmetryMap", symmetryMap);
    shader->setTexture(6, "s_lightMap", lightmapTex);
    shader->setTexture(8, "s_voidDistMap", voidDistMap);

    uint64_t voidState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_DEPTH_TEST_LESS |
                         BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                               BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setTransform(glm::value_ptr(model));
    bgfx::setState(voidState);
    bgfx::setVertexBuffer(0, voidVbo);
    bgfx::setIndexBuffer(voidEbo);
    bgfx::submit(0, shader->program);
  }
}

void Terrain::RenderToView(bgfx::ViewId viewId, const glm::mat4 &view,
                           const glm::mat4 &proj, float time,
                           const glm::vec3 &viewPos) {
  if (indexCount == 0 || !shader)
    return;

  bgfx::setViewTransform(viewId, glm::value_ptr(view), glm::value_ptr(proj));

  glm::mat4 model(1.0f);
  bgfx::setTransform(glm::value_ptr(model));

  // Uniforms — no fog, no shadows for minimap
  shader->setVec4("u_terrainParams",
                  glm::vec4(time, 0.0f, m_luminosity, -1.0f)); // w=-1: minimap, no void darkening
  shader->setVec4("u_fogParams",
                  glm::vec4(99999.0f, 100000.0f, -99999.0f, 1.0f));
  shader->setVec3("u_fogColor", glm::vec3(0.0f));
  shader->setVec3("u_viewPos", viewPos);
  shader->setVec4("u_shadowParams", glm::vec4(0.0f));

  applyDynamicLights();

  shader->setTexture(0, "s_tileTextures", tileTextureArray);
  shader->setTexture(1, "s_layer1Map", layer1InfoMap);
  shader->setTexture(2, "s_layer2Map", layer2InfoMap);
  shader->setTexture(3, "s_alphaMap", alphaMap);
  shader->setTexture(4, "s_attributeMap", attributeMap);
  shader->setTexture(5, "s_symmetryMap", symmetryMap);
  shader->setTexture(6, "s_lightMap", lightmapTex);
  shader->setTexture(8, "s_voidDistMap", voidDistMap);

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
  bgfx::setState(state);

  bgfx::setVertexBuffer(0, vbo);
  bgfx::setIndexBuffer(ebo);
  bgfx::submit(viewId, shader->program);
}

void Terrain::SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx) {
  m_shadowMapTex = tex;
  m_lightMtx = lightMtx;
  m_shadowEnabled = bgfx::isValid(tex);
}

void Terrain::setupMesh(const std::vector<float> &heightmap,
                        const std::vector<glm::vec3> &lightmap,
                        const std::vector<uint8_t> &rawAttributes,
                        const std::vector<bool> &bridgeMask,
                        const std::vector<float> &voidDist,
                        const std::vector<float> &terrainDist,
                        const std::vector<float> &bridgeDist) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  const int size = TerrainParser::TERRAIN_SIZE;
  bool hasAttrs = (rawAttributes.size() >= (size_t)(size * size));
  bool hasBridgeMask = (bridgeMask.size() >= (size_t)(size * size));
  bool hasVoidDist = (voidDist.size() >= (size_t)(size * size));

  for (int z = 0; z < size; ++z) {
    for (int x = 0; x < size; ++x) {
      int idx = z * size + x;
      Vertex v;
      v.position = glm::vec3(static_cast<float>(z) * 100.0f, heightmap[idx],
                             static_cast<float>(x) * 100.0f);
      v.texCoord =
          glm::vec2(static_cast<float>(x) / size, static_cast<float>(z) / size);
      v.color = glm::vec3(1.0f);
      vertices.push_back(v);
    }
  }

  // Generate indices: skip void quads (normal terrain rendering)
  for (int z = 0; z < size - 1; ++z) {
    for (int x = 0; x < size - 1; ++x) {
      int i00 = z * size + x;
      int i01 = z * size + (x + 1);
      int i10 = (z + 1) * size + x;
      int i11 = (z + 1) * size + (x + 1);
      // Skip void quads (top-left corner check only — matching original engine).
      // Terrain quads at void edges intentionally extend one cell into void to
      // fill gaps behind world objects (tentacles, cliff walls).
      if (hasAttrs && (rawAttributes[i00] & 0x08) &&
          !(hasBridgeMask && bridgeMask[i00]))
        continue;
      int current = z * size + x;
      int next = current + size;
      indices.push_back(current);
      indices.push_back(current + 1);
      indices.push_back(next + 1);
      indices.push_back(current);
      indices.push_back(next + 1);
      indices.push_back(next);
    }
  }
  indexCount = indices.size();

  // Create BGFX vertex buffer
  const bgfx::Memory *vMem =
      bgfx::copy(vertices.data(), vertices.size() * sizeof(Vertex));
  vbo = bgfx::createVertexBuffer(vMem, s_terrainLayout);

  // Create BGFX index buffer (32-bit: 256x256 = 65536 vertices exceeds uint16)
  const bgfx::Memory *iMem =
      bgfx::copy(indices.data(), indices.size() * sizeof(uint32_t));
  ebo = bgfx::createIndexBuffer(iMem, BGFX_BUFFER_INDEX32);

  voidIndexCount = 0;
}

void Terrain::setupTextures(const TerrainData &data,
                            const std::string &base_path) {
  const int size = TerrainParser::TERRAIN_SIZE;

  // Layer 1 index map (NEAREST, R8 normalized)
  {
    const bgfx::Memory *mem =
        bgfx::copy(data.mapping.layer1.data(), size * size);
    layer1InfoMap = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Layer 2 index map
  {
    const bgfx::Memory *mem =
        bgfx::copy(data.mapping.layer2.data(), size * size);
    layer2InfoMap = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Alpha blend map (LINEAR, R32F)
  {
    const bgfx::Memory *mem =
        bgfx::copy(data.mapping.alpha.data(), size * size * sizeof(float));
    alphaMap = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::R32F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, // default = linear
        mem);
  }

  // Attribute map (NEAREST, R8 normalized — shader recovers uint via *255+0.5)
  {
    const bgfx::Memory *mem =
        bgfx::copy(data.mapping.attributes.data(), size * size);
    attributeMap = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Symmetry map (NEAREST, R8 normalized)
  {
    std::vector<uint8_t> symData;
    if (data.mapping.symmetry.empty()) {
      symData.assign(size * size, 0);
    } else {
      symData = data.mapping.symmetry;
    }
    const bgfx::Memory *mem = bgfx::copy(symData.data(), size * size);
    symmetryMap = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Lightmap texture (LINEAR, RGBA32F — BGFX has no RGB32F)
  {
    std::vector<float> lightRGBA(size * size * 4);
    for (int i = 0; i < size * size; ++i) {
      if (i < (int)data.lightmap.size()) {
        lightRGBA[i * 4 + 0] = data.lightmap[i].r;
        lightRGBA[i * 4 + 1] = data.lightmap[i].g;
        lightRGBA[i * 4 + 2] = data.lightmap[i].b;
      } else {
        lightRGBA[i * 4 + 0] = 1.0f;
        lightRGBA[i * 4 + 1] = 1.0f;
        lightRGBA[i * 4 + 2] = 1.0f;
      }
      // Alpha: default -1.0 (terrain). Overwritten for void cells in Load().
      lightRGBA[i * 4 + 3] = -1.0f;
    }
    m_baselineLightRGBA = lightRGBA;

    const bgfx::Memory *mem =
        bgfx::copy(lightRGBA.data(), lightRGBA.size() * sizeof(float));
    lightmapTex = bgfx::createTexture2D(
        size, size, false, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        mem);
  }

  // Tile texture array (256x256 x 32 layers, RGBA8, with mipmaps for anisotropic)
  const int tile_res = 256;
  const int max_tiles = 32;

  tileTextureArray = bgfx::createTexture2D(
      tile_res, tile_res, true, max_tiles, bgfx::TextureFormat::RGBA8,
      BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);

  // Helper: upload mip 0 + generate box-filtered mip chain for a tile layer
  auto uploadTileWithMips = [&](int layer, const unsigned char *mip0, int baseW, int baseH) {
    bgfx::updateTexture2D(tileTextureArray, layer, 0, 0, 0, baseW, baseH,
                          bgfx::copy(mip0, baseW * baseH * 4));
    std::vector<unsigned char> prev(mip0, mip0 + baseW * baseH * 4);
    int mW = baseW, mH = baseH;
    for (int mip = 1; mW > 1 || mH > 1; ++mip) {
      int nW = std::max(1, mW / 2);
      int nH = std::max(1, mH / 2);
      std::vector<unsigned char> next(nW * nH * 4);
      for (int y = 0; y < nH; ++y) {
        for (int x = 0; x < nW; ++x) {
          int sx = x * 2, sy = y * 2;
          int sx1 = std::min(sx + 1, mW - 1), sy1 = std::min(sy + 1, mH - 1);
          for (int c = 0; c < 4; ++c) {
            int sum = (int)prev[(sy * mW + sx) * 4 + c]
                    + (int)prev[(sy * mW + sx1) * 4 + c]
                    + (int)prev[(sy1 * mW + sx) * 4 + c]
                    + (int)prev[(sy1 * mW + sx1) * 4 + c];
            next[(y * nW + x) * 4 + c] = (unsigned char)(sum / 4);
          }
        }
      }
      bgfx::updateTexture2D(tileTextureArray, layer, mip, 0, 0, nW, nH,
                            bgfx::copy(next.data(), next.size()));
      prev = std::move(next);
      mW = nW;
      mH = nH;
    }
  };

  // Fill all slots with neutral dark brown (RGBA)
  {
    std::vector<uint8_t> neutral(tile_res * tile_res * 4);
    for (int p = 0; p < tile_res * tile_res; ++p) {
      neutral[p * 4 + 0] = 80;
      neutral[p * 4 + 1] = 70;
      neutral[p * 4 + 2] = 55;
      neutral[p * 4 + 3] = 255;
    }
    for (int slot = 0; slot < max_tiles; ++slot) {
      uploadTileWithMips(slot, neutral.data(), tile_res, tile_res);
    }
  }

  // Tile set
  std::vector<std::string> tile_names = {
      "TileGrass01",  "TileGrass02",  "TileGround01", "TileGround02",
      "TileGround03", "TileWater01",  "TileWood01",   "TileRock01",
      "TileRock02",   "TileRock03",   "TileRock04",   "TileRock05",
      "TileRock06",   "TileRock07",
  };
  if (worldID == 73 || worldID == 74) {
    tile_names[10] = "AlphaTile01";
  }
  {
    std::string extTest = base_path + "/ExtTile01.OZJ";
    std::ifstream check(extTest);
    if (check.good()) {
      for (int i = 1; i <= 16; i++) {
        char name[16];
        snprintf(name, sizeof(name), "ExtTile%02d", i);
        tile_names.push_back(name);
      }
    }
  }

  // Scan which tile indices are actually used
  std::set<int> usedTileIndices;
  const size_t cells = TerrainParser::TERRAIN_SIZE * TerrainParser::TERRAIN_SIZE;
  for (size_t i = 0; i < cells && i < data.mapping.layer1.size(); ++i) {
    usedTileIndices.insert(data.mapping.layer1[i]);
    if (data.mapping.alpha[i] > 0.0f)
      usedTileIndices.insert(data.mapping.layer2[i]);
  }

  for (int idx : usedTileIndices) {
    if (idx >= (int)tile_names.size()) {
      std::cerr << "[Terrain] WARNING: Terrain uses tile index " << idx
                << " which has no tile name (max=" << tile_names.size() - 1
                << ")" << std::endl;
    }
  }

  // Cache first successfully loaded texture data as fallback
  std::vector<unsigned char> fallback_data;
  int fallback_w = 0, fallback_h = 0;

  for (int i = 0; i < (int)tile_names.size(); ++i) {
    if (usedTileIndices.find(i) == usedTileIndices.end())
      continue;

    int w = 0, h = 0;
    std::vector<unsigned char> raw_data;
    std::vector<std::string> extensions = {".OZJ", ".jpg", ".OZT"};
    for (const auto &ext : extensions) {
      std::string path = base_path + "/" + tile_names[i] + ext;
      if (ext == ".OZT") {
        raw_data = TextureLoader::LoadOZTRaw(path, w, h);
      } else {
        raw_data = TextureLoader::LoadOZJRaw(path, w, h);
      }
      if (!raw_data.empty())
        break;
    }

    if (raw_data.empty()) {
      if (!fallback_data.empty()) {
        w = fallback_w;
        h = fallback_h;
        raw_data = fallback_data;
        std::cerr << "[Terrain] Tile " << i << " (" << tile_names[i]
                  << ") missing, using fallback" << std::endl;
      } else {
        std::cerr << "[Terrain] Tile " << i << " (" << tile_names[i]
                  << ") missing, no fallback available" << std::endl;
        continue;
      }
    } else {
      std::cout << "[Terrain] Loaded tile " << i << ": " << tile_names[i]
                << " (" << w << "x" << h << ")" << std::endl;
      if (fallback_data.empty()) {
        fallback_data = raw_data;
        fallback_w = w;
        fallback_h = h;
      }
    }

    // Convert to RGBA if needed (BGFX array requires uniform format)
    bool isRGBA = (raw_data.size() == (size_t)w * h * 4);
    std::vector<unsigned char> rgba;
    const unsigned char *uploadData = raw_data.data();
    size_t uploadSize = raw_data.size();

    if (!isRGBA) {
      rgba.resize(w * h * 4);
      for (int p = 0; p < w * h; ++p) {
        rgba[p * 4 + 0] = raw_data[p * 3 + 0];
        rgba[p * 4 + 1] = raw_data[p * 3 + 1];
        rgba[p * 4 + 2] = raw_data[p * 3 + 2];
        rgba[p * 4 + 3] = 255;
      }
      uploadData = rgba.data();
      uploadSize = rgba.size();
    }

    if (w == tile_res && h == tile_res) {
      uploadTileWithMips(i, uploadData, w, h);
    } else if (w == 128 && h == 128) {
      // 128x128 tiles: tile 2x2 to fill 256x256
      std::vector<unsigned char> tiled(256 * 256 * 4);
      for (int y = 0; y < 256; ++y) {
        int sy = y % 128;
        for (int x = 0; x < 256; ++x) {
          int sx = x % 128;
          memcpy(&tiled[(y * 256 + x) * 4], &uploadData[(sy * 128 + sx) * 4],
                 4);
        }
      }
      uploadTileWithMips(i, tiled.data(), 256, 256);
    } else if (w > 0 && h > 0) {
      int upload_w = std::min(w, tile_res);
      int upload_h = std::min(h, tile_res);
      std::vector<unsigned char> sub(upload_w * upload_h * 4);
      for (int y = 0; y < upload_h; ++y) {
        memcpy(&sub[y * upload_w * 4], &uploadData[y * w * 4], upload_w * 4);
      }
      uploadTileWithMips(i, sub.data(), upload_w, upload_h);
    }
  }
}

void Terrain::applyDynamicLights() {
  if (m_baselineLightRGBA.empty())
    return;

  const int S = TerrainParser::TERRAIN_SIZE;

  // One-time debug: verify dynamic lights are being applied
  static bool s_debugOnce = true;
  if (s_debugOnce && plCount > 0) {
    s_debugOnce = false;
    printf("[Terrain/BGFX] applyDynamicLights: plCount=%d, lightmapTex valid=%d, "
           "baseline[0] RGB=(%.3f,%.3f,%.3f)\n",
           plCount, bgfx::isValid(lightmapTex) ? 1 : 0,
           m_baselineLightRGBA[0], m_baselineLightRGBA[1], m_baselineLightRGBA[2]);
  }

  // Reset working lightmap from baseline
  m_workingLightRGBA = m_baselineLightRGBA;

  for (int li = 0; li < plCount; ++li) {
    float gx = plPositions[li].z / 100.0f;
    float gz = plPositions[li].x / 100.0f;

    int cellRange = 3;
    if (li < (int)plRanges.size() && plRanges[li] > 0.0f) {
      cellRange = std::max(1, std::min(5, (int)(plRanges[li] / 100.0f)));
    }

    static float s_flickerPhase = 0.0f;
    if (li == 0)
      s_flickerPhase += 0.04f;
    glm::vec3 color = plColors[li];
    int objType = (li < (int)plObjectTypes.size()) ? plObjectTypes[li] : 0;
    if (objType == -1) {
      // Spell light: pass through
    } else if (objType == 90 || objType == 98) {
      float phase = s_flickerPhase + (float)li * 1.7f;
      float L = 0.75f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.8f, L * 0.6f);
    } else if (objType == 150) {
      float phase = s_flickerPhase + (float)li * 2.3f;
      float L = 0.3f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.6f, L * 0.2f);
    } else if (objType == 41 || objType == 42) {
      float phase = s_flickerPhase + (float)li * 1.3f;
      float L = 0.5f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.55f, L * 0.2f);
    } else if (objType == 50 || objType == 51 || objType == 55) {
      float phase = s_flickerPhase + (float)li * 1.5f;
      float L = 0.5f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.6f, L * 0.25f);
    } else if (objType == 52) {
      float phase = s_flickerPhase + (float)li * 1.1f;
      float L = 0.6f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.5f, L * 0.15f);
    } else if (objType == 80) {
      float phase = s_flickerPhase + (float)li * 1.9f;
      float L = 0.45f + 0.05f * std::sin(phase);
      color = glm::vec3(L, L * 0.6f, L * 0.3f);
    } else if (objType == 130 || objType == 131 || objType == 132) {
      float phase = s_flickerPhase + (float)li * 2.1f;
      float L = 0.5f + 0.04f * std::sin(phase);
      color = glm::vec3(L, L * 0.7f, L * 0.4f);
    } else {
      color *= 0.5f;
    }

    float rf = (float)cellRange;
    int gxi = (int)gx;
    int gzi = (int)gz;

    for (int sz = gzi - cellRange; sz <= gzi + cellRange; ++sz) {
      if (sz < 0 || sz >= S)
        continue;
      for (int sx = gxi - cellRange; sx <= gxi + cellRange; ++sx) {
        if (sx < 0 || sx >= S)
          continue;
        float xd = gx - (float)sx;
        float zd = gz - (float)sz;
        float dist = sqrtf(xd * xd + zd * zd);
        float lf = (rf - dist) / rf;
        if (lf <= 0.0f)
          continue;
        int idx = sz * S + sx;
        m_workingLightRGBA[idx * 4 + 0] += color.r * lf;
        m_workingLightRGBA[idx * 4 + 1] += color.g * lf;
        m_workingLightRGBA[idx * 4 + 2] += color.b * lf;
        // alpha stays 1.0
      }
    }
  }

  // Recreate lightmap texture with updated data.
  // bgfx::updateTexture2D on RGBA32F does not reliably update on Metal backend,
  // so we destroy+recreate instead. Skip every other frame since fire flicker
  // at 30hz is indistinguishable from 60hz, halving the GPU texture churn.
  static int s_lightmapFrame = 0;
  if ((++s_lightmapFrame & 1) == 0 || !bgfx::isValid(lightmapTex)) {
    TexDestroy(lightmapTex);
    const bgfx::Memory *mem = bgfx::copy(
        m_workingLightRGBA.data(), m_workingLightRGBA.size() * sizeof(float));
    lightmapTex = bgfx::createTexture2D(
        S, S, false, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
  }
}

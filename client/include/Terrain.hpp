#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "TerrainParser.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Terrain {
public:
  Terrain();
  ~Terrain();

  void Init();
  void Load(const TerrainData &data, int worldID, const std::string &data_path,
            const std::vector<uint8_t> &rawAttributes = {},
            const std::vector<bool> &bridgeMask = {});
  void Render(const glm::mat4 &view, const glm::mat4 &projection, float time,
              const glm::vec3 &viewPos = glm::vec3(0.0f));
  void RenderToView(bgfx::ViewId viewId, const glm::mat4 &view,
                    const glm::mat4 &proj, float time,
                    const glm::vec3 &viewPos = glm::vec3(0.0f));
  void SetDebugMode(int mode) { debugMode = mode; }
  int GetDebugMode() const { return debugMode; }
  void SetPointLights(const std::vector<glm::vec3> &positions,
                      const std::vector<glm::vec3> &colors,
                      const std::vector<float> &ranges,
                      const std::vector<int> &objectTypes = {});
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx);
  void SetShadowDebug(bool d) { m_shadowDebug = d; }
  void SetFogColor(const glm::vec3 &c) { m_fogColor = c; }
  void SetFogRange(float near_, float far_) { m_fogNear = near_; m_fogFar = far_; }
  void SetFogHeight(float base, float fade) { m_fogHeightBase = base; m_fogHeightFade = fade; }

  void Cleanup();

  // Physics helper
  float GetHeight(float x, float y);

private:
  void setupMesh(const std::vector<float> &heightmap,
                 const std::vector<glm::vec3> &lightmap,
                 const std::vector<uint8_t> &rawAttributes,
                 const std::vector<bool> &bridgeMask = {});
  void setupTextures(const TerrainData &data, const std::string &base_path);
  void applyDynamicLights();

  int debugMode = 0;
  float m_luminosity = 1.0f;
  glm::vec3 m_fogColor = glm::vec3(0.117f, 0.078f, 0.039f);
  float m_fogNear = 1500.0f;
  float m_fogFar = 3500.0f;
  float m_fogHeightBase = -99999.0f;
  float m_fogHeightFade = 1.0f;
  std::vector<glm::vec3> plPositions, plColors;
  std::vector<float> plRanges;
  std::vector<int> plObjectTypes;
  int plCount = 0;
  size_t indexCount = 0;
  int worldID = 0;

  // Heightmap for physics
  std::vector<float> m_heightmap;

  struct Vertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec3 color;
  };

  std::unique_ptr<Shader> shader;
  bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ebo = BGFX_INVALID_HANDLE;

  // Shadow map state
  bgfx::TextureHandle m_shadowMapTex = BGFX_INVALID_HANDLE;
  glm::mat4 m_lightMtx{1.0f};
  bool m_shadowEnabled = false;
  bool m_shadowDebug = false;

  TexHandle tileTextureArray = kInvalidTex;
  TexHandle layer1InfoMap = kInvalidTex;
  TexHandle layer2InfoMap = kInvalidTex;
  TexHandle alphaMap = kInvalidTex;
  TexHandle attributeMap = kInvalidTex;
  TexHandle symmetryMap = kInvalidTex;
  TexHandle lightmapTex = kInvalidTex;

  // CPU-side dynamic lightmap (RGBA32F for BGFX upload)
  std::vector<float> m_baselineLightRGBA;
  std::vector<float> m_workingLightRGBA;
};

#endif

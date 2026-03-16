#ifndef CLICK_EFFECT_HPP
#define CLICK_EFFECT_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class ClickEffect {
public:
  void Init();
  void LoadAssets(const std::string &dataPath);
  void Show(const glm::vec3 &pos);
  void Hide();
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              float deltaTime, Shader *shader);
  void Cleanup();

  bool IsVisible() const { return m_visible; }

  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }

private:
  float getTerrainHeight(float worldX, float worldZ) const;
  void drawGroundQuad(float cx, float cz, float halfSize, float hOff);

  // Ground quad GPU resources
  bgfx::DynamicVertexBufferHandle m_dynVbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle m_quadEbo = BGFX_INVALID_HANDLE;

  // Textures
  TexHandle m_ringTex = kInvalidTex;   // cursorpin02.OZJ
  TexHandle m_waveTex = kInvalidTex;   // cursorpin01.OZJ
  TexHandle m_glowTex = kInvalidTex;   // Magic_Ground1.OZJ

  // BMD spinning cone model
  std::unique_ptr<BMDData> m_bmd;
  std::vector<MeshBuffers> m_modelBuffers;
  float m_animFrame = 0.0f;
  static constexpr float ANIM_SPEED = 8.0f;

  // Effect state
  bool m_visible = false;
  glm::vec3 m_pos{0.0f};
  float m_lifetime = 0.0f;
  float m_scale = 1.8f;
  bool m_shrinking = true;
  float m_glowAngle = 0.0f;

  // Expanding wave rings
  struct Wave {
    float scale;
    float alpha;
  };
  std::vector<Wave> m_waves;
  float m_waveTimer = 0.0f;

  // External data (non-owning)
  const TerrainData *m_terrainData = nullptr;
};

#endif // CLICK_EFFECT_HPP

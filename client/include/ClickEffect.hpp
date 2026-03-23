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

class VFXManager;

// Main 5.2 click-to-move cursor effect (MODEL_MOVE_TARGETPOSITION_EFFECT).
// Six-layer system matching ZzzEffect.cpp:
//   1. MoveTargetPosEffect.bmd: 3D spinning cone, scale 0.6, RENDER_BRIGHT
//   2. Pulsing ring (cursorpin02): scale oscillates 0.8↔1.8 at ±0.15/tick
//   3. Shrinking rings (cursorpin01): scale 1.2→0.2, spawned every 15 ticks
//   4. Rotating glow (BITMAP_MAGIC SubType 11): two overlapping layers
//   5. Spark column (BITMAP_SPARK+1): ascending particles per tick at +110 height
//   6. Orange tint (1.0, 0.7, 0.3), additive blend
// Total lifetime: 30 ticks (~1.2 sec at 25fps)
class ClickEffect {
public:
  void Init();
  void LoadAssets(const std::string &dataPath);
  void Show(const glm::vec3 &pos);
  void Hide();
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              float deltaTime, Shader *shader);
  void Cleanup();

  bool IsVisible() const { return m_visible; }
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetVFXManager(VFXManager *vfx) { m_vfxManager = vfx; }

private:
  float getTerrainHeight(float worldX, float worldZ) const;
  void drawGroundQuad(float cx, float cz, float halfSize, float hOff);
  void submitQuad(Shader *shader, const glm::vec3 &eye, TexHandle tex,
                  float alpha, const glm::vec3 &tint, uint64_t state);

  bgfx::DynamicVertexBufferHandle m_dynVbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle m_quadEbo = BGFX_INVALID_HANDLE;

  TexHandle m_shrinkTex = kInvalidTex;  // cursorpin01 (BITMAP_TARGET_POSITION_EFFECT1)
  TexHandle m_pulseTex = kInvalidTex;   // cursorpin02 (BITMAP_TARGET_POSITION_EFFECT2)
  TexHandle m_glowTex = kInvalidTex;    // magic glow (BITMAP_MAGIC SubType 11)

  // BMD spinning cone model (MODEL_MOVE_TARGETPOSITION_EFFECT)
  std::unique_ptr<BMDData> m_bmd;
  std::vector<MeshBuffers> m_modelBuffers;
  float m_animFrame = 0.0f;
  float m_bmdAlpha = 1.0f;     // Main 5.2: BlendMeshLight fades in last 10 ticks

  bool m_visible = false;
  glm::vec3 m_pos{0.0f};

  // Parent effect state (30 ticks total)
  float m_parentLife = 0.0f;  // ticks remaining
  float m_spawnTimer = 0.0f;  // ticks since last shrink ring spawn

  // Pulsing ring (EFFECT2): one instance, oscillates 0.8↔1.8
  float m_pulseScale = 1.8f;
  float m_pulseAlpha = 1.0f;
  int m_pulseDir = 0;  // 0=shrinking, 1=growing

  // Rotating glow (BITMAP_MAGIC SubType 11)
  float m_glowRotation = 0.0f;  // degrees, +2/tick on each axis
  float m_glowAlpha = 1.0f;

  // Shrinking rings (EFFECT1): up to 4 overlapping instances
  static constexpr int MAX_SHRINK_RINGS = 4;
  struct ShrinkRing {
    float scale = 0.0f;
    float alpha = 1.0f;
    int life = 0;  // ticks remaining
    bool active = false;
  };
  ShrinkRing m_shrinkRings[MAX_SHRINK_RINGS] = {};

  const TerrainData *m_terrainData = nullptr;
  VFXManager *m_vfxManager = nullptr;
};

#endif // CLICK_EFFECT_HPP

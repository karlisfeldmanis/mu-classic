#ifndef BOID_MANAGER_HPP
#define BOID_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "HeroCharacter.hpp" // For PointLight
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "TextureLoader.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Boid AI states (Main 5.2: GOBoid.cpp lines 27-30)
enum class BoidAI : uint8_t {
  FLY = 0,    // Soaring at altitude
  DOWN = 1,   // Descending toward ground
  GROUND = 2, // Resting on terrain
  UP = 3      // Ascending back to altitude
};

// A single ambient boid (bird/bat/butterfly/crow)
struct Boid {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 angle{0.0f};     // MU-space Euler angles (degrees)
  glm::vec3 direction{0.0f}; // Movement direction / look-ahead
  float velocity = 1.0f;
  float alpha = 0.0f;
  float alphaTarget = 1.0f;
  float timer = 0.0f;
  float scale = 0.8f;
  float shadowScale = 10.0f;
  float animFrame = 0.0f;
  float priorAnimFrame = 0.0f;
  int action = 0;
  BoidAI ai = BoidAI::FLY;
  float gravity = 8.0f; // Max turn rate for flocking (degrees/tick)
  int subType = 0;      // Bounce counter for despawn
  int lifetime = 0;
  float respawnDelay = 0.0f; // Cooldown before respawn (seconds)
  float stuckTimer = 0.0f;   // Time spent barely moving (for stuck detection)
  glm::vec3 prevPosition{0.0f}; // Previous position for stuck detection
};

// A single ambient fish (Lorencia water tiles)
struct Fish {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 angle{0.0f};
  float velocity = 0.6f;
  float alpha = 0.0f;
  float alphaTarget = 0.3f;
  float scale = 0.5f;
  float animFrame = 0.0f;
  float priorAnimFrame = 0.0f;
  int action = 0;
  int subType = 0; // Wall-hit counter
  int lifetime = 0;
  float timer = 0.0f;  // Phase offset for sinusoidal swimming patterns
  glm::vec3 tint{1.0f}; // Color tint variety
};

// A falling leaf particle (Main 5.2: ZzzEffectFireLeave.cpp)
struct LeafParticle {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  glm::vec3 angle{0.0f};        // Euler rotation (degrees)
  glm::vec3 turningForce{0.0f}; // Angular velocity (degrees/tick)
  float alpha = 1.0f;
  float scale = 1.0f; // Particle size multiplier (snow uses larger)
  bool onGround = false;
};

class BoidManager {
public:
  void Init(const std::string &dataPath);
  void Update(float deltaTime, const glm::vec3 &heroPos, int heroAction,
              float worldTime);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderLeaves(const glm::mat4 &view, const glm::mat4 &proj,
                    const glm::vec3 &camPos);
  void Cleanup();

  // Terrain linkage
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lm) {
    m_terrainLightmap = lm;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLightmapTexture(bgfx::TextureHandle tex) { m_lightmapTex = tex; }
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetCameraFadeStart(float dist) { m_cameraFadeStart = dist; }
  void SetCameraView(const glm::mat4 &viewProj) {
    m_viewProj = viewProj;
  }
  void SetMapId(int mapId) {
    m_mapId = mapId;
    // Clear all creatures on map change — prevents stale entities
    for (auto &b : m_boids) { b.live = false; b.respawnDelay = 2.0f; }
    for (auto &b : m_bats) { b.live = false; b.respawnDelay = 2.0f; }
    for (auto &b : m_butterflies) { b.live = false; b.respawnDelay = 2.0f; }
    for (auto &f : m_fishs) f.live = false;
    for (auto &r : m_rats) r.live = false;
    for (auto &l : m_leaves) l.live = false;
    // Dungeon is enclosed — bats need to be visible close to camera
    m_cameraFadeStart = (mapId == 1) ? 80.0f : 350.0f;
  }

private:
  static constexpr int MAX_BOIDS = 3;       // Lorencia bird count (reduced for ambience)
  static constexpr int MAX_BATS = 3;        // Dungeon bat count (reduced from 5)
  static constexpr int MAX_BUTTERFLIES = 3; // Noria butterfly count (reduced from 5)
  static constexpr int MAX_FISHS = 20;      // Main 5.2: up to 40 boids, 20 for perf
  static constexpr int MAX_RATS = 5;        // Dungeon rat count
  static constexpr int MAX_LEAVES = 160;    // Shared leaf/snow count (doubled for denser Devias snow)
  static constexpr int MAX_POINT_LIGHTS = 64;

  Boid m_boids[MAX_BOIDS];
  Boid m_bats[MAX_BATS];               // Dungeon bats (reuse Boid struct)
  Boid m_butterflies[MAX_BUTTERFLIES];  // Noria butterflies (reuse Boid struct)
  Fish m_fishs[MAX_FISHS];
  Fish m_rats[MAX_RATS];               // Dungeon rats (ground critters, reuse Fish struct)
  LeafParticle m_leaves[MAX_LEAVES];

  // Bird model
  std::unique_ptr<BMDData> m_birdBmd;
  std::vector<MeshBuffers> m_birdMeshes;
  std::vector<BoneWorldMatrix> m_birdBones;

  // Bat model (Main 5.2: MODEL_BAT01 = Object2/Bat01.bmd)
  std::unique_ptr<BMDData> m_batBmd;
  std::vector<MeshBuffers> m_batMeshes;
  std::vector<BoneWorldMatrix> m_batBones;

  // Butterfly model (Main 5.2: MODEL_BUTTERFLY01 = Object1/Butterfly01.bmd)
  std::unique_ptr<BMDData> m_butterflyBmd;
  std::vector<MeshBuffers> m_butterflyMeshes;
  std::vector<BoneWorldMatrix> m_butterflyBones;

  // Fish model
  std::unique_ptr<BMDData> m_fishBmd;
  std::vector<MeshBuffers> m_fishMeshes;
  std::vector<BoneWorldMatrix> m_fishBones;

  // Rat model (Main 5.2: MODEL_RAT01 = Object2/Rat01.bmd)
  std::unique_ptr<BMDData> m_ratBmd;
  std::vector<MeshBuffers> m_ratMeshes;
  std::vector<BoneWorldMatrix> m_ratBones;

  // Shadow mesh buffers (one per boid for bird, one per fish)
  struct ShadowMesh {
    bgfx::DynamicVertexBufferHandle vbo = BGFX_INVALID_HANDLE;
    int vertexCount = 0;
  };
  ShadowMesh m_birdShadow;
  ShadowMesh m_batShadow;
  ShadowMesh m_butterflyShadow;
  ShadowMesh m_fishShadow;
  ShadowMesh m_ratShadow;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;

  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  bgfx::TextureHandle m_lightmapTex = BGFX_INVALID_HANDLE;
  float m_luminosity = 1.0f;
  float m_cameraFadeStart = 350.0f; // Distance at which boids start fading (default 350)
  glm::mat4 m_viewProj{1.0f};
  int m_mapId = 0; // 0=Lorencia, 1=Dungeon

  // World time accumulator (ticks at 25fps equivalent)
  float m_worldTime = 0.0f;

  // Helpers
  float getTerrainHeight(float worldX, float worldZ) const;
  glm::vec3 sampleTerrainLight(const glm::vec3 &pos) const;
  uint8_t getTerrainLayer1(float worldX, float worldZ) const;
  uint8_t getTerrainAttribute(float worldX, float worldZ) const;

  void updateBoids(float dt, const glm::vec3 &heroPos, int heroAction);
  void updateBats(float dt, const glm::vec3 &heroPos);
  void updateRats(float dt, const glm::vec3 &heroPos);
  void updateButterflies(float dt, const glm::vec3 &heroPos);
  void updateFishs(float dt, const glm::vec3 &heroPos);
  void moveBird(Boid &b, const glm::vec3 &heroPos, int heroAction, float tickFrac);
  void moveBat(Boid &b, const glm::vec3 &heroPos, float tickFrac);
  void moveButterfly(Boid &b, const glm::vec3 &heroPos, float tickFrac);
  void moveBoidGroup(Boid &b, float tickFrac);
  void moveBoidFlock(Boid &b, int selfIdx, float tickFrac);
  void alphaFade(float &alpha, float target, float dt);
  bool isInCameraView(const glm::vec3 &pos) const;

  void renderBoid(const Boid &b, const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye);
  void renderBat(const Boid &b, const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye);
  void renderButterfly(const Boid &b, const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye);
  void renderFish(const Fish &f, const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye);
  void renderRat(const Fish &r, const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye);

  // Falling leaves (Main 5.2: ZzzEffectFireLeave.cpp)
  std::unique_ptr<Shader> m_leafShader;
  TexHandle m_leafTexture = kInvalidTex;
  bgfx::DynamicVertexBufferHandle m_leafDynVBO = BGFX_INVALID_HANDLE;
  bgfx::DynamicIndexBufferHandle m_leafDynEBO = BGFX_INVALID_HANDLE;
  void updateLeaves(float dt, const glm::vec3 &heroPos);
  void spawnLeaf(LeafParticle &leaf, const glm::vec3 &heroPos);

  // Devias snow (Main 5.2: CreateDeviasSnow / MoveEtcLeaf)
  void updateSnow(float dt, const glm::vec3 &heroPos);
  void spawnSnow(LeafParticle &s, const glm::vec3 &heroPos);
};

#endif // BOID_MANAGER_HPP

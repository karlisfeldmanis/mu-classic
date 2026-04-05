#ifndef OBJECT_RENDERER_HPP
#define OBJECT_RENDERER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class VFXManager;

class ObjectRenderer {
public:
  void Init();
  void LoadObjects(const std::vector<ObjectData> &objects,
                   const std::string &objectDir);
  // Generic naming: ObjectXX.bmd (for non-Lorencia maps like World74)
  // fallbackDir: if set, try Lorencia naming from this dir when generic fails
  void LoadObjectsGeneric(const std::vector<ObjectData> &objects,
                          const std::string &objectDir,
                          const std::string &fallbackDir = "");
  void Render(const glm::mat4 &view, const glm::mat4 &projection,
              const glm::vec3 &cameraPos, float currentTime = 0.0f);
  void Cleanup();
  void SetPointLights(const std::vector<glm::vec3> &positions,
                      const std::vector<glm::vec3> &colors,
                      const std::vector<float> &ranges);
  void SetTerrainLightmap(const std::vector<glm::vec3> &lightmap);
  void SetLightmapTexture(TexHandle tex) { m_lightmapTex = tex; }
  void SetTerrainMapping(const MapData *mapping) { terrainMapping = mapping; }
  void SetTerrainHeightmap(const std::vector<float> &hm) { terrainHeightmap = hm; }
  void SetTypeAlpha(const std::unordered_map<int, float> &alphaMap);
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetFogEnabled(bool e) { m_fogEnabled = e; }
  void SetFogColor(const glm::vec3 &c) { m_fogColor = c; }
  void SetFogRange(float near_, float far_) { m_fogNear = near_; m_fogFar = far_; }
  void SetTypeFilter(const std::vector<int> &types) { m_typeFilter.assign(types.begin(), types.end()); }
  void SetMapId(int mapId) { m_mapId = mapId; }
  void SetVFXManager(VFXManager *vfx) { m_vfxManager = vfx; }

  int GetInstanceCount() const { return (int)instances.size(); }
  int GetModelCount() const { return (int)modelCache.size(); }

  // Developer tuner: per-type position offset and scale (applied in Render)
  void SetTypeOffset(int type, const glm::vec3 &off) { m_typeOffset[type] = off; }
  void SetTypeScaleMult(int type, float s) { m_typeScaleMult[type] = s; }
  void SetTypeRotOffset(int type, const glm::vec3 &deg) { m_typeRotOffset[type] = deg; }
  void SetTypeBaseAlpha(int type, float a) { m_typeBaseAlpha[type] = a; }
  void ClearTypeOverrides(int type) { m_typeOffset.erase(type); m_typeScaleMult.erase(type); m_typeRotOffset.erase(type); m_typeBaseAlpha.erase(type); }
  glm::vec3 GetTypeOffset(int type) const { auto it = m_typeOffset.find(type); return it != m_typeOffset.end() ? it->second : glm::vec3(0); }
  glm::vec3 GetTypeRotOffset(int type) const { auto it = m_typeRotOffset.find(type); return it != m_typeRotOffset.end() ? it->second : glm::vec3(0); }
  float GetTypeScaleMult(int type) const { auto it = m_typeScaleMult.find(type); return it != m_typeScaleMult.end() ? it->second : 1.0f; }
  float GetTypeBaseAlpha(int type) const { auto it = m_typeBaseAlpha.find(type); return it != m_typeBaseAlpha.end() ? it->second : 1.0f; }

  struct TypeDebugInfo {
    int instanceCount = 0;
    glm::vec3 aabbMin{0}, aabbMax{0};
    glm::vec3 firstInstancePos{0};
    std::string filename;
  };
  TypeDebugInfo GetTypeDebugInfo(int type) const;
  std::vector<int> GetLoadedTypes() const;

  // Interactive objects (sittable chairs, pose boxes) — Main 5.2 OPERATE system
  enum class InteractType { SIT, POSE };
  struct InteractiveObject {
    int type;             // Object type ID
    glm::vec3 worldPos;   // World position (extracted from model matrix)
    float facingAngle;    // MU angle in degrees (for character alignment)
    bool alignToObject;   // Whether character should face the object's angle
    InteractType action;  // SIT or POSE
    float radius;         // Picking radius
    float height;         // Picking height
  };
  const std::vector<InteractiveObject> &GetInteractiveObjects() const {
    return m_interactiveObjects;
  }

  struct ObjectInstance {
    int type;
    glm::mat4 modelMatrix;
    glm::vec3 terrainLight = glm::vec3(1.0f); // Lightmap color at object position
    float animPhaseOffset = 0.0f; // Per-instance animation frame offset (trees)
  };
  const std::vector<ObjectInstance> &GetInstances() const { return instances; }

  // Door animation: proximity-based rotation/translation (Main 5.2: ZzzObject.cpp:3871)
  // Call after LoadObjects/LoadObjectsGeneric to detect doors.
  void InitDoors();
  // Call each frame with hero position. Updates door instance transforms.
  void UpdateDoors(const glm::vec3 &heroPos, float deltaTime);
  // Reset door states after warp (so doors re-trigger on proximity)
  void ResetDoorStates();

  // Devias type 100: rotating lightning sprites ("northern lights")
  // Main 5.2: ZzzObject.cpp:2813-2822 — two counter-rotating BITMAP_LIGHTNING+1 sprites
  void RenderLightningSprites(const glm::mat4 &view, const glm::mat4 &projection,
                              float currentTime);

  // Lost Tower skull tracking (Main 5.2: ZzzEffectFireLeave.cpp:88-113)
  void UpdateSkulls(const glm::vec3 &heroPos, bool heroMoving, float deltaTime);

  // Lost Tower orb/column sprites (types 19/20/40)
  // Main 5.2: ZzzObject.cpp:2928-2960 — BITMAP_LIGHTNING+1 sprites at bones
  void RenderOrbSprites(const glm::mat4 &view, const glm::mat4 &projection,
                        float currentTime);

private:
  // Devias door state (types 20,65,88 = swinging, 86 = sliding)
  // Main 5.2: ZzzObject.cpp:3871-3913
  struct DoorState {
    int instanceIdx;        // Index into instances[]
    glm::vec3 origPos;      // Original GL position (for distance check)
    float origAngleDeg;     // Original MU Z rotation in degrees (HeadAngle[2])
    float currentAngleDeg;  // Current MU Z rotation in degrees
    glm::vec3 rotRad;       // Original rotation xyz in radians (for matrix rebuild)
    float scale;            // Original scale
    bool isSliding;         // true = type 86 (translate), false = rotate
    bool soundPlayed;       // Prevent door sound spam
  };
  std::vector<DoorState> m_doors;
  float m_doorCooldown = 0.0f; // Suppress door sounds after map load

  // Lost Tower skull tracking (type 38, 777 instances)
  // Main 5.2: ZzzEffectFireLeave.cpp:88-113 CheckSkull()
  struct SkullState {
    glm::vec3 direction{0.0f};     // Drift velocity
    glm::vec2 headAngle{0.0f};     // Rotation delta (pitch, yaw)
    bool wasTracking = false;      // For sound trigger
  };
  std::unordered_map<int, SkullState> m_skullStates;  // Key = instance index
  struct ModelCache {
    std::vector<MeshBuffers> meshBuffers;
    std::vector<BoneWorldMatrix> boneMatrices;
    int blendMeshTexId = -1; // BlendMesh texture ID for window light marking

    // CPU animation support (cloth, signs, mechanical)
    bool isAnimated = false;
    std::unique_ptr<BMDData> bmdData; // retained for re-skinning / GPU bone compute
    int numAnimationKeys = 0;

    // GPU skeletal animation (trees — too many instances for CPU re-skinning)
    bool isGPUAnimated = false;
    std::vector<glm::mat4> gpuBoneMatrices; // Computed each frame, uploaded as uniforms

    // BMD local-space AABB (for debug visualization)
    glm::vec3 aabbMin = glm::vec3(0.0f);
    glm::vec3 aabbMax = glm::vec3(0.0f);
  };
  std::unordered_map<int, ModelCache> modelCache;

  // Animation state per animated model type (shared across instances)
  struct AnimState {
    float frame = 0.0f;
  };
  std::unordered_map<int, AnimState> animStates;
  float lastAnimTime = 0.0f;

  std::vector<ObjectInstance> instances;

  std::vector<glm::vec3> plPositions, plColors;
  std::vector<float> plRanges;
  int plCount = 0;

  std::vector<glm::vec3> terrainLightmap; // Copy of terrain lightmap for sampling
  const MapData *terrainMapping = nullptr; // For grass-on-tile filtering
  std::vector<float> terrainHeightmap;     // For cliff fade height lookup
  std::unordered_map<int, float> typeAlphaMap; // Per-type alpha for roof hiding
  std::unordered_map<int, glm::vec3> m_typeOffset;    // Dev tuner: per-type XYZ offset
  std::unordered_map<int, float> m_typeScaleMult;     // Dev tuner: per-type scale mult
  std::unordered_map<int, glm::vec3> m_typeRotOffset;  // Dev tuner: per-type XYZ rotation (degrees)
  std::unordered_map<int, float> m_typeBaseAlpha;       // Dev tuner: per-type base alpha (transparency)
  float m_luminosity = 1.0f;
  bool m_fogEnabled = true;
  glm::vec3 m_fogColor = glm::vec3(0.117f, 0.078f, 0.039f);
  float m_fogNear = 1500.0f;
  float m_fogFar = 3500.0f;
  std::vector<int> m_typeFilter; // If non-empty, only render these types
  int m_mapId = 0; // 0=Lorencia, 1=Dungeon
  VFXManager *m_vfxManager = nullptr;
  std::vector<InteractiveObject> m_interactiveObjects;
  TexHandle m_lightmapTex = kInvalidTex; // Terrain lightmap GPU texture for per-pixel lighting
  TexHandle m_chromeTexture = kInvalidTex;
  TexHandle m_lightningSpriteTex = kInvalidTex;
  // Devias type 100 object world positions (model hidden, sprites only)
  std::vector<glm::vec3> m_lightningPositions;
  std::unique_ptr<Shader> m_spriteShader;
  bgfx::VertexBufferHandle m_spriteQuadVBO = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle m_spriteQuadEBO = BGFX_INVALID_HANDLE;
  // Bilinear sample terrain lightmap at world position
  glm::vec3 SampleTerrainLight(const glm::vec3 &worldPos) const;
  // Bilinear sample terrain height at world position
  float SampleTerrainHeight(float worldX, float worldZ) const;

  std::unique_ptr<Shader> shader;
  std::unique_ptr<Shader> skinnedShader; // GPU-skinned program (bone matrices + tree sway)
  bgfx::UniformHandle u_boneMatrices = BGFX_INVALID_HANDLE;

  void UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                  const std::vector<BoneWorldMatrix> &bones,
                  std::vector<MeshBuffers> &out, bool dynamic = false,
                  const std::string &fallbackTexDir = "");

  // GPU-skinned upload: stores RAW vertex positions + bone indices (no CPU transform)
  void UploadMeshGPUSkinned(const Mesh_t &mesh, const std::string &baseDir,
                            std::vector<MeshBuffers> &out);

  void RetransformMesh(const Mesh_t &mesh,
                       const std::vector<BoneWorldMatrix> &bones,
                       MeshBuffers &mb);

  static std::string GetObjectBMDFilename(int type);
};

#endif // OBJECT_RENDERER_HPP

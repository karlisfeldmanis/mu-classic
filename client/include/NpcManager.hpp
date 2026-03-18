#ifndef NPC_MANAGER_HPP
#define NPC_MANAGER_HPP

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
struct ImDrawList;

// PointLight defined in HeroCharacter.hpp
#include "HeroCharacter.hpp"

// NPC spawn data received from server (0x13 viewport packet)
struct ServerNpcSpawn {
  uint16_t serverIndex; // Unique NPC index (1001+)
  uint16_t type;  // NPC type ID (253=Amy, 250=Merchant, etc.)
  uint8_t gridX;
  uint8_t gridY;
  uint8_t dir;    // 0-7 direction
};

// NPC interaction data (for mouse picking, name labels)
struct NpcInfo {
  glm::vec3 position;
  float radius;    // Cylindrical collision radius (~80 units)
  float height;    // Collision height (~200 units)
  std::string name;
  uint16_t type;
};

class NpcManager {
public:
  // Full init with hardcoded NPCs (fallback when no server)
  void Init(const std::string &dataPath);

  // Two-phase init for server mode:
  // 1) InitModels() loads BMD models + shaders only
  // 2) AddNpcByType() places individual NPCs from server data
  void InitModels(const std::string &dataPath);
  void AddNpcByType(uint16_t npcType, uint8_t gridX, uint8_t gridY, uint8_t dir,
                    uint16_t serverIndex = 0);
  void ClearSpawnedNpcs() { m_npcs.clear(); }

  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram);
  void SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx);
  void RenderLabels(ImDrawList *dl, const glm::mat4 &view, const glm::mat4 &proj,
                    int winW, int winH, const glm::vec3 &camPos, int hoveredNpc);
  int PickLabel(float screenX, float screenY, const glm::mat4 &view,
                const glm::mat4 &proj, int winW, int winH,
                const glm::vec3 &camPos) const;
  void Cleanup();

  // Terrain linkage
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lm) {
    m_terrainLightmap = lm;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetMapId(int mapId) { m_mapId = mapId; }
  void SetVFXManager(VFXManager *vfx) { m_vfxManager = vfx; }
  int GetNpcCount() const { return (int)m_npcs.size(); }
  NpcInfo GetNpcInfo(int index) const;

  // Quest marker state — precomputed per guard NPC type
  // marker: '!' = available, '?' = in-progress or completable, '\0' = none
  struct GuardMarker {
    uint16_t guardType;
    char marker;     // '!', '?', '\0'
    bool isGold;     // true=gold, false=grey
  };
  void SetQuestMarkers(const std::vector<GuardMarker> &markers);

  // Server-driven NPC movement (guard patrol)
  void SetNpcMoveTarget(uint16_t serverIndex, float worldX, float worldZ);

  // Guard interaction: pass player position and which NPC is being interacted with
  void SetPlayerPosition(const glm::vec3 &pos) { m_playerPos = pos; }
  void SetInteractingNpc(int npcIndex) { m_interactingNpc = npcIndex; }

private:
  // Shared model data (loaded once per NPC model type)
  struct NpcModel {
    std::string name;
    BMDData *skeleton;             // Non-owning, points into m_ownedBmds
    std::vector<BMDData *> parts;  // Non-owning body part BMDs
    BMDData *weaponBmd = nullptr;  // Non-owning weapon model (guards only)
    int weaponAttachBone = -1;     // -1 = no weapon, 33 = R hand, 42 = L hand
    int defaultAction = 0;        // Starting action (6 = spear idle for guards)
    int rootBone = -1;            // Root bone index for LockPositions
    std::vector<BoneWorldMatrix> cachedWeaponBones; // Static weapon bind-pose (computed once)
  };

  // Per-NPC instance
  struct NpcInstance {
    int modelIdx;       // Index into m_models
    glm::vec3 position;
    float facing;
    float spawnFacing;  // Original facing direction from spawn data
    float animFrame = 0.0f;
    int action = 0;
    float scale = 1.0f;
    uint16_t npcType = 0;
    uint16_t serverIndex = 0; // Server-assigned index (1001+)
    std::string name;


    // NPC ambient sound cooldown (seconds until next trigger)
    float soundCooldown = 0.0f;

    // Guard movement (server-driven via 0x14 NPC_MOVE packets)
    bool isMoving = false;
    glm::vec3 moveTarget{0.0f};
    int walkAction = 0; // BMD action for walking (1 for player model)
    bool isGuard = false;     // true for types 247, 249

    // Per-instance mesh buffers (for CPU re-skinning)
    struct BodyPart {
      int bmdIdx; // Which part in NpcModel::parts (or -1 for skeleton meshes)
      std::vector<MeshBuffers> meshBuffers;
    };
    std::vector<BodyPart> bodyParts;

    // Shadow
    struct ShadowMesh {
      bgfx::DynamicVertexBufferHandle vbo = BGFX_INVALID_HANDLE;
      int vertexCount = 0;
    };
    std::vector<ShadowMesh> shadowMeshes;
    std::vector<BoneWorldMatrix> cachedBones;

    // Weapon (guards only — attached to hand bone)
    std::vector<MeshBuffers> weaponMeshBuffers;
    std::vector<ShadowMesh> weaponShadowMeshes;
  };

  std::vector<std::unique_ptr<BMDData>> m_ownedBmds; // Owns all loaded BMDs
  std::vector<NpcModel> m_models;
  std::vector<NpcInstance> m_npcs;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;
  std::unique_ptr<Shader> m_outlineShader;

  // Shadow map state
  bgfx::TextureHandle m_shadowMapTex = BGFX_INVALID_HANDLE;
  glm::mat4 m_lightMtx{1.0f};

  // Path to NPC textures and data root
  std::string m_npcTexPath;
  std::string m_dataPath;

  // External data
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
  int m_mapId = 0;
  VFXManager *m_vfxManager = nullptr;

  // NPC type → model index mapping (for server-spawned NPCs)
  std::unordered_map<uint16_t, int> m_typeToModel;
  // NPC type → scale overrides
  std::unordered_map<uint16_t, float> m_typeScale;
  bool m_modelsLoaded = false;

  // Guard interaction state
  glm::vec3 m_playerPos{0.0f};
  int m_interactingNpc = -1; // NPC index with open quest dialog
  int m_prevInteractingNpc = -1; // Previous frame's interacting NPC (for detecting close)

  // Helpers
  int loadModel(const std::string &npcPath, const std::string &skeletonFile,
                const std::vector<std::string> &partFiles,
                const std::string &modelName);
  void addNpc(int modelIdx, int gridX, int gridY, int dir, float scale = 1.0f);
  float snapToTerrain(float worldX, float worldZ);
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;
  // Quest marker state (3 chains)
  // Precomputed quest markers per guard NPC type
  std::vector<GuardMarker> m_guardMarkers;

  // Main 5.2: default NPC PlaySpeed = 0.25 per tick at 25fps = 6.25 fps
  static constexpr float ANIM_SPEED = 6.25f;
};

#endif // NPC_MANAGER_HPP

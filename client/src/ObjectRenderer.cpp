#include "ObjectRenderer.hpp"
#include <algorithm>
#include "FrustumCulling.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// BlendMesh texture ID lookup — returns the BMD texture slot index
// that identifies the "window light" mesh for each object type.
// Returns -1 if the object type has no BlendMesh.
// Source: reference ZzzObject.cpp OpenObjectsEnc / CreateObject
static int GetBlendMeshTexId(int type, int mapId = -1) {
  switch (type) {
  case 117:
    return 4; // House03 — window glow (flicker)
  case 118:
    return 8; // House04 — window glow (flicker + UV scroll)
  case 119:
    return 2; // House05 — window glow (flicker + UV scroll)
  case 122:
    return 4; // HouseWall02 — window glow (flicker)
  case 52:
    return 1; // Bonfire01 — fire glow
  case 90:
    return 1; // StreetLight01 — lamp glow
  case 150:
    return 1; // Candle01 — candle glow
  case 98:
    return 2; // Carriage01 — lantern glow
  case 105:
    return 3; // Waterspout01 — water UV scroll
  case 3:
    return (mapId == 4) ? 1 : -1; // Lost Tower: fire torch glow mesh (re02.tga)
  case 4:
    return (mapId == 4) ? 1 : -1; // Lost Tower: fire brazier glow mesh (re02.tga)
  case 41:
    if (mapId == 3) return 0; // Noria: flowing additive mesh 0
    return 1; // Dungeon: DungeonGate02 torch — fire glow mesh
  case 42:
    if (mapId == 3) return -1; // Noria: StreamMesh (no BlendMesh)
    return 1; // Dungeon: DungeonGate03 torch — fire glow mesh
  // Devias-only (World3) BlendMesh objects — Main 5.2: ZzzObject.cpp
  // Types 19/92/93 are aurora curtains on Devias but cannons on Lorencia
  case 19:
    if (mapId == 3) return 0; // Noria: additive mesh 0
    if (mapId == 4) return 4; // Lost Tower: fire torch additive mesh 4
    return (mapId == 2) ? 0 : -1; // Devias: aurora additive
  case 20:
    return (mapId == 4) ? 4 : -1; // Lost Tower: lightning pillar additive mesh 4
  case 23:
    return (mapId == 4) ? 1 : -1; // Lost Tower: window/grate additive mesh 1
  case 92:
  case 93:
    return (mapId == 2) ? 0 : -1; // Aurora additive only on Devias
  // Noria-specific BlendMesh objects — Main 5.2: ZzzObject.cpp WD_3NORIA
  case 1:
    return (mapId == 3) ? 1 : -1; // Noria: glowing mesh 1
  case 9:
    return (mapId == 3) ? 3 : -1; // Noria: glowing mesh 3
  case 17:
    return (mapId == 3) ? 0 : -1; // Noria: additive mesh 0
  case 18:
    if (mapId == 4) return 1; // Lost Tower: additive mesh 1
    return (mapId == 3) ? 2 : -1; // Noria: glowing mesh 2
  case 37:
    return (mapId == 3) ? 0 : -1; // Noria: additive mesh 0
  case 39:
    return (mapId == 3) ? 1 : -1; // Noria: glowing mesh 1
  case 54:
    return 1; // Tomb glow
  case 56:
    return 1; // MerchantAnimal glow
  case 78:
    return 3; // StoneMuWall torch/window glow
  default:
    return -1;
  }
}

// Door types for Devias (Main 5.2: ZzzObject.cpp:3871-3913)
static bool IsDoorType(int type) {
  return type == 20 || type == 65 || type == 86 || type == 88;
}

// Type-to-filename mapping based on reference _enum.h + MapManager.cpp
// AccessModel(TYPE, dir, "BaseName", index):
//   index == -1 → BaseName.bmd
//   index < 10  → BaseName0X.bmd
//   else        → BaseNameX.bmd
std::string ObjectRenderer::GetObjectBMDFilename(int type) {
  struct TypeEntry {
    int baseType;
    const char *baseName;
    int startIndex; // 1-based index for first item, -1 for no index
  };

  // Ranges: type = baseType + offset → BaseName(startIndex + offset).bmd
  static const TypeEntry ranges[] = {
      {0, "Tree", 1},            // 0-19: Tree01-Tree20
      {20, "Grass", 1},          // 20-29: Grass01-Grass10
      {30, "Stone", 1},          // 30-39: Stone01-Stone10
      {40, "StoneStatue", 1},    // 40-42: StoneStatue01-03
      {43, "SteelStatue", 1},    // 43: SteelStatue01
      {44, "Tomb", 1},           // 44-46: Tomb01-03
      {50, "FireLight", 1},      // 50-51: FireLight01-02
      {52, "Bonfire", 1},        // 52: Bonfire01
      {55, "DoungeonGate", 1},   // 55: DoungeonGate01
      {56, "MerchantAnimal", 1}, // 56-57: MerchantAnimal01-02
      {58, "TreasureDrum", 1},   // 58: TreasureDrum01
      {59, "TreasureChest", 1},  // 59: TreasureChest01
      {60, "Ship", 1},           // 60: Ship01
      {65, "SteelWall", 1},      // 65-67: SteelWall01-03
      {68, "SteelDoor", 1},      // 68: SteelDoor01
      {69, "StoneWall", 1},      // 69-74: StoneWall01-06
      {75, "StoneMuWall", 1},    // 75-78: StoneMuWall01-04
      {80, "Bridge", 1},         // 80: Bridge01
      {81, "Fence", 1},          // 81-84: Fence01-04
      {85, "BridgeStone", 1},    // 85: BridgeStone01
      {90, "StreetLight", 1},    // 90: StreetLight01
      {91, "Cannon", 1},         // 91-93: Cannon01-03
      {95, "Curtain", 1},        // 95: Curtain01
      {96, "Sign", 1},           // 96-97: Sign01-02
      {98, "Carriage", 1},       // 98-101: Carriage01-04
      {102, "Straw", 1},         // 102-103: Straw01-02
      {105, "Waterspout", 1},    // 105: Waterspout01
      {106, "Well", 1},          // 106-109: Well01-04
      {110, "Hanging", 1},       // 110: Hanging01
      {111, "Stair", 1},         // 111: Stair01
      {115, "House", 1},         // 115-119: House01-05
      {120, "Tent", 1},          // 120: Tent01
      {121, "HouseWall", 1},     // 121-126: HouseWall01-06
      {127, "HouseEtc", 1},      // 127-129: HouseEtc01-03
      {130, "Light", 1},         // 130-132: Light01-03
      {133, "PoseBox", 1},       // 133: PoseBox01
      {140, "Furniture", 1},     // 140-146: Furniture01-07
      {150, "Candle", 1},        // 150: Candle01
      {151, "Beer", 1},          // 151-153: Beer01-03
  };

  static const int numRanges = sizeof(ranges) / sizeof(ranges[0]);

  // Find the range that contains this type (search backwards to find best
  // match)
  const TypeEntry *best = nullptr;
  for (int i = 0; i < numRanges; ++i) {
    if (type >= ranges[i].baseType) {
      best = &ranges[i];
    }
  }

  if (!best || type < best->baseType)
    return "";

  int offset = type - best->baseType;
  int index = best->startIndex + offset;

  // Format: BaseName0X.bmd (zero-padded for index < 10)
  char buf[64];
  if (index < 10)
    snprintf(buf, sizeof(buf), "%s0%d.bmd", best->baseName, index);
  else
    snprintf(buf, sizeof(buf), "%s%d.bmd", best->baseName, index);

  return buf;
}

void ObjectRenderer::Init() {
  shader = Shader::Load("vs_model.bin", "fs_model.bin");
  skinnedShader = Shader::Load("vs_model_skinned.bin", "fs_model.bin");
  if (!shader || !skinnedShader)
    std::cerr << "[ObjectRenderer] Failed to load BGFX shaders" << std::endl;
}

void ObjectRenderer::UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                                const std::vector<BoneWorldMatrix> &bones,
                                std::vector<MeshBuffers> &out, bool dynamic,
                                const std::string &fallbackTexDir) {
  MeshBuffers mb;

  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(), srcVert.Position);
        vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }
      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back((uint16_t)(startIdx + v));
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(), srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }
        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back((uint16_t)(vertices.size() - 1));
      }
    }
  }

  mb.indexCount = (int)indices.size();
  mb.vertexCount = (int)vertices.size();
  mb.isDynamic = dynamic;

  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  static bgfx::VertexLayout s_objLayout;
  static bool layoutInit = false;
  if (!layoutInit) {
    s_objLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    layoutInit = true;
  }

  uint32_t vbSize = (uint32_t)(vertices.size() * sizeof(Vertex));
  uint32_t ibSize = (uint32_t)(indices.size() * sizeof(uint16_t));

  if (dynamic) {
    mb.dynVbo = bgfx::createDynamicVertexBuffer(
        bgfx::copy(vertices.data(), vbSize), s_objLayout, BGFX_BUFFER_ALLOW_RESIZE);
  } else {
    mb.vbo = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), vbSize), s_objLayout);
  }
  mb.ebo = bgfx::createIndexBuffer(bgfx::copy(indices.data(), ibSize));

  auto texResult = TextureLoader::ResolveWithInfo(baseDir, mesh.TextureName);
  if (!TexValid(texResult.textureID) && !fallbackTexDir.empty())
    texResult = TextureLoader::ResolveWithInfo(fallbackTexDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;
  mb.textureName = mesh.TextureName;

  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;
  mb.bmdTextureId = mesh.Texture;

  out.push_back(mb);
}

void ObjectRenderer::UploadMeshGPUSkinned(const Mesh_t &mesh,
                                           const std::string &baseDir,
                                           std::vector<MeshBuffers> &out) {
  MeshBuffers mb;

  struct BgfxSkinnedVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
    float boneIndex;
    float _pad[3];
  };
  std::vector<BgfxSkinnedVertex> vertices;
  std::vector<uint16_t> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();
    for (int v = 0; v < 3; ++v) {
      BgfxSkinnedVertex vert{};
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
      vert.pos = srcVert.Position;
      vert.normal = srcNorm.Normal;
      vert.boneIndex = (float)srcVert.Node;
      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back((uint16_t)(startIdx + v));
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        BgfxSkinnedVertex vert{};
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
        vert.boneIndex = (float)srcVert.Node;
        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back((uint16_t)(vertices.size() - 1));
      }
    }
  }

  mb.indexCount = (int)indices.size();
  mb.vertexCount = (int)vertices.size();
  mb.isDynamic = false;
  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  static bgfx::VertexLayout s_objSkinnedLayout;
  static bool layoutInit = false;
  if (!layoutInit) {
    s_objSkinnedLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .end();
    layoutInit = true;
  }

  uint32_t vbSize = (uint32_t)(vertices.size() * sizeof(BgfxSkinnedVertex));
  uint32_t ibSize = (uint32_t)(indices.size() * sizeof(uint16_t));

  mb.vbo = bgfx::createVertexBuffer(
      bgfx::copy(vertices.data(), vbSize), s_objSkinnedLayout);
  mb.ebo = bgfx::createIndexBuffer(bgfx::copy(indices.data(), ibSize));

  auto texResult = TextureLoader::ResolveWithInfo(baseDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;
  mb.textureName = mesh.TextureName;

  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;
  mb.bmdTextureId = mesh.Texture;

  out.push_back(mb);
}

void ObjectRenderer::LoadObjects(const std::vector<ObjectData> &objects,
                                 const std::string &objectDir) {
  int skipped = 0;

  // Load all objects
  for (auto &obj : objects) {
    std::string filename = GetObjectBMDFilename(obj.type);
    if (filename.empty()) {
      ++skipped;
      continue;
    }

    // Load model into cache if not already loaded
    if (modelCache.find(obj.type) == modelCache.end()) {
      std::string fullPath = objectDir + "/" + filename;
      auto bmd = BMDParser::Parse(fullPath);
      if (!bmd) {
        // Mark as failed so we don't retry
        modelCache[obj.type] = ModelCache{};
        ++skipped;
        continue;
      }

      ModelCache cache;
      cache.boneMatrices = ComputeBoneMatrices(bmd.get());
      cache.blendMeshTexId = GetBlendMeshTexId(obj.type, m_mapId);

      // Compute post-bone AABB (raw BMD AABB is pre-bone; bone transform changes size)
      if (!cache.boneMatrices.empty()) {
        glm::vec3 mn = bmd->Min, mx = bmd->Max;
        glm::vec3 corners[8] = {
          {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
          {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
        };
        glm::vec3 newMin(1e9f), newMax(-1e9f);
        const auto &bm = cache.boneMatrices[0];
        for (int i = 0; i < 8; ++i) {
          glm::vec3 t = MuMath::TransformPoint(
            (const float(*)[4])bm.data(), corners[i]);
          newMin = glm::min(newMin, t);
          newMax = glm::max(newMax, t);
        }
        cache.aabbMin = newMin;
        cache.aabbMax = newMax;
      } else {
        cache.aabbMin = bmd->Min;
        cache.aabbMax = bmd->Max;
      }

      // Detect animated models (>1 keyframe in first action)
      auto shouldCPUAnimate = [](int t) {
        // CPU re-skinning: low instance count types only
        return t == 56 || t == 57 ||   // MerchantAnimal01-02
               t == 60 ||              // Ship01
               t == 72 || t == 74 ||   // StoneWall04/06 (flag banners)
               t == 90 ||              // StreetLight01
               t == 95 ||              // Curtain01
               t == 96 || t == 97 ||   // Sign01-02
               t == 98 ||              // Carriage01
               t == 105 ||             // Waterspout01
               t == 110 ||             // Hanging01
               t == 118 || t == 119 || // House04-05
               t == 120 ||             // Tent01
               t == 150;               // Candle01
      };
      // GPU skinning: high instance count animated types (trees)
      bool isTree = (obj.type >= 0 && obj.type <= 19);
      bool hasAnim = !bmd->Actions.empty() && bmd->Actions[0].NumAnimationKeys > 1;

      if (hasAnim && isTree) {
        // GPU-skinned path: store raw vertices + bone indices
        cache.isGPUAnimated = true;
        cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMeshGPUSkinned(bmd->Meshes[mi], objectDir + "/",
                               cache.meshBuffers);
        }
      } else {
        if (hasAnim && shouldCPUAnimate(obj.type)) {
          cache.isAnimated = true;
          cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        }
        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMesh(bmd->Meshes[mi], objectDir + "/", cache.boneMatrices,
                     cache.meshBuffers, cache.isAnimated);
        }
      }

      // Mark BlendMesh meshes (window light / glow)
      if (cache.blendMeshTexId >= 0) {
        for (auto &mb : cache.meshBuffers) {
          if (mb.bmdTextureId == cache.blendMeshTexId) {
            mb.isWindowLight = true;
          }
        }
      }

      // Retain BMD data for animated types (CPU re-skinning or GPU bone compute)
      if (cache.isAnimated || cache.isGPUAnimated) {
        cache.bmdData = std::move(bmd);
        std::cout << "  [" << (cache.isGPUAnimated ? "GPU-Anim" : "CPU-Anim")
                  << "] type " << obj.type
                  << " keys=" << cache.numAnimationKeys << std::endl;
      }

      modelCache[obj.type] = std::move(cache);
    }

    // Skip instances with empty model cache (failed to load)
    auto &cache = modelCache[obj.type];
    if (cache.meshBuffers.empty()) {
      ++skipped;
      continue;
    }



    glm::vec3 objPos = obj.position;

    // Fix floating candle — model origin is above base, lower onto table surface
    if (obj.type == 150)
      objPos.y -= 50.0f;

    // Snap Grass05-08 (types 24-27) to terrain height on Lorencia
    // EncTerrain1.obj has bad Y coords for these grass objects
    if (m_mapId == 0 && obj.type >= 24 && obj.type <= 27) {
      float terrH = SampleTerrainHeight(objPos.x, objPos.z);
      objPos.y = terrH;
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), objPos);
    // MU Z-up → OpenGL Y-up coordinate conversion
    // Position maps as: GL_X=MU_Y, GL_Y=MU_Z, GL_Z=MU_X
    // Model geometry must match: Rz(-90)*Ry(-90) permutes axes correctly
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    // Apply MU rotation (stored as radians in obj.rotation, raw degrees in
    // mu_angle_raw)
    model = glm::rotate(model, obj.rotation.z,
                        glm::vec3(0.0f, 0.0f, 1.0f)); // MU Z rotation
    model = glm::rotate(model, obj.rotation.y,
                        glm::vec3(0.0f, 1.0f, 0.0f)); // MU Y rotation
    model = glm::rotate(model, obj.rotation.x,
                        glm::vec3(1.0f, 0.0f, 0.0f)); // MU X rotation
    model = glm::scale(model, glm::vec3(obj.scale));

    // Sample terrain lightmap at object's world position
    glm::vec3 worldPos = glm::vec3(model[3]);
    glm::vec3 tLight = SampleTerrainLight(worldPos);

    // Per-instance animation phase offset (unique per tree from position hash)
    float phaseOff = 0.0f;
    if (obj.type >= 0 && obj.type <= 19) {
      glm::vec3 wp = glm::vec3(model[3]);
      phaseOff = std::fmod(std::abs(wp.x * 0.137f + wp.z * 0.251f + wp.y * 0.193f), 1.0f);
    }
    instances.push_back({obj.type, model, tLight, phaseOff});

    // Collect interactive objects for sit/pose system (Main 5.2 OPERATE)
    // Lorencia: type 6=Tree07 (sit), 133=PoseBox (pose), 145=Furniture06 (sit),
    // 146=Furniture07 (sit)
    InteractType iact = InteractType::SIT;
    bool isInteractive = false;
    bool alignAngle = false;
    float pickRadius = 30.0f;
    float pickHeight = 100.0f;
    switch (obj.type) {
    case 6:   // MODEL_TREE01+6 — sit, no angle
      iact = InteractType::SIT;
      isInteractive = true;
      pickRadius = 40.0f;
      pickHeight = 120.0f;
      break;
    case 133: // MODEL_POSE_BOX — pose, with angle
      iact = InteractType::POSE;
      isInteractive = true;
      alignAngle = true;
      pickRadius = 20.0f;
      pickHeight = 100.0f;
      break;
    case 145: // MODEL_FURNITURE01+5 — sit, with angle
      iact = InteractType::SIT;
      isInteractive = true;
      break;
    case 146: // MODEL_FURNITURE01+6 — sit, no angle
      iact = InteractType::SIT;
      isInteractive = true;
      break;
    default:
      break;
    }
    if (isInteractive) {
      InteractiveObject io;
      io.type = obj.type;
      io.worldPos = worldPos;
      io.facingAngle = obj.mu_angle_raw.z; // MU Z rotation in degrees
      io.alignToObject = alignAngle;
      io.action = iact;
      io.radius = pickRadius;
      io.height = pickHeight;
      m_interactiveObjects.push_back(io);
    }
  }  // end object loop

  // Grass types 20-21: EncTerrain1.obj has corrupted position/rotation data.
  // Seed tuner maps so corrections are applied in Render() and visible in F2 UI.
  if (m_typeOffset.find(20) == m_typeOffset.end()) {
    m_typeOffset[20] = glm::vec3(33.0f, -107.8f, 0.0f);
    m_typeScaleMult[20] = 0.61f;
  }
  if (m_typeOffset.find(21) == m_typeOffset.end()) {
    m_typeOffset[21] = glm::vec3(22.6f, -85.2f, 180.9f);
    m_typeRotOffset[21] = glm::vec3(25.1f, -68.8f, 7.8f);
    m_typeScaleMult[21] = 0.73f;
  }

  std::cout << "[ObjectRenderer] Loaded " << instances.size()
            << " object instances, " << modelCache.size()
            << " unique models, " << m_interactiveObjects.size()
            << " interactive objects, skipped " << skipped << std::endl;

}

void ObjectRenderer::LoadObjectsGeneric(
    const std::vector<ObjectData> &objects, const std::string &objectDir,
    const std::string &fallbackDir) {
  int skipped = 0;
  int fromFallback = 0;

  for (auto &obj : objects) {
    // Load model into cache if not already loaded
    if (modelCache.find(obj.type) == modelCache.end()) {
      // Try 1: Generic naming from objectDir (ObjectXX.bmd)
      int idx = obj.type + 1;
      char buf[64];
      if (idx < 10)
        snprintf(buf, sizeof(buf), "Object0%d.bmd", idx);
      else
        snprintf(buf, sizeof(buf), "Object%d.bmd", idx);

      std::string fullPath = objectDir + "/" + buf;
      std::string texDir = objectDir + "/";
      std::string fallbackTexDir; // For texture fallback when BMD from objectDir
      auto bmd = BMDParser::Parse(fullPath);
      if (bmd && !fallbackDir.empty())
        fallbackTexDir = fallbackDir + "/"; // BMD from primary dir, fallback textures to Object1/

      // Try 2: Fallback to Lorencia naming from fallbackDir
      if (!bmd && !fallbackDir.empty()) {
        std::string lorName = GetObjectBMDFilename(obj.type);
        if (!lorName.empty()) {
          fullPath = fallbackDir + "/" + lorName;
          texDir = fallbackDir + "/";
          fallbackTexDir.clear(); // Already using fallback dir for textures
          bmd = BMDParser::Parse(fullPath);
          if (bmd)
            ++fromFallback;
        }
      }

      if (!bmd) {
        modelCache[obj.type] = ModelCache{};
        ++skipped;
        continue;
      }

      ModelCache cache;
      cache.boneMatrices = ComputeBoneMatrices(bmd.get());
      cache.blendMeshTexId = GetBlendMeshTexId(obj.type, m_mapId);
      // Post-bone AABB
      if (!cache.boneMatrices.empty()) {
        glm::vec3 mn = bmd->Min, mx = bmd->Max;
        glm::vec3 corners[8] = {
          {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
          {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
        };
        glm::vec3 newMin(1e9f), newMax(-1e9f);
        const auto &bm = cache.boneMatrices[0];
        for (int i = 0; i < 8; ++i) {
          glm::vec3 t = MuMath::TransformPoint(
            (const float(*)[4])bm.data(), corners[i]);
          newMin = glm::min(newMin, t);
          newMax = glm::max(newMax, t);
        }
        cache.aabbMin = newMin;
        cache.aabbMax = newMax;
      } else {
        cache.aabbMin = bmd->Min;
        cache.aabbMax = bmd->Max;
      }

      // GPU tree sway only for Lorencia (LoadObjects path). Non-Lorencia tree
      // BMDs (ObjectXX.bmd) have different bone/animation structures that the
      // sway shader misinterprets, causing severe distortion.
      bool isTree = false;
      bool hasAnim = !bmd->Actions.empty() && bmd->Actions[0].NumAnimationKeys > 1;

      if (hasAnim && isTree) {
        // GPU-skinned path for trees (enables vertex shader sway)
        cache.isGPUAnimated = true;
        cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMeshGPUSkinned(bmd->Meshes[mi], texDir, cache.meshBuffers);
        }
      } else {
        // CPU animation: retransform shared mesh once per type per frame.
        // Cost is per-type (not per-instance), so no instance count limit needed.
        if (hasAnim) {
          cache.isAnimated = true;
          cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        }

        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMesh(bmd->Meshes[mi], texDir, cache.boneMatrices,
                     cache.meshBuffers, cache.isAnimated, fallbackTexDir);
        }
      }

      // Mark BlendMesh meshes (fire glow / torch light)
      if (cache.blendMeshTexId >= 0) {
        for (auto &mb : cache.meshBuffers) {
          if (mb.bmdTextureId == cache.blendMeshTexId) {
            mb.isWindowLight = true;
          }
        }
      }

      if (cache.isAnimated) {
        cache.bmdData = std::move(bmd);
        std::cout << "  [CPU-Anim] type " << obj.type
                  << " keys=" << cache.numAnimationKeys << std::endl;
      }

      modelCache[obj.type] = std::move(cache);
    }

    auto &cache = modelCache[obj.type];
    if (cache.meshBuffers.empty()) {
      ++skipped;
      continue;
    }

    // Skip types where ALL meshes have zero geometry (invisible placeholder BMDs),
    // but keep interactive objects (pose triggers, sit spots) which are intentionally invisible
    {
      bool hasVisibleMesh = false;
      for (auto &mb : cache.meshBuffers) {
        if (mb.indexCount > 0 && !mb.hidden) { hasVisibleMesh = true; break; }
      }
      bool isInteractiveType = (obj.type == 38 || obj.type == 91 || obj.type == 133);
      if (!hasVisibleMesh && !isInteractiveType) {
        ++skipped;
        continue;
      }
    }

    // Build model matrix (same transform as LoadObjects)
    glm::vec3 objPos = obj.position;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), objPos);
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, obj.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, obj.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, obj.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(obj.scale));

    glm::vec3 worldPos = glm::vec3(model[3]);
    glm::vec3 tLight = SampleTerrainLight(worldPos);

    float phaseOff2 = 0.0f;
    if (obj.type >= 0 && obj.type <= 19) {
      phaseOff2 = std::fmod(std::abs(worldPos.x * 0.137f + worldPos.z * 0.251f + worldPos.y * 0.193f), 1.0f);
    }
    int instIdx = (int)instances.size();
    instances.push_back({obj.type, model, tLight, phaseOff2});

    // Collect interactive objects for sit/pose system (Devias types)
    // Main 5.2: types 22,25,40,45,55,73 use CreateOperate
    {
      InteractType iact = InteractType::SIT;
      bool isInteractive = false;
      float pickRadius = 30.0f;
      float pickHeight = 100.0f;
      switch (obj.type) {
      case 22: case 25: case 40: case 45: case 55: case 73:
        iact = InteractType::SIT;
        isInteractive = true;
        pickRadius = 35.0f;
        pickHeight = 110.0f;
        break;
      case 91: // Devias pose trigger (Main 5.2: CreateOperate + HiddenMesh=-2, bbox 40x40x160)
        iact = InteractType::POSE;
        isInteractive = true;
        pickRadius = 40.0f;
        pickHeight = 160.0f;
        break;
      case 133: // MODEL_POSE_BOX (shared from Lorencia)
        iact = InteractType::POSE;
        isInteractive = true;
        pickRadius = 20.0f;
        pickHeight = 100.0f;
        break;
      default:
        // Noria interactive objects (Main 5.2: ZzzObject.cpp WD_3NORIA)
        if (m_mapId == 3) {
          switch (obj.type) {
          case 8: // Chair/bench (Object09.bmd) — CreateOperate, sit
            iact = InteractType::SIT;
            isInteractive = true;
            pickRadius = 35.0f;
            pickHeight = 110.0f;
            break;
          case 90: // StreetLight01 lamp post — visible pose trigger
            iact = InteractType::POSE;
            isInteractive = true;
            pickRadius = 40.0f;
            pickHeight = 160.0f;
            break;
          }
        }
        break;
      }
      if (isInteractive) {
        InteractiveObject io;
        io.type = obj.type;
        io.worldPos = worldPos;
        io.facingAngle = obj.mu_angle_raw.z;
        io.alignToObject = true;
        io.action = iact;
        io.radius = pickRadius;
        io.height = pickHeight;
        m_interactiveObjects.push_back(io);
      }
    }

    // Collect Devias type 100 positions for lightning sprite effect
    if (m_mapId == 2 && obj.type == 100) {
      m_lightningPositions.push_back(worldPos);
    }

    // Register Devias doors with their original transform data
    if (m_mapId == 2 && IsDoorType(obj.type)) {
      DoorState ds;
      ds.instanceIdx = instIdx;
      ds.origPos = worldPos;
      ds.origAngleDeg = (float)((int)obj.mu_angle_raw.z % 360);
      ds.currentAngleDeg = ds.origAngleDeg;
      ds.rotRad = obj.rotation;
      ds.scale = obj.scale;
      ds.isSliding = (obj.type == 86);
      ds.soundPlayed = true;  // Start true: suppress sound until player walks away and returns
      m_doors.push_back(ds);
    }
  }

  if (!m_doors.empty()) {
    std::cout << "[ObjectRenderer] Registered " << m_doors.size()
              << " Devias doors" << std::endl;
    m_doorCooldown = 1.5f; // Suppress door sounds for 1.5s after map load
  }
  if (!m_lightningPositions.empty()) {
    std::cout << "[ObjectRenderer] Found " << m_lightningPositions.size()
              << " type-100 lightning sprite positions" << std::endl;
    // Load lightning2.OZJ for the sprite effect (Main 5.2: BITMAP_LIGHTNING+1)
    // Derive Data/ root from objectDir (e.g. "Data/Object3" → "Data")
    std::string dataRoot = objectDir.substr(0, objectDir.rfind('/'));
    std::string lightningTexPath = dataRoot + "/Effect/lightning2.OZJ";
    m_lightningSpriteTex = TextureLoader::LoadOZJ(lightningTexPath);
    if (!TexValid(m_lightningSpriteTex))
      std::cerr << "[ObjectRenderer] Failed to load " << lightningTexPath << std::endl;
    // Billboard shader + quad geometry
    m_spriteShader = Shader::Load("vs_billboard.bin", "fs_billboard.bin");
    if (!bgfx::isValid(m_spriteQuadVBO)) {
      static bgfx::VertexLayout layout;
      layout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      float qv[] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f,
                      0.5f,  0.5f, 0.0f, -0.5f,  0.5f, 0.0f};
      uint16_t qi[] = {0, 1, 2, 0, 2, 3};
      m_spriteQuadVBO = bgfx::createVertexBuffer(bgfx::copy(qv, sizeof(qv)), layout);
      m_spriteQuadEBO = bgfx::createIndexBuffer(bgfx::copy(qi, sizeof(qi)));
    }
  }
  // Main 5.2: Chrome01 texture for Lost Tower fire StreamMesh rendering
  if (m_mapId == 4 && !TexValid(m_chromeTexture)) {
    std::string dataRoot = objectDir.substr(0, objectDir.rfind('/'));
    m_chromeTexture = TextureLoader::LoadOZJ(dataRoot + "/Effect/Chrome01.OZJ");
  }
  std::cout << "[ObjectRenderer] Generic: Loaded " << instances.size()
            << " instances, " << modelCache.size() << " unique models ("
            << fromFallback << " from fallback), skipped " << skipped
            << std::endl;
}

void ObjectRenderer::SetTerrainLightmap(
    const std::vector<glm::vec3> &lightmap) {
  terrainLightmap = lightmap;
}

glm::vec3 ObjectRenderer::SampleTerrainLight(const glm::vec3 &worldPos) const {
  const int SIZE = 256;
  if (terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  // World → grid: WorldX maps to grid Z, WorldZ maps to grid X
  // (MU Y→WorldX, MU X→WorldZ; lightmap indexed as [z * SIZE + x])
  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;

  int xi = (int)gx;
  int zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi;
  float zd = gz - (float)zi;

  // 4 corners for bilinear interpolation
  const glm::vec3 &c00 = terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = terrainLightmap[(zi + 1) * SIZE + (xi + 1)];

  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

// Bilinear interpolation of terrain height at world position
// Used for snapping rotated grass objects to terrain
float ObjectRenderer::SampleTerrainHeight(float worldX, float worldZ) const {
  const int SIZE = 256;
  if (terrainHeightmap.size() < (size_t)(SIZE * SIZE))
    return 0.0f;

  // World → grid: WorldX maps to grid Z, WorldZ maps to grid X
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;

  // Clamp to valid range
  gz = std::clamp(gz, 0.0f, (float)(SIZE - 2));
  gx = std::clamp(gx, 0.0f, (float)(SIZE - 2));

  int xi = (int)gx;
  int zi = (int)gz;
  float xd = gx - (float)xi;
  float zd = gz - (float)zi;

  // Bilinear interpolation of 4 corners
  float h00 = terrainHeightmap[zi * SIZE + xi];
  float h10 = terrainHeightmap[zi * SIZE + (xi + 1)];
  float h01 = terrainHeightmap[(zi + 1) * SIZE + xi];
  float h11 = terrainHeightmap[(zi + 1) * SIZE + (xi + 1)];

  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
         h01 * (1 - xd) * zd + h11 * xd * zd;
}

void ObjectRenderer::SetTypeAlpha(
    const std::unordered_map<int, float> &alphaMap) {
  typeAlphaMap = alphaMap;
}

void ObjectRenderer::SetPointLights(const std::vector<glm::vec3> &positions,
                                    const std::vector<glm::vec3> &colors,
                                    const std::vector<float> &ranges) {
  plCount = std::min((int)positions.size(), 64);
  plPositions.assign(positions.begin(), positions.begin() + plCount);
  plColors.assign(colors.begin(), colors.begin() + plCount);
  plRanges.assign(ranges.begin(), ranges.begin() + plCount);
}

void ObjectRenderer::RetransformMesh(const Mesh_t &mesh,
                                     const std::vector<BoneWorldMatrix> &bones,
                                     MeshBuffers &mb) {
  if (!mb.isDynamic || mb.vertexCount == 0 || !bgfx::isValid(mb.dynVbo))
    return;

  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  vertices.reserve(mb.vertexCount);

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(), srcVert.Position);
        vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }
      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(), srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }
        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
      }
    }
  }

  bgfx::update(mb.dynVbo, 0,
               bgfx::copy(vertices.data(), (uint32_t)(vertices.size() * sizeof(Vertex))));
}


void ObjectRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                            const glm::vec3 &cameraPos, float currentTime) {
  if (instances.empty() || !shader)
    return;

  // Extract frustum planes from VP matrix for culling
  auto frustum = FrustumCulling::ExtractFrustumPlanes(projection, view);

  // Upload point lights
  shader->uploadPointLights(plCount, plPositions.data(), plColors.data(), plRanges.data());

  // Lazy-init bone matrix uniform handle
  if (!bgfx::isValid(u_boneMatrices)) {
    u_boneMatrices = bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 48);
  }

  // Advance skeletal animation for animated model types
  {
    const float ANIM_SPEED = 4.0f;
    const float TREE_ANIM_SPEED = 4.0f;
    float dt = (lastAnimTime > 0.0f) ? (currentTime - lastAnimTime) : 0.0f;
    if (dt > 0.0f && dt < 1.0f) {
      for (auto &[type, cache] : modelCache) {
        if (!cache.bmdData)
          continue;

        if (cache.isGPUAnimated) {
          auto &state = animStates[type];
          state.frame += TREE_ANIM_SPEED * dt;
          if (state.frame >= (float)cache.numAnimationKeys)
            state.frame = std::fmod(state.frame, (float)cache.numAnimationKeys);
          continue;
        }

        if (!cache.isAnimated)
          continue;

        auto &state = animStates[type];
        state.frame += ANIM_SPEED * dt;
        if (state.frame >= (float)cache.numAnimationKeys)
          state.frame = std::fmod(state.frame, (float)cache.numAnimationKeys);

        auto bones = ComputeBoneMatricesInterpolated(cache.bmdData.get(), 0, state.frame);

        bool isWallFlag = (type == 72 || type == 74);
        if (isWallFlag) {
          const float blend = 0.3f;
          auto &rest = cache.boneMatrices;
          for (int bi = 0; bi < (int)bones.size() && bi < (int)rest.size(); ++bi) {
            for (int r = 0; r < 3; ++r)
              for (int c = 0; c < 4; ++c)
                bones[bi][r][c] = rest[bi][r][c] + blend * (bones[bi][r][c] - rest[bi][r][c]);
          }
        }

        for (int mi = 0; mi < (int)cache.meshBuffers.size() &&
                         mi < (int)cache.bmdData->Meshes.size(); ++mi) {
          if (isWallFlag && cache.meshBuffers[mi].textureName.find("badge") == std::string::npos)
            continue;
          RetransformMesh(cache.bmdData->Meshes[mi], bones, cache.meshBuffers[mi]);
        }
      }
    }
    lastAnimTime = currentTime;
  }

  // BlendMesh flicker + UV scroll (pure math, same as GL)
  float flickerBase = 0.72f + 0.09f * std::sin(currentTime * 4.7f)
                            + 0.07f * std::sin(currentTime * 11.3f + 1.3f)
                            + 0.04f * std::sin(currentTime * 21.7f + 3.7f);
  float uvScroll = -std::fmod(currentTime, 1.0f);

  float dungeonWaterScroll = 0.0f;
  if (m_mapId == 1) {
    int wt = (int)(currentTime * 1000.0f) % 1000;
    dungeonWaterScroll = -(float)wt * 0.001f;
  }

  // Common uniforms (BGFX uniforms persist within a frame until overwritten)
  glm::vec3 sunPos = cameraPos + glm::vec3(0, 8000, 0);

  for (auto &inst : instances) {
    if (inst.type == 133 || (inst.type >= 130 && inst.type <= 132))
      continue;
    if (m_mapId == 1 && (inst.type == 39 || inst.type == 40 || inst.type == 51 ||
         inst.type == 52 || inst.type == 60))
      continue;
    if (m_mapId == 4 && (inst.type == 24 || inst.type == 25)) {
      // Type 24/25: HiddenMesh=-2 (invisible mesh). Type 24 fire columns
      // are created via FireEffect emitters registered in main.cpp, not here.
      continue;
    }
    if (m_mapId == 2 && (inst.type == 91 || inst.type == 100))
      continue;
    if (m_mapId == 3 && inst.type == 38) // Noria pose box (HiddenMesh=-2)
      continue;
    if (m_mapId == 0 && (inst.type == 20 || inst.type == 21)) // Lorencia grass (bad Y coords)
      continue;
    if (!m_typeFilter.empty()) {
      bool allowed = false;
      for (int t : m_typeFilter)
        if (inst.type == t) { allowed = true; break; }
      if (!allowed) continue;
    }

    // Frustum culling
    {
      glm::vec3 objPos = glm::vec3(inst.modelMatrix[3]);
      float cullRadius = 500.0f;
      if (!FrustumCulling::IsSphereInFrustum(frustum, objPos, cullRadius))
        continue;
    }

    auto it = modelCache.find(inst.type);
    if (it == modelCache.end()) continue;

    float instAlpha = 1.0f;
    auto alphaIt = typeAlphaMap.find(inst.type);
    if (alphaIt != typeAlphaMap.end()) {
      instAlpha = alphaIt->second;
      if (instAlpha < 0.01f) continue;
    }
    // Dev tuner: per-type base alpha (permanent transparency, e.g. grass foliage)
    auto baseAlphaIt = m_typeBaseAlpha.find(inst.type);
    if (baseAlphaIt != m_typeBaseAlpha.end()) {
      instAlpha *= baseAlphaIt->second;
      if (instAlpha < 0.01f) continue;
    }

    // Determine if this instance uses GPU skinning
    bool useGPUSkin = it->second.isGPUAnimated && it->second.bmdData;
    Shader *activeShader = useGPUSkin ? skinnedShader.get() : shader.get();
    if (!activeShader) continue;

    // GPU skinning: compute and upload per-instance bone matrices
    if (useGPUSkin) {
      auto &state = animStates[inst.type];
      float numKeys = (float)it->second.numAnimationKeys;
      float instFrame = std::fmod(state.frame + inst.animPhaseOffset * numKeys, numKeys);

      auto bones = ComputeBoneMatricesInterpolated(it->second.bmdData.get(), 0, instFrame);
      int count = std::min((int)bones.size(), 48);
      static std::vector<glm::mat4> tmpMats;
      tmpMats.resize(count);
      for (int bi = 0; bi < count; ++bi) {
        auto &bm = bones[bi];
        tmpMats[bi][0] = glm::vec4(bm[0][0], bm[1][0], bm[2][0], 0.0f);
        tmpMats[bi][1] = glm::vec4(bm[0][1], bm[1][1], bm[2][1], 0.0f);
        tmpMats[bi][2] = glm::vec4(bm[0][2], bm[1][2], bm[2][2], 0.0f);
        tmpMats[bi][3] = glm::vec4(bm[0][3], bm[1][3], bm[2][3], 1.0f);
      }
      bgfx::setUniform(u_boneMatrices, glm::value_ptr(tmpMats[0]), count);
      activeShader->setVec4("u_skinParams",
          glm::vec4(inst.animPhaseOffset * 6.2832f, currentTime, 0.0f, 0.0f));
    }

    bool hasBlendMesh = it->second.blendMeshTexId >= 0;
    bool hasUVScroll = (inst.type == 118 || inst.type == 119 || inst.type == 105);
    // Noria flowing objects with UV scroll (Main 5.2 MoveObject)
    bool isNoriaFlowing = (m_mapId == 3 &&
        (inst.type == 18 || inst.type == 41 || inst.type == 42 || inst.type == 43));
    if (m_mapId == 3 && inst.type == 41) hasUVScroll = true;
    bool isDungeonWater = (m_mapId == 1 && inst.type == 22);
    bool disableCullForObj = (m_mapId == 1 &&
        ((inst.type >= 44 && inst.type <= 46) ||
         (inst.type >= 22 && inst.type <= 24) ||
         inst.type == 35 || inst.type == 11 || inst.type == 53));

    // Two-pass rendering: opaque meshes first (write depth), then
    // alpha/additive meshes (read depth only).
    auto &meshBufs = it->second.meshBuffers;

    int passOrder[2] = {0, 1}; // 0=opaque pass, 1=alpha pass
    for (int pass : passOrder) {
    int meshIdx = 0;
    for (auto &mb : meshBufs) {
      if (mb.indexCount == 0 || mb.hidden) { ++meshIdx; continue; }
      if (!TexValid(mb.texture)) { ++meshIdx; continue; }
      bool isTransparent = mb.hasAlpha || mb.isWindowLight || mb.bright;
      if (pass == 0 && isTransparent) { ++meshIdx; continue; }
      if (pass == 1 && !isTransparent) { ++meshIdx; continue; }
      // Apply dev tuner offsets/scale/rotation if present
      {
        glm::mat4 renderMat = inst.modelMatrix;
        auto offIt = m_typeOffset.find(inst.type);
        if (offIt != m_typeOffset.end())
          renderMat[3] += glm::vec4(offIt->second, 0.0f);
        auto rotIt = m_typeRotOffset.find(inst.type);
        if (rotIt != m_typeRotOffset.end()) {
          glm::vec3 p = glm::vec3(renderMat[3]);
          glm::vec3 r = rotIt->second;
          glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(r.x), glm::vec3(1,0,0));
          rot = glm::rotate(rot, glm::radians(r.y), glm::vec3(0,1,0));
          rot = glm::rotate(rot, glm::radians(r.z), glm::vec3(0,0,1));
          renderMat = glm::translate(glm::mat4(1.0f), p) * rot
                    * glm::translate(glm::mat4(1.0f), -p) * renderMat;
        }
        auto scaleIt = m_typeScaleMult.find(inst.type);
        if (scaleIt != m_typeScaleMult.end() && scaleIt->second != 1.0f) {
          glm::vec3 p = glm::vec3(renderMat[3]);
          renderMat = glm::translate(glm::mat4(1.0f), p)
                    * glm::scale(glm::mat4(1.0f), glm::vec3(scaleIt->second))
                    * glm::translate(glm::mat4(1.0f), -p) * renderMat;
        }
        bgfx::setTransform(glm::value_ptr(renderMat));
      }

      // Bind vertex/index buffers
      if (mb.isDynamic) {
        bgfx::setVertexBuffer(0, mb.dynVbo);
      } else {
        bgfx::setVertexBuffer(0, mb.vbo);
      }
      bgfx::setIndexBuffer(mb.ebo);

      // Set texture
      activeShader->setTexture(0, "s_texColor", mb.texture);

      // Common uniforms for this draw call
      // u_params: x=objectAlpha, y=blendMeshLight, z=chromeMode, w=chromeTime
      float blendLight = 1.0f;
      float useFog = m_fogEnabled ? 1.0f : 0.0f;
      glm::vec2 texOffset(0.0f);
      bool isTentacle = (m_mapId == 1 && (inst.type == 23 || inst.type == 24 || inst.type == 35));

      // Determine blend state
      uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                     | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                     | BGFX_STATE_MSAA;
      bool isAdditive = false;

      // Dungeon water: mesh index 1 gets additive UV scroll
      if (isDungeonWater && meshIdx == 1) {
        texOffset = glm::vec2(0.0f, dungeonWaterScroll);
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
        isAdditive = true;
      } else if (isNoriaFlowing && meshIdx == 0) {
        // Noria stream/flowing objects (Main 5.2 MoveObject):
        // Type 18: V scroll, Type 41: BlendMesh+V scroll (handled below),
        // Type 42: StreamMesh U scroll, Type 43: StreamMesh U scroll (opposite)
        int wt;
        if (inst.type == 18) {
          wt = (int)(currentTime * 1000.0f) % 1000;
          texOffset = glm::vec2(0.0f, (float)wt * 0.001f);
        } else if (inst.type == 42) {
          wt = (int)(currentTime * 1000.0f) % 500;
          texOffset = glm::vec2(-(float)wt * 0.002f, 0.0f);
        } else if (inst.type == 43) {
          wt = (int)(currentTime * 1000.0f) % 500;
          texOffset = glm::vec2((float)wt * 0.002f, 0.0f);
        }
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
        isAdditive = true;
      } else if (mb.isWindowLight && hasBlendMesh) {
        // BlendMesh: additive blending with intensity flicker
        float intensity = flickerBase;
        if (inst.type == 52)
          intensity = 0.70f + 0.12f * std::sin(currentTime * 6.3f)
                            + 0.08f * std::sin(currentTime * 14.1f + 0.7f)
                            + 0.05f * std::sin(currentTime * 27.3f + 2.1f);
        if (inst.type == 90)
          intensity = 0.7f + 0.1f * std::sin(currentTime * 5.3f);
        if (inst.type == 150 || inst.type == 98 || inst.type == 105)
          intensity = 1.0f;
        if (m_mapId == 1 && (inst.type == 41 || inst.type == 42)) {
          // Dungeon torches: phase-shifted flicker
          float phase = inst.modelMatrix[3][0] * 0.013f;
          intensity = 0.78f + 0.10f * std::sin(currentTime * 3.8f + phase)
                            + 0.06f * std::sin(currentTime * 9.5f + phase * 2.1f);
        }
        if (m_mapId == 4 && (inst.type == 3 || inst.type == 4 ||
                             inst.type == 19 || inst.type == 20 || inst.type == 23)) {
          // Lost Tower torches/fire/windows: phase-shifted flicker
          float phase = inst.modelMatrix[3][0] * 0.013f;
          intensity = 0.78f + 0.10f * std::sin(currentTime * 3.8f + phase)
                            + 0.06f * std::sin(currentTime * 9.5f + phase * 2.1f);
        }
        // Noria BlendMesh objects: steady glow
        if (m_mapId == 3) intensity = 1.0f;
        blendLight = intensity;
        if (hasUVScroll) {
          if (m_mapId == 3 && inst.type == 41) {
            // Noria type 41: slower V scroll (Main 5.2: WorldTime%2000*0.0005)
            int wt = (int)(currentTime * 1000.0f) % 2000;
            texOffset = glm::vec2(0.0f, (float)wt * 0.0005f);
          } else {
            texOffset = glm::vec2(0.0f, uvScroll);
          }
        }
        // Lost Tower chrome trees & orbs: U scroll on BlendMesh only
        // Main 5.2: BlendMeshTexCoordU = -(WorldTime%1000)*0.001f
        if (m_mapId == 4 && (inst.type == 3 || inst.type == 4 ||
                             inst.type == 19 || inst.type == 20)) {
          int wt = (int)(currentTime * 1000.0f) % 1000;
          texOffset.x = -(float)wt * 0.001f;
        }
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        // Fountain (type 105) keeps depth write so water blocks objects behind it.
        // Window lights/torches disable depth write so glow is transparent.
        if (inst.type != 105)
          state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
        isAdditive = true;
      } else if (mb.bright) {
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
        isAdditive = true;
      } else if (mb.noneBlend) {
        // No blending, opaque — still respect double-sided objects (tentacles etc.)
        if (!disableCullForObj)
          state |= BGFX_STATE_CULL_CW;
      } else {
        // Normal mesh: cull backfaces unless alpha or special double-sided
        if (!mb.hasAlpha && !disableCullForObj) {
          state |= BGFX_STATE_CULL_CW;
        }
        if (mb.hasAlpha || instAlpha < 1.0f) {
          state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
      }

      // Set uniforms
      // Main 5.2: Lost Tower fire objects (3,4,19,20) use chrome-mapped StreamMesh
      // rendering for "hot metal" fire glow instead of plain additive
      bool useChromeBlend = isAdditive && mb.isWindowLight && m_mapId == 4 &&
          TexValid(m_chromeTexture) &&
          (inst.type == 3 || inst.type == 4 || inst.type == 19 || inst.type == 20);
      float chromeMode = useChromeBlend ? 1.0f : 0.0f;
      float chromeTime = useChromeBlend ? currentTime : 0.0f;
      if (useChromeBlend) {
        // Override texture with chrome environment map, tinted orange
        activeShader->setTexture(0, "s_texColor", m_chromeTexture);
        activeShader->setVec4("u_glowColor", glm::vec4(1.0f, 0.2f, 0.1f, 1.0f));
      }
      activeShader->setVec4("u_params", glm::vec4(instAlpha, blendLight, chromeMode, chromeTime));
      float cliffFadeFlag = 0.0f;
      float cliffTopH = 0.0f;
      // Type 11: cliff wall objects; type 127: HouseEtc01 (92.8-unit foundation)
      // Type 105: Waterspout01 (fountain basin extends below terrain)
      bool needsCliffFade = (inst.type == 11) || (inst.type == 127) || (inst.type == 105) || isTentacle;
      if (needsCliffFade && terrainHeightmap.size() >= 256 * 256) {
        cliffFadeFlag = 1.0f;
        float wx = inst.modelMatrix[3][0];
        float wz = inst.modelMatrix[3][2];
        int cz = std::clamp((int)(wx / 100.0f), 0, 255);
        int cx = std::clamp((int)(wz / 100.0f), 0, 255);
        for (int dz = -3; dz <= 3; dz++)
          for (int dx = -3; dx <= 3; dx++) {
            int iz = std::clamp(cz + dz, 0, 255);
            int ix = std::clamp(cx + dx, 0, 255);
            cliffTopH = std::max(cliffTopH, terrainHeightmap[iz * 256 + ix]);
          }
      }
      activeShader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, cliffFadeFlag, cliffTopH));
      activeShader->setVec4("u_lightPos", glm::vec4(sunPos, 0.0f));
      // Indoor maps (Dungeon, Lost Tower): minimal sun direction — lighting from
      // lightmap only. Main 5.2: indoor objects use BodyLight from lightmap,
      // no directional sun contribution on walls.
      float sunIntensity = (m_mapId == 1 || m_mapId == 4) ? 0.4f : 1.0f;
      activeShader->setVec4("u_lightColor", glm::vec4(sunIntensity, sunIntensity, sunIntensity, 0.0f));
      activeShader->setVec4("u_viewPos", glm::vec4(cameraPos, 0.0f));
      // w=1.0 enables per-pixel lightmap sampling in shader (smooth across meshes)
      // Tentacle/steam objects (23,24,35) in dungeon span into void areas where
      // lightmap RGB is near-zero. Per-pixel sampling makes those fragments
      // nearly black against the black background. Use per-object uniform instead.
      if (isTentacle) {
        glm::vec3 tl = glm::max(inst.terrainLight, glm::vec3(0.50f));
        activeShader->setVec4("u_terrainLight", glm::vec4(tl, 0.0f));
      } else if (TexValid(m_lightmapTex)) {
        activeShader->setVec4("u_terrainLight", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        activeShader->setTexture(2, "s_lightMap", m_lightmapTex);
      } else {
        activeShader->setVec4("u_terrainLight", glm::vec4(inst.terrainLight, 0.0f));
      }
      if (!useChromeBlend)
        activeShader->setVec4("u_glowColor", glm::vec4(0.0f));
      activeShader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
      activeShader->setVec4("u_fogParams", glm::vec4(m_fogNear, m_fogFar, useFog, 0.0f));
      activeShader->setVec4("u_fogColor", glm::vec4(m_fogColor, 0.0f));
      activeShader->setVec4("u_texCoordOffset", glm::vec4(texOffset, 0.0f, 0.0f));
      activeShader->setVec4("u_shadowParams", glm::vec4(0.0f));

      bgfx::setState(state);
      bgfx::submit(0, activeShader->program);
      ++meshIdx;
    }
    } // end pass loop
  }
}

// ── Door animation (Main 5.2: ZzzObject.cpp:3871-3913) ──

void ObjectRenderer::InitDoors() {
  // Doors are registered during LoadObjectsGeneric for Devias (mapId==2).
  // This method exists for explicit re-initialization if needed.
}

// Helper: rebuild a model matrix with modified MU Z angle
static glm::mat4 BuildModelMatrix(const glm::vec3 &pos, const glm::vec3 &rotRad,
                                   float scale) {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
  m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  m = glm::rotate(m, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::rotate(m, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
  m = glm::rotate(m, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
  m = glm::scale(m, glm::vec3(scale));
  return m;
}

// Smooth angle interpolation toward target (Main 5.2: TurnAngle2)
static float TurnAngle2(float current, float target, float maxStep) {
  float diff = target - current;
  while (diff > 180.0f) diff -= 360.0f;
  while (diff < -180.0f) diff += 360.0f;
  if (std::fabs(diff) < maxStep) return target;
  return current + (diff > 0.0f ? maxStep : -maxStep);
}

void ObjectRenderer::UpdateDoors(const glm::vec3 &heroPos, float deltaTime) {
  if (m_doorCooldown > 0.0f) m_doorCooldown -= deltaTime;
  for (auto &door : m_doors) {
    auto &inst = instances[door.instanceIdx];
    float dx = heroPos.x - door.origPos.x;
    float dz = heroPos.z - door.origPos.z;
    float dist = std::sqrt(dx * dx + dz * dz);

    if (dist < 200.0f) {
      if (door.isSliding) {
        // Sliding door (type 86): translate position based on proximity
        // Main 5.2 uses Angle[2] to determine slide direction
        float slideAmount = (200.0f - dist) * 2.0f;
        glm::vec3 newPos = door.origPos;
        // Slide direction: MU angle 90/270 -> MU_Y (GL X), 0/180 -> MU_X (GL Z)
        float angleMod = door.origAngleDeg;
        if (angleMod < 0) angleMod += 360.0f;
        if (angleMod == 90.0f) newPos.x += slideAmount;
        else if (angleMod == 270.0f) newPos.x -= slideAmount;
        else if (angleMod == 0.0f) newPos.z += slideAmount;
        else if (angleMod == 180.0f) newPos.z -= slideAmount;
        inst.modelMatrix[3] = glm::vec4(newPos, 1.0f);
        if (!door.soundPlayed && m_doorCooldown <= 0.0f) {
          SoundManager::Play(SOUND_DOOR02);
          door.soundPlayed = true;
        }
      } else {
        // Swinging door (types 20,65,88): rotate based on proximity
        float swingAmount = (200.0f - dist) * 0.5f;
        float angleMod = door.origAngleDeg;
        if (angleMod < 0) angleMod += 360.0f;
        float newAngle = door.origAngleDeg;
        if (angleMod == 90.0f)
          newAngle = 30.0f - swingAmount;
        else if (angleMod == 270.0f)
          newAngle = 330.0f + swingAmount;
        else if (angleMod == 0.0f)
          newAngle = 300.0f - swingAmount;
        else if (angleMod == 180.0f)
          newAngle = 240.0f + swingAmount;
        door.currentAngleDeg = newAngle;
        glm::vec3 newRot = door.rotRad;
        newRot.z = glm::radians(newAngle);
        inst.modelMatrix = BuildModelMatrix(door.origPos, newRot, door.scale);
        if (!door.soundPlayed && m_doorCooldown <= 0.0f) {
          SoundManager::Play(SOUND_DOOR01);
          door.soundPlayed = true;
        }
      }
    } else {
      // Close: return to original position/angle
      door.soundPlayed = false;
      if (door.isSliding) {
        // Lerp position back
        glm::vec3 curPos = glm::vec3(inst.modelMatrix[3]);
        glm::vec3 newPos = curPos + (door.origPos - curPos) * 0.2f;
        inst.modelMatrix[3] = glm::vec4(newPos, 1.0f);
      } else {
        // Smoothly rotate back (10 degrees per tick ≈ 250 deg/s at 25fps)
        door.currentAngleDeg = TurnAngle2(door.currentAngleDeg, door.origAngleDeg, 10.0f);
        glm::vec3 newRot = door.rotRad;
        newRot.z = glm::radians(door.currentAngleDeg);
        inst.modelMatrix = BuildModelMatrix(door.origPos, newRot, door.scale);
      }
    }
  }
}

void ObjectRenderer::ResetDoorStates() {
  for (auto &door : m_doors) {
    door.soundPlayed = true;
  }
  m_doorCooldown = 0.5f;
}

// ── Lost Tower skull tracking (Main 5.2: ZzzEffectFireLeave.cpp:88-113) ──

void ObjectRenderer::UpdateSkulls(const glm::vec3 &heroPos, bool heroMoving,
                                   float deltaTime) {
  if (m_mapId != 4) return; // Lost Tower only

  const float TRACK_DIST = 50.0f;
  const float TRACK_STRENGTH = 0.4f;
  const float DECAY = 0.6f;

  for (size_t i = 0; i < instances.size(); ++i) {
    auto &inst = instances[i];
    if (inst.type != 38 && inst.type != 39) continue;

    auto &state = m_skullStates[i];
    glm::vec3 skullPos = glm::vec3(inst.modelMatrix[3]);

    float dx = heroPos.x - skullPos.x;
    float dz = heroPos.z - skullPos.z;
    float dist = std::sqrt(dx * dx + dz * dz);

    // Activate tracking: player moving AND within range AND skull not already drifting
    if (heroMoving && dist < TRACK_DIST && glm::length(state.direction) < 0.1f) {
      // Main 5.2: Vector(-dx*0.4f, -dy*0.4f, 0, Direction)
      state.direction.x = -dx * TRACK_STRENGTH;
      state.direction.z = -dz * TRACK_STRENGTH;
      state.headAngle.x = -dz * 4.0f;  // Pitch (MU Y → GL Z)
      state.headAngle.y = -dx * 4.0f;  // Yaw (MU X → GL X)

      if (!state.wasTracking) {
        SoundManager::Play3D(SOUND_BONE2, skullPos.x, skullPos.y, skullPos.z);
        state.wasTracking = true;
      }
    }

    // Decay per frame (Main 5.2: VectorScale(Direction, 0.6, Direction))
    state.direction *= DECAY;
    state.headAngle *= DECAY;

    // Apply drift (Main 5.2: VectorAdd(Position, Direction, Position))
    // Note: deltaTime * 25 converts to ~25fps tick rate
    inst.modelMatrix[3][0] += state.direction.x * deltaTime * 25.0f;
    inst.modelMatrix[3][2] += state.direction.z * deltaTime * 25.0f;

    // Rotation update: Main 5.2 adds HeadAngle to Angle each tick
    // We'd need to track accumulated rotation in state, but for simplicity
    // we can skip rotation since the drift + sound is the main effect

    // Reset tracking flag when far away
    if (dist >= TRACK_DIST * 1.5f)
      state.wasTracking = false;
  }
}


// ── Devias type 100: rotating lightning sprites (Main 5.2 "northern lights") ──

void ObjectRenderer::RenderLightningSprites(const glm::mat4 &view,
                                             const glm::mat4 &projection,
                                             float currentTime) {
  if (m_lightningPositions.empty() || !m_spriteShader || !TexValid(m_lightningSpriteTex))
    return;

  // Main 5.2 (ZzzObject.cpp:2816): Rotation = (int)(WorldTime*0.1f) % 360
  // Blend: EnableAlphaBlend() = pure additive (ONE, ONE)
  // Color: glColor4f(Lum, Lum, Lum, Lum), alpha irrelevant with ONE,ONE blend
  float rotDeg = std::fmod(currentTime * 36.0f, 360.0f);
  float rotRad = glm::radians(rotDeg);
  float lum = m_luminosity;

  // Build instance data: 2 sprites per position (counter-rotating)
  uint32_t count = (uint32_t)m_lightningPositions.size() * 2;
  const uint16_t stride = 48; // 3 × vec4

  uint32_t avail = bgfx::getAvailInstanceDataBuffer(count, stride);
  if (avail == 0) return;
  count = std::min(count, avail);

  bgfx::InstanceDataBuffer idb;
  bgfx::allocInstanceDataBuffer(&idb, count, stride);

  uint8_t *data = idb.data;
  for (size_t i = 0; i < m_lightningPositions.size(); ++i) {
    glm::vec3 pos = m_lightningPositions[i];
    pos.y += 150.0f; // Main 5.2: Vector(0,0,150,p) Z-offset → our Y

    for (int s = 0; s < 2 && (i * 2 + s) < count; ++s) {
      float *d = (float *)data;
      d[0] = pos.x; d[1] = pos.y; d[2] = pos.z; // worldPos
      d[3] = 250.0f;                               // scale (2.5 * TERRAIN_SCALE)
      d[4] = (s == 0) ? rotRad : -rotRad;          // rotation (counter-rotating)
      d[5] = -1.0f;                                 // frame < 0 = full texture
      d[6] = 1.0f;                                  // alpha = 1.0 (ONE,ONE ignores it)
      d[7] = 0.0f;
      d[8] = lum; d[9] = lum; d[10] = lum;         // color = white * luminosity
      d[11] = 0.0f;
      data += stride;
    }
  }

  bgfx::setVertexBuffer(0, m_spriteQuadVBO);
  bgfx::setIndexBuffer(m_spriteQuadEBO);
  bgfx::setInstanceDataBuffer(&idb);
  m_spriteShader->setTexture(0, "s_fireTex", m_lightningSpriteTex);

  // Main 5.2: pure additive blend (ONE, ONE) — alpha channel irrelevant
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                         BGFX_STATE_BLEND_ONE);
  bgfx::setState(state);
  bgfx::submit(0, m_spriteShader->program);
}

// ── Lost Tower orb sprites (Main 5.2: ZzzObject.cpp:2928-2960) ──

void ObjectRenderer::RenderOrbSprites(const glm::mat4 &view,
                                      const glm::mat4 &projection,
                                      float currentTime) {
  if (m_mapId != 4 || !m_spriteShader || !TexValid(m_lightningSpriteTex))
    return;

  // Main 5.2: Rotation = (WorldTime*0.1f) % 360
  float rotDeg = std::fmod(currentTime * 36.0f, 360.0f);
  float rotRad = glm::radians(rotDeg);

  // Collect sprite positions from bone-based objects
  struct OrbSprite {
    glm::vec3 position;
    float scale;
    glm::vec3 color;
  };
  std::vector<OrbSprite> sprites;

  for (auto &inst : instances) {
    // Type 19 (MagicOrb), Type 20 (LightningOrb): sprites at bones 15, 19, 21
    if (inst.type == 19 || inst.type == 20) {
      auto it = modelCache.find(inst.type);
      if (it == modelCache.end() || !it->second.bmdData) continue;

      // Compute bone matrices (bind pose, action 0)
      auto bones = ComputeBoneMatricesInterpolated(it->second.bmdData.get(), 0, 0.0f);

      // Transform bone positions to world space
      // Main 5.2: CreateSprite(..., &o->BoneTransform[BoneIndex], ...)
      glm::mat4 worldMat = inst.modelMatrix;

      for (int boneIdx : {15, 19, 21}) {
        if (boneIdx >= (int)bones.size()) continue;

        // Extract bone position (column 3 of bone matrix)
        glm::vec3 boneLocal(bones[boneIdx][3][0], bones[boneIdx][3][1], bones[boneIdx][3][2]);
        glm::vec3 boneWorld = glm::vec3(worldMat * glm::vec4(boneLocal, 1.0f));

        // Main 5.2: Scale 0.03 for bones 15/19, 0.15 for bone 21
        float scale = (boneIdx == 21) ? 15.0f : 3.0f; // 0.15 * 100, 0.03 * 100

        // Color: Type 19=MagicOrb (red/orange), Type 20=LightningOrb (blue/cyan)
        glm::vec3 color = (inst.type == 19)
          ? glm::vec3(1.0f, 0.5f, 0.2f)   // Orange/red
          : glm::vec3(0.5f, 0.8f, 1.0f);  // Blue/cyan

        // 2 counter-rotating sprites per bone (Main 5.2: CreateSprite x2)
        sprites.push_back({boneWorld, scale, color});
        sprites.push_back({boneWorld, scale, color});
      }
    }

    // Type 40 (LightningColumn): 2 sprites at position + 260 Y-offset
    if (inst.type == 40) {
      glm::vec3 pos = glm::vec3(inst.modelMatrix[3]);
      pos.y += 260.0f; // Main 5.2: Vector(0,0,260,p) → our Y
      sprites.push_back({pos, 250.0f, glm::vec3(1.0f)}); // Large white
      sprites.push_back({pos, 250.0f, glm::vec3(1.0f)});
    }
  }

  if (sprites.empty()) return;

  // Build instance buffer
  uint32_t count = (uint32_t)sprites.size();
  const uint16_t stride = 48;

  uint32_t avail = bgfx::getAvailInstanceDataBuffer(count, stride);
  if (avail == 0) return;
  count = std::min(count, avail);

  bgfx::InstanceDataBuffer idb;
  bgfx::allocInstanceDataBuffer(&idb, count, stride);

  uint8_t *data = idb.data;
  for (size_t i = 0; i < count / 2; ++i) {
    auto &sprite = sprites[i];
    for (int s = 0; s < 2 && (i * 2 + s) < count; ++s) {
      float *d = (float *)data;
      d[0] = sprite.position.x; d[1] = sprite.position.y; d[2] = sprite.position.z;
      d[3] = sprite.scale;
      d[4] = (s == 0) ? rotRad : -rotRad;  // Counter-rotating
      d[5] = -1.0f;  // frame < 0 = full texture
      d[6] = 1.0f;   // alpha
      d[7] = 0.0f;
      d[8] = sprite.color.x * m_luminosity;
      d[9] = sprite.color.y * m_luminosity;
      d[10] = sprite.color.z * m_luminosity;
      d[11] = 0.0f;
      data += stride;
    }
  }

  bgfx::setVertexBuffer(0, m_spriteQuadVBO);
  bgfx::setIndexBuffer(m_spriteQuadEBO);
  bgfx::setInstanceDataBuffer(&idb);
  m_spriteShader->setTexture(0, "s_fireTex", m_lightningSpriteTex);

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                         BGFX_STATE_BLEND_ONE);
  bgfx::setState(state);
  bgfx::submit(0, m_spriteShader->program);
}

void ObjectRenderer::Cleanup() {
  for (auto &[type, cache] : modelCache) {
    for (auto &mb : cache.meshBuffers) {
      if (bgfx::isValid(mb.vbo)) bgfx::destroy(mb.vbo);
      if (bgfx::isValid(mb.dynVbo)) bgfx::destroy(mb.dynVbo);
      if (bgfx::isValid(mb.ebo)) bgfx::destroy(mb.ebo);
      TexDestroy(mb.texture);
    }
  }
  modelCache.clear();
  instances.clear();
  m_doors.clear();
  m_interactiveObjects.clear();
  m_lightningPositions.clear();
  m_doorCooldown = 0.0f;
  if (bgfx::isValid(u_boneMatrices)) {
    bgfx::destroy(u_boneMatrices);
    u_boneMatrices = BGFX_INVALID_HANDLE;
  }
  TexDestroy(m_chromeTexture);
  TexDestroy(m_lightningSpriteTex);
  if (bgfx::isValid(m_spriteQuadVBO)) { bgfx::destroy(m_spriteQuadVBO); m_spriteQuadVBO = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(m_spriteQuadEBO)) { bgfx::destroy(m_spriteQuadEBO); m_spriteQuadEBO = BGFX_INVALID_HANDLE; }
  m_spriteShader.reset();
  shader.reset();
  skinnedShader.reset();
}

ObjectRenderer::TypeDebugInfo ObjectRenderer::GetTypeDebugInfo(int type) const {
  TypeDebugInfo info;
  info.filename = GetObjectBMDFilename(type);
  auto cacheIt = modelCache.find(type);
  if (cacheIt != modelCache.end()) {
    info.aabbMin = cacheIt->second.aabbMin;
    info.aabbMax = cacheIt->second.aabbMax;
  }
  for (auto &inst : instances) {
    if (inst.type == type) {
      if (info.instanceCount == 0)
        info.firstInstancePos = glm::vec3(inst.modelMatrix[3]);
      ++info.instanceCount;
    }
  }
  return info;
}

std::vector<int> ObjectRenderer::GetLoadedTypes() const {
  std::vector<int> types;
  types.reserve(modelCache.size());
  for (auto &[t, _] : modelCache) {
    // Only include types that have renderable meshes
    bool hasGeometry = false;
    for (auto &mb : _.meshBuffers)
      if (mb.indexCount > 0) { hasGeometry = true; break; }
    if (hasGeometry)
      types.push_back(t);
  }
  std::sort(types.begin(), types.end());
  return types;
}


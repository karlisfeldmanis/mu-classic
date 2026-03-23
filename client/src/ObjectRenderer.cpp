#include "ObjectRenderer.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
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
    return (mapId == 2) ? 0 : -1; // Devias: aurora additive
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

    // Skip grass objects (types 20-29) on non-grass terrain tiles.
    // Original engine only renders grass BMDs on grass terrain (layer1 0 or 1).
    if (obj.type >= 20 && obj.type <= 29 && terrainMapping) {
      const int S = 256;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      if (gz >= 0 && gx >= 0 && gz < S && gx < S) {
        uint8_t tile = terrainMapping->layer1[gz * S + gx];
        if (tile != 0 && tile != 1) {
          ++skipped;
          continue;
        }
      }
    }

    // Snap grass objects (types 20-29) to terrain heightmap to prevent floating
    glm::vec3 objPos = obj.position;
    if (obj.type >= 20 && obj.type <= 29 &&
        terrainHeightmap.size() >= 256 * 256) {
      const int S = 256;
      float gz = objPos.x / 100.0f;
      float gx = objPos.z / 100.0f;
      gz = std::clamp(gz, 0.0f, (float)(S - 2));
      gx = std::clamp(gx, 0.0f, (float)(S - 2));
      int xi = (int)gx, zi = (int)gz;
      float xd = gx - (float)xi, zd = gz - (float)zi;
      float h00 = terrainHeightmap[zi * S + xi];
      float h10 = terrainHeightmap[zi * S + (xi + 1)];
      float h01 = terrainHeightmap[(zi + 1) * S + xi];
      float h11 = terrainHeightmap[(zi + 1) * S + (xi + 1)];
      objPos.y = h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
                 h01 * (1 - xd) * zd + h11 * xd * zd;
    }

    // Fix floating candle — model origin is above base, lower onto table surface
    if (obj.type == 150)
      objPos.y -= 50.0f;

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
      phaseOff = std::fmod(wp.x * 0.0137f + wp.z * 0.0251f + wp.y * 0.0193f, 1.0f);
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
      phaseOff2 = std::fmod(worldPos.x * 0.0137f + worldPos.z * 0.0251f + worldPos.y * 0.0193f, 1.0f);
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
  glm::mat4 vp = projection * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                          vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                          vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                          vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                          vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                          vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                          vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  // Upload point lights
  shader->uploadPointLights(plCount, plPositions.data(), plColors.data(), plRanges.data());

  // Lazy-init bone matrix uniform handle
  if (!bgfx::isValid(u_boneMatrices)) {
    u_boneMatrices = bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 48);
  }

  // Advance skeletal animation for animated model types
  {
    const float ANIM_SPEED = 4.0f;
    const float TREE_ANIM_SPEED = 8.0f;
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
    if (m_mapId == 2 && (inst.type == 91 || inst.type == 100))
      continue;
    if (m_mapId == 3 && inst.type == 38) // Noria pose box (HiddenMesh=-2)
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
      bool outside = false;
      for (int p = 0; p < 6; ++p) {
        if (frustum[p].x * objPos.x + frustum[p].y * objPos.y +
                frustum[p].z * objPos.z + frustum[p].w < -cullRadius) {
          outside = true;
          break;
        }
      }
      if (outside) continue;
    }

    auto it = modelCache.find(inst.type);
    if (it == modelCache.end()) continue;

    float instAlpha = 1.0f;
    auto alphaIt = typeAlphaMap.find(inst.type);
    if (alphaIt != typeAlphaMap.end()) {
      instAlpha = alphaIt->second;
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
      bgfx::setTransform(glm::value_ptr(inst.modelMatrix));

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
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
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
        if (mb.hasAlpha) {
          state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
      }

      // Set uniforms
      activeShader->setVec4("u_params", glm::vec4(instAlpha, blendLight, 0.0f, 0.0f));
      float cliffFadeFlag = 0.0f;
      float cliffTopH = 0.0f;
      bool needsCliffFade = (inst.type == 11) || isTentacle;
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
      activeShader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
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
      activeShader->setVec4("u_glowColor", glm::vec4(0.0f));
      activeShader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
      activeShader->setVec4("u_fogParams", glm::vec4(m_fogNear, m_fogFar, useFog, 0.0f));
      activeShader->setVec4("u_fogColor", glm::vec4(m_fogColor, 0.0f));
      activeShader->setVec4("u_texCoordOffset", glm::vec4(texOffset, 0.0f, 0.0f));
      activeShader->setVec4("u_shadowParams", glm::vec4(0.0f)); // no shadow on world objects

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

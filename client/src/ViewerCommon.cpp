#include "ViewerCommon.hpp"
#include <cmath>

#include <bgfx/bgfx.h>
#include <cstring>

// ---------- BGFX vertex layout ----------

static bgfx::VertexLayout s_modelLayout;
static bool s_modelLayoutInit = false;

const bgfx::VertexLayout &GetModelVertexLayout() {
  if (!s_modelLayoutInit) {
    s_modelLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    s_modelLayoutInit = true;
  }
  return s_modelLayout;
}

// ---------- BGFX Mesh upload ----------

void UploadMeshWithBones(const Mesh_t &mesh, const std::string &textureDir,
                         const std::vector<BoneWorldMatrix> &bones,
                         std::vector<MeshBuffers> &out, AABB &aabb,
                         bool dynamic) {
  MeshBuffers mb;

  std::vector<ViewerVertex> vertices;
  std::vector<uint16_t> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();
    for (int v = 0; v < 3; ++v) {
      ViewerVertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back((uint16_t)(startIdx + v));

      aabb.min = glm::min(aabb.min, vert.pos);
      aabb.max = glm::max(aabb.max, vert.pos);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        ViewerVertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
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

  const auto &layout = GetModelVertexLayout();
  uint32_t vbSize = (uint32_t)(vertices.size() * sizeof(ViewerVertex));
  uint32_t ibSize = (uint32_t)(indices.size() * sizeof(uint16_t));

  if (dynamic) {
    mb.dynVbo = bgfx::createDynamicVertexBuffer(
        bgfx::copy(vertices.data(), vbSize), layout, BGFX_BUFFER_ALLOW_RESIZE);
  } else {
    mb.vbo = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), vbSize), layout);
  }

  mb.ebo = bgfx::createIndexBuffer(bgfx::copy(indices.data(), ibSize));

  // Load texture
  auto texResult = TextureLoader::ResolveWithInfo(textureDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;

  // Parse texture script flags
  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;

  // Main 5.2: BITMAP_HIDE — meshes with "hide" textures are never rendered.
  {
    std::string texLower = mesh.TextureName;
    std::transform(texLower.begin(), texLower.end(), texLower.begin(),
                   ::tolower);
    auto slash = texLower.find_last_of("\\/");
    if (slash != std::string::npos)
      texLower = texLower.substr(slash + 1);
    auto dot = texLower.find_last_of('.');
    if (dot != std::string::npos)
      texLower = texLower.substr(0, dot);

    if (texLower == "hide" || texLower == "hide_m" || texLower == "hide22") {
      mb.hidden = true;
    }
    if (texLower.find("flail00") != std::string::npos) {
      mb.bright = true;
    }
  }

  mb.bmdTextureId = mesh.Texture;
  out.push_back(mb);
}

// ---------- BGFX Retransform ----------

void RetransformMeshWithBones(const Mesh_t &mesh,
                              const std::vector<BoneWorldMatrix> &bones,
                              MeshBuffers &mb) {
  if (!mb.isDynamic || mb.vertexCount == 0 || !bgfx::isValid(mb.dynVbo))
    return;

  std::vector<ViewerVertex> vertices;
  vertices.reserve(mb.vertexCount);

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    for (int v = 0; v < 3; ++v) {
      ViewerVertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
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
        ViewerVertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
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

  uint32_t size = (uint32_t)(vertices.size() * sizeof(ViewerVertex));
  bgfx::update(mb.dynVbo, 0, bgfx::copy(vertices.data(), size));
}

// ---------- BGFX Mesh cleanup ----------

void CleanupMeshBuffers(std::vector<MeshBuffers> &buffers) {
  for (auto &mb : buffers) {
    if (bgfx::isValid(mb.vbo))
      bgfx::destroy(mb.vbo);
    if (bgfx::isValid(mb.dynVbo))
      bgfx::destroy(mb.dynVbo);
    if (bgfx::isValid(mb.ebo))
      bgfx::destroy(mb.ebo);
    TexDestroy(mb.texture);
  }
  buffers.clear();
}

// ---------- OrbitCamera (shared, no GL/BGFX dependency) ----------

glm::vec3 OrbitCamera::GetEyePosition() const {
  float yawRad = glm::radians(yaw);
  float pitchRad = glm::radians(pitch);

  glm::vec3 offset;
  offset.x = distance * cos(pitchRad) * cos(yawRad);
  offset.y = -distance * sin(pitchRad);
  offset.z = distance * cos(pitchRad) * sin(yawRad);

  return center + offset;
}

glm::mat4 OrbitCamera::GetViewMatrix() const {
  return glm::lookAt(GetEyePosition(), center, glm::vec3(0, 1, 0));
}

// ---------- macOS activation ----------

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

void ActivateMacOSApp() {
#ifdef __APPLE__
  id app =
      ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
                                     sel_registerName("sharedApplication"));
  ((void (*)(id, SEL, long))objc_msgSend)(
      app, sel_registerName("setActivationPolicy:"),
      0); // NSApplicationActivationPolicyRegular
  ((void (*)(id, SEL, BOOL))objc_msgSend)(
      app, sel_registerName("activateIgnoringOtherApps:"), YES);
#endif
}

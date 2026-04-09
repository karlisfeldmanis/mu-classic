#ifndef MESH_BUFFERS_HPP
#define MESH_BUFFERS_HPP

#include "TextureLoader.hpp" // TexHandle, TexValid, kInvalidTex
#include <string>

struct MeshBuffers {
  bgfx::VertexBufferHandle        vbo    = BGFX_INVALID_HANDLE; // Static meshes
  bgfx::DynamicVertexBufferHandle dynVbo = BGFX_INVALID_HANDLE; // Animated meshes
  bgfx::IndexBufferHandle         ebo    = BGFX_INVALID_HANDLE;
  int indexCount = 0;
  int vertexCount = 0;    // For dynamic VBO re-upload sizing
  bool isDynamic = false;  // True = uses dynVbo for per-frame bone animation
  TexHandle texture = kInvalidTex;

  // Per-mesh rendering flags (parsed from texture name suffixes)
  bool hasAlpha = false;  // Texture has alpha channel (32-bit TGA / RGBA)
  bool noneBlend = false; // _N suffix: disable blending, render opaque
  bool hidden = false;      // _H suffix: skip rendering entirely
  bool bright = false;      // _R suffix: additive blending
  bool streamMesh = false;  // _S suffix: UV scroll target (Main 5.2 StreamMesh)

  // BlendMesh system: window light / glow mesh identification
  int bmdTextureId = -1;      // Mesh_t::Texture value from BMD
  bool isWindowLight = false;  // True if this mesh is a BlendMesh target

  std::string textureName; // Original BMD texture name (for debugging)
};

#endif // MESH_BUFFERS_HPP

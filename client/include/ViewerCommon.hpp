#ifndef VIEWER_COMMON_HPP
#define VIEWER_COMMON_HPP

#include "BMDStructs.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "TextureLoader.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

// Interleaved vertex for mesh upload (pos + normal + texcoord = 32 bytes)
struct ViewerVertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 tex;
};

#include <bgfx/bgfx.h>
// Shared vertex layout for model meshes (initialized once)
const bgfx::VertexLayout &GetModelVertexLayout();

// Orbit camera for 3D model viewing (LMB drag rotate, scroll zoom)
struct OrbitCamera {
  float yaw = 45.0f;
  float pitch = -25.0f;
  float distance = 500.0f;
  glm::vec3 center{0.0f};

  glm::vec3 GetEyePosition() const;
  glm::mat4 GetViewMatrix() const;
};

// Upload a BMD mesh to GPU with bone transforms applied to vertices.
// Appends one MeshBuffers to 'out'. Updates aabb with transformed positions.
void UploadMeshWithBones(const Mesh_t &mesh, const std::string &textureDir,
                         const std::vector<BoneWorldMatrix> &bones,
                         std::vector<MeshBuffers> &out, AABB &aabb,
                         bool dynamic = false);

// Re-skin an already-uploaded dynamic mesh with new bone matrices.
void RetransformMeshWithBones(const Mesh_t &mesh,
                              const std::vector<BoneWorldMatrix> &bones,
                              MeshBuffers &mb);

// Delete GPU resources for a list of MeshBuffers
void CleanupMeshBuffers(std::vector<MeshBuffers> &buffers);

// macOS: bring GLFW window to foreground (no-op on other platforms)
void ActivateMacOSApp();

#endif // VIEWER_COMMON_HPP

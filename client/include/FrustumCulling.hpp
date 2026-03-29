#pragma once
#include <glm/glm.hpp>
#include <array>

// Frustum culling utilities for view frustum tests
// Used by MonsterManager, NpcManager, ObjectRenderer
namespace FrustumCulling {

// Extract 6 frustum planes from view-projection matrix
// Returns planes in order: left, right, bottom, top, near, far
inline std::array<glm::vec4, 6> ExtractFrustumPlanes(const glm::mat4 &proj,
                                                       const glm::mat4 &view) {
  glm::mat4 vp = proj * view;
  std::array<glm::vec4, 6> frustum;

  // Left plane
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                         vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
  // Right plane
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                         vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
  // Bottom plane
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                         vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
  // Top plane
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                         vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
  // Near plane
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                         vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
  // Far plane
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                         vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

  // Normalize planes for correct distance calculations
  for (int i = 0; i < 6; ++i) {
    frustum[i] /= glm::length(glm::vec3(frustum[i]));
  }

  return frustum;
}

// Test if a sphere is inside the view frustum
// Returns true if sphere is at least partially visible
inline bool IsSphereInFrustum(const std::array<glm::vec4, 6> &frustum,
                               const glm::vec3 &center,
                               float radius) {
  for (int p = 0; p < 6; ++p) {
    float dist = frustum[p].x * center.x + frustum[p].y * center.y +
                 frustum[p].z * center.z + frustum[p].w;
    if (dist < -radius) {
      return false; // Sphere is completely outside this plane
    }
  }
  return true; // Sphere is inside or intersecting frustum
}

} // namespace FrustumCulling

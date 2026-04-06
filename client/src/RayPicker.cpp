#include "RayPicker.hpp"
#include "Camera.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "ObjectRenderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace {
const TerrainData *s_td = nullptr;
Camera *s_cam = nullptr;
NpcManager *s_npcs = nullptr;
MonsterManager *s_monsters = nullptr;
GroundItem *s_groundItems = nullptr;
int s_maxGroundItems = 0;
ObjectRenderer *s_objRenderer = nullptr;
} // namespace

namespace RayPicker {

void Init(const TerrainData *td, Camera *cam, NpcManager *npcs,
          MonsterManager *monsters, GroundItem *groundItems,
          int maxGroundItems, ObjectRenderer *objRenderer) {
  s_td = td;
  s_cam = cam;
  s_npcs = npcs;
  s_monsters = monsters;
  s_groundItems = groundItems;
  s_maxGroundItems = maxGroundItems;
  s_objRenderer = objRenderer;
}

float GetTerrainHeight(float worldX, float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  // World -> grid: WorldX maps to gridZ, WorldZ maps to gridX
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = s_td->heightmap[zi * S + xi];
  float h10 = s_td->heightmap[zi * S + (xi + 1)];
  float h01 = s_td->heightmap[(zi + 1) * S + xi];
  float h11 = s_td->heightmap[(zi + 1) * S + (xi + 1)];
  return (h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
          h01 * (1 - xd) * zd + h11 * xd * zd);
}

bool IsWalkable(float worldX, float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= S || gz >= S)
    return false;
  uint8_t attr = s_td->mapping.attributes[gz * S + gx];
  // TW_NOMOVE (0x04) and TW_NOGROUND (0x08) block character movement.
  return (attr & (0x04 | 0x08)) == 0;
}

bool ScreenToTerrain(GLFWwindow *window, double mouseX, double mouseY,
                     glm::vec3 &outWorld) {
  if (!s_td)
    return false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  // NDC coordinates
  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = s_cam->GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = s_cam->GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  // Unproject near and far points
  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayOrigin = glm::vec3(nearPt);
  glm::vec3 rayDir = glm::normalize(glm::vec3(farPt) - rayOrigin);

  // March along ray, find where it crosses the terrain
  float stepSize = 50.0f;
  float maxDist = 10000.0f;
  float prevT = 0.0f;
  float prevAbove =
      rayOrigin.y - GetTerrainHeight(rayOrigin.x, rayOrigin.z);

  for (float t = stepSize; t < maxDist; t += stepSize) {
    glm::vec3 p = rayOrigin + rayDir * t;
    // Bounds check
    if (p.x < 0 || p.z < 0 || p.x > 25500.0f || p.z > 25500.0f)
      continue;
    float terrH = GetTerrainHeight(p.x, p.z);
    float above = p.y - terrH;

    if (above < 0.0f) {
      // Crossed below terrain -- binary search for precise intersection
      float lo = prevT, hi = t;
      for (int i = 0; i < 8; ++i) {
        float mid = (lo + hi) * 0.5f;
        glm::vec3 mp = rayOrigin + rayDir * mid;
        float mh = GetTerrainHeight(mp.x, mp.z);
        if (mp.y > mh)
          lo = mid;
        else
          hi = mid;
      }
      glm::vec3 hit = rayOrigin + rayDir * ((lo + hi) * 0.5f);
      outWorld = glm::vec3(hit.x, GetTerrainHeight(hit.x, hit.z), hit.z);
      return true;
    }
    prevT = t;
    prevAbove = above;
  }
  return false;
}

int PickNpc(GLFWwindow *window, double mouseX, double mouseY) {
  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = s_cam->GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = s_cam->GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < s_npcs->GetNpcCount(); ++i) {
    NpcInfo info = s_npcs->GetNpcInfo(i);
    float r = info.radius * 0.8f; // Slightly tighter for NPCs
    float yMin = info.position.y;
    float yMax = info.position.y + info.height;

    // Ray-cylinder intersection in XZ plane
    float dx = rayO.x - info.position.x;
    float dz = rayO.z - info.position.z;
    float a = rayD.x * rayD.x + rayD.z * rayD.z;
    float b = 2.0f * (dx * rayD.x + dz * rayD.z);
    float c = dx * dx + dz * dz - r * r;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
      continue;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);

    // Check both intersection points
    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

int PickMonster(GLFWwindow *window, double mouseX, double mouseY) {
  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = s_cam->GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = s_cam->GetViewMatrix();
  glm::mat4 vp = proj * view;
  glm::mat4 invVP = glm::inverse(vp);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  // Two-pass picker: collect all ray hits, then pick by screen-space
  // distance to cursor. This makes it much easier to select specific
  // monsters in dense packs — the one visually closest to cursor wins.
  struct Hit { int idx; float t; };
  Hit hits[64];
  int hitCount = 0;

  for (int i = 0; i < s_monsters->GetMonsterCount(); ++i) {
    MonsterInfo info = s_monsters->GetMonsterInfo(i);
    if (info.state == MonsterState::DEAD || info.state == MonsterState::DYING)
      continue;
    // Skip own summon — allow clicking through to monsters behind it
    if (s_monsters->IsOwnSummon(i))
      continue;
    float r = info.radius * 1.2f;
    float yMin = info.position.y;
    float yMax = info.position.y + info.height;

    float dx = rayO.x - info.position.x;
    float dz = rayO.z - info.position.z;
    float a = rayD.x * rayD.x + rayD.z * rayD.z;
    float b = 2.0f * (dx * rayD.x + dz * rayD.z);
    float c = dx * dx + dz * dz - r * r;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
      continue;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);

    float bestHitT = 1e9f;
    // Check cylinder walls
    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestHitT)
        bestHitT = t;
    }

    // Check Top Cap (Disk at yMax)
    if (rayD.y != 0.0f) {
      float tCap = (yMax - rayO.y) / rayD.y;
      if (tCap > 0 && tCap < bestHitT) {
        glm::vec3 pCap = rayO + rayD * tCap;
        float distSq = (pCap.x - info.position.x) *
                            (pCap.x - info.position.x) +
                        (pCap.z - info.position.z) *
                            (pCap.z - info.position.z);
        if (distSq <= r * r)
          bestHitT = tCap;
      }
    }

    if (bestHitT < 1e8f && hitCount < 64)
      hits[hitCount++] = {i, bestHitT};
  }

  if (hitCount == 0)
    return -1;
  if (hitCount == 1)
    return hits[0].idx;

  // Multiple hits: pick the monster whose screen-space center is closest
  // to the cursor. This gives intuitive "click what you see" behavior.
  int bestIdx = -1;
  float bestScreenDist = 1e9f;

  for (int h = 0; h < hitCount; ++h) {
    MonsterInfo info = s_monsters->GetMonsterInfo(hits[h].idx);
    // Project monster center (at mid-height) to screen
    glm::vec3 center = info.position + glm::vec3(0, info.height * 0.5f, 0);
    glm::vec4 clip = vp * glm::vec4(center, 1.0f);
    if (clip.w <= 0.001f)
      continue;
    float sx = (clip.x / clip.w * 0.5f + 0.5f) * winW;
    float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * winH;
    float dx = sx - (float)mouseX;
    float dy = sy - (float)mouseY;
    float screenDist = dx * dx + dy * dy;

    if (screenDist < bestScreenDist) {
      bestScreenDist = screenDist;
      bestIdx = hits[h].idx;
    }
  }

  return bestIdx;
}

// Ray-OBB intersection: test ray against an oriented bounding box defined by
// a world-space transform matrix applied to a local AABB [bMin, bMax].
// Returns hit distance t, or -1.0 on miss.
static float RayOBBIntersect(const glm::vec3 &rayO, const glm::vec3 &rayD,
                             const glm::mat4 &worldMat,
                             const glm::vec3 &bMin, const glm::vec3 &bMax) {
  // Transform ray into OBB local space
  glm::mat4 inv = glm::inverse(worldMat);
  glm::vec3 localO = glm::vec3(inv * glm::vec4(rayO, 1.0f));
  glm::vec3 localD = glm::vec3(inv * glm::vec4(rayD, 0.0f));

  // Standard ray-AABB slab test in local space
  float tmin = -1e9f, tmax = 1e9f;
  for (int axis = 0; axis < 3; ++axis) {
    if (fabsf(localD[axis]) < 1e-8f) {
      // Ray parallel to slab — miss if origin outside
      if (localO[axis] < bMin[axis] || localO[axis] > bMax[axis])
        return -1.0f;
    } else {
      float invD = 1.0f / localD[axis];
      float t0 = (bMin[axis] - localO[axis]) * invD;
      float t1 = (bMax[axis] - localO[axis]) * invD;
      if (t0 > t1) std::swap(t0, t1);
      tmin = std::max(tmin, t0);
      tmax = std::min(tmax, t1);
      if (tmin > tmax)
        return -1.0f;
    }
  }
  return tmin > 0 ? tmin : (tmax > 0 ? tmax : -1.0f);
}

int PickGroundItem(GLFWwindow *window, double mouseX, double mouseY) {
  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = s_cam->GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = s_cam->GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < s_maxGroundItems; ++i) {
    if (!s_groundItems[i].active)
      continue;

    const auto &gi = s_groundItems[i];

    // Try AABB-based picking using the item's actual model bounds
    const char *modelName = ItemDatabase::GetDropModelName(gi.defIndex);
    LoadedItemModel *model = modelName ? ItemModelManager::Get(modelName) : nullptr;

    if (model && model->bmd) {
      // Build world transform matching RenderItemWorld (ItemModelManager.cpp)
      glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
      glm::mat4 mod = glm::translate(glm::mat4(1.0f), gi.position);
      // BMD-to-world coordinate basis
      mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      // Item rotation (ZYX order matching AngleMatrix)
      if (gi.angle.z != 0)
        mod = glm::rotate(mod, glm::radians(gi.angle.z), glm::vec3(0, 0, 1));
      if (gi.angle.y != 0)
        mod = glm::rotate(mod, glm::radians(gi.angle.y), glm::vec3(0, 1, 0));
      if (gi.angle.x != 0)
        mod = glm::rotate(mod, glm::radians(gi.angle.x), glm::vec3(1, 0, 0));
      mod = glm::scale(mod, glm::vec3(gi.scale));
      mod = glm::translate(mod, -tCenter);

      // Expand AABB slightly for easier clicking (20% padding)
      glm::vec3 pad = (model->transformedMax - model->transformedMin) * 0.1f;
      glm::vec3 bMin = model->transformedMin - pad;
      glm::vec3 bMax = model->transformedMax + pad;

      float t = RayOBBIntersect(rayO, rayD, mod, bMin, bMax);
      if (t > 0 && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    } else {
      // Fallback: sphere test for items without loaded model (zen piles etc.)
      float r = 50.0f;
      glm::vec3 oc = rayO - gi.position;
      float b = glm::dot(oc, rayD);
      float c = glm::dot(oc, oc) - r * r;
      float h = b * b - c;
      if (h < 0.0f)
        continue;
      float t = -b - sqrtf(h);
      if (t > 0 && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

int PickInteractiveObject(GLFWwindow *window, double mouseX, double mouseY) {
  if (!s_objRenderer)
    return -1;
  const auto &objects = s_objRenderer->GetInteractiveObjects();
  if (objects.empty())
    return -1;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = s_cam->GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = s_cam->GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < (int)objects.size(); ++i) {
    const auto &obj = objects[i];
    float r = obj.radius;
    float yMin = obj.worldPos.y;
    float yMax = obj.worldPos.y + obj.height;

    // Ray-cylinder intersection in XZ plane
    float dx = rayO.x - obj.worldPos.x;
    float dz = rayO.z - obj.worldPos.z;
    float a = rayD.x * rayD.x + rayD.z * rayD.z;
    float b = 2.0f * (dx * rayD.x + dz * rayD.z);
    float c = dx * dx + dz * dz - r * r;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
      continue;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);

    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

} // namespace RayPicker

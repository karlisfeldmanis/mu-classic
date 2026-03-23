#include "RayPicker.hpp"
#include "Camera.hpp"
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
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < s_monsters->GetMonsterCount(); ++i) {
    MonsterInfo info = s_monsters->GetMonsterInfo(i);
    if (info.state == MonsterState::DEAD || info.state == MonsterState::DYING)
      continue;
    // Skip own summon — allow clicking through to monsters behind it
    if (s_monsters->IsOwnSummon(i))
      continue;
    // Traps (KIND_TRAP) cannot be targeted by clicking
    if (info.type >= 100 && info.type <= 102)
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

    // Check cylinder walls
    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }

    // Check Top Cap (Disk at yMax)
    if (rayD.y != 0.0f) {
      float tCap = (yMax - rayO.y) / rayD.y;
      if (tCap > 0 && tCap < bestT) {
        glm::vec3 pCap = rayO + rayD * tCap;
        float distSq = (pCap.x - info.position.x) *
                            (pCap.x - info.position.x) +
                        (pCap.z - info.position.z) *
                            (pCap.z - info.position.z);
        if (distSq <= r * r) {
          bestT = tCap;
          bestIdx = i;
        }
      }
    }
  }
  return bestIdx;
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

    float r = 50.0f; // Click radius around item
    glm::vec3 pos = s_groundItems[i].position;

    // Ray-sphere intersection (approximate for item)
    glm::vec3 oc = rayO - pos;
    float b = glm::dot(oc, rayD);
    float c = glm::dot(oc, oc) - r * r;
    float h = b * b - c;
    if (h < 0.0f)
      continue; // No hit

    float t = -b - sqrtf(h);
    if (t > 0 && t < bestT) {
      bestT = t;
      bestIdx = i;
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

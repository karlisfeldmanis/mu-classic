#include "BMDUtils.hpp"
#include "BMDParser.hpp"
#include <cmath>
#include <cstring>

namespace MuMath {

void AngleQuaternion(const glm::vec3 &angles, float q[4]) {
  float sr = sinf(angles.x * 0.5f), cr = cosf(angles.x * 0.5f);
  float sp = sinf(angles.y * 0.5f), cp = cosf(angles.y * 0.5f);
  float sy = sinf(angles.z * 0.5f), cy = cosf(angles.z * 0.5f);
  q[0] = sr * cp * cy - cr * sp * sy;
  q[1] = cr * sp * cy + sr * cp * sy;
  q[2] = cr * cp * sy - sr * sp * cy;
  q[3] = cr * cp * cy + sr * sp * sy;
}

void QuaternionMatrix(const float q[4], float m[3][4]) {
  m[0][0] = 1.0f - 2.0f * q[1] * q[1] - 2.0f * q[2] * q[2];
  m[1][0] = 2.0f * q[0] * q[1] + 2.0f * q[3] * q[2];
  m[2][0] = 2.0f * q[0] * q[2] - 2.0f * q[3] * q[1];
  m[0][1] = 2.0f * q[0] * q[1] - 2.0f * q[3] * q[2];
  m[1][1] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[2] * q[2];
  m[2][1] = 2.0f * q[1] * q[2] + 2.0f * q[3] * q[0];
  m[0][2] = 2.0f * q[0] * q[2] + 2.0f * q[3] * q[1];
  m[1][2] = 2.0f * q[1] * q[2] - 2.0f * q[3] * q[0];
  m[2][2] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[1] * q[1];
  m[0][3] = m[1][3] = m[2][3] = 0.0f;
}

void ConcatTransforms(const float in1[3][4], const float in2[3][4],
                      float out[3][4]) {
  out[0][0] =
      in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
  out[0][1] =
      in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
  out[0][2] =
      in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
  out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
              in1[0][2] * in2[2][3] + in1[0][3];
  out[1][0] =
      in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
  out[1][1] =
      in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
  out[1][2] =
      in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
  out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
              in1[1][2] * in2[2][3] + in1[1][3];
  out[2][0] =
      in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
  out[2][1] =
      in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
  out[2][2] =
      in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
  out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
              in1[2][2] * in2[2][3] + in1[2][3];
}

glm::vec3 TransformPoint(const float m[3][4], const glm::vec3 &v) {
  return glm::vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
                   m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                   m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]);
}

glm::vec3 RotateVector(const float m[3][4], const glm::vec3 &v) {
  return glm::vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                   m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                   m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}

void AngleMatrix(const glm::vec3 &anglesDeg, float matrix[3][4]) {
  // Exact copy of ZzzMathLib.cpp AngleMatrix — using original variable names
  // angles[0]=X → sr/cr, angles[1]=Y → sp/cp, angles[2]=Z → sy/cy
  float angle;
  float sr, sp, sy, cr, cp, cy;

  angle = anglesDeg.z * ((float)M_PI / 180.0f);
  sy = sinf(angle);
  cy = cosf(angle);
  angle = anglesDeg.y * ((float)M_PI / 180.0f);
  sp = sinf(angle);
  cp = cosf(angle);
  angle = anglesDeg.x * ((float)M_PI / 180.0f);
  sr = sinf(angle);
  cr = cosf(angle);

  matrix[0][0] = cp * cy;
  matrix[1][0] = cp * sy;
  matrix[2][0] = -sp;
  matrix[0][1] = sr * sp * cy + cr * (-sy);
  matrix[1][1] = sr * sp * sy + cr * cy;
  matrix[2][1] = sr * cp;
  matrix[0][2] = cr * sp * cy + (-sr) * (-sy);
  matrix[1][2] = cr * sp * sy + (-sr) * cy;
  matrix[2][2] = cr * cp;
  matrix[0][3] = 0.0f;
  matrix[1][3] = 0.0f;
  matrix[2][3] = 0.0f;
}

BoneWorldMatrix BuildWeaponOffsetMatrix(const glm::vec3 &rotDeg,
                                        const glm::vec3 &offset) {
  float m[3][4];
  AngleMatrix(rotDeg, m);
  m[0][3] = offset.x;
  m[1][3] = offset.y;
  m[2][3] = offset.z;

  BoneWorldMatrix result;
  memcpy(result.data(), m, sizeof(float) * 12);
  return result;
}

} // namespace MuMath

std::vector<BoneWorldMatrix> ComputeBoneMatrices(const BMDData *bmd, int action,
                                                 int frame,
                                                 const glm::vec3 &bodyAngle) {
  int numBones = (int)bmd->Bones.size();
  std::vector<BoneWorldMatrix> world(numBones);

  // Identity init
  for (int i = 0; i < numBones; ++i) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        world[i][r][c] = (r == c) ? 1.0f : 0.0f;
  }

  if (bmd->Actions.empty() || action >= (int)bmd->Actions.size())
    return world;

  // Main 5.2: AngleMatrix(BodyAngle, ParentMatrix) for root bones
  bool hasBodyAngle =
      (bodyAngle.x != 0.0f || bodyAngle.y != 0.0f || bodyAngle.z != 0.0f);
  float parentMatrix[3][4];
  if (hasBodyAngle) {
    MuMath::AngleMatrix(bodyAngle, parentMatrix);
  }

  for (int i = 0; i < numBones; ++i) {
    auto &bone = bmd->Bones[i];
    if (bone.Dummy)
      continue;
    if (action >= (int)bone.BoneMatrixes.size())
      continue;
    auto &bm = bone.BoneMatrixes[action];
    if (bm.Position.empty() || bm.Rotation.empty())
      continue;

    int f = (frame < (int)bm.Position.size()) ? frame : 0;

    float quat[4];
    MuMath::AngleQuaternion(bm.Rotation[f], quat);
    float local[3][4];
    MuMath::QuaternionMatrix(quat, local);
    local[0][3] = bm.Position[f].x;
    local[1][3] = bm.Position[f].y;
    local[2][3] = bm.Position[f].z;

    if (bone.Parent == -1) {
      if (hasBodyAngle) {
        // Main 5.2: R_ConcatTransforms(ParentMatrix, Matrix, BoneMatrix[i])
        float result[3][4];
        MuMath::ConcatTransforms(parentMatrix, local, result);
        memcpy(world[i].data(), result, sizeof(float) * 12);
      } else {
        memcpy(world[i].data(), local, sizeof(float) * 12);
      }
    } else if (bone.Parent >= 0 && bone.Parent < numBones) {
      float result[3][4];
      MuMath::ConcatTransforms((const float(*)[4])world[bone.Parent].data(),
                               local, result);
      memcpy(world[i].data(), result, sizeof(float) * 12);
    }
  }

  return world;
}

// Quaternion spherical linear interpolation (shortest path)
static glm::vec4 QuaternionSlerp(const glm::vec4 &q1, const glm::vec4 &q2,
                                 float t) {
  float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
  glm::vec4 q2adj = q2;
  if (dot < 0.0f) {
    dot = -dot;
    q2adj = -q2adj;
  }
  if (dot > 0.9999f) {
    // Nearly identical — lerp and normalize
    return glm::normalize(q1 * (1.0f - t) + q2adj * t);
  }
  float theta = acosf(dot);
  float sinTheta = sinf(theta);
  float w1 = sinf((1.0f - t) * theta) / sinTheta;
  float w2 = sinf(t * theta) / sinTheta;
  return q1 * w1 + q2adj * w2;
}

bool GetInterpolatedBoneData(const BMDData *bmd, int action, float frame,
                             int boneIdx, glm::vec3 &outPos,
                             glm::vec4 &outQuat) {
  if (action < 0 || action >= (int)bmd->Actions.size())
    return false;
  if (boneIdx < 0 || boneIdx >= (int)bmd->Bones.size())
    return false;

  auto &bone = bmd->Bones[boneIdx];
  if (action >= (int)bone.BoneMatrixes.size())
    return false;

  auto &bm = bone.BoneMatrixes[action];
  if (bm.Position.empty() || bm.Quaternion.empty())
    return false;

  int numKeys = bmd->Actions[action].NumAnimationKeys;
  if (numKeys <= 0)
    return false;

  int frame0 = (int)frame;
  int frame1 = frame0 + 1;
  float t = frame - (float)frame0;

  frame0 = frame0 % numKeys;
  frame1 = frame1 % numKeys;

  int f0 = (frame0 < (int)bm.Position.size()) ? frame0 : 0;
  int f1 = (frame1 < (int)bm.Position.size()) ? frame1 : 0;

  outQuat = QuaternionSlerp(bm.Quaternion[f0], bm.Quaternion[f1], t);
  outPos = bm.Position[f0] * (1.0f - t) + bm.Position[f1] * t;
  return true;
}

// ── Output-parameter versions (reuse pre-allocated vector, zero allocs) ──

void ComputeBoneMatricesInterpolated(const BMDData *bmd, int action, float frame,
                                     std::vector<BoneWorldMatrix> &world,
                                     const glm::vec3 &bodyAngle) {
  int numBones = (int)bmd->Bones.size();
  world.resize(numBones); // No-op if already correct size (capacity preserved)

  // Identity init
  for (int i = 0; i < numBones; ++i)
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        world[i][r][c] = (r == c) ? 1.0f : 0.0f;

  bool hasBodyAngle =
      (bodyAngle.x != 0.0f || bodyAngle.y != 0.0f || bodyAngle.z != 0.0f);
  float parentMatrix[3][4];
  if (hasBodyAngle) {
    MuMath::AngleMatrix(bodyAngle, parentMatrix);
  }

  for (int i = 0; i < numBones; ++i) {
    glm::vec3 pos;
    glm::vec4 q;
    if (!GetInterpolatedBoneData(bmd, action, frame, i, pos, q))
      continue;

    float quat[4] = {q.x, q.y, q.z, q.w};
    float local[3][4];
    MuMath::QuaternionMatrix(quat, local);
    local[0][3] = pos.x;
    local[1][3] = pos.y;
    local[2][3] = pos.z;

    auto &bone = bmd->Bones[i];
    if (bone.Parent == -1) {
      if (hasBodyAngle) {
        float result[3][4];
        MuMath::ConcatTransforms(parentMatrix, local, result);
        memcpy(world[i].data(), result, sizeof(float) * 12);
      } else {
        memcpy(world[i].data(), local, sizeof(float) * 12);
      }
    } else if (bone.Parent >= 0 && bone.Parent < numBones) {
      float result[3][4];
      MuMath::ConcatTransforms((const float(*)[4])world[bone.Parent].data(),
                               local, result);
      memcpy(world[i].data(), result, sizeof(float) * 12);
    }
  }
}

void ComputeBoneMatricesBlended(const BMDData *bmd, int action1, float frame1,
                                int action2, float frame2, float blendAlpha,
                                std::vector<BoneWorldMatrix> &world,
                                const glm::vec3 &bodyAngle) {
  int numBones = (int)bmd->Bones.size();
  world.resize(numBones);

  // Identity init
  for (int i = 0; i < numBones; ++i)
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        world[i][r][c] = (r == c) ? 1.0f : 0.0f;

  bool hasBodyAngle =
      (bodyAngle.x != 0.0f || bodyAngle.y != 0.0f || bodyAngle.z != 0.0f);
  float parentMatrix[3][4];
  if (hasBodyAngle) {
    MuMath::AngleMatrix(bodyAngle, parentMatrix);
  }

  for (int i = 0; i < numBones; ++i) {
    glm::vec3 pos1, pos2;
    glm::vec4 q1, q2;
    bool v1 = GetInterpolatedBoneData(bmd, action1, frame1, i, pos1, q1);
    bool v2 = GetInterpolatedBoneData(bmd, action2, frame2, i, pos2, q2);

    if (!v1 && !v2)
      continue;

    glm::vec3 posInterp;
    glm::vec4 qInterp;

    if (v1 && v2) {
      posInterp = pos1 * (1.0f - blendAlpha) + pos2 * blendAlpha;
      qInterp = QuaternionSlerp(q1, q2, blendAlpha);
    } else if (v1) {
      posInterp = pos1;
      qInterp = q1;
    } else {
      posInterp = pos2;
      qInterp = q2;
    }

    float quat[4] = {qInterp.x, qInterp.y, qInterp.z, qInterp.w};
    float local[3][4];
    MuMath::QuaternionMatrix(quat, local);
    local[0][3] = posInterp.x;
    local[1][3] = posInterp.y;
    local[2][3] = posInterp.z;

    auto &bone = bmd->Bones[i];
    if (bone.Parent == -1) {
      if (hasBodyAngle) {
        float result[3][4];
        MuMath::ConcatTransforms(parentMatrix, local, result);
        memcpy(world[i].data(), result, sizeof(float) * 12);
      } else {
        memcpy(world[i].data(), local, sizeof(float) * 12);
      }
    } else if (bone.Parent >= 0 && bone.Parent < numBones) {
      float result[3][4];
      MuMath::ConcatTransforms((const float(*)[4])world[bone.Parent].data(),
                               local, result);
      memcpy(world[i].data(), result, sizeof(float) * 12);
    }
  }
}

// ── Legacy return-by-value wrappers (for call sites that don't need perf) ──

std::vector<BoneWorldMatrix>
ComputeBoneMatricesInterpolated(const BMDData *bmd, int action, float frame,
                                const glm::vec3 &bodyAngle) {
  std::vector<BoneWorldMatrix> world;
  ComputeBoneMatricesInterpolated(bmd, action, frame, world, bodyAngle);
  return world;
}

std::vector<BoneWorldMatrix>
ComputeBoneMatricesBlended(const BMDData *bmd, int action1, float frame1,
                           int action2, float frame2, float blendAlpha,
                           const glm::vec3 &bodyAngle) {
  std::vector<BoneWorldMatrix> world;
  ComputeBoneMatricesBlended(bmd, action1, frame1, action2, frame2, blendAlpha,
                             world, bodyAngle);
  return world;
}

AABB ComputeTransformedAABB(const BMDData *bmd,
                            const std::vector<BoneWorldMatrix> &bones) {
  AABB box;
  for (auto &mesh : bmd->Meshes) {
    for (auto &vert : mesh.Vertices) {
      glm::vec3 pos = vert.Position;
      int boneIdx = vert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        pos = MuMath::TransformPoint((const float(*)[4])bones[boneIdx].data(),
                                     vert.Position);
      }
      box.min = glm::min(box.min, pos);
      box.max = glm::max(box.max, pos);
    }
  }
  return box;
}

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

void BMDParser::Decrypt(uint8_t *dst, const uint8_t *src, size_t size) {
  static const uint8_t xorKey[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                     0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                     0x37, 0xB3, 0xE7, 0xA2};

  uint8_t wKey = 0x5E;
  for (size_t i = 0; i < size; ++i) {
    dst[i] = (src[i] ^ xorKey[i % 16]) - wKey;
    wKey = (uint8_t)(src[i] + 0x3D);
  }
}

template <typename T> T ReadRaw(const uint8_t *data, size_t &ptr) {
  T val;
  memcpy(&val, data + ptr, sizeof(T));
  ptr += sizeof(T);
  return val;
}

std::unique_ptr<BMDData> BMDParser::Parse(const std::string &path, bool quiet) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[BMDParser] Cannot open file: " << path << std::endl;
    return nullptr;
  }

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer(size);
  file.read((char *)buffer.data(), size);

  if (buffer.size() < 4 || memcmp(buffer.data(), "BMD", 3) != 0) {
    std::cerr << "[BMDParser] Invalid BMD header: " << path << std::endl;
    return nullptr;
  }

  uint8_t version = buffer[3];
  size_t ptr = 4;

  if (!quiet)
    std::cout << "[BMDParser] " << path << " Version: 0x" << std::hex
              << (int)version << std::dec << " File size: " << size << std::endl;

  auto bmd = std::make_unique<BMDData>();
  bmd->Version = version;

  const uint8_t *data = buffer.data();
  std::vector<uint8_t> decrypted;

  if (version == 0xC) {
    uint32_t encSize = ReadRaw<uint32_t>(buffer.data(), ptr);
    if (encSize > 100 * 1024 * 1024) {
      std::cerr << "[BMDParser] Invalid encrypted size: " << encSize
                << std::endl;
      return nullptr;
    }
    decrypted.resize(encSize);
    Decrypt(decrypted.data(), buffer.data() + ptr, encSize);
    data = decrypted.data();
    ptr = 0;
  }

  // Header: Name (32 bytes)
  char name[33] = {0};
  memcpy(name, data + ptr, 32);
  bmd->Name = name;
  ptr += 32;

  short numMeshes = ReadRaw<short>(data, ptr);
  short numBones = ReadRaw<short>(data, ptr);
  short numActions = ReadRaw<short>(data, ptr);

  if (!quiet)
    std::cout << "[BMDParser] Meshes: " << numMeshes << " Bones: " << numBones
              << " Actions: " << numActions << std::endl;

  if (numMeshes < 0 || numMeshes > 1000) {
    std::cerr << "[BMDParser] Invalid mesh count: " << numMeshes << std::endl;
    return nullptr;
  }

  bmd->Meshes.resize(numMeshes);
  for (int i = 0; i < numMeshes; ++i) {
    auto &m = bmd->Meshes[i];
    m.NumVertices = ReadRaw<short>(data, ptr);
    m.NumNormals = ReadRaw<short>(data, ptr);
    m.NumTexCoords = ReadRaw<short>(data, ptr);
    m.NumTriangles = ReadRaw<short>(data, ptr);
    m.Texture = ReadRaw<short>(data, ptr);

    if (m.NumVertices < 0 || m.NumVertices > 10000) {
      std::cerr << " [BMDParser] Invalid vertex count: " << m.NumVertices
                << std::endl;
      return nullptr;
    }

    m.Vertices.resize(m.NumVertices);
    memcpy(m.Vertices.data(), data + ptr, m.NumVertices * 16);
    ptr += m.NumVertices * 16;

    m.Normals.resize(m.NumNormals);
    memcpy(m.Normals.data(), data + ptr, m.NumNormals * 20);
    ptr += m.NumNormals * 20;

    m.TexCoords.resize(m.NumTexCoords);
    memcpy(m.TexCoords.data(), data + ptr, m.NumTexCoords * 8);
    ptr += m.NumTexCoords * 8;

    m.Triangles.resize(m.NumTriangles);
    for (int j = 0; j < m.NumTriangles; ++j) {
      memcpy(&m.Triangles[j], data + ptr, 34);
      ptr += 64;
    }

    char texName[33] = {0};
    memcpy(texName, data + ptr, 32);
    m.TextureName = texName;
    ptr += 32;

    if (!quiet) {
      std::cout << " [Mesh " << i << "] Verts: " << m.NumVertices
                << " Norms: " << m.NumNormals << " Tex: " << m.NumTexCoords
                << " Tris: " << m.NumTriangles << " Texture: " << m.TextureName;
      std::cout << std::endl;
    }
  }

  bmd->Actions.resize(numActions);
  for (int i = 0; i < numActions; ++i) {
    auto &a = bmd->Actions[i];
    a.NumAnimationKeys = ReadRaw<short>(data, ptr);
    a.LockPositions = ReadRaw<bool>(data, ptr);
    if (a.LockPositions && a.NumAnimationKeys > 0) {
      a.Positions.resize(a.NumAnimationKeys);
      memcpy(a.Positions.data(), data + ptr, a.NumAnimationKeys * 12);
      ptr += a.NumAnimationKeys * 12;
    }
  }

  bmd->Bones.resize(numBones);
  for (int i = 0; i < numBones; ++i) {
    auto &b = bmd->Bones[i];
    b.Dummy = ReadRaw<char>(data, ptr);
    if (!b.Dummy) {
      memcpy(b.Name, data + ptr, 32);
      ptr += 32;
      b.Parent = ReadRaw<short>(data, ptr);
      b.BoneMatrixes.resize(numActions);
      for (int j = 0; j < numActions; ++j) {
        auto &bm = b.BoneMatrixes[j];
        int numKeys = bmd->Actions[j].NumAnimationKeys;
        if (numKeys > 0) {
          bm.Position.resize(numKeys);
          bm.Rotation.resize(numKeys);
          bm.Quaternion.resize(numKeys);
          memcpy(bm.Position.data(), data + ptr, numKeys * 12);
          ptr += numKeys * 12;
          memcpy(bm.Rotation.data(), data + ptr, numKeys * 12);
          ptr += numKeys * 12;
          for (int k = 0; k < numKeys; ++k) {
            float q[4];
            MuMath::AngleQuaternion(bm.Rotation[k], q);
            bm.Quaternion[k] = glm::vec4(q[0], q[1], q[2], q[3]);
          }
        }
      }
    }
  }

  // Calculate AABB from all meshes (bind pose)
  if (!bmd->Meshes.empty()) {
    glm::vec3 min(1e9f);
    glm::vec3 max(-1e9f);
    bool anyVert = false;
    for (const auto &mesh : bmd->Meshes) {
      for (const auto &v : mesh.Vertices) {
        min = glm::min(min, v.Position);
        max = glm::max(max, v.Position);
        anyVert = true;
      }
    }
    if (anyVert) {
      bmd->Min = min;
      bmd->Max = max;
      if (!quiet)
        std::cout << "[BMDParser] AABB: Min(" << min.x << "," << min.y << ","
                  << min.z << ") Max(" << max.x << "," << max.y << "," << max.z
                  << ")" << std::endl;
    }
  }

  return bmd;
}

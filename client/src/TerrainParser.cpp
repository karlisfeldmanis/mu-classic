#include "TerrainParser.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

const uint8_t MAP_XOR_KEY[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                 0x37, 0xB3, 0xE7, 0xA2};

TerrainData TerrainParser::LoadWorld(int world_id,
                                     const std::string &data_path) {
  TerrainData result;
  std::string world_dir = "World" + std::to_string(world_id);
  std::string base_path = data_path + "/" + world_dir + "/";

  // 1. Heightmap - Try TerrainHeight.OZB, then TerrainH.ozh, etc.
  std::string height_path = base_path + "TerrainHeight.OZB";
  {
    std::ifstream check(height_path);
    if (!check.good()) {
      height_path =
          base_path + "Terrain" + std::to_string(world_id + 1) + ".ozh";
    }
  }
  result.heightmap = ParseHeightFile(height_path);

  // 2. Mapping - Try EncTerrainX.map, then TerrainX.map, then Terrain.map
  std::string map_path =
      base_path + "EncTerrain" + std::to_string(world_id) + ".map";
  {
    std::ifstream check(map_path);
    if (!check.good()) {
      map_path = base_path + "Terrain" + std::to_string(world_id) + ".map";
      std::ifstream check2(map_path);
      if (!check2.good()) {
        map_path = base_path + "Terrain.map";
      }
    }
  }
  result.mapping = ParseMappingFile(map_path);

  // 3. Attributes - Try EncTerrainX.att, then TerrainX.att, then Terrain.att
  std::string att_path =
      base_path + "EncTerrain" + std::to_string(world_id) + ".att";
  {
    std::ifstream check(att_path);
    if (!check.good()) {
      att_path = base_path + "Terrain" + std::to_string(world_id) + ".att";
      std::ifstream check2(att_path);
      if (!check2.good()) {
        att_path = base_path + "Terrain.att";
      }
    }
  }
  auto att_res = ParseAttributesFile(att_path);
  result.mapping.attributes = att_res.first;
  result.mapping.symmetry = att_res.second;

  // 4. Objects - Try EncTerrainX.obj, then TerrainX.obj
  std::string obj_path =
      base_path + "EncTerrain" + std::to_string(world_id) + ".obj";
  {
    std::ifstream check(obj_path);
    if (!check.good()) {
      obj_path = base_path + "Terrain" + std::to_string(world_id) + ".obj";
    }
  }
  result.objects = ParseObjectsFile(obj_path);

  // 5. Lightmap - Try TerrainLight.OZJ
  std::string light_path = base_path + "TerrainLight.OZJ";
  result.lightmap = ParseLightFile(light_path);

  // 6. Apply directional sun lighting (CreateTerrainLight from reference)
  // Computes terrain normals from heightmap, modulates lightmap by
  // dot(normal, sunDir) + 0.5  —  adds relief shading to hills/slopes
  if (!result.heightmap.empty() && !result.lightmap.empty()) {
    const int S = TERRAIN_SIZE;
    // Sun direction in MU coordinates: (0.5, -0.5, 0.5) for normal worlds
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, -0.5f, 0.5f));

    for (int y = 0; y < S; ++y) {
      for (int x = 0; x < S; ++x) {
        int idx = y * S + x;

        // Compute terrain normal from heightmap finite differences
        // MU coords: X=right, Y=forward, Z=up (height)
        float h = result.heightmap[idx];
        float hx = (x < S - 1) ? result.heightmap[y * S + (x + 1)] : h;
        float hy = (y < S - 1) ? result.heightmap[(y + 1) * S + x] : h;
        float dz_dx = hx - h; // Height change per cell in MU X
        float dz_dy = hy - h; // Height change per cell in MU Y
        // Normal = normalize(-dz_dx, -dz_dy, 100) where 100 = TERRAIN_SCALE
        glm::vec3 normal = glm::normalize(glm::vec3(-dz_dx, -dz_dy, 100.0f));

        // Reference: Luminosity = dot(normal, sunDir) + 0.5, clamped [0, 1]
        float luminosity = glm::dot(normal, sunDir) + 0.5f;
        luminosity = glm::clamp(luminosity, 0.0f, 1.0f);

        result.lightmap[idx] *= luminosity;

        // Prevent pitch-black cells at steep terrain edges (water/cliff drops).
        // Original per-vertex lighting has min 0.2 (IntensityTransform floor),
        // but BodyLight can still be near-zero from dark lightmap JPEG pixels.
        // Floor ensures objects and terrain at water edges remain visible.
        // Skip for void cells (TW_NOGROUND) — they should stay dark per the
        // pre-baked lightmap JPEG (Main 5.2 void edge rendering).
        bool isVoid = !result.mapping.attributes.empty() &&
                      (result.mapping.attributes[idx] & 0x08) != 0;
        if (!isVoid)
          result.lightmap[idx] = glm::max(result.lightmap[idx], glm::vec3(0.30f));
      }
    }
    std::cout << "[TerrainParser] Applied directional sun lighting (sunDir="
              << sunDir.x << "," << sunDir.y << "," << sunDir.z << ")"
              << std::endl;
  }

  return result;
}

std::vector<uint8_t>
TerrainParser::DecryptMapFile(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> decrypted(data.size());
  uint8_t map_key = 0x5E;

  for (size_t i = 0; i < data.size(); ++i) {
    uint8_t src_byte = data[i];
    uint8_t xor_byte = MAP_XOR_KEY[i % 16];
    uint8_t val = (src_byte ^ xor_byte) - map_key;
    decrypted[i] = val;
    map_key = src_byte + 0x3D;
  }
  return decrypted;
}

std::vector<float> TerrainParser::ParseHeightFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Cannot open height file: " << path << std::endl;
    return std::vector<float>(TERRAIN_SIZE * TERRAIN_SIZE, 0.0f);
  }

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  size_t expected_size = TERRAIN_SIZE * TERRAIN_SIZE;
  file.seekg(size - expected_size, std::ios::beg);

  std::vector<uint8_t> raw_heights(expected_size);
  file.read((char *)raw_heights.data(), expected_size);

  std::vector<float> heights(expected_size);
  for (size_t i = 0; i < expected_size; ++i) {
    heights[i] = static_cast<float>(raw_heights[i]) * 1.5f;
  }
  return heights;
}

MapData TerrainParser::ParseMappingFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  MapData res;
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;
  if (!file) {
    std::cerr << "[TerrainParser] Cannot open mapping file: " << path
              << std::endl;
    res.layer1.resize(cells, 0);
    res.layer2.resize(cells, 0);
    res.alpha.resize(cells, 0.0f);
    return res;
  }

  std::vector<uint8_t> raw_data((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = DecryptMapFile(raw_data);

  // Sven format (OpenTerrainMapping in ZzzLodTerrain.cpp):
  //   Byte 0: Version
  //   Byte 1: Map Number
  //   Bytes 2..65537: Layer1 (256x256)
  //   Bytes 65538..131073: Layer2 (256x256)
  //   Bytes 131074..196609: Alpha (256x256 bytes, each /255 -> float)
  const size_t expected_size = 2 + cells * 3;

  if (data.size() < expected_size) {
    std::cerr << "[TerrainParser] Mapping file too small: " << data.size()
              << " bytes (expected " << expected_size << ")" << std::endl;
    res.layer1.resize(cells, 0);
    res.layer2.resize(cells, 0);
    res.alpha.resize(cells, 0.0f);
    return res;
  }

  res.map_number = static_cast<int>(data[1]);
  size_t ptr = 2; // Skip version + map number

  res.layer1.assign(data.begin() + ptr, data.begin() + ptr + cells);
  ptr += cells;

  res.layer2.assign(data.begin() + ptr, data.begin() + ptr + cells);
  ptr += cells;

  res.alpha.resize(cells);
  for (size_t i = 0; i < cells; ++i) {
    res.alpha[i] = static_cast<float>(data[ptr + i]) / 255.0f;
  }

  std::cout << "[TerrainParser] Loaded mapping: map_number=" << res.map_number
            << ", " << data.size() << " bytes" << std::endl;

  return res;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
TerrainParser::ParseAttributesFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;
  if (!file) {
    std::cerr << "[TerrainParser] Cannot open attribute file: " << path
              << std::endl;
    return {std::vector<uint8_t>(cells, 0), std::vector<uint8_t>(cells, 0)};
  }

  std::vector<uint8_t> raw_data((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

  // Sven's OpenTerrainAttribute: decrypt then BuxConvert
  std::vector<uint8_t> data = DecryptMapFile(raw_data);

  uint8_t bux_code[3] = {0xFC, 0xCF, 0xAB};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] ^= bux_code[i % 3];
  }

  std::vector<uint8_t> attributes(cells, 0);
  std::vector<uint8_t> symmetry(cells, 0);

  // Sven format: 4-byte header (version, map, width, height)
  // Then either BYTE[cells] or WORD[cells] attribute data
  const size_t byte_format_size = 4 + cells;
  const size_t word_format_size = 4 + cells * sizeof(uint16_t);

  if (data.size() >= word_format_size) {
    // WORD format: 2 bytes per cell (low byte = attributes, high byte = extra)
    for (size_t i = 0; i < cells; ++i) {
      attributes[i] = data[4 + i * 2];
      symmetry[i] = data[5 + i * 2];
    }
    std::cout << "[TerrainParser] Loaded attributes (WORD format): "
              << data.size() << " bytes" << std::endl;
  } else if (data.size() >= byte_format_size) {
    // BYTE format: 1 byte per cell
    for (size_t i = 0; i < cells; ++i) {
      attributes[i] = data[4 + i];
    }
    std::cout << "[TerrainParser] Loaded attributes (BYTE format): "
              << data.size() << " bytes" << std::endl;
  } else {
    std::cerr << "[TerrainParser] Attribute file too small: " << data.size()
              << " bytes" << std::endl;
  }

  return {attributes, symmetry};
}

std::vector<ObjectData>
TerrainParser::ParseObjectsFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[TerrainParser] Cannot open object file: " << path
              << std::endl;
    return {};
  }

  std::vector<uint8_t> raw_data((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = DecryptMapFile(raw_data);

  if (data.size() < 4) {
    std::cerr << "[TerrainParser] Object file too small: " << data.size()
              << std::endl;
    return {};
  }

  // Header: Version(1) + MapNumber(1) + Count(2)
  uint8_t version = data[0];
  uint8_t mapNumber = data[1];
  short count;
  memcpy(&count, data.data() + 2, sizeof(short));

  std::cout << "[TerrainParser] Object file: version=" << (int)version
            << " map=" << (int)mapNumber << " count=" << count << std::endl;

  if (count < 0 || count > 10000) {
    std::cerr << "[TerrainParser] Invalid object count: " << count << std::endl;
    return {};
  }

  // Each object: Type(2) + Position(12) + Angle(12) + Scale(4) = 30 bytes
  const size_t recordSize = 30;
  const size_t expectedSize = 4 + (size_t)count * recordSize;
  if (data.size() < expectedSize) {
    std::cerr << "[TerrainParser] Object file too small for " << count
              << " objects: " << data.size() << " < " << expectedSize
              << std::endl;
    return {};
  }

  std::vector<ObjectData> objects;
  objects.reserve(count);
  size_t ptr = 4;

  for (int i = 0; i < count; ++i) {
    ObjectData obj{};
    short rawType;
    memcpy(&rawType, data.data() + ptr, 2);
    obj.type = rawType;
    ptr += 2;

    float mu_pos[3], mu_angle[3];
    memcpy(mu_pos, data.data() + ptr, 12);
    ptr += 12;
    memcpy(mu_angle, data.data() + ptr, 12);
    ptr += 12;
    memcpy(&obj.scale, data.data() + ptr, 4);
    ptr += 4;

    // Store raw MU values
    obj.mu_pos_raw = glm::vec3(mu_pos[0], mu_pos[1], mu_pos[2]);
    obj.mu_angle_raw = glm::vec3(mu_angle[0], mu_angle[1], mu_angle[2]);

    // Convert MU coords to OpenGL Y-up world coords
    // Terrain mapping: MU_Y(z-loop) → WorldX, MU_X(x-loop) → WorldZ
    obj.position = glm::vec3(mu_pos[1], mu_pos[2], mu_pos[0]);

    // Convert degrees to radians
    obj.rotation =
        glm::vec3(glm::radians(mu_angle[0]), glm::radians(mu_angle[1]),
                  glm::radians(mu_angle[2]));

    objects.push_back(obj);
  }

  std::cout << "[TerrainParser] Loaded " << objects.size() << " objects"
            << std::endl;
  return objects;
}

std::vector<glm::vec3> TerrainParser::ParseLightFile(const std::string &path) {
  int w, h;
  auto raw_data = TextureLoader::LoadOZJRaw(path, w, h);

  std::vector<glm::vec3> lightmap(TERRAIN_SIZE * TERRAIN_SIZE, glm::vec3(1.0f));

  if (raw_data.empty() || w != TERRAIN_SIZE || h != TERRAIN_SIZE) {
    if (!raw_data.empty()) {
      std::cerr << "[TerrainParser] Lightmap size mismatch: " << w << "x" << h
                << " (expected 256x256)" << std::endl;
    }
    return lightmap;
  }

  // LoadOZJRaw now uses TJFLAG_BOTTOMUP (matching Sven's OpenJpegBuffer),
  // so the decoded data is already in bottom-to-top order matching OpenGL.
  // No manual row flip needed.
  for (int z = 0; z < TERRAIN_SIZE; ++z) {
    for (int x = 0; x < TERRAIN_SIZE; ++x) {
      int src_idx = (z * TERRAIN_SIZE + x) * 3;
      lightmap[z * TERRAIN_SIZE + x] =
          glm::vec3(static_cast<float>(raw_data[src_idx + 0]) / 255.0f,
                    static_cast<float>(raw_data[src_idx + 1]) / 255.0f,
                    static_cast<float>(raw_data[src_idx + 2]) / 255.0f);
    }
  }

  return lightmap;
}

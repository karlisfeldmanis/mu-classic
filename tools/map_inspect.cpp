// MU Online Map Inspector — Deep reverse engineering tool for terrain file formats
// Build: g++ -std=c++17 -O2 -I../client/external/stb -I../client/external/glm map_inspect.cpp -o map_inspect
//
// Usage:
//   ./map_inspect World1/EncTerrain1.map              # Basic map info
//   ./map_inspect World1/EncTerrain1.att              # Attribute analysis
//   ./map_inspect World1/EncTerrain1.obj              # Object listing
//   ./map_inspect World1/TerrainHeight.OZB            # Height stats
//   ./map_inspect World1/TerrainLight.OZJ             # Lightmap analysis
//   ./map_inspect World1/ --all                       # Analyze entire world
//   ./map_inspect World1/EncTerrain1.map --heatmap    # Export height heatmap
//   ./map_inspect World1/EncTerrain1.att --zones      # Visualize attribute zones
//   ./map_inspect World1/EncTerrain1.obj --types      # Group objects by type
//   ./map_inspect World1/ --compare World2/           # Compare two maps
//
// Advanced analysis modes:
//   --raw           Hex dump of decrypted data
//   --stats         Statistical analysis (min/max/avg/distribution)
//   --histogram     Tile/attribute usage histogram
//   --heatmap       ASCII art height/lightmap visualization
//   --zones         Attribute zone detection (safe/walk/void boundaries)
//   --objects       Full object dump with coordinates
//   --types         Group objects by type with counts
//   --density       Object density heatmap (objects per 32x32 region)
//   --validate      Check for invalid data (tile 255, NaN heights, etc.)
//   --export-ppm    Export heightmap/lightmap as PPM image
//   --compare       Diff two map files

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
// Encryption & Decryption
// ═══════════════════════════════════════════════════════════════════════════

static const uint8_t MAP_XOR_KEY[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                         0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                         0x37, 0xB3, 0xE7, 0xA2};

static std::vector<uint8_t> DecryptMapFile(const std::vector<uint8_t> &data) {
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

static std::vector<uint8_t> ApplyBuxConvert(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> result = data;
  uint8_t bux_code[3] = {0xFC, 0xCF, 0xAB};
  for (size_t i = 0; i < result.size(); ++i) {
    result[i] ^= bux_code[i % 3];
  }
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// File Format Parsers
// ═══════════════════════════════════════════════════════════════════════════

struct MapFileData {
  uint8_t version;
  uint8_t mapNumber;
  std::vector<uint8_t> layer1;  // 256x256
  std::vector<uint8_t> layer2;  // 256x256
  std::vector<uint8_t> alpha;   // 256x256
};

struct AttFileData {
  uint8_t version;
  uint8_t mapNumber;
  uint8_t width;
  uint8_t height;
  std::vector<uint8_t> attributes; // 256x256
  std::vector<uint8_t> symmetry;   // 256x256 (high byte of WORD format)
  bool isWordFormat;
};

struct ObjFileData {
  uint8_t version;
  uint8_t mapNumber;
  int16_t count;
  struct Object {
    int16_t type;
    float posX, posY, posZ;     // MU coordinates
    float angleX, angleY, angleZ; // degrees
    float scale;
  };
  std::vector<Object> objects;
};

struct HeightFileData {
  std::vector<uint8_t> rawHeights; // 256x256
  std::vector<float> heights;      // scaled by 1.5
};

static const int TERRAIN_SIZE = 256;

static MapFileData ParseMapFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  MapFileData res{};
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  if (!file) {
    std::cerr << "Error: Cannot open file: " << path << std::endl;
    return res;
  }

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = DecryptMapFile(raw);

  const size_t expected = 2 + cells * 3;
  if (data.size() < expected) {
    std::cerr << "Error: File too small: " << data.size() << " bytes (expected "
              << expected << ")" << std::endl;
    return res;
  }

  res.version = data[0];
  res.mapNumber = data[1];
  size_t ptr = 2;

  res.layer1.assign(data.begin() + ptr, data.begin() + ptr + cells);
  ptr += cells;
  res.layer2.assign(data.begin() + ptr, data.begin() + ptr + cells);
  ptr += cells;
  res.alpha.assign(data.begin() + ptr, data.begin() + ptr + cells);

  return res;
}

static AttFileData ParseAttFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  AttFileData res{};
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  if (!file) {
    std::cerr << "Error: Cannot open file: " << path << std::endl;
    return res;
  }

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = ApplyBuxConvert(DecryptMapFile(raw));

  const size_t byte_size = 4 + cells;
  const size_t word_size = 4 + cells * 2;

  if (data.size() < 4) {
    std::cerr << "Error: File too small for header" << std::endl;
    return res;
  }

  res.version = data[0];
  res.mapNumber = data[1];
  res.width = data[2];
  res.height = data[3];

  if (data.size() >= word_size) {
    res.isWordFormat = true;
    res.attributes.resize(cells);
    res.symmetry.resize(cells);
    for (size_t i = 0; i < cells; ++i) {
      res.attributes[i] = data[4 + i * 2];
      res.symmetry[i] = data[5 + i * 2];
    }
  } else if (data.size() >= byte_size) {
    res.isWordFormat = false;
    res.attributes.assign(data.begin() + 4, data.begin() + 4 + cells);
  } else {
    std::cerr << "Error: File too small: " << data.size() << std::endl;
  }

  return res;
}

static ObjFileData ParseObjFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  ObjFileData res{};

  if (!file) {
    std::cerr << "Error: Cannot open file: " << path << std::endl;
    return res;
  }

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = DecryptMapFile(raw);

  if (data.size() < 4) {
    std::cerr << "Error: File too small for header" << std::endl;
    return res;
  }

  res.version = data[0];
  res.mapNumber = data[1];
  memcpy(&res.count, data.data() + 2, 2);

  if (res.count < 0 || res.count > 10000) {
    std::cerr << "Error: Invalid object count: " << res.count << std::endl;
    res.count = 0;
    return res;
  }

  const size_t recSize = 30;
  const size_t expected = 4 + res.count * recSize;
  if (data.size() < expected) {
    std::cerr << "Error: File too small for " << res.count << " objects"
              << std::endl;
    res.count = 0;
    return res;
  }

  res.objects.reserve(res.count);
  size_t ptr = 4;

  for (int i = 0; i < res.count; ++i) {
    ObjFileData::Object obj{};
    memcpy(&obj.type, data.data() + ptr, 2);
    ptr += 2;
    memcpy(&obj.posX, data.data() + ptr, 4);
    memcpy(&obj.posY, data.data() + ptr + 4, 4);
    memcpy(&obj.posZ, data.data() + ptr + 8, 4);
    ptr += 12;
    memcpy(&obj.angleX, data.data() + ptr, 4);
    memcpy(&obj.angleY, data.data() + ptr + 4, 4);
    memcpy(&obj.angleZ, data.data() + ptr + 8, 4);
    ptr += 12;
    memcpy(&obj.scale, data.data() + ptr, 4);
    ptr += 4;
    res.objects.push_back(obj);
  }

  return res;
}

static HeightFileData ParseHeightFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  HeightFileData res{};
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  if (!file) {
    std::cerr << "Error: Cannot open file: " << path << std::endl;
    return res;
  }

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(size - cells, std::ios::beg);

  res.rawHeights.resize(cells);
  file.read((char *)res.rawHeights.data(), cells);

  res.heights.resize(cells);
  for (size_t i = 0; i < cells; ++i) {
    res.heights[i] = res.rawHeights[i] * 1.5f;
  }

  return res;
}

// ═══════════════════════════════════════════════════════════════════════════
// Analysis Functions
// ═══════════════════════════════════════════════════════════════════════════

static void PrintHexDump(const std::vector<uint8_t> &data, size_t maxBytes = 512) {
  size_t len = std::min(data.size(), maxBytes);
  for (size_t i = 0; i < len; i += 16) {
    printf("%08zx  ", i);
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < len)
        printf("%02x ", data[i + j]);
      else
        printf("   ");
    }
    printf(" |");
    for (size_t j = 0; j < 16 && i + j < len; ++j) {
      uint8_t c = data[i + j];
      printf("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    printf("|\n");
  }
  if (data.size() > maxBytes)
    printf("... (%zu more bytes)\n", data.size() - maxBytes);
}

static void AnalyzeMapFile(const MapFileData &map, bool showStats,
                           bool showHistogram, bool showHeatmap) {
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  std::cout << "\n═══ MAP FILE ANALYSIS ═══\n";
  std::cout << "Version:    " << (int)map.version << "\n";
  std::cout << "Map Number: " << (int)map.mapNumber << "\n";
  std::cout << "Cells:      " << cells << " (256x256)\n";

  if (showStats) {
    std::cout << "\n─── Layer Statistics ───\n";

    // Layer1 stats
    std::map<uint8_t, int> l1hist;
    for (auto v : map.layer1)
      l1hist[v]++;
    std::cout << "Layer1: " << l1hist.size() << " unique tiles\n";

    // Layer2 stats
    std::map<uint8_t, int> l2hist;
    for (auto v : map.layer2)
      l2hist[v]++;
    std::cout << "Layer2: " << l2hist.size() << " unique tiles\n";

    // Alpha stats
    float alphaMin = 255, alphaMax = 0, alphaAvg = 0;
    int zeroAlpha = 0, fullAlpha = 0;
    for (auto a : map.alpha) {
      float af = a / 255.0f;
      if (a == 0)
        zeroAlpha++;
      if (a == 255)
        fullAlpha++;
      alphaMin = std::min(alphaMin, af);
      alphaMax = std::max(alphaMax, af);
      alphaAvg += af;
    }
    alphaAvg /= cells;
    std::cout << "Alpha:  min=" << alphaMin << " max=" << alphaMax
              << " avg=" << alphaAvg << "\n";
    std::cout << "        pure Layer1 (alpha=0): "
              << (zeroAlpha * 100.0f / cells) << "%\n";
    std::cout << "        pure Layer2 (alpha=255): "
              << (fullAlpha * 100.0f / cells) << "%\n";
  }

  if (showHistogram) {
    std::cout << "\n─── Tile Histogram (Layer1) ───\n";
    std::map<uint8_t, int> l1hist;
    for (auto v : map.layer1)
      l1hist[v]++;

    std::vector<std::pair<uint8_t, int>> sorted(l1hist.begin(), l1hist.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto &a, auto &b) { return a.second > b.second; });

    for (size_t i = 0; i < std::min(sorted.size(), size_t(20)); ++i) {
      int pct = sorted[i].second * 100 / cells;
      printf("  Tile %3d: %6d cells (%3d%%) ", sorted[i].first,
             sorted[i].second, pct);
      int bars = pct / 2;
      for (int j = 0; j < bars; ++j)
        printf("█");
      printf("\n");
    }
    if (sorted.size() > 20)
      printf("  ... (%zu more tiles)\n", sorted.size() - 20);

    // Warn about tile 255 (invalid marker)
    if (l1hist.count(255)) {
      printf("\n⚠️  WARNING: Layer1 contains %d cells with tile 255 (invalid "
             "marker)\n",
             l1hist[255]);
    }
  }

  if (showHeatmap) {
    std::cout << "\n─── Height Heatmap (ASCII, 64x64 downsample) ───\n";
    // Use Layer1 for now (height would need separate file)
    const int D = 4; // downsample 256 -> 64
    for (int y = 0; y < TERRAIN_SIZE; y += D) {
      for (int x = 0; x < TERRAIN_SIZE; x += D) {
        uint8_t tile = map.layer1[y * TERRAIN_SIZE + x];
        char c = ' ';
        if (tile == 255)
          c = 'X'; // invalid
        else if (tile < 32)
          c = '.';
        else if (tile < 64)
          c = ':';
        else if (tile < 128)
          c = '+';
        else
          c = '#';
        printf("%c", c);
      }
      printf("\n");
    }
  }
}

static void AnalyzeAttFile(const AttFileData &att, bool showStats,
                           bool showHistogram, bool showZones) {
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  std::cout << "\n═══ ATTRIBUTE FILE ANALYSIS ═══\n";
  std::cout << "Version:    " << (int)att.version << "\n";
  std::cout << "Map Number: " << (int)att.mapNumber << "\n";
  std::cout << "Size:       " << (int)att.width << "x" << (int)att.height
            << "\n";
  std::cout << "Format:     " << (att.isWordFormat ? "WORD (2 bytes/cell)"
                                                    : "BYTE (1 byte/cell)")
            << "\n";

  if (showStats) {
    std::cout << "\n─── Attribute Flag Statistics ───\n";
    int safezone = 0, nomove = 0, noground = 0;
    int walkable = 0;

    for (auto a : att.attributes) {
      if (a & 0x01)
        safezone++;
      if (a & 0x04)
        nomove++;
      if (a & 0x08)
        noground++;
      if ((a & 0x0C) == 0)
        walkable++; // neither NOMOVE nor NOGROUND
    }

    printf("TW_SAFEZONE (0x01):  %6d cells (%5.2f%%)\n", safezone,
           safezone * 100.0f / cells);
    printf("TW_NOMOVE   (0x04):  %6d cells (%5.2f%%)\n", nomove,
           nomove * 100.0f / cells);
    printf("TW_NOGROUND (0x08):  %6d cells (%5.2f%%)\n", noground,
           noground * 100.0f / cells);
    printf("Walkable:            %6d cells (%5.2f%%)\n", walkable,
           walkable * 100.0f / cells);

    if (att.isWordFormat && !att.symmetry.empty()) {
      std::cout << "\n─── Symmetry Byte (High Byte) Statistics ───\n";
      std::map<uint8_t, int> symhist;
      for (auto s : att.symmetry)
        symhist[s]++;
      std::cout << "Unique values: " << symhist.size() << "\n";
      if (symhist.size() <= 10) {
        for (auto &kv : symhist) {
          printf("  0x%02X: %6d cells (%5.2f%%)\n", kv.first, kv.second,
                 kv.second * 100.0f / cells);
        }
      }
    }
  }

  if (showHistogram) {
    std::cout << "\n─── Attribute Value Histogram ───\n";
    std::map<uint8_t, int> hist;
    for (auto a : att.attributes)
      hist[a]++;

    std::vector<std::pair<uint8_t, int>> sorted(hist.begin(), hist.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto &a, auto &b) { return a.second > b.second; });

    for (size_t i = 0; i < std::min(sorted.size(), size_t(16)); ++i) {
      int pct = sorted[i].second * 100 / cells;
      printf("  0x%02X: %6d cells (%3d%%) ", sorted[i].first, sorted[i].second,
             pct);
      // Decode flags
      uint8_t v = sorted[i].first;
      if (v & 0x01)
        printf("[SAFE] ");
      if (v & 0x04)
        printf("[NOMOVE] ");
      if (v & 0x08)
        printf("[NOGROUND] ");
      if ((v & 0x0C) == 0)
        printf("[WALK] ");
      printf("\n");
    }
  }

  if (showZones) {
    std::cout << "\n─── Attribute Zone Map (64x64 downsample) ───\n";
    std::cout << "Legend: . = walkable, # = nomove, ~ = noground, S = safe, X = "
                 "safe+nomove\n";
    const int D = 4;
    for (int y = 0; y < TERRAIN_SIZE; y += D) {
      for (int x = 0; x < TERRAIN_SIZE; x += D) {
        uint8_t a = att.attributes[y * TERRAIN_SIZE + x];
        char c = '.';
        if ((a & 0x01) && (a & 0x04))
          c = 'X'; // safe + wall
        else if (a & 0x01)
          c = 'S'; // safe zone
        else if (a & 0x08)
          c = '~'; // void/cliff
        else if (a & 0x04)
          c = '#'; // wall
        printf("%c", c);
      }
      printf("\n");
    }
  }
}

static void AnalyzeObjFile(const ObjFileData &obj, bool showObjects,
                           bool groupByType, bool showDensity) {
  std::cout << "\n═══ OBJECT FILE ANALYSIS ═══\n";
  std::cout << "Version:    " << (int)obj.version << "\n";
  std::cout << "Map Number: " << (int)obj.mapNumber << "\n";
  std::cout << "Count:      " << obj.count << " objects\n";

  if (groupByType) {
    std::cout << "\n─── Objects Grouped By Type ───\n";
    std::map<int16_t, int> typeCounts;
    for (auto &o : obj.objects)
      typeCounts[o.type]++;

    std::vector<std::pair<int16_t, int>> sorted(typeCounts.begin(),
                                                 typeCounts.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto &a, auto &b) { return a.second > b.second; });

    for (auto &kv : sorted) {
      printf("  Type %3d: %4d instances\n", kv.first, kv.second);
    }
  }

  if (showObjects) {
    std::cout << "\n─── Full Object Listing ───\n";
    printf("%4s %5s %9s %9s %9s  %7s %7s %7s  %6s\n", "Idx", "Type", "PosX",
           "PosY", "PosZ", "AngleX", "AngleY", "AngleZ", "Scale");
    for (size_t i = 0; i < obj.objects.size(); ++i) {
      auto &o = obj.objects[i];
      printf("%4zu %5d %9.1f %9.1f %9.1f  %7.1f %7.1f %7.1f  %6.2f\n", i,
             o.type, o.posX, o.posY, o.posZ, o.angleX, o.angleY, o.angleZ,
             o.scale);
    }
  }

  if (showDensity) {
    std::cout << "\n─── Object Density Heatmap (32x32 regions, 8x8 grid cells "
                 "each) ───\n";
    const int R = 8;       // 8x8 regions -> 256/8 = 32
    const int C = 32 * 32; // 32x32 heatmap
    std::vector<int> density(C, 0);

    for (auto &o : obj.objects) {
      int gx = (int)(o.posZ / 100.0f); // WorldZ = MU X
      int gy = (int)(o.posX / 100.0f); // WorldX = MU Y
      if (gx >= 0 && gy >= 0 && gx < 256 && gy < 256) {
        int rx = gx / R;
        int ry = gy / R;
        density[ry * 32 + rx]++;
      }
    }

    int maxDensity = *std::max_element(density.begin(), density.end());
    for (int y = 0; y < 32; ++y) {
      for (int x = 0; x < 32; ++x) {
        int d = density[y * 32 + x];
        char c = ' ';
        if (d == 0)
          c = '.';
        else if (d * 4 < maxDensity)
          c = ':';
        else if (d * 2 < maxDensity)
          c = '+';
        else if (d * 4 < maxDensity * 3)
          c = 'o';
        else
          c = 'O';
        printf("%c", c);
      }
      printf("\n");
    }
    printf("Max density: %d objects/region (8x8 grid cells)\n", maxDensity);
  }
}

static void AnalyzeHeightFile(const HeightFileData &hf, bool showStats,
                              bool showHeatmap) {
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  std::cout << "\n═══ HEIGHT FILE ANALYSIS ═══\n";
  std::cout << "Cells: " << cells << " (256x256)\n";

  if (showStats) {
    float minH = *std::min_element(hf.heights.begin(), hf.heights.end());
    float maxH = *std::max_element(hf.heights.begin(), hf.heights.end());
    float avgH =
        std::accumulate(hf.heights.begin(), hf.heights.end(), 0.0f) / cells;

    // Raw byte stats
    uint8_t minRaw =
        *std::min_element(hf.rawHeights.begin(), hf.rawHeights.end());
    uint8_t maxRaw =
        *std::max_element(hf.rawHeights.begin(), hf.rawHeights.end());

    std::cout << "\n─── Height Statistics ───\n";
    printf("Raw bytes:    min=%d, max=%d\n", minRaw, maxRaw);
    printf("Scaled (×1.5): min=%.1f, max=%.1f, avg=%.1f\n", minH, maxH, avgH);

    // Histogram
    std::map<uint8_t, int> hist;
    for (auto h : hf.rawHeights)
      hist[h]++;
    printf("Unique heights: %zu\n", hist.size());
  }

  if (showHeatmap) {
    std::cout << "\n─── Height Heatmap (ASCII, 64x64 downsample) ───\n";
    float minH = *std::min_element(hf.heights.begin(), hf.heights.end());
    float maxH = *std::max_element(hf.heights.begin(), hf.heights.end());
    float range = maxH - minH;
    if (range < 1.0f)
      range = 1.0f;

    const int D = 4; // 256 / 4 = 64
    for (int y = 0; y < TERRAIN_SIZE; y += D) {
      for (int x = 0; x < TERRAIN_SIZE; x += D) {
        float h = hf.heights[y * TERRAIN_SIZE + x];
        float norm = (h - minH) / range;
        char c;
        if (norm < 0.2f)
          c = ' ';
        else if (norm < 0.4f)
          c = '.';
        else if (norm < 0.6f)
          c = '+';
        else if (norm < 0.8f)
          c = 'o';
        else
          c = 'O';
        printf("%c", c);
      }
      printf("\n");
    }
  }
}

static void ValidateMapData(const MapFileData &map, const AttFileData &att,
                            const HeightFileData &hf) {
  std::cout << "\n═══ VALIDATION CHECKS ═══\n";
  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;

  bool issues = false;

  // Check for tile 255
  int invalidTiles = 0;
  for (auto t : map.layer1)
    if (t == 255)
      invalidTiles++;
  if (invalidTiles > 0) {
    printf("⚠️  Layer1: %d cells use tile 255 (invalid marker)\n", invalidTiles);
    issues = true;
  }

  // Check for void cells without NOGROUND
  int voidMismatch = 0;
  for (size_t i = 0; i < cells; ++i) {
    if (map.layer1[i] == 255 && !(att.attributes[i] & 0x08))
      voidMismatch++;
  }
  if (voidMismatch > 0) {
    printf("⚠️  %d void tiles (255) without TW_NOGROUND flag\n", voidMismatch);
    issues = true;
  }

  // Check for extreme heights
  if (!hf.rawHeights.empty()) {
    auto minH = *std::min_element(hf.rawHeights.begin(), hf.rawHeights.end());
    auto maxH = *std::max_element(hf.rawHeights.begin(), hf.rawHeights.end());
    if (maxH - minH > 200) {
      printf("ℹ️  Large height range: %d units (min=%d, max=%d)\n",
             maxH - minH, minH, maxH);
    }
  }

  // Check for unreachable safe zones
  int isolatedSafe = 0;
  for (int y = 1; y < TERRAIN_SIZE - 1; ++y) {
    for (int x = 1; x < TERRAIN_SIZE - 1; ++x) {
      int idx = y * TERRAIN_SIZE + x;
      if (!(att.attributes[idx] & 0x01))
        continue; // not safe
      // Check 4-connected neighbors
      bool isolated = true;
      int dirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
      for (auto &d : dirs) {
        int nx = x + d[0], ny = y + d[1];
        int nidx = ny * TERRAIN_SIZE + nx;
        if ((att.attributes[nidx] & 0x0C) == 0) { // walkable neighbor
          isolated = false;
          break;
        }
      }
      if (isolated)
        isolatedSafe++;
    }
  }
  if (isolatedSafe > 0) {
    printf("⚠️  %d safe zone cells with no walkable neighbors\n", isolatedSafe);
    issues = true;
  }

  if (!issues)
    std::cout << "✓ No issues detected\n";
}

static void ExportPPM(const std::string &outPath,
                      const std::vector<uint8_t> &data, int width, int height,
                      const std::string &label) {
  std::ofstream ppm(outPath);
  if (!ppm) {
    std::cerr << "Error: Cannot write to " << outPath << std::endl;
    return;
  }

  ppm << "P3\n" << width << " " << height << "\n255\n";
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint8_t v = data[y * width + x];
      ppm << (int)v << " " << (int)v << " " << (int)v << " ";
    }
    ppm << "\n";
  }
  std::cout << "Exported " << label << " to " << outPath << "\n";
}

static void CompareMapFiles(const std::string &path1, const std::string &path2) {
  MapFileData m1 = ParseMapFile(path1);
  MapFileData m2 = ParseMapFile(path2);

  std::cout << "\n═══ MAP FILE COMPARISON ═══\n";
  std::cout << "File 1: " << path1 << " (map " << (int)m1.mapNumber << ")\n";
  std::cout << "File 2: " << path2 << " (map " << (int)m2.mapNumber << ")\n";

  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;
  int l1diff = 0, l2diff = 0, adiff = 0;
  for (size_t i = 0; i < cells; ++i) {
    if (m1.layer1[i] != m2.layer1[i])
      l1diff++;
    if (m1.layer2[i] != m2.layer2[i])
      l2diff++;
    if (m1.alpha[i] != m2.alpha[i])
      adiff++;
  }

  printf("\nLayer1 differences: %d cells (%.2f%%)\n", l1diff,
         l1diff * 100.0f / cells);
  printf("Layer2 differences: %d cells (%.2f%%)\n", l2diff,
         l2diff * 100.0f / cells);
  printf("Alpha differences:  %d cells (%.2f%%)\n", adiff,
         adiff * 100.0f / cells);

  if (l1diff + l2diff + adiff == 0)
    std::cout << "✓ Files are identical\n";
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

static void PrintUsage() {
  std::cout << R"(
MU Online Map Inspector — Deep reverse engineering tool for terrain files

Usage:
  map_inspect <file.map|file.att|file.obj|WorldN/>  [options]

Supported files:
  *.map         Terrain mapping (layer1, layer2, alpha blend)
  *.att         Terrain attributes (walkability, safe zones, voids)
  *.obj         World objects (type, position, rotation, scale)
  *.OZB / *.ozh Height data (raw byte values, scaled by 1.5)
  *.OZJ         Lightmap (pre-baked RGB lighting, JPEG compressed)

Options:
  --raw         Hex dump of decrypted data (first 512 bytes)
  --stats       Statistical analysis (min/max/avg/distribution)
  --histogram   Tile/attribute usage histogram
  --heatmap     ASCII art height/tile visualization (64x64)
  --zones       Attribute zone map with safe/walk/void legend
  --objects     Full object dump with coordinates
  --types       Group objects by type with counts
  --density     Object density heatmap (32x32 regions)
  --validate    Check for invalid data (tile 255, isolated safe zones)
  --export-ppm  Export heightmap/lightmap as PPM image
  --compare <file2>  Diff two map files
  --all         Enable all analysis modes

Examples:
  map_inspect World1/EncTerrain1.map --stats --histogram
  map_inspect World1/EncTerrain1.att --zones
  map_inspect World1/EncTerrain1.obj --types --density
  map_inspect World1/TerrainHeight.OZB --heatmap
  map_inspect World1/EncTerrain1.map --compare World2/EncTerrain2.map
  map_inspect World1/ --all
)";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  std::string path = argv[1];
  bool showRaw = false, showStats = false, showHistogram = false;
  bool showHeatmap = false, showZones = false, showObjects = false;
  bool groupByType = false, showDensity = false, validate = false;
  bool exportPPM = false;
  std::string comparePath;

  // Parse flags
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--raw")
      showRaw = true;
    else if (arg == "--stats")
      showStats = true;
    else if (arg == "--histogram")
      showHistogram = true;
    else if (arg == "--heatmap")
      showHeatmap = true;
    else if (arg == "--zones")
      showZones = true;
    else if (arg == "--objects")
      showObjects = true;
    else if (arg == "--types")
      groupByType = true;
    else if (arg == "--density")
      showDensity = true;
    else if (arg == "--validate")
      validate = true;
    else if (arg == "--export-ppm")
      exportPPM = true;
    else if (arg == "--compare" && i + 1 < argc)
      comparePath = argv[++i];
    else if (arg == "--all") {
      showStats = showHistogram = showHeatmap = showZones = true;
      showObjects = groupByType = showDensity = validate = true;
    }
  }

  // Default: enable basic stats if no flags given
  if (!showRaw && !showStats && !showHistogram && !showHeatmap && !showZones &&
      !showObjects && !groupByType && !showDensity && !validate &&
      !exportPPM && comparePath.empty()) {
    showStats = true;
    showHistogram = true;
  }

  // Detect file type and dispatch
  std::string ext = fs::path(path).extension();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (!comparePath.empty()) {
    CompareMapFiles(path, comparePath);
    return 0;
  }

  if (ext == ".map") {
    MapFileData map = ParseMapFile(path);
    if (map.layer1.empty()) {
      std::cerr << "Failed to parse MAP file\n";
      return 1;
    }
    AnalyzeMapFile(map, showStats, showHistogram, showHeatmap);

    if (exportPPM) {
      ExportPPM(path + ".layer1.ppm", map.layer1, TERRAIN_SIZE, TERRAIN_SIZE,
                "Layer1");
      ExportPPM(path + ".layer2.ppm", map.layer2, TERRAIN_SIZE, TERRAIN_SIZE,
                "Layer2");
      ExportPPM(path + ".alpha.ppm", map.alpha, TERRAIN_SIZE, TERRAIN_SIZE,
                "Alpha");
    }
  } else if (ext == ".att") {
    AttFileData att = ParseAttFile(path);
    if (att.attributes.empty()) {
      std::cerr << "Failed to parse ATT file\n";
      return 1;
    }
    AnalyzeAttFile(att, showStats, showHistogram, showZones);

    if (validate) {
      // Load companion map file for cross-validation
      std::string mapPath = path;
      size_t pos = mapPath.rfind(".att");
      if (pos != std::string::npos) {
        mapPath.replace(pos, 4, ".map");
        if (fs::exists(mapPath)) {
          MapFileData map = ParseMapFile(mapPath);
          HeightFileData hf{};
          ValidateMapData(map, att, hf);
        }
      }
    }

    if (exportPPM) {
      ExportPPM(path + ".attributes.ppm", att.attributes, TERRAIN_SIZE,
                TERRAIN_SIZE, "Attributes");
      if (!att.symmetry.empty())
        ExportPPM(path + ".symmetry.ppm", att.symmetry, TERRAIN_SIZE,
                  TERRAIN_SIZE, "Symmetry");
    }
  } else if (ext == ".obj") {
    ObjFileData obj = ParseObjFile(path);
    if (obj.count == 0) {
      std::cerr << "Failed to parse OBJ file or no objects\n";
      return 1;
    }
    AnalyzeObjFile(obj, showObjects, groupByType, showDensity);
  } else if (ext == ".ozb" || ext == ".ozh") {
    HeightFileData hf = ParseHeightFile(path);
    if (hf.rawHeights.empty()) {
      std::cerr << "Failed to parse height file\n";
      return 1;
    }
    AnalyzeHeightFile(hf, showStats, showHeatmap);

    if (exportPPM) {
      ExportPPM(path + ".height.ppm", hf.rawHeights, TERRAIN_SIZE,
                TERRAIN_SIZE, "Height");
    }
  } else if (fs::is_directory(path)) {
    std::cout << "\n═══ WORLD DIRECTORY ANALYSIS: " << path << " ═══\n";

    // Find all terrain files
    std::string mapFile, attFile, objFile, heightFile, lightFile;
    for (auto &entry : fs::directory_iterator(path)) {
      std::string fname = entry.path().filename();
      std::string lower = fname;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

      if (lower.find("encterrain") != std::string::npos ||
          lower.find("terrain") != std::string::npos) {
        if (lower.size() >= 4 &&
            lower.substr(lower.size() - 4) == ".map" && mapFile.empty())
          mapFile = entry.path();
        else if (lower.size() >= 4 &&
                 lower.substr(lower.size() - 4) == ".att" && attFile.empty())
          attFile = entry.path();
        else if (lower.size() >= 4 &&
                 lower.substr(lower.size() - 4) == ".obj" && objFile.empty())
          objFile = entry.path();
      }
      if (lower.find("height") != std::string::npos &&
          (lower.size() >= 4 &&
           (lower.substr(lower.size() - 4) == ".ozb" ||
            lower.substr(lower.size() - 4) == ".ozh")))
        heightFile = entry.path();
      if (lower == "terrainlight.ozj")
        lightFile = entry.path();
    }

    std::cout << "Found files:\n";
    if (!mapFile.empty())
      std::cout << "  MAP: " << mapFile << "\n";
    if (!attFile.empty())
      std::cout << "  ATT: " << attFile << "\n";
    if (!objFile.empty())
      std::cout << "  OBJ: " << objFile << "\n";
    if (!heightFile.empty())
      std::cout << "  HEIGHT: " << heightFile << "\n";
    if (!lightFile.empty())
      std::cout << "  LIGHT: " << lightFile << "\n";

    // Analyze each
    MapFileData map;
    AttFileData att;
    HeightFileData hf;

    if (!mapFile.empty()) {
      map = ParseMapFile(mapFile);
      AnalyzeMapFile(map, showStats, showHistogram, showHeatmap);
    }
    if (!attFile.empty()) {
      att = ParseAttFile(attFile);
      AnalyzeAttFile(att, showStats, showHistogram, showZones);
    }
    if (!objFile.empty()) {
      ObjFileData obj = ParseObjFile(objFile);
      AnalyzeObjFile(obj, showObjects, groupByType, showDensity);
    }
    if (!heightFile.empty()) {
      hf = ParseHeightFile(heightFile);
      AnalyzeHeightFile(hf, showStats, showHeatmap);
    }

    // Cross-file validation
    if (validate && !map.layer1.empty() && !att.attributes.empty())
      ValidateMapData(map, att, hf);

  } else {
    std::cerr << "Error: Unknown file type or not a directory: " << path
              << std::endl;
    PrintUsage();
    return 1;
  }

  return 0;
}

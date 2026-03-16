#include "UITexture.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static TexHandle LoadPNG(const std::string &path, int &outW, int &outH) {
  int w, h, channels;
  unsigned char *data =
      stbi_load(path.c_str(), &w, &h, &channels, 4); // force RGBA
  if (!data) {
    printf("[UITexture] Failed to load PNG: %s (%s)\n", path.c_str(),
           stbi_failure_reason());
    return kInvalidTex;
  }

  outW = w;
  outH = h;

  auto tex = bgfx::createTexture2D(
      (uint16_t)w, (uint16_t)h, false, 1,
      bgfx::TextureFormat::RGBA8,
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      bgfx::copy(data, w * h * 4));
  stbi_image_free(data);
  printf("[UITexture] PNG %s: %dx%d (idx=%d)\n", path.c_str(), w, h, tex.idx);
  return tex;
}

// Helper: get OZT bpp from file header (for hasAlpha detection)
static int GetOZTBpp(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return 0;
  unsigned char hdr[48];
  file.read(reinterpret_cast<char *>(hdr), sizeof(hdr));
  auto bytesRead = file.gcount();
  if (bytesRead < 24)
    return 0;
  size_t offset = 0;
  if (bytesRead > 24) {
    if (hdr[4 + 2] == 2 || hdr[4 + 2] == 10)
      offset = 4;
    else if (bytesRead > 48 && (hdr[24 + 2] == 2 || hdr[24 + 2] == 10))
      offset = 24;
  }
  if (offset + 16 < (size_t)bytesRead)
    return hdr[offset + 16]; // bpp field
  return 0;
}

// Helper: get OZT/OZJ dimensions from file header
static void GetOZTDimensions(const std::string &path, int &w, int &h) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return;
  unsigned char hdr[48];
  file.read(reinterpret_cast<char *>(hdr), sizeof(hdr));
  auto bytesRead = file.gcount();
  if (bytesRead < 24)
    return;
  size_t offset = 0;
  if (bytesRead > 24) {
    if (hdr[4 + 2] == 2 || hdr[4 + 2] == 10)
      offset = 4;
    else if (bytesRead > 48 && (hdr[24 + 2] == 2 || hdr[24 + 2] == 10))
      offset = 24;
  }
  if (offset + 15 < (size_t)bytesRead) {
    w = hdr[offset + 12] | (hdr[offset + 13] << 8);
    h = hdr[offset + 14] | (hdr[offset + 15] << 8);
  }
}

UITexture UITexture::Load(const std::string &path) {
  UITexture tex;

  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  // Determine format by extension
  bool isOZJ = lower.ends_with(".ozj") || lower.ends_with(".jpg") ||
               lower.ends_with(".jpeg");
  bool isOZT = lower.ends_with(".ozt") || lower.ends_with(".tga");
  bool isPNG = lower.ends_with(".png");

  if (isPNG) {
    tex.id = LoadPNG(path, tex.width, tex.height);
    tex.isOZT = false;
    tex.hasAlpha = true;
  } else if (isOZJ) {
    tex.id = TextureLoader::LoadOZJ(path);
    tex.isOZT = false;
    tex.hasAlpha = false;
  } else if (isOZT) {
    tex.id = TextureLoader::LoadOZT(path);
    tex.isOZT = true;
    if (TexValid(tex.id))
      tex.hasAlpha = (GetOZTBpp(path) == 32);
  } else {
    printf("[UITexture] Unknown format: %s\n", path.c_str());
    return tex;
  }

  // Get dimensions
  if (TexValid(tex.id)) {
    // For BGFX, query dimensions from file header (no GPU query available)
    if (!isPNG) {
      if (isOZJ) {
        int w = 0, h = 0;
        auto raw = TextureLoader::LoadOZJRaw(path, w, h);
        tex.width = w;
        tex.height = h;
      } else {
        GetOZTDimensions(path, tex.width, tex.height);
      }
    }
    // Set clamp wrap mode — already set during creation for PNG
    // For OZJ/OZT loaded via TextureLoader, they use default repeat wrap.
    // UI textures need clamp, but BGFX doesn't support changing sampler
    // state after creation. This is handled at draw time via sampler flags.

    if (!isPNG)
      printf("[UITexture] %s %s: %dx%d (idx=%d)\n", isOZT ? "OZT" : "OZJ",
             path.c_str(), tex.width, tex.height, tex.id.idx);
  } else {
    printf("[UITexture] FAILED to load: %s\n", path.c_str());
  }

  return tex;
}

void UITexture::Destroy() {
  TexDestroy(id);
}

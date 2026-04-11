#include "TextureLoader.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <turbojpeg.h>

namespace fs = std::filesystem;

std::vector<unsigned char> TextureLoader::LoadOZJRaw(const std::string &path,
                                                     int &width, int &height) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[TextureLoader] Cannot open file: " << path << std::endl;
    return {};
  }

  std::vector<unsigned char> full_data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
  if (full_data.size() < 4)
    return {};

  const unsigned char *jpeg_ptr = full_data.data();
  size_t jpeg_size = full_data.size();

  // MU Online OZJ files (and some renamed JPGs) often have a 24-byte header.
  // We skip it if we find the JPEG magic number (FF D8) at offset 24.
  if (full_data.size() > 24 && full_data[24] == 0xFF && full_data[25] == 0xD8) {
    jpeg_ptr += 24;
    jpeg_size -= 24;
  } else if (full_data[0] == 0xFF && full_data[1] == 0xD8) {
    // Standard JPEG starting at offset 0
  } else {
    std::cerr << "[TextureLoader] Invalid JPEG format: " << path << std::endl;
    return {};
  }

  tjhandle decompressor = tjInitDecompress();
  int subsamp, colorspace;
  if (tjDecompressHeader3(decompressor, jpeg_ptr, jpeg_size, &width, &height,
                          &subsamp, &colorspace) < 0) {
    std::cerr << "[TextureLoader] TurboJPEG header error for " << path << ": "
              << tjGetErrorStr2(decompressor) << std::endl;
    tjDestroy(decompressor);
    return {};
  }

  std::vector<unsigned char> image_data(width * height * 3);
  // Keep native JPEG top-to-bottom row order. In OpenGL, the first row of
  // glTexImage2D data maps to texCoord v=0.  MU UV coordinates use DirectX
  // convention (v=0 = top of image), so uploading top-to-bottom data means
  // v=0 correctly samples the top of the texture — no flip needed.
  if (tjDecompress2(decompressor, jpeg_ptr, jpeg_size, image_data.data(), width,
                    0, height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    std::cerr << "[TextureLoader] TurboJPEG decompression error for " << path
              << ": " << tjGetErrorStr2(decompressor) << std::endl;
    tjDestroy(decompressor);
    return {};
  }
  tjDestroy(decompressor);
  return image_data;
}

// Generate full mipchain with premultiplied-alpha filtering.
// This prevents alpha-tested textures (trees, grass) from getting muddy halos
// at lower mip levels. RGB is weighted by alpha before box-filtering, then
// divided back out, so transparent pixels don't bleed dark edges into opaque ones.
// Input: RGBA8 data, width, height. Returns packed mip0+mip1+...+mip_last blob.
static const bgfx::Memory *GenerateMipmapRGBA8(const unsigned char *mip0,
                                                 int w, int h) {
  // Calculate total size for all mip levels
  uint32_t totalSize = 0;
  int mW = w, mH = h;
  while (mW > 0 && mH > 0) {
    totalSize += mW * mH * 4;
    if (mW == 1 && mH == 1) break;
    mW = std::max(1, mW / 2);
    mH = std::max(1, mH / 2);
  }

  const bgfx::Memory *mem = bgfx::alloc(totalSize);
  unsigned char *dst = mem->data;

  // Copy mip 0
  memcpy(dst, mip0, w * h * 4);

  // Generate subsequent mips via premultiplied-alpha box filter
  const unsigned char *prev = dst;
  int prevW = w, prevH = h;
  dst += w * h * 4;

  while (prevW > 1 || prevH > 1) {
    int nW = std::max(1, prevW / 2);
    int nH = std::max(1, prevH / 2);
    for (int y = 0; y < nH; ++y) {
      for (int x = 0; x < nW; ++x) {
        int sx = x * 2, sy = y * 2;
        int sx1 = std::min(sx + 1, prevW - 1);
        int sy1 = std::min(sy + 1, prevH - 1);
        // 4 source texels
        const unsigned char *p00 = &prev[(sy  * prevW + sx)  * 4];
        const unsigned char *p10 = &prev[(sy  * prevW + sx1) * 4];
        const unsigned char *p01 = &prev[(sy1 * prevW + sx)  * 4];
        const unsigned char *p11 = &prev[(sy1 * prevW + sx1) * 4];
        // Premultiplied alpha: weight RGB by alpha
        float a0 = p00[3] / 255.0f, a1 = p10[3] / 255.0f;
        float a2 = p01[3] / 255.0f, a3 = p11[3] / 255.0f;
        float aSum = a0 + a1 + a2 + a3;
        float aAvg = aSum * 0.25f;
        unsigned char *out = &dst[(y * nW + x) * 4];
        if (aSum > 0.001f) {
          // Weighted average: opaque pixels contribute more to RGB
          float invA = 1.0f / aSum;
          out[0] = (unsigned char)std::clamp((p00[0]*a0 + p10[0]*a1 + p01[0]*a2 + p11[0]*a3) * invA, 0.0f, 255.0f);
          out[1] = (unsigned char)std::clamp((p00[1]*a0 + p10[1]*a1 + p01[1]*a2 + p11[1]*a3) * invA, 0.0f, 255.0f);
          out[2] = (unsigned char)std::clamp((p00[2]*a0 + p10[2]*a1 + p01[2]*a2 + p11[2]*a3) * invA, 0.0f, 255.0f);
        } else {
          out[0] = out[1] = out[2] = 0;
        }
        out[3] = (unsigned char)std::clamp(aAvg * 255.0f, 0.0f, 255.0f);
      }
    }
    prev = dst;
    dst += nW * nH * 4;
    prevW = nW;
    prevH = nH;
  }
  return mem;
}

// Same for BGRA8 (OZT 32-bit) — swizzle is B,G,R,A instead of R,G,B,A
static const bgfx::Memory *GenerateMipmapBGRA8(const unsigned char *mip0,
                                                 int w, int h) {
  uint32_t totalSize = 0;
  int mW = w, mH = h;
  while (mW > 0 && mH > 0) {
    totalSize += mW * mH * 4;
    if (mW == 1 && mH == 1) break;
    mW = std::max(1, mW / 2);
    mH = std::max(1, mH / 2);
  }

  const bgfx::Memory *mem = bgfx::alloc(totalSize);
  unsigned char *dst = mem->data;
  memcpy(dst, mip0, w * h * 4);

  const unsigned char *prev = dst;
  int prevW = w, prevH = h;
  dst += w * h * 4;

  while (prevW > 1 || prevH > 1) {
    int nW = std::max(1, prevW / 2);
    int nH = std::max(1, prevH / 2);
    for (int y = 0; y < nH; ++y) {
      for (int x = 0; x < nW; ++x) {
        int sx = x * 2, sy = y * 2;
        int sx1 = std::min(sx + 1, prevW - 1);
        int sy1 = std::min(sy + 1, prevH - 1);
        const unsigned char *p00 = &prev[(sy  * prevW + sx)  * 4];
        const unsigned char *p10 = &prev[(sy  * prevW + sx1) * 4];
        const unsigned char *p01 = &prev[(sy1 * prevW + sx)  * 4];
        const unsigned char *p11 = &prev[(sy1 * prevW + sx1) * 4];
        // BGRA: alpha is [3], same position
        float a0 = p00[3] / 255.0f, a1 = p10[3] / 255.0f;
        float a2 = p01[3] / 255.0f, a3 = p11[3] / 255.0f;
        float aSum = a0 + a1 + a2 + a3;
        float aAvg = aSum * 0.25f;
        unsigned char *out = &dst[(y * nW + x) * 4];
        if (aSum > 0.001f) {
          float invA = 1.0f / aSum;
          out[0] = (unsigned char)std::clamp((p00[0]*a0 + p10[0]*a1 + p01[0]*a2 + p11[0]*a3) * invA, 0.0f, 255.0f);
          out[1] = (unsigned char)std::clamp((p00[1]*a0 + p10[1]*a1 + p01[1]*a2 + p11[1]*a3) * invA, 0.0f, 255.0f);
          out[2] = (unsigned char)std::clamp((p00[2]*a0 + p10[2]*a1 + p01[2]*a2 + p11[2]*a3) * invA, 0.0f, 255.0f);
        } else {
          out[0] = out[1] = out[2] = 0;
        }
        out[3] = (unsigned char)std::clamp(aAvg * 255.0f, 0.0f, 255.0f);
      }
    }
    prev = dst;
    dst += nW * nH * 4;
    prevW = nW;
    prevH = nH;
  }
  return mem;
}

TexHandle TextureLoader::LoadOZJ(const std::string &path) {
  int w, h;
  auto data = LoadOZJRaw(path, w, h);
  if (data.empty())
    return kInvalidTex;

  // Convert RGB to RGBA (BGFX prefers RGBA8 for universal support)
  std::vector<unsigned char> rgba(w * h * 4);
  for (int i = 0; i < w * h; ++i) {
    rgba[i * 4 + 0] = data[i * 3 + 0];
    rgba[i * 4 + 1] = data[i * 3 + 1];
    rgba[i * 4 + 2] = data[i * 3 + 2];
    rgba[i * 4 + 3] = 255;
  }
  return bgfx::createTexture2D(
      (uint16_t)w, (uint16_t)h, true, 1,
      bgfx::TextureFormat::RGBA8,
      BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC,
      GenerateMipmapRGBA8(rgba.data(), w, h));
}

// OZT/TGA parsing and decompression (shared between LoadOZT and LoadOZTRaw)
struct OZTParseResult {
  std::vector<unsigned char> pixels; // Decompressed, V-flipped pixel data
  int width = 0, height = 0, bpp = 0;
  bool valid = false;
};

static OZTParseResult ParseOZT(const std::string &path) {
  OZTParseResult result;

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[TextureLoader] Cannot open OZT: " << path << std::endl;
    return result;
  }

  std::vector<unsigned char> full_data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

  size_t offset = 0;
  if (full_data.size() > 24) {
    if (full_data[4 + 2] == 2 || full_data[4 + 2] == 10)
      offset = 4;
    else if (full_data[24 + 2] == 2 || full_data[24 + 2] == 10)
      offset = 24;
  }

  if (full_data.size() < offset + 18)
    return result;

  unsigned char *header = &full_data[offset];
  unsigned char imageType = header[2];
  result.width = header[12] | (header[13] << 8);
  result.height = header[14] | (header[15] << 8);
  result.bpp = header[16];

  if (result.bpp != 24 && result.bpp != 32) {
    std::cerr << "[TextureLoader] Unsupported OZT BPP: " << result.bpp
              << " in " << path << std::endl;
    return result;
  }

  int bytesPerPixel = result.bpp / 8;
  size_t pixelDataOffset = offset + 18;
  size_t expectedSize = (size_t)result.width * result.height * bytesPerPixel;

  if (pixelDataOffset >= full_data.size()) {
    std::cerr << "[TextureLoader] OZT pixel data offset past end of file: "
              << path << std::endl;
    return result;
  }

  unsigned char *pixel_data = &full_data[pixelDataOffset];

  // RLE decompression for type 10
  if (imageType == 10) {
    result.pixels.resize(expectedSize);
    size_t srcIdx = 0, dstIdx = 0;
    size_t srcSize = full_data.size() - pixelDataOffset;

    while (dstIdx < expectedSize && srcIdx < srcSize) {
      unsigned char packetHeader = pixel_data[srcIdx++];
      int count = (packetHeader & 0x7F) + 1;

      if (packetHeader & 0x80) {
        if (srcIdx + bytesPerPixel > srcSize)
          break;
        for (int i = 0; i < count && dstIdx + bytesPerPixel <= expectedSize;
             ++i) {
          memcpy(&result.pixels[dstIdx], &pixel_data[srcIdx], bytesPerPixel);
          dstIdx += bytesPerPixel;
        }
        srcIdx += bytesPerPixel;
      } else {
        size_t rawBytes = (size_t)count * bytesPerPixel;
        if (srcIdx + rawBytes > srcSize)
          break;
        if (dstIdx + rawBytes > expectedSize)
          break;
        memcpy(&result.pixels[dstIdx], &pixel_data[srcIdx], rawBytes);
        srcIdx += rawBytes;
        dstIdx += rawBytes;
      }
    }

    if (dstIdx != expectedSize) {
      std::cerr << "[TextureLoader] RLE decompression size mismatch in " << path
                << " (got " << dstIdx << ", expected " << expectedSize << ")"
                << std::endl;
      return result;
    }
  } else {
    result.pixels.assign(pixel_data,
                         pixel_data + std::min(expectedSize,
                                               full_data.size() - pixelDataOffset));
    if (result.pixels.size() < expectedSize)
      return result;
  }

  // V-flip: MU OZT data is always stored top-to-bottom (matching the
  // reference engine which unconditionally reverses row order with ny-1-y).
  // Flip to bottom-to-top for OpenGL's texture origin convention.
  // BGFX also gets the same flip to maintain identical UV mapping.
  {
    int rowSize = result.width * bytesPerPixel;
    std::vector<unsigned char> rowTemp(rowSize);
    for (int y = 0; y < result.height / 2; ++y) {
      unsigned char *topRow = result.pixels.data() + y * rowSize;
      unsigned char *botRow =
          result.pixels.data() + (result.height - 1 - y) * rowSize;
      memcpy(rowTemp.data(), topRow, rowSize);
      memcpy(topRow, botRow, rowSize);
      memcpy(botRow, rowTemp.data(), rowSize);
    }
  }

  result.valid = true;
  return result;
}

TexHandle TextureLoader::LoadOZT(const std::string &path) {
  auto parsed = ParseOZT(path);
  if (!parsed.valid)
    return kInvalidTex;

  int bytesPerPixel = parsed.bpp / 8;

  if (parsed.bpp == 32) {
    // Data is BGRA — upload with CPU-generated mipmaps
    return bgfx::createTexture2D(
        (uint16_t)parsed.width, (uint16_t)parsed.height, true, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC,
        GenerateMipmapBGRA8(parsed.pixels.data(), parsed.width, parsed.height));
  } else {
    // 24-bit BGR — convert to RGBA8
    std::vector<unsigned char> rgba(parsed.width * parsed.height * 4);
    for (int i = 0; i < parsed.width * parsed.height; ++i) {
      rgba[i * 4 + 0] = parsed.pixels[i * 3 + 2]; // R (from B)
      rgba[i * 4 + 1] = parsed.pixels[i * 3 + 1]; // G
      rgba[i * 4 + 2] = parsed.pixels[i * 3 + 0]; // B (from R)
      rgba[i * 4 + 3] = 255;
    }
    return bgfx::createTexture2D(
        (uint16_t)parsed.width, (uint16_t)parsed.height, true, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC,
        GenerateMipmapRGBA8(rgba.data(), parsed.width, parsed.height));
  }
}

std::vector<unsigned char> TextureLoader::LoadOZTRaw(const std::string &path,
                                                     int &width, int &height) {
  auto parsed = ParseOZT(path);
  if (!parsed.valid)
    return {};

  width = parsed.width;
  height = parsed.height;
  int bytesPerPixel = parsed.bpp / 8;

  // Convert BGR(A) to RGB(A) for consistency with LoadOZJRaw
  for (size_t i = 0; i + 2 < parsed.pixels.size(); i += bytesPerPixel)
    std::swap(parsed.pixels[i], parsed.pixels[i + 2]);

  return std::move(parsed.pixels);
}

TexHandle TextureLoader::LoadByExtension(const std::string &path) {
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find(".ozj") != std::string::npos ||
      lower.find(".jpg") != std::string::npos ||
      lower.find(".jpeg") != std::string::npos) {
    return LoadOZJ(path);
  }
  return LoadOZT(path);
}

static TextureLoadResult LoadByExtensionWithInfo(const std::string &path) {
  TextureLoadResult result;
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find(".ozj") != std::string::npos ||
      lower.find(".jpg") != std::string::npos ||
      lower.find(".jpeg") != std::string::npos) {
    result.textureID = TextureLoader::LoadOZJ(path);
    result.hasAlpha = false; // JPEG never has alpha
    return result;
  }
  result.textureID = TextureLoader::LoadOZT(path);
  if (TexValid(result.textureID)) {
    // Determine alpha from file format (works for both GL and BGFX)
    std::ifstream file(path, std::ios::binary);
    if (file) {
      std::vector<unsigned char> hdr(48);
      file.read(reinterpret_cast<char *>(hdr.data()), hdr.size());
      size_t offset = 0;
      if (hdr.size() > 24) {
        if (hdr[4 + 2] == 2 || hdr[4 + 2] == 10)
          offset = 4;
        else if (hdr[24 + 2] == 2 || hdr[24 + 2] == 10)
          offset = 24;
      }
      if (offset + 16 < hdr.size())
        result.hasAlpha = (hdr[offset + 16] == 32);
    }
  }
  return result;
}

// Case-insensitive file lookup in a directory (also resolves directory case)
static std::string FindFileCI(const std::string &dir,
                              const std::string &filename) {
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  std::string actualDir = dir;
  std::error_code ec;

  // If the exact directory doesn't exist, try to find a case-insensitive match
  // for the last component
  if (!fs::exists(actualDir, ec)) {
    std::string parentDir = fs::path(actualDir).parent_path().string();
    std::string targetFolder = fs::path(actualDir).filename().string();
    std::string targetLower = targetFolder;
    std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                   ::tolower);

    if (fs::exists(parentDir, ec)) {
      for (auto &entry : fs::directory_iterator(parentDir, ec)) {
        if (ec)
          break;
        if (entry.is_directory()) {
          std::string folderName = entry.path().filename().string();
          std::string folderLower = folderName;
          std::transform(folderLower.begin(), folderLower.end(),
                         folderLower.begin(), ::tolower);
          if (folderLower == targetLower) {
            actualDir = entry.path().string();
            break;
          }
        }
      }
    }
  }

  for (auto &entry : fs::directory_iterator(actualDir, ec)) {
    if (ec)
      break;

    if (!entry.is_regular_file())
      continue;
    std::string name = entry.path().filename().string();
    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                   ::tolower);
    if (nameLower == lower)
      return entry.path().string();
  }
  return "";
}

TexHandle TextureLoader::Resolve(const std::string &directory,
                              const std::string &bmdTextureName) {
  // Strip any Windows path prefix (e.g. "Data2\\Object1\\candle.jpg")
  std::string baseName = bmdTextureName;
  auto pos = baseName.find_last_of("\\/");
  if (pos != std::string::npos)
    baseName = baseName.substr(pos + 1);

  // Try exact filename first (case-insensitive)
  std::string found = FindFileCI(directory, baseName);
  if (!found.empty())
    return LoadByExtension(found);

  // Strip extension and try common MU texture extensions.
  // Prefer the same format family as the original extension so that
  // e.g. "tree_01.tga" resolves to tree_01.OZT (has alpha) not tree_01.OZJ.
  std::string stem = baseName;
  std::string origExt;
  auto dotPos = stem.find_last_of('.');
  if (dotPos != std::string::npos) {
    origExt = stem.substr(dotPos);
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::tolower);
    stem = stem.substr(0, dotPos);
  }

  const char *tgaExts[] = {".OZT", ".ozt", ".tga", ".TGA"};
  const char *jpgExts[] = {".OZJ", ".ozj", ".jpg", ".JPG"};
  bool preferTGA =
      (origExt == ".tga" || origExt == ".ozt" || origExt == ".bmp");

  auto tryExts = [&](const char *list[], int count) -> TexHandle {
    for (int i = 0; i < count; ++i) {
      found = FindFileCI(directory, stem + list[i]);
      if (!found.empty())
        return LoadByExtension(found);
    }
    return kInvalidTex;
  };

  TexHandle result;
  if (preferTGA) {
    result = tryExts(tgaExts, 4);
    if (!TexValid(result))
      result = tryExts(jpgExts, 4);
  } else {
    result = tryExts(jpgExts, 4);
    if (!TexValid(result))
      result = tryExts(tgaExts, 4);
  }
  if (TexValid(result))
    return result;

  std::cerr << "[TextureLoader::Resolve] Could not find texture: " << baseName
            << " in " << directory << std::endl;
  return kInvalidTex;
}

TextureScriptFlags
TextureLoader::ParseScriptFlags(const std::string &textureName) {
  TextureScriptFlags flags;

  std::string name = textureName;
  auto slashPos = name.find_last_of("\\/");
  if (slashPos != std::string::npos)
    name = name.substr(slashPos + 1);

  auto dotPos = name.find_last_of('.');
  if (dotPos != std::string::npos)
    name = name.substr(0, dotPos);

  auto underPos = name.find_last_of('_');
  if (underPos == std::string::npos || underPos == name.size() - 1)
    return flags;

  std::string suffix = name.substr(underPos + 1);

  for (char c : suffix) {
    char cu = std::toupper(c);
    if (cu != 'R' && cu != 'H' && cu != 'N' && cu != 'S')
      return flags; // Not a script suffix
  }

  for (char c : suffix) {
    switch (std::toupper(c)) {
    case 'R':
      flags.bright = true;
      break;
    case 'H':
      flags.hidden = true;
      break;
    case 'N':
      flags.noneBlend = true;
      break;
    case 'S':
      flags.streamMesh = true;
      break;
    }
  }

  return flags;
}

TextureLoadResult
TextureLoader::ResolveWithInfo(const std::string &directory,
                               const std::string &bmdTextureName) {
  std::string baseName = bmdTextureName;
  auto pos = baseName.find_last_of("\\/");
  if (pos != std::string::npos)
    baseName = baseName.substr(pos + 1);

  std::string found = FindFileCI(directory, baseName);
  if (!found.empty())
    return LoadByExtensionWithInfo(found);

  std::string stem = baseName;
  std::string origExt;
  auto dotPos = stem.find_last_of('.');
  if (dotPos != std::string::npos) {
    origExt = stem.substr(dotPos);
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::tolower);
    stem = stem.substr(0, dotPos);
  }

  const char *tgaExts[] = {".OZT", ".ozt", ".tga", ".TGA"};
  const char *jpgExts[] = {".OZJ", ".ozj", ".jpg", ".JPG"};
  bool preferTGA =
      (origExt == ".tga" || origExt == ".ozt" || origExt == ".bmp");

  auto tryExts = [&](const char *list[], int count) -> TextureLoadResult {
    for (int i = 0; i < count; ++i) {
      found = FindFileCI(directory, stem + list[i]);
      if (!found.empty())
        return LoadByExtensionWithInfo(found);
    }
    return {kInvalidTex, false};
  };

  // Main 5.2: always try OZT first — if it exists, it has alpha data that matters
  TextureLoadResult result;
  result = tryExts(tgaExts, 4);
  if (!TexValid(result.textureID))
    result = tryExts(jpgExts, 4);
  if (TexValid(result.textureID))
    return result;

  std::cerr << "[TextureLoader::Resolve] Could not find texture: " << baseName
            << " in " << directory << std::endl;
  return {kInvalidTex, false};
}

#ifndef TEXTURE_LOADER_HPP
#define TEXTURE_LOADER_HPP

#include <bgfx/bgfx.h>
#include <string>
#include <vector>

// Unified texture handle type for BGFX backend
using TexHandle = bgfx::TextureHandle;
inline bool TexValid(TexHandle h) { return bgfx::isValid(h); }
inline void TexDestroy(TexHandle &h) {
  if (bgfx::isValid(h)) {
    bgfx::destroy(h);
    h = BGFX_INVALID_HANDLE;
  }
}
inline constexpr TexHandle kInvalidTex = BGFX_INVALID_HANDLE;

// Convert TexHandle to integer suitable for (ImTextureID) cast
inline uintptr_t TexImID(TexHandle h) { return (uintptr_t)h.idx; }

struct TextureLoadResult {
  TexHandle textureID = kInvalidTex;
  bool hasAlpha = false; // true if RGBA/32-bit
};

// Rendering flags parsed from MU Online texture name suffixes.
// Pattern: "basename_FLAGS.ext" where FLAGS is a combination of R, H, N, S.
struct TextureScriptFlags {
  bool bright = false;    // _R: additive blending
  bool hidden = false;    // _H: skip rendering
  bool noneBlend = false; // _N: disable blending, render opaque
};

class TextureLoader {
public:
  static TexHandle LoadOZJ(const std::string &path);
  static std::vector<unsigned char> LoadOZJRaw(const std::string &path,
                                               int &width, int &height);
  static TexHandle LoadOZT(const std::string &path);
  static std::vector<unsigned char> LoadOZTRaw(const std::string &path,
                                               int &width, int &height);

  // Load texture by extension (OZJ/JPG -> LoadOZJ, OZT/TGA -> LoadOZT)
  static TexHandle LoadByExtension(const std::string &path);

  // Resolve a BMD texture name to an actual file and load it.
  // Handles path prefixes, extension variants, and case-insensitive lookup.
  static TexHandle Resolve(const std::string &directory,
                        const std::string &bmdTextureName);

  // Resolve and return alpha info alongside the texture handle.
  static TextureLoadResult ResolveWithInfo(const std::string &directory,
                                           const std::string &bmdTextureName);

  // Parse texture script flags from a texture filename.
  static TextureScriptFlags ParseScriptFlags(const std::string &textureName);
};

#endif

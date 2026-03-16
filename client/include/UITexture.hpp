#ifndef UI_TEXTURE_HPP
#define UI_TEXTURE_HPP

#include "TextureLoader.hpp" // TexHandle, TexValid, kInvalidTex
#include <string>

// Texture handle with tracked dimensions and format info.
struct UITexture {
    TexHandle id = kInvalidTex;
    int width = 0;
    int height = 0;
    bool isOZT = false;   // OZT textures are V-flipped for 3D; need flip back for ImGui
    bool hasAlpha = false;

    // Load a texture from file, auto-detecting format by extension.
    // Supports: .ozj, .jpg, .jpeg -> JPEG loader
    //           .ozt, .tga        -> TGA loader (with V-flip)
    //           .png              -> PNG loader (via stb_image)
    static UITexture Load(const std::string& path);

    void Destroy();

    explicit operator bool() const { return TexValid(id); }
};

#endif // UI_TEXTURE_HPP

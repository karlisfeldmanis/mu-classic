#include "ChromeGlow.hpp"
#include "BMDUtils.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

namespace ChromeGlow {

static Textures s_tex;

// ─── Main 5.2 PartObjectColor: per-item-type glow colors (ZzzObject.cpp:6483-6768) ───
// Maps (category, itemIndex) to the glow RGB used for chrome enhancement passes.
// Color table has 44 entries; most items default to Color 0 (warm gold).
glm::vec3 GetPartObjectColor(int category, int itemIndex) {
  static const glm::vec3 kColors[] = {
    {1.0f, 0.5f, 0.0f},   //  0: default gold
    {1.0f, 0.2f, 0.0f},   //  1: red-orange
    {0.0f, 0.5f, 1.0f},   //  2: blue
    {0.0f, 0.5f, 1.0f},   //  3: blue (alt)
    {0.0f, 0.8f, 0.4f},   //  4: teal
    {1.0f, 1.0f, 1.0f},   //  5: white
    {0.6f, 0.8f, 0.4f},   //  6: green-gray
    {0.9f, 0.8f, 1.0f},   //  7: lavender
    {0.8f, 0.8f, 1.0f},   //  8: light blue
    {0.5f, 0.5f, 0.8f},   //  9: muted blue
    {0.75f,0.65f,0.5f},   // 10: warm beige
    {0.35f,0.35f,0.6f},   // 11: dusk blue
    {0.47f,0.67f,0.6f},   // 12: seafoam
    {0.0f, 0.3f, 0.6f},   // 13: deep blue
    {0.65f,0.65f,0.55f},  // 14: sandy
    {0.2f, 0.3f, 0.6f},   // 15: dark blue
    {0.8f, 0.46f,0.25f},  // 16: copper
    {0.65f,0.45f,0.3f},   // 17: bronze
    {0.5f, 0.4f, 0.3f},   // 18: dark bronze
    {0.37f,0.37f,1.0f},   // 19: vivid blue
    {0.3f, 0.7f, 0.3f},   // 20: green
    {0.5f, 0.4f, 1.0f},   // 21: purple-blue
    {0.45f,0.45f,0.23f},  // 22: olive
    {0.3f, 0.3f, 0.45f},  // 23: slate
    {0.6f, 0.5f, 0.2f},   // 24: amber
    {0.6f, 0.6f, 0.6f},   // 25: silver
    {0.3f, 0.7f, 0.3f},   // 26: green (alt)
    {0.5f, 0.6f, 0.7f},   // 27: steel blue
    {0.45f,0.45f,0.23f},  // 28: olive (alt)
    {0.2f, 0.7f, 0.3f},   // 29: emerald
    {0.7f, 0.3f, 0.3f},   // 30: crimson
    {0.7f, 0.5f, 0.3f},   // 31: warm orange
    {0.5f, 0.2f, 0.7f},   // 32: purple
    {0.8f, 0.4f, 0.6f},   // 33: pink
    {0.6f, 0.4f, 0.8f},   // 34: violet
    {0.7f, 0.4f, 0.4f},   // 35: dusty rose
    {0.5f, 0.5f, 0.7f},   // 36: periwinkle
    {0.7f, 0.5f, 0.7f},   // 37: mauve
    {0.2f, 0.4f, 0.7f},   // 38: royal blue
    {0.3f, 0.6f, 0.4f},   // 39: sage
    {0.7f, 0.2f, 0.2f},   // 40: dark red
    {0.7f, 0.2f, 0.7f},   // 41: magenta
    {0.8f, 0.4f, 0.0f},   // 42: dark gold
    {0.8f, 0.6f, 0.2f},   // 43: golden
  };
  static const int kNumColors = sizeof(kColors) / sizeof(kColors[0]);

  int color = 0;

  // Armor categories 7-11 (helm, armor, pants, gloves, boots)
  // Main 5.2 ZzzObject.cpp lines 6661-6718
  if (category >= 7 && category <= 11) {
    switch (itemIndex) {
    case 1:  color = 1;  break;
    case 3:  color = 3;  break;
    case 4:  color = 5;  break;
    case 6:  color = 6;  break;
    case 9:  color = 2;  break;
    case 12: color = 2;  break;
    case 13: color = 4;  break;
    case 14: color = 5;  break;
    case 15: color = 7;  break;
    case 16: color = 10; break;
    case 17: color = 9;  break;
    case 18: color = 5;  break;
    case 19: color = 9;  break;
    case 20: color = 9;  break;
    case 21: color = 16; break;
    case 22: color = 17; break;
    case 23: color = 11; break;
    case 24: color = 16; break;
    case 25: color = 11; break;
    case 26: color = 12; break;
    case 27: color = 10; break;
    case 28: color = 15; break;
    case 29: color = 18; break;
    case 30: color = 19; break;
    case 31: color = 20; break;
    case 32: color = 21; break;
    case 33: color = 22; break;
    case 34: color = 24; break;
    case 35: color = 25; break;
    case 36: color = 26; break;
    case 37: color = 27; break;
    case 38: color = 28; break;
    case 39: color = 29; break;
    case 40: color = 30; break;
    case 41: color = 31; break;
    case 42: color = 32; break;
    case 43: color = 33; break;
    case 44: color = 34; break;
    case 45: color = 36; break;
    case 46: color = 42; break;
    case 47: color = 37; break;
    default: color = 0;  break;
    }
  }
  // Weapon-specific overrides (Main 5.2 ZzzObject.cpp lines 6487-6658)
  else if (category == 0) { // Swords
    switch (itemIndex) {
    case 14: color = 2;  break;
    case 20: color = 10; break;
    case 21: color = 5;  break;
    case 22: color = 18; break;
    case 23: color = 23; break;
    case 24: color = 24; break;
    case 25: color = 27; break;
    case 28: color = 8;  break;
    case 31: color = 10; break;
    default: color = 0;  break;
    }
  } else if (category == 1) { // Axes
    color = 0;
  } else if (category == 2) { // Maces
    switch (itemIndex) {
    case 8:  color = 9;  break;
    case 9:  color = 10; break;
    case 10: color = 12; break;
    case 12: color = 16; break;
    case 14: color = 22; break;
    case 15: color = 28; break;
    case 17: color = 40; break;
    case 18: color = 5;  break;
    default: color = 0;  break;
    }
  } else if (category == 3) { // Spears
    switch (itemIndex) {
    case 9:  color = 1;  break;
    case 10: color = 9;  break;
    case 11: color = 20; break;
    default: color = 0;  break;
    }
  } else if (category == 4) { // Bows
    switch (itemIndex) {
    case 5:  color = 5;  break;
    case 7:  color = 0;  break;
    case 13: color = 5;  break;
    case 15: color = 0;  break;
    case 17: color = 9;  break;
    case 18: color = 10; break;
    case 19: color = 9;  break;
    case 20: color = 16; break;
    case 21: color = 20; break;
    case 22: color = 26; break;
    case 23: color = 35; break;
    case 24: color = 36; break;
    default: color = 0;  break;
    }
  } else if (category == 5) { // Staffs
    switch (itemIndex) {
    case 5:  color = 2;  break;
    case 9:  color = 5;  break;
    case 11: color = 17; break;
    case 12: color = 19; break;
    case 13: color = 25; break;
    case 14: color = 24; break;
    case 15: color = 15; break;
    case 16: color = 1;  break;
    case 17: color = 3;  break;
    case 18: color = 30; break;
    case 19: color = 21; break;
    case 20: color = 5;  break;
    case 22: color = 1;  break;
    case 30: color = 1;  break;
    case 31: color = 19; break;
    case 33: color = 43; break;
    case 34: color = 5;  break;
    default: color = 0;  break;
    }
  } else if (category == 6) { // Shields
    switch (itemIndex) {
    case 16: color = 6;  break;
    case 19: color = 29; break;
    case 20: color = 36; break;
    case 21: color = 30; break;
    default: color = 0;  break;
    }
  }

  if (color < 0 || color >= kNumColors) color = 0;
  return kColors[color];
}

// Main 5.2 PartObjectColor2: secondary color modulator for CHROME2/CHROME4 passes
// ZzzObject.cpp lines 6771-6853
glm::vec3 GetPartObjectColor2(int category, int itemIndex) {
  static const glm::vec3 kColors2[] = {
    {1.0f, 1.0f, 1.0f}, // 0: neutral
    {1.0f, 0.5f, 0.0f}, // 1: warm orange
    {0.0f, 0.5f, 1.0f}, // 2: cyan/blue
    {1.0f, 1.0f, 1.0f}, // 3: pure white
  };

  int color = -1; // -1 = use primary color (PartObjectColor)

  if (category >= 7 && category <= 11) {
    switch (itemIndex) {
    case 4:  color = 1; break;
    case 14: color = 1; break;
    case 15: color = 1; break;
    case 17: color = 1; break;
    case 18: color = 2; break;
    case 21: color = 3; break;
    case 39: color = 1; break;
    case 40: color = 1; break;
    case 41: color = 1; break;
    case 42: color = 1; break;
    case 43: color = 2; break;
    case 44: color = 3; break;
    default: color = -1; break; // Use primary color for CHROME2/CHROME4
    }
  } else if (category == 0) {
    if (itemIndex == 14) color = 2;
    else color = -1;
  } else if (category == 4) {
    if (itemIndex == 5 || itemIndex == 13) color = 2;
    else color = -1;
  } else if (category == 5) {
    if (itemIndex == 5) color = 2;
    else color = -1;
  } else {
    color = -1;
  }

  // Main 5.2 PartObjectColor2 Color=0 multiplies existing BodyLight (which has
  // the primary color). Our shader uses scene lighting instead. To match, fall
  // back to the primary color so CHROME2/CHROME4 pass inherits the item's tint.
  if (color < 0)
    return GetPartObjectColor(category, itemIndex);
  return kColors2[color];
}

void LoadTextures(const std::string &dataPath) {
  if (TexValid(s_tex.chrome1)) return;
  s_tex.chrome1 = TextureLoader::LoadOZJ(dataPath + "/Effect/Chrome01.OZJ");
  s_tex.chrome2 = TextureLoader::LoadOZJ(dataPath + "/Effect/Chrome02.OZJ");
  s_tex.shiny   = TextureLoader::LoadOZJ(dataPath + "/Effect/Shiny01.OZJ");
  std::cout << "[ChromeGlow] Textures loaded: chrome1="
            << (TexValid(s_tex.chrome1) ? 1 : 0)
            << " chrome2=" << (TexValid(s_tex.chrome2) ? 1 : 0)
            << " shiny=" << (TexValid(s_tex.shiny) ? 1 : 0) << std::endl;
}

void DeleteTextures() {
  TexDestroy(s_tex.chrome1);
  TexDestroy(s_tex.chrome2);
  TexDestroy(s_tex.shiny);
}

const Textures &GetTextures() { return s_tex; }

static TexHandle TextureForMode(int chromeMode) {
  if (chromeMode == 2 || chromeMode == 4) return s_tex.chrome2;
  if (chromeMode == 3) return s_tex.shiny;
  return s_tex.chrome1;
}

int GetGlowPasses(int enhanceLevel, int category, int itemIndex,
                  GlowPass *outPasses) {
  if (enhanceLevel < 7 || !TexValid(s_tex.chrome1)) return 0;

  // Main 5.2 ZzzObject.cpp RenderPartObjectEffect:
  // +7:  1 pass  = CHROME  (Chrome01.OZJ)
  // +9:  2 passes = CHROME + METAL (Chrome01 + Shiny01)
  // +11: 3 passes = CHROME2 + METAL + CHROME (Chrome02 + Shiny01 + Chrome01)
  // +13: 3 passes = CHROME4 + METAL + CHROME (Chrome02 + Shiny01 + Chrome01)
  int n = 0;
  if (enhanceLevel >= 13) {
    outPasses[0] = {4, TextureForMode(4), GetPartObjectColor2(category, itemIndex)};
    outPasses[1] = {3, TextureForMode(3), GetPartObjectColor(category, itemIndex)};
    outPasses[2] = {1, TextureForMode(1), GetPartObjectColor(category, itemIndex)};
    n = 3;
  } else if (enhanceLevel >= 11) {
    outPasses[0] = {2, TextureForMode(2), GetPartObjectColor2(category, itemIndex)};
    outPasses[1] = {3, TextureForMode(3), GetPartObjectColor(category, itemIndex)};
    outPasses[2] = {1, TextureForMode(1), GetPartObjectColor(category, itemIndex)};
    n = 3;
  } else if (enhanceLevel >= 9) {
    outPasses[0] = {1, TextureForMode(1), GetPartObjectColor(category, itemIndex)};
    outPasses[1] = {3, TextureForMode(3), GetPartObjectColor(category, itemIndex)};
    n = 2;
  } else {
    outPasses[0] = {1, TextureForMode(1), GetPartObjectColor(category, itemIndex)};
    n = 1;
  }
  return n;
}

void RenderGlowMeshes(Shader *shader,
                      const std::vector<::MeshBuffers> &meshes,
                      const GlowRenderParams &params) {
  if (!shader || params.enhanceLevel < 7) return;
  if (!TexValid(s_tex.chrome1)) return;

  GlowPass passes[3];
  int n = GetGlowPasses(params.enhanceLevel, params.category,
                        params.itemIndex, passes);
  if (n <= 0) return;

  uint64_t glowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                     | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ADD;

  for (int gp = 0; gp < n; ++gp) {
    glm::vec3 color = passes[gp].color;
    if (params.colorScale != 1.0f)
      color *= params.colorScale;

    // Glow uniforms: objectAlpha=1, bml=1, chromeMode, chromeTime
    // IMPORTANT: Set ALL uniforms to prevent BGFX uniform persistence bugs
    shader->setVec4("u_params", glm::vec4(1.0f, 1.0f,
                    (float)passes[gp].chromeMode, params.time));
    shader->setVec4("u_params2", glm::vec4(params.luminosity, 0.0f, 0.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(params.viewPos, 0.0f));
    shader->setVec4("u_lightPos", glm::vec4(params.viewPos + glm::vec3(0, 500, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(color, 0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    shader->setVec4("u_shadowParams", glm::vec4(0.0f));

    for (auto &mb : meshes) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      if (params.skipBlendMesh >= 0 && mb.bmdTextureId == params.skipBlendMesh)
        continue;

      bgfx::setTransform(glm::value_ptr(*params.modelMat));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      shader->setTexture(0, "s_texColor", passes[gp].texture);
      bgfx::setState(glowState);
      bgfx::submit(params.viewId, shader->program);
    }
  }
}

} // namespace ChromeGlow

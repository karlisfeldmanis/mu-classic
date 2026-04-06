#pragma once

#include "TextureLoader.hpp" // TexHandle, TexValid, TexDestroy
#include "Shader.hpp"
#include "MeshBuffers.hpp"
#include <glm/glm.hpp>
#include <string>

// Consolidated chrome/metal glow rendering for enhanced items (+7/+9/+11/+13).
// Main 5.2 ZzzObject.cpp RenderPartObjectEffect + PartObjectColor/PartObjectColor2.
// Replaces duplicated glow code across HeroCharacter, CharacterSelect,
// ItemModelManager, and NpcManager.
namespace ChromeGlow {

struct Textures {
  TexHandle chrome1 = kInvalidTex;
  TexHandle chrome2 = kInvalidTex;
  TexHandle shiny   = kInvalidTex;
};

struct GlowPass {
  int chromeMode; // 1=CHROME, 2=CHROME2, 3=METAL, 4=CHROME4
  TexHandle texture = kInvalidTex;
  glm::vec3 color;
};

// Load/delete the 3 shared environment-map textures (call once at init/shutdown)
void LoadTextures(const std::string &dataPath);
void DeleteTextures();
const Textures &GetTextures();

// Main 5.2 PartObjectColor: 44-color palette for CHROME/METAL passes
glm::vec3 GetPartObjectColor(int category, int itemIndex);
// Main 5.2 PartObjectColor2: 4-color modulator for CHROME2/CHROME4 passes
glm::vec3 GetPartObjectColor2(int category, int itemIndex);

// Fill outPasses with the glow pass definitions for a given enhancement level.
// Returns number of passes (0 if level < 7, 1-3 otherwise).
// Each pass has chromeMode, texture, and color already resolved.
int GetGlowPasses(int enhanceLevel, int category, int itemIndex,
                  GlowPass *outPasses);

// ── Centralized glow rendering helper ──
// Renders all glow passes for a set of mesh buffers at the given enhancement
// level. This replaces the duplicated glow rendering loops in HeroCharacter,
// CharacterSelect, NpcManager, and ItemModelManager.
//
// Parameters:
//   shader       - The model shader to use
//   meshes       - Vector of mesh buffers to render with glow
//   modelMat     - Model transform matrix
//   enhanceLevel - Item enhancement level (+7, +9, +11, +13)
//   category     - Item category (0-14)
//   itemIndex    - Item index within category
//   viewId       - BGFX view ID to submit to
//   time         - Current time for chrome animation (glfwGetTime)
//   skipBlendMesh- BlendMesh index to skip (-1 = skip none)
//   colorScale   - Per-pass color multiplier (1.0 = normal, <1.0 for weapons)
struct GlowRenderParams {
  const glm::mat4 *modelMat;
  int enhanceLevel;
  int category;
  int itemIndex;
  uint8_t viewId;
  float time;
  int skipBlendMesh;    // -1 = don't skip any
  float colorScale;     // 1.0 = normal
  glm::vec3 viewPos;    // Camera position (for u_viewPos, u_lightPos)
  float luminosity;     // Map luminosity (for u_params2)
};

void RenderGlowMeshes(Shader *shader,
                      const std::vector<::MeshBuffers> &meshes,
                      const GlowRenderParams &params);

} // namespace ChromeGlow

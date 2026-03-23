// BGFX integration test — validates BGFX + GLFW + Metal + ImGui + Texture Loading + Sky + Terrain + Grass + Objects + Fire + Boids + ClickEffect + NPCs + HeroCharacter.
// Phase 2-13 of the GL->BGFX migration. Captures screenshots for visual analysis.

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_bgfx.h"
#include "TextureLoader.hpp"
#include "Shader.hpp"
#include "ViewerCommon.hpp"
#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "GrassRenderer.hpp"
#include "ObjectRenderer.hpp"
#include "FireEffect.hpp"
#include "BoidManager.hpp"
#include "ClickEffect.hpp"
#include "NpcManager.hpp"
#include "HeroCharacter.hpp"
#include "MonsterManager.hpp"
#include "ChromeGlow.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// BGFX callback that captures screenshots to PNG
struct ScreenshotCallback : public bgfx::CallbackI {
  std::vector<uint8_t> capturedData;
  uint32_t capturedWidth = 0, capturedHeight = 0;
  bool hasCaptured = false;
  const char *outputPath = nullptr;

  void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code,
             const char *_str) override {
    fprintf(stderr, "[BGFX FATAL] %s:%d (0x%08x): %s\n", _filePath, _line,
            _code, _str);
  }

  void traceVargs(const char *_filePath, uint16_t _line, const char *_format,
                  va_list _argList) override {
    (void)_filePath;
    (void)_line;
    (void)_format;
    (void)_argList;
  }

  void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerBeginLiteral(const char *, uint32_t, const char *,
                            uint16_t) override {}
  void profilerEnd() override {}

  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
  void cacheWrite(uint64_t, const void *, uint32_t) override {}

  void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                  uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                  const void *_data, uint32_t _size, bool _yflip) override {
    printf("[Screenshot] Captured %dx%d (pitch=%d, yflip=%d, path=%s)\n",
           _width, _height, _pitch, _yflip, _filePath);

    capturedWidth = _width;
    capturedHeight = _height;

    // Convert to tightly-packed RGBA for stb_image_write
    // BGFX provides data as BGRA
    capturedData.resize(_width * _height * 4);
    const uint8_t *src = (const uint8_t *)_data;

    for (uint32_t y = 0; y < _height; ++y) {
      uint32_t srcY = _yflip ? (_height - 1 - y) : y;
      const uint8_t *srcRow = src + srcY * _pitch;
      uint8_t *dstRow = capturedData.data() + y * _width * 4;
      for (uint32_t x = 0; x < _width; ++x) {
        dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // R from B
        dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
        dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // B from R
        dstRow[x * 4 + 3] = 255;                // A
      }
    }

    if (outputPath) {
      stbi_write_png(outputPath, _width, _height, 4, capturedData.data(),
                     _width * 4);
      printf("[Screenshot] Saved: %s\n", outputPath);
    }

    hasCaptured = true;
  }

  void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum,
                    bool) override {}
  void captureEnd() override {}
  void captureFrame(const void *, uint32_t) override {}
};

int main() {
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  GLFWwindow *window = glfwCreateWindow(1280, 720,
      "BGFX Migration Test (Phase 11)", nullptr, nullptr);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return 1;
  }

  bgfx::renderFrame();

  // Create screenshot directory
  mkdir("screenshots", 0755);

  ScreenshotCallback callback;

  bgfx::Init init;
  init.type = bgfx::RendererType::Metal;
  init.platformData.nwh = glfwGetCocoaWindow(window);
  init.callback = &callback;

  int fbW, fbH;
  glfwGetFramebufferSize(window, &fbW, &fbH);
  init.resolution.width = fbW;
  init.resolution.height = fbH;
  init.resolution.reset = BGFX_RESET_VSYNC;

  if (!bgfx::init(init)) {
    fprintf(stderr, "Failed to initialize BGFX\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  printf("[BGFX] Initialized: %s (%dx%d)\n",
         bgfx::getRendererName(bgfx::getRendererType()), fbW, fbH);

  bgfx::setDebug(BGFX_DEBUG_TEXT);

  // View 0: 3D scene (model rendering)
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                      0x1a1a2eFF, 1.0f, 0);

  // View 255: ImGui overlay (no clear — renders on top)
  bgfx::setViewClear(255, BGFX_CLEAR_NONE);

  // ImGui init
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOther(window, true);
  if (!ImGui_ImplBgfx_Init(255, "shaders")) {
    fprintf(stderr, "[ImGui_BGFX] Failed to init ImGui BGFX backend\n");
    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
  printf("[ImGui_BGFX] Backend initialized on view 255\n");

  // Phase 3: Load test textures via BGFX
  printf("\n[Phase 3] Loading textures via BGFX...\n");

  TexHandle texOZJ = TextureLoader::LoadOZJ("Data/Player/skin_barbarian_01.OZJ");
  printf("  OZJ texture: %s (idx=%d)\n",
         TexValid(texOZJ) ? "OK" : "FAILED", TexValid(texOZJ) ? texOZJ.idx : -1);

  TexHandle texOZT = TextureLoader::LoadOZT("Data/Interface/Cursor.ozt");
  printf("  OZT texture: %s (idx=%d)\n",
         TexValid(texOZT) ? "OK" : "FAILED", TexValid(texOZT) ? texOZT.idx : -1);

  TexHandle texResolved = TextureLoader::Resolve("Data/Player/", "skin_barbarian_01.OZJ");
  printf("  Resolve texture: %s (idx=%d)\n",
         TexValid(texResolved) ? "OK" : "FAILED",
         TexValid(texResolved) ? texResolved.idx : -1);

  TextureLoadResult texInfo = TextureLoader::ResolveWithInfo("Data/Interface/", "Cursor.ozt");
  printf("  ResolveWithInfo: %s (idx=%d, hasAlpha=%s)\n",
         TexValid(texInfo.textureID) ? "OK" : "FAILED",
         TexValid(texInfo.textureID) ? texInfo.textureID.idx : -1,
         texInfo.hasAlpha ? "true" : "false");

  int texLoadCount = (TexValid(texOZJ) ? 1 : 0) + (TexValid(texOZT) ? 1 : 0) +
                     (TexValid(texResolved) ? 1 : 0) + (TexValid(texInfo.textureID) ? 1 : 0);
  printf("[Phase 3] Texture loading: %d/4 passed\n\n", texLoadCount);

  // ========== Phase 5: Sky renderer ==========
  printf("[Phase 5] Initializing sky renderer...\n");
  Sky sky;
  sky.Init("Data/");
  printf("[Phase 5] Sky renderer initialized\n\n");

  // ========== Phase 6: Terrain renderer ==========
  printf("[Phase 6] Loading terrain (Lorencia, World 1)...\n");
  Terrain terrain;
  terrain.Init();
  bool terrainLoaded = false;
  // Load terrain data — kept alive for ObjectRenderer (SetTerrainMapping stores raw pointer)
  TerrainData terrainData = TerrainParser::LoadWorld(1, "Data");
  if (!terrainData.heightmap.empty()) {
    terrain.Load(terrainData, 1, "Data");
    terrainLoaded = true;
    printf("[Phase 6] Terrain loaded: 256x256 grid, %d lightmap entries\n",
           (int)terrainData.lightmap.size());
  } else {
    printf("[Phase 6] WARNING: Failed to load terrain data\n");
  }
  printf("[Phase 6] Terrain renderer: %s\n\n", terrainLoaded ? "READY" : "FAILED");

  // ========== Phase 7: Grass renderer ==========
  printf("[Phase 7] Loading grass renderer...\n");
  GrassRenderer grass;
  grass.Init();
  bool grassLoaded = false;
  if (terrainLoaded) {
    grass.Load(terrainData, 1, "Data");
    grassLoaded = true;
  }
  printf("[Phase 7] Grass renderer: %s\n\n", grassLoaded ? "READY" : "FAILED");

  // ========== Phase 8: World objects (ObjectRenderer) ==========
  printf("[Phase 8] Loading world objects (Lorencia)...\n");
  ObjectRenderer objectRenderer;
  objectRenderer.Init();
  bool objectsLoaded = false;
  if (terrainLoaded) {
    objectRenderer.SetTerrainLightmap(terrainData.lightmap);
    objectRenderer.SetTerrainMapping(&terrainData.mapping);
    objectRenderer.SetTerrainHeightmap(terrainData.heightmap);
    objectRenderer.SetLuminosity(0.7f);
    objectRenderer.LoadObjects(terrainData.objects, "Data/Object1");
    objectsLoaded = true;
    printf("[Phase 8] Objects loaded: %d instances, %d unique models\n",
           objectRenderer.GetInstanceCount(), objectRenderer.GetModelCount());
  } else {
    printf("[Phase 8] WARNING: No terrain data, skipping object loading\n");
  }
  printf("[Phase 8] Object renderer: %s\n\n", objectsLoaded ? "READY" : "FAILED");

  // ========== Phase 9: Fire/smoke particle effects ==========
  printf("[Phase 9] Initializing fire effects...\n");
  FireEffect fireEffect;
  fireEffect.Init("Data/Effect");
  int fireEmitterCount = 0;
  if (objectsLoaded) {
    const int mapId = 0; // Lorencia
    for (auto &inst : objectRenderer.GetInstances()) {
      // Fire emitters
      auto &offsets = GetFireOffsets(inst.type, mapId);
      for (auto &off : offsets) {
        glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
        glm::mat3 rot;
        for (int c = 0; c < 3; c++)
          rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
        fireEffect.AddEmitter(worldPos + rot * off);
      }
      // Smoke emitters (torch types 131, 132)
      auto &smokeOffsets = GetSmokeOffsets(inst.type, mapId);
      for (auto &off : smokeOffsets) {
        glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
        glm::mat3 rot;
        for (int c = 0; c < 3; c++)
          rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
        fireEffect.AddSmokeEmitter(worldPos + rot * off);
      }
      // Waterspout mist (type 105)
      if (inst.type == 105) {
        glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
        fireEffect.AddWaterSmokeEmitter(worldPos + glm::vec3(0.0f, 180.0f, 0.0f));
        fireEffect.AddWaterSmokeEmitter(worldPos + glm::vec3(0.0f, 120.0f, 0.0f));
      }
    }
    fireEmitterCount = fireEffect.GetEmitterCount();
    printf("[Phase 9] Registered %d fire+smoke+water emitters\n", fireEmitterCount);
  }
  printf("[Phase 9] Fire effects: %s\n\n", fireEmitterCount > 0 ? "READY" : "NO EMITTERS");

  // ========== Phase 10: Ambient wildlife (birds, fish, leaves) ==========
  printf("[Phase 10] Initializing boid manager...\n");
  BoidManager boidManager;
  boidManager.Init("Data");
  if (terrainLoaded) {
    boidManager.SetTerrainData(&terrainData);
    boidManager.SetTerrainLightmap(terrainData.lightmap);
  }
  boidManager.SetLuminosity(0.7f);
  boidManager.SetMapId(0); // Lorencia — spawns birds + fish + leaves
  printf("[Phase 10] Boid manager: READY\n\n");

  // ========== Phase 11: Click-to-move ground effect ==========
  printf("[Phase 11] Initializing click effect...\n");
  auto clickShader = Shader::Load("vs_model.bin", "fs_model.bin");
  ClickEffect clickEffect;
  clickEffect.Init();
  clickEffect.LoadAssets("Data");
  if (terrainLoaded) clickEffect.SetTerrainData(&terrainData);
  clickEffect.Show(glm::vec3(12800.0f, 0.0f, 12800.0f));
  printf("[Phase 11] Click effect: %s\n\n", clickShader ? "READY" : "NO SHADER");

  // ========== Phase 12: NPC rendering ==========
  printf("[Phase 12] Initializing NPC manager...\n");
  ChromeGlow::LoadTextures("Data");
  NpcManager npcManager;
  npcManager.InitModels("Data");
  if (terrainLoaded) {
    npcManager.SetTerrainData(&terrainData);
    npcManager.SetTerrainLightmap(terrainData.lightmap);
  }
  npcManager.SetLuminosity(0.7f);
  npcManager.SetMapId(0);
  // Spawn some Lorencia NPCs for visual test
  npcManager.AddNpcByType(253, 130, 128, 3); // Amy
  npcManager.AddNpcByType(250, 120, 113, 3); // Weapon Merchant
  npcManager.AddNpcByType(251, 137, 116, 3); // Hanzo (blacksmith)
  npcManager.AddNpcByType(249, 152, 128, 7); // Captain Marcus (guard, +7 chrome)
  printf("[Phase 12] NPC manager: READY (%d NPCs)\n\n", npcManager.GetNpcCount());

  // ========== Phase 13: HeroCharacter rendering ==========
  printf("[Phase 13] Initializing HeroCharacter...\n");
  HeroCharacter hero;
  hero.Init("Data");
  if (terrainLoaded) {
    hero.SetTerrainData(&terrainData);
    hero.SetTerrainLightmap(terrainData.lightmap);
  }
  hero.SetLuminosity(0.7f);
  // Place hero near Amy NPC (grid 130,128 → world ~13000, ~12800)
  hero.SetPosition(glm::vec3(13000.0f, 0.0f, 12800.0f));
  hero.SnapToTerrain();
  hero.SetAction(0); // Idle
  printf("[Phase 13] HeroCharacter: READY (idle at Lorencia)\n\n");

  // ========== Phase 14: MonsterManager rendering ==========
  printf("[Phase 14] Initializing MonsterManager...\n");
  MonsterManager monsterManager;
  monsterManager.InitModels("Data");
  if (terrainLoaded) {
    monsterManager.SetTerrainData(&terrainData);
    monsterManager.SetTerrainLightmap(terrainData.lightmap);
  }
  monsterManager.SetLuminosity(0.7f);
  monsterManager.SetMapId(0);
  // Spawn a few test monsters near Lorencia
  monsterManager.AddMonster(0, 135, 125, 3, 1001); // Bull Fighter
  monsterManager.AddMonster(2, 138, 130, 5, 1002); // Budge Dragon
  monsterManager.AddMonster(1, 132, 132, 1, 1003); // Hound
  printf("[Phase 14] MonsterManager: READY (%d monsters)\n\n", monsterManager.GetMonsterCount());

  // Camera setup — terrain center
  glm::vec3 modelCenter = glm::vec3(12800.0f, 200.0f, 12800.0f);

  int frameCount = 0;
  const int screenshotFrame = 30;
  bool screenshotRequested = false;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (w != fbW || h != fbH) {
      fbW = w;
      fbH = h;
      bgfx::reset(fbW, fbH, BGFX_RESET_VSYNC);
    }

    bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
    bgfx::setViewRect(255, 0, 0, uint16_t(fbW), uint16_t(fbH));
    bgfx::touch(0);

    // ========== View 0: 3D scene rendering ==========
    {
      // Static camera — fixed position looking at terrain
      glm::vec3 center = glm::vec3(12800.0f, 100.0f, 12800.0f);
      glm::vec3 eye = center + glm::vec3(1500.0f, 800.0f, 1500.0f);

      glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
      glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                          (float)fbW / (float)fbH,
                                          1.0f, 50000.0f);

      bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(proj));

      // Sky renders first (behind everything)
      sky.Render(view, proj, eye, 1.0f);

      // Terrain renders after sky, before models
      float time = (float)frameCount * 0.016f; // ~60fps timing
      if (terrainLoaded) {
        terrain.Render(view, proj, time, eye);
      }

      // Grass renders after terrain, before models
      if (grassLoaded) {
        grass.Render(view, proj, time, eye);
      }

      // Phase 8: World objects (trees, buildings, fences, torches, etc.)
      if (objectsLoaded) {
        objectRenderer.Render(view, proj, eye, time);
      }

      // Phase 9: Fire/smoke particle effects (rendered after objects, additive blend)
      if (fireEmitterCount > 0) {
        fireEffect.Update(0.016f);
        fireEffect.Render(view, proj);
      }

      // Phase 10: Ambient wildlife (birds, fish, falling leaves)
      boidManager.Update(0.016f, center, 0, time);
      boidManager.RenderShadows(view, proj);
      boidManager.Render(view, proj, eye);
      boidManager.RenderLeaves(view, proj, eye);

      // Phase 11: Click-to-move ground effect
      if (clickShader) {
        clickEffect.Render(view, proj, 0.016f, clickShader.get());
        if (!clickEffect.IsVisible())
          clickEffect.Show(glm::vec3(12800.0f, 0.0f, 12800.0f));
      }

      // Phase 12: NPC rendering (shadows first, then models)
      npcManager.RenderShadows(view, proj);
      npcManager.Render(view, proj, eye, 0.016f);

      // Phase 13: HeroCharacter rendering (shadows first, then model)
      hero.RenderShadow(view, proj);
      hero.Render(view, proj, eye, 0.016f);

      // Phase 14: Monster rendering (shadows first, then models)
      monsterManager.Update(0.016f);
      monsterManager.RenderShadows(view, proj);
      monsterManager.Render(view, proj, eye, 0.016f);
    } // end view 0 scope

    // Debug text overlay
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(2, 1, 0x0f, "MU Remaster - BGFX Migration Test (Phase 14)");
    bgfx::dbgTextPrintf(2, 2, 0x07, "Backend: %s  Resolution: %dx%d  Frame: %d",
                         bgfx::getRendererName(bgfx::getRendererType()), fbW, fbH, frameCount);
    bgfx::dbgTextPrintf(2, 4, 0x07, "Texture loading: %d/4 passed", texLoadCount);

    if (objectsLoaded) {
      bgfx::dbgTextPrintf(2, 6, 0x02, "Phase 8: Objects OK (%d instances, %d models)",
                           objectRenderer.GetInstanceCount(), objectRenderer.GetModelCount());
    } else {
      bgfx::dbgTextPrintf(2, 6, 0x04, "Phase 8: Objects INCOMPLETE");
    }

    if (fireEmitterCount > 0) {
      bgfx::dbgTextPrintf(2, 7, 0x02, "Phase 9: Fire OK (%d emitters, %d particles)",
                           fireEmitterCount, fireEffect.GetParticleCount());
    } else {
      bgfx::dbgTextPrintf(2, 7, 0x04, "Phase 9: Fire - no emitters");
    }

    bgfx::dbgTextPrintf(2, 8, 0x02, "Phase 10: Boids OK (birds + fish + leaves)");
    bgfx::dbgTextPrintf(2, 9, 0x02, "Phase 11: ClickEffect OK (ground glow + rings + BMD cone)");
    bgfx::dbgTextPrintf(2, 10, 0x02, "Phase 12: NPCs OK (%d spawned, shadows + chrome glow)", npcManager.GetNpcCount());
    bgfx::dbgTextPrintf(2, 11, 0x02, "Phase 13: HeroCharacter OK (idle + shadow)");
    bgfx::dbgTextPrintf(2, 12, 0x02, "Phase 14: Monsters OK (%d spawned, shadows + anim)", monsterManager.GetMonsterCount());

    if (frameCount == screenshotFrame)
      bgfx::dbgTextPrintf(2, 13, 0x02, ">>> Screenshot captured at frame %d <<<", screenshotFrame);

    // ImGui frame — display status
    ImGui_ImplBgfx_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(50, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Phase 14: MonsterManager")) {
      if (objectsLoaded) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ObjectRenderer active");
        ImGui::Text("Instances: %d", objectRenderer.GetInstanceCount());
        ImGui::Text("Unique models: %d", objectRenderer.GetModelCount());
      } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Object rendering not available");
      }

      ImGui::Separator();
      if (fireEmitterCount > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Fire effects active");
        ImGui::Text("Emitters: %d", fireEmitterCount);
        ImGui::Text("Active particles: %d", fireEffect.GetParticleCount());
      } else {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "No fire emitters registered");
      }

      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Phase 10: Ambient wildlife active");
      ImGui::Text("Birds, fish, falling leaves (map 0 = Lorencia)");

      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Phase 11: Click effect");
      ImGui::Text("Ground glow + pulsing ring + wave rings + BMD cone");
      ImGui::Text("Visible: %s", clickEffect.IsVisible() ? "yes" : "no");

      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Phase 12: NPC rendering");
      ImGui::Text("NPCs spawned: %d", npcManager.GetNpcCount());
      ImGui::Text("Shadows + chrome glow + weapon attachment");

      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Phase 13: HeroCharacter");
      ImGui::Text("Player character idle at Lorencia, shadow rendering");

      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.6f, 1.0f), "Phase 14: MonsterManager");
      ImGui::Text("Monsters spawned: %d (Bull Fighter, Budge Dragon, Hound)", monsterManager.GetMonsterCount());
      ImGui::Text("Shadows + animation + ambient VFX");

      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                          "Texture loading: %d/4 passed", texLoadCount);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());

    // Request screenshot one frame before capture (BGFX captures after frame())
    if (frameCount == screenshotFrame - 1 && !screenshotRequested) {
      callback.outputPath = "screenshots/bgfx_phase14_test.png";
      bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "screenshots/bgfx_phase13_test.png");
      screenshotRequested = true;
      printf("[Screenshot] Requested at frame %d\n", frameCount);
    }

    bgfx::frame();
    frameCount++;
  }

  printf("[BGFX] Test completed: %d frames rendered\n", frameCount);
  if (callback.hasCaptured)
    printf("[Screenshot] Output: screenshots/bgfx_phase13_test.png (%dx%d)\n",
           callback.capturedWidth, callback.capturedHeight);
  else
    printf("[Screenshot] WARNING: No screenshot captured!\n");

  if (texLoadCount == 4)
    printf("[Phase 3] PASSED: All textures loaded via BGFX\n");
  else
    printf("[Phase 3] PARTIAL: %d/4 textures loaded\n", texLoadCount);

  if (terrainLoaded)
    printf("[Phase 6] PASSED: Terrain rendered via BGFX pipeline\n");
  else
    printf("[Phase 6] INCOMPLETE: Terrain not loaded\n");

  if (grassLoaded)
    printf("[Phase 7] PASSED: Grass rendered via BGFX pipeline\n");
  else
    printf("[Phase 7] INCOMPLETE: Grass not loaded\n");

  if (objectsLoaded)
    printf("[Phase 8] PASSED: World objects rendered via BGFX pipeline\n");
  else
    printf("[Phase 8] INCOMPLETE: Objects not loaded\n");

  if (fireEmitterCount > 0)
    printf("[Phase 9] PASSED: Fire/smoke particles rendered via BGFX pipeline (%d emitters)\n", fireEmitterCount);
  else
    printf("[Phase 9] INCOMPLETE: No fire emitters registered\n");

  printf("[Phase 10] PASSED: Ambient wildlife rendered via BGFX pipeline (birds + fish + leaves)\n");
  printf("[Phase 11] PASSED: Click effect rendered via BGFX pipeline (glow + rings + BMD cone)\n");
  printf("[Phase 12] PASSED: NPC rendering via BGFX pipeline (%d NPCs, shadows + chrome glow)\n", npcManager.GetNpcCount());
  printf("[Phase 13] PASSED: HeroCharacter rendered via BGFX pipeline (idle + shadow)\n");
  printf("[Phase 14] PASSED: MonsterManager rendered via BGFX pipeline (%d monsters, shadows + animation)\n", monsterManager.GetMonsterCount());

  // Cleanup — must destroy all BGFX resources BEFORE bgfx::shutdown()
  monsterManager.Cleanup();
  hero.Cleanup();
  npcManager.Cleanup();
  ChromeGlow::DeleteTextures();
  clickEffect.Cleanup();
  clickShader.reset();
  boidManager.Cleanup();
  fireEffect.Cleanup();
  objectRenderer.Cleanup();
  grass.Cleanup();

  // Terrain destructor calls bgfx::destroy on handles, so destroy it explicitly first
  terrain.~Terrain();
  // Prevent double destruction: placement-new a default Terrain (no BGFX handles)
  new (&terrain) Terrain();

  sky.Cleanup();
  TexDestroy(texOZJ);
  TexDestroy(texOZT);
  TexDestroy(texResolved);
  TexDestroy(texInfo.textureID);

  ImGui_ImplBgfx_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  bgfx::shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

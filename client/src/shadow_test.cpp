// Shadow map test with verified FBO readback timing.
// Proven: FBOs work on Metal. Now verify cube renders to shadow FBO.

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstring>
#include <vector>

static const int W = 800, H = 600;
static const int SHADOW_SIZE = 512;

struct ScreenCB : public bgfx::CallbackI {
  bool captured = false;
  void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* s) override { fprintf(stderr, "FATAL: %s\n", s); }
  void traceVargs(const char*, uint16_t, const char*, va_list) override {}
  void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
  void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
  void profilerEnd() override {}
  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
  void cacheWrite(uint64_t, const void*, uint32_t) override {}
  void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
  void captureEnd() override {}
  void captureFrame(const void*, uint32_t) override {}
  void screenShot(const char* path, uint32_t w, uint32_t h, uint32_t pitch,
                  bgfx::TextureFormat::Enum, const void* data, uint32_t, bool yflip) override {
    std::vector<uint8_t> rgba(w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
      uint32_t srcY = yflip ? (h - 1 - y) : y;
      const uint8_t *src = (const uint8_t*)data + srcY * pitch;
      uint8_t *dst = rgba.data() + y * w * 4;
      for (uint32_t x = 0; x < w; x++) {
        dst[x*4+0] = src[x*4+2]; dst[x*4+1] = src[x*4+1];
        dst[x*4+2] = src[x*4+0]; dst[x*4+3] = src[x*4+3];
      }
    }
    stbi_write_png(path, w, h, 4, rgba.data(), w * 4);
    printf("[Screenshot] Saved %s (%dx%d)\n", path, w, h);
    captured = true;
  }
};

struct PosVertex {
  float x, y, z;
  static bgfx::VertexLayout layout;
  static void init() {
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
  }
};
bgfx::VertexLayout PosVertex::layout;

struct GroundVertex {
  float x, y, z; float u, v;
  static bgfx::VertexLayout layout;
  static void init() {
    layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  }
};
bgfx::VertexLayout GroundVertex::layout;

static PosVertex s_cubeVerts[] = {
  {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1},
  { 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},
  {-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1},
  {-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1},
  { 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},
  {-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},
};
static uint16_t s_cubeIdx[] = {
   0, 1, 2,  0, 2, 3,   4, 5, 6,  4, 6, 7,
   8, 9,10,  8,10,11,  12,13,14, 12,14,15,
  16,17,18, 16,18,19,  20,21,22, 20,22,23,
};
static GroundVertex s_groundVerts[] = {
  {-10,0,-10, 0,0}, {10,0,-10, 1,0}, {10,0,10, 1,1}, {-10,0,10, 0,1},
};
static uint16_t s_groundIdx[] = { 0, 1, 2, 0, 2, 3 };

int main() {
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *win = glfwCreateWindow(W, H, "Shadow Test", nullptr, nullptr);
  if (!win) return 1;

  ScreenCB cb;
  bgfx::Init bi;
  bi.type = bgfx::RendererType::Metal;
  bi.resolution.width = W;
  bi.resolution.height = H;
  bi.resolution.reset = BGFX_RESET_VSYNC;
  bi.platformData.nwh = glfwGetCocoaWindow(win);
  bi.callback = &cb;
  if (!bgfx::init(bi)) return 1;

  PosVertex::init();
  GroundVertex::init();

  auto cubeVbo = bgfx::createVertexBuffer(bgfx::makeRef(s_cubeVerts, sizeof(s_cubeVerts)), PosVertex::layout);
  auto cubeEbo = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeIdx, sizeof(s_cubeIdx)));
  auto groundVbo = bgfx::createVertexBuffer(bgfx::makeRef(s_groundVerts, sizeof(s_groundVerts)), GroundVertex::layout);
  auto groundEbo = bgfx::createIndexBuffer(bgfx::makeRef(s_groundIdx, sizeof(s_groundIdx)));

  auto depthShader = Shader::Load("vs_depth.bin", "fs_depth.bin");
  auto groundShader = Shader::Load("vs_ground.bin", "fs_ground.bin");
  if (!depthShader || !groundShader) { fprintf(stderr, "Shader load failed\n"); return 1; }

  // Shadow FBO: BGRA8 color + D16 depth (for proper depth testing)
  auto shadowTex = bgfx::createTexture2D(SHADOW_SIZE, SHADOW_SIZE, false, 1,
                                          bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT);
  auto shadowDepth = bgfx::createTexture2D(SHADOW_SIZE, SHADOW_SIZE, false, 1,
                                            bgfx::TextureFormat::D16, BGFX_TEXTURE_RT_WRITE_ONLY);
  bgfx::TextureHandle shadowAtts[] = { shadowTex, shadowDepth };
  auto shadowFB = bgfx::createFrameBuffer(2, shadowAtts, false);

  // Readback
  auto readTex = bgfx::createTexture2D(SHADOW_SIZE, SHADOW_SIZE, false, 1,
                                        bgfx::TextureFormat::BGRA8,
                                        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
  std::vector<uint8_t> readData(SHADOW_SIZE * SHADOW_SIZE * 4, 0);

  const uint8_t SV = 0, MV = 1;

  // Light
  glm::vec3 ld = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
  glm::vec3 lp = -ld * 20.0f;
  glm::vec3 up(0,0,1);
  if (std::abs(glm::dot(ld, up)) > 0.99f) up = {1,0,0};
  auto lv = glm::lookAt(lp, glm::vec3(0), up);
  // Metal uses [0,1] Z clip range, not [-1,1] like OpenGL
  bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
  printf("[Test] homogeneousDepth=%d\n", homogeneousDepth);
  auto lpr = homogeneousDepth
    ? glm::ortho(-15.f, 15.f, -15.f, 15.f, 1.f, 50.f)           // [-1,1] for OpenGL
    : glm::orthoRH_ZO(-15.f, 15.f, -15.f, 15.f, 1.f, 50.f);     // [0,1] for Metal/DX
  auto lmtx = lpr * lv;

  // Camera
  auto view = glm::lookAt(glm::vec3(0,8,12), glm::vec3(0), glm::vec3(0,1,0));
  auto proj = homogeneousDepth
    ? glm::perspective(glm::radians(45.f), (float)W/H, 0.1f, 100.f)
    : glm::perspectiveRH_ZO(glm::radians(45.f), (float)W/H, 0.1f, 100.f);

  auto cubeMdl = glm::translate(glm::mat4(1), glm::vec3(0,2,0));
  auto gndMdl = glm::mat4(1);

  int frame = 0;
  uint32_t readbackFrame = UINT32_MAX;
  bool readbackDone = false;

  while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();

    // Shadow pass
    bgfx::setViewName(SV, "Shadow");
    bgfx::setViewRect(SV, 0, 0, SHADOW_SIZE, SHADOW_SIZE);
    bgfx::setViewClear(SV, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFFFFFFFF, 1.0f, 0);
    bgfx::setViewFrameBuffer(SV, shadowFB);
    bgfx::setViewTransform(SV, glm::value_ptr(lv), glm::value_ptr(lpr));

    bgfx::setTransform(glm::value_ptr(cubeMdl));
    bgfx::setVertexBuffer(0, cubeVbo);
    bgfx::setIndexBuffer(cubeEbo);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_CULL_CCW);
    bgfx::submit(SV, depthShader->program);

    // Main pass
    bgfx::setViewName(MV, "Main");
    bgfx::setViewRect(MV, 0, 0, W, H);
    bgfx::setViewClear(MV, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x404060FF, 1.0f, 0);
    bgfx::setViewTransform(MV, glm::value_ptr(view), glm::value_ptr(proj));

    // Ground
    bgfx::setTransform(glm::value_ptr(gndMdl));
    bgfx::setVertexBuffer(0, groundVbo);
    bgfx::setIndexBuffer(groundEbo);
    groundShader->setMat4("u_lightMtx", lmtx);
    groundShader->setTexture(0, "s_shadowMap", shadowTex);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(MV, groundShader->program);

    // Cube in main view
    bgfx::setTransform(glm::value_ptr(cubeMdl));
    bgfx::setVertexBuffer(0, cubeVbo);
    bgfx::setIndexBuffer(cubeEbo);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_CULL_CCW);
    bgfx::submit(MV, depthShader->program);

    // Readback on frame 3
    if (frame == 3) {
      bgfx::blit(MV, readTex, 0, 0, shadowTex);
      readbackFrame = bgfx::readTexture(readTex, readData.data());
      printf("[Test] Readback scheduled, ready at frame %u\n", readbackFrame);
    }

    bgfx::frame();
    frame++;

    // Process readback
    if (!readbackDone && frame >= (int)readbackFrame + 2 && readbackFrame != UINT32_MAX) {
      int darkRed = 0, green = 0, other = 0;
      uint8_t minR = 255, maxR = 0, minG = 255, maxG = 0;
      for (int i = 0; i < SHADOW_SIZE * SHADOW_SIZE; i++) {
        uint8_t r = readData[i*4+2], g = readData[i*4+1];
        if (r >= 120 && r <= 135 && g == 0) darkRed++;
        else if (r == 0 && g == 255) green++;
        else other++;
        if (r < minR) minR = r; if (r > maxR) maxR = r;
        if (g < minG) minG = g; if (g > maxG) maxG = g;
      }
      printf("[Readback] darkRed=%d green=%d other=%d total=%d\n", darkRed, green, other, SHADOW_SIZE*SHADOW_SIZE);
      printf("[Readback] R range: %d-%d, G range: %d-%d\n", minR, maxR, minG, maxG);
      printf("[Readback] Pixel[0] BGRA: %d %d %d %d\n", readData[0], readData[1], readData[2], readData[3]);

      // Save
      std::vector<uint8_t> rgbaOut(SHADOW_SIZE * SHADOW_SIZE * 4);
      for (int i = 0; i < SHADOW_SIZE * SHADOW_SIZE; i++) {
        rgbaOut[i*4+0] = readData[i*4+2]; rgbaOut[i*4+1] = readData[i*4+1];
        rgbaOut[i*4+2] = readData[i*4+0]; rgbaOut[i*4+3] = readData[i*4+3];
      }
      stbi_write_png("/tmp/shadow_fbo.png", SHADOW_SIZE, SHADOW_SIZE, 4, rgbaOut.data(), SHADOW_SIZE*4);
      printf("[Readback] Saved /tmp/shadow_fbo.png\n");
      readbackDone = true;
    }

    // Screenshot
    if (frame == 25) {
      bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "/tmp/shadow_test.png");
    }
    if (cb.captured) { printf("[Test] Done.\n"); break; }
    if (frame > 40) { printf("[Test] Timeout.\n"); break; }
  }

  bgfx::destroy(cubeVbo);
  bgfx::destroy(cubeEbo);
  bgfx::destroy(groundVbo);
  bgfx::destroy(groundEbo);
  depthShader->destroy();
  groundShader->destroy();
  if (bgfx::isValid(readTex)) bgfx::destroy(readTex);
  if (bgfx::isValid(shadowFB)) bgfx::destroy(shadowFB);
  if (bgfx::isValid(shadowTex)) bgfx::destroy(shadowTex);
  if (bgfx::isValid(shadowDepth)) bgfx::destroy(shadowDepth);
  bgfx::shutdown();
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}

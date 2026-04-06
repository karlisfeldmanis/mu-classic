// ImGui BGFX rendering backend for MU Remaster.
// Converts ImGui draw lists to BGFX transient buffers and submits them.
// Based on the rendering approach from bgfx/examples/common/imgui/imgui.cpp.

#include "imgui_impl_bgfx.h"
#include "imgui.h"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct ImGui_ImplBgfx_Data {
  bgfx::ViewId viewId = 255;
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle fontTexture = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout;
};

static ImGui_ImplBgfx_Data *s_bd = nullptr;

static bgfx::ShaderHandle loadShaderBin(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "[ImGui_BGFX] Failed to open shader: %s\n", path);
    return BGFX_INVALID_HANDLE;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  const bgfx::Memory *mem = bgfx::alloc(size + 1);
  fread(mem->data, 1, size, f);
  mem->data[size] = '\0';
  fclose(f);
  return bgfx::createShader(mem);
}

bool ImGui_ImplBgfx_Init(bgfx::ViewId viewId, const char *shaderDir) {
  ImGuiIO &io = ImGui::GetIO();
  if (s_bd)
    return false; // Already initialized

  s_bd = new ImGui_ImplBgfx_Data();
  s_bd->viewId = viewId;

  io.BackendRendererName = "imgui_impl_bgfx";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  // Load shaders
  std::string dir = shaderDir;
  if (!dir.empty() && dir.back() != '/')
    dir += '/';
  bgfx::ShaderHandle vsh = loadShaderBin((dir + "vs_imgui.bin").c_str());
  bgfx::ShaderHandle fsh = loadShaderBin((dir + "fs_imgui.bin").c_str());
  if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
    fprintf(stderr, "[ImGui_BGFX] Failed to load shaders from %s\n",
            shaderDir);
    if (bgfx::isValid(vsh))
      bgfx::destroy(vsh);
    if (bgfx::isValid(fsh))
      bgfx::destroy(fsh);
    delete s_bd;
    s_bd = nullptr;
    return false;
  }

  s_bd->program = bgfx::createProgram(vsh, fsh, true);
  if (!bgfx::isValid(s_bd->program)) {
    fprintf(stderr, "[ImGui_BGFX] Failed to create shader program!\n");
    delete s_bd;
    s_bd = nullptr;
    return false;
  }

  // Texture sampler uniform
  s_bd->s_tex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

  // ImGui vertex layout: ImVec2 pos, ImVec2 uv, ImU32 col
  s_bd->layout.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  printf("[ImGui_BGFX] Init OK — view=%d, program=%d\n",
         viewId, s_bd->program.idx);
  return true;
}

void ImGui_ImplBgfx_Shutdown() {
  if (!s_bd)
    return;

  if (bgfx::isValid(s_bd->fontTexture))
    bgfx::destroy(s_bd->fontTexture);
  if (bgfx::isValid(s_bd->program))
    bgfx::destroy(s_bd->program);
  if (bgfx::isValid(s_bd->s_tex))
    bgfx::destroy(s_bd->s_tex);

  delete s_bd;
  s_bd = nullptr;
}

// Create or update a bgfx texture from ImTextureData
static void ImGui_ImplBgfx_CreateOrUpdateTexture(ImTextureData *tex) {
  // Destroy old handle if updating
  bgfx::TextureHandle old = {(uint16_t)(uintptr_t)tex->TexID};
  if (tex->TexID != ImTextureID_Invalid && bgfx::isValid(old))
    bgfx::destroy(old);

  bgfx::TextureHandle th = bgfx::createTexture2D(
      (uint16_t)tex->Width, (uint16_t)tex->Height, false, 1,
      bgfx::TextureFormat::RGBA8, 0,
      bgfx::copy(tex->Pixels, tex->Width * tex->Height * 4));

  tex->SetTexID((ImTextureID)(uintptr_t)th.idx);
  tex->Status = ImTextureStatus_OK;

  // Track main font texture handle
  if (tex == ImGui::GetIO().Fonts->TexData)
    s_bd->fontTexture = th;
}

static void ImGui_ImplBgfx_DestroyTexture(ImTextureData *tex) {
  if (tex->TexID != ImTextureID_Invalid) {
    bgfx::TextureHandle th = {(uint16_t)(uintptr_t)tex->TexID};
    if (bgfx::isValid(th))
      bgfx::destroy(th);
    tex->TexID = ImTextureID_Invalid;
  }
  tex->Status = ImTextureStatus_Destroyed;
}

void ImGui_ImplBgfx_NewFrame() {
  if (!s_bd)
    return;

  // Process texture lifecycle events (ImGui 1.92+)
  ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
  for (ImTextureData *tex : pio.Textures) {
    if (tex->Status == ImTextureStatus_WantCreate ||
        tex->Status == ImTextureStatus_WantUpdates) {
      ImGui_ImplBgfx_CreateOrUpdateTexture(tex);
    } else if (tex->Status == ImTextureStatus_WantDestroy) {
      ImGui_ImplBgfx_DestroyTexture(tex);
    }
  }
}

void ImGui_ImplBgfx_SetViewId(bgfx::ViewId viewId) {
  if (s_bd)
    s_bd->viewId = viewId;
}

void ImGui_ImplBgfx_InvalidateFontsTexture() {
  // No-op: bgfx::reset() does not destroy regular textures, only render targets.
  // Font texture survives reset and does not need recreation.
}

void ImGui_ImplBgfx_RenderDrawData(ImDrawData *drawData) {
  if (!s_bd || !drawData)
    return;

  // Avoid rendering when minimized
  int fbW = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
  int fbH = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
  if (fbW <= 0 || fbH <= 0)
    return;

  bgfx::ViewId viewId = s_bd->viewId;
  bgfx::setViewName(viewId, "ImGui");
  bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

  // Setup orthographic projection
  const bgfx::Caps *caps = bgfx::getCaps();
  float ortho[16];
  float L = drawData->DisplayPos.x;
  float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
  float T = drawData->DisplayPos.y;
  float B = drawData->DisplayPos.y + drawData->DisplaySize.y;
  bx::mtxOrtho(ortho, L, R, B, T, 0.0f, 1000.0f, 0.0f,
               caps->homogeneousDepth);
  bgfx::setViewTransform(viewId, nullptr, ortho);
  bgfx::setViewRect(viewId, 0, 0, uint16_t(fbW), uint16_t(fbH));

  const ImVec2 clipPos = drawData->DisplayPos;
  const ImVec2 clipScale = drawData->FramebufferScale;

  for (int n = 0; n < drawData->CmdListsCount; n++) {
    const ImDrawList *cmdList = drawData->CmdLists[n];
    uint32_t numVertices = (uint32_t)cmdList->VtxBuffer.Size;
    uint32_t numIndices = (uint32_t)cmdList->IdxBuffer.Size;

    // Check transient buffer availability
    if (numVertices !=
            bgfx::getAvailTransientVertexBuffer(numVertices, s_bd->layout) ||
        numIndices != bgfx::getAvailTransientIndexBuffer(numIndices,
                                                         sizeof(ImDrawIdx) == 4))
      break;

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, numVertices, s_bd->layout);
    bgfx::allocTransientIndexBuffer(&tib, numIndices,
                                    sizeof(ImDrawIdx) == 4);

    memcpy(tvb.data, cmdList->VtxBuffer.Data,
           numVertices * sizeof(ImDrawVert));
    memcpy(tib.data, cmdList->IdxBuffer.Data,
           numIndices * sizeof(ImDrawIdx));

    for (int cmdI = 0; cmdI < cmdList->CmdBuffer.Size; cmdI++) {
      const ImDrawCmd *cmd = &cmdList->CmdBuffer[cmdI];

      if (cmd->UserCallback) {
        cmd->UserCallback(cmdList, cmd);
        continue;
      }
      if (cmd->ElemCount == 0)
        continue;

      // Project scissor/clipping rect into framebuffer space
      ImVec4 clipRect;
      clipRect.x = (cmd->ClipRect.x - clipPos.x) * clipScale.x;
      clipRect.y = (cmd->ClipRect.y - clipPos.y) * clipScale.y;
      clipRect.z = (cmd->ClipRect.z - clipPos.x) * clipScale.x;
      clipRect.w = (cmd->ClipRect.w - clipPos.y) * clipScale.y;

      if (clipRect.x >= fbW || clipRect.y >= fbH || clipRect.z < 0.0f ||
          clipRect.w < 0.0f)
        continue;

      uint16_t sx = uint16_t(clipRect.x > 0.0f ? clipRect.x : 0.0f);
      uint16_t sy = uint16_t(clipRect.y > 0.0f ? clipRect.y : 0.0f);
      uint16_t sw =
          uint16_t((clipRect.z < 65535.0f ? clipRect.z : 65535.0f) - sx);
      uint16_t sh =
          uint16_t((clipRect.w < 65535.0f ? clipRect.w : 65535.0f) - sy);
      bgfx::setScissor(sx, sy, sw, sh);

      // Resolve texture handle
      bgfx::TextureHandle th = s_bd->fontTexture;
      ImTextureID texId = cmd->GetTexID();
      if (texId) {
        uint16_t idx = (uint16_t)(uintptr_t)texId;
        th = {idx};
      }

      uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_MSAA |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                             BGFX_STATE_BLEND_INV_SRC_ALPHA);

      bgfx::setTexture(0, s_bd->s_tex, th);
      bgfx::setState(state);
      bgfx::setVertexBuffer(0, &tvb);
      bgfx::setIndexBuffer(&tib, cmd->IdxOffset, cmd->ElemCount);
      bgfx::submit(viewId, s_bd->program);
    }
  }
}

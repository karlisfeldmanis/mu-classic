// ImGui BGFX rendering backend for MU Remaster.
// Input handling is done by imgui_impl_glfw (unchanged).
// This replaces imgui_impl_opengl3 for the BGFX migration.

#ifndef IMGUI_IMPL_BGFX_H
#define IMGUI_IMPL_BGFX_H

#include <bgfx/bgfx.h>

struct ImDrawData;

// Call after bgfx::init() and ImGui::CreateContext().
// shaderDir: path to directory containing vs_imgui.bin / fs_imgui.bin
bool ImGui_ImplBgfx_Init(bgfx::ViewId viewId, const char *shaderDir);

// Call before bgfx::shutdown() and ImGui::DestroyContext().
void ImGui_ImplBgfx_Shutdown();

// Call once per frame before ImGui::NewFrame().
void ImGui_ImplBgfx_NewFrame();

// Call after ImGui::Render() to submit draw data to BGFX.
void ImGui_ImplBgfx_RenderDrawData(ImDrawData *drawData);

// Change the BGFX view ID for subsequent render passes.
void ImGui_ImplBgfx_SetViewId(bgfx::ViewId viewId);

#endif // IMGUI_IMPL_BGFX_H

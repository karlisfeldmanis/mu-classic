#ifndef UI_COORDS_HPP
#define UI_COORDS_HPP

#include <GLFW/glfw3.h>

// Virtual 1280x720 coordinate system for the modern HUD.
// Supports optional scale for centered rendering at reduced size.
// Offsets are computed dynamically so fullscreen/resize works correctly.
struct UICoords {
  GLFWwindow *window = nullptr;

  static constexpr float VIRTUAL_W = 1280.0f;
  static constexpr float VIRTUAL_H = 720.0f;

  // Scale for centered rendering (default: full size)
  float scale = 1.0f;

  // Configure for centered rendering at given scale (e.g. 0.7 for 70%)
  void SetCenteredScale(float s) { scale = s; }

  float ToScreenX(float vx) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    float offsetX = (float)w * (1.0f - scale) * 0.5f;
    return offsetX + vx * (float)w / VIRTUAL_W * scale;
  }

  float ToScreenY(float vy) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    float offsetY = (float)h * (1.0f - scale) * 0.5f;
    return offsetY + vy * (float)h / VIRTUAL_H * scale;
  }

  float ToVirtualX(float sx) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    float offsetX = (float)w * (1.0f - scale) * 0.5f;
    return (sx - offsetX) * VIRTUAL_W / ((float)w * scale);
  }

  float ToVirtualY(float sy) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    float offsetY = (float)h * (1.0f - scale) * 0.5f;
    return (sy - offsetY) * VIRTUAL_H / ((float)h * scale);
  }
};

#endif // UI_COORDS_HPP

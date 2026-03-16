#ifndef UI_COORDS_HPP
#define UI_COORDS_HPP

#include <GLFW/glfw3.h>

// Virtual 1280x720 coordinate system for the modern HUD.
// Supports optional scale + offset for centered rendering at reduced size.
struct UICoords {
  GLFWwindow *window = nullptr;

  static constexpr float VIRTUAL_W = 1280.0f;
  static constexpr float VIRTUAL_H = 720.0f;

  // Scale and offset for centered rendering (default: full size, no offset)
  float scale = 1.0f;
  float offsetX = 0.0f; // screen pixels
  float offsetY = 0.0f; // screen pixels

  // Configure for centered rendering at given scale (e.g. 0.7 for 70%)
  void SetCenteredScale(float s) {
    scale = s;
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    offsetX = (float)w * (1.0f - s) * 0.5f;
    offsetY =
        (float)h * (1.0f - s) * 0.5f; // anchor to center instead of bottom
  }

  float ToScreenX(float vx) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    return offsetX + vx * (float)w / VIRTUAL_W * scale;
  }

  float ToScreenY(float vy) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    return offsetY + vy * (float)h / VIRTUAL_H * scale;
  }

  float ToVirtualX(float sx) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    return (sx - offsetX) * VIRTUAL_W / ((float)w * scale);
  }

  float ToVirtualY(float sy) const {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    return (sy - offsetY) * VIRTUAL_H / ((float)h * scale);
  }
};

#endif // UI_COORDS_HPP

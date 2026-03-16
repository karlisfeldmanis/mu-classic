#ifndef SCREENSHOT_HPP
#define SCREENSHOT_HPP

#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class Screenshot {
public:
  static void Capture(GLFWwindow *window, const std::string &filename = "");

  // Low-level GIF capture: accumulate frames then write
  static void BeginGif(int width, int height, float scale = 1.0f,
                       int skipCount = 0);
  static void AddGifFrame(GLFWwindow *window);
  static void SaveGif(const std::string &path,
                      int delayCs = 4); // delay in centiseconds

  // High-level GIF recording with warmup (shared across apps)
  static void StartRecording(GLFWwindow *window, const std::string &savePath,
                             int frameCount = 72, int delayCs = 4,
                             float scale = 1.0f, int skipCount = 0,
                             int warmupFrames = 30);
  static bool TickRecording(GLFWwindow *window); // returns true when finished
  static bool IsRecording();
  static bool IsWarmingUp();
  static float GetProgress(); // 0.0 to 1.0 (across warmup + recording)

  struct GifFrame {
    std::vector<unsigned char> pixels; // RGB, flipped
    int width, height;
  };

private:
  static std::vector<GifFrame> gifFrames;
  static int gifWidth, gifHeight;

  // High-level recording state
  static bool recording;
  static bool warmingUp;
  static int recFrameTarget;
  static int recFrameCurrent;
  static int recWarmupTarget;
  static int recWarmupCurrent;
  static int recDelayCs;
  static std::string recSavePath;
};

#endif

#ifndef SKY_HPP
#define SKY_HPP

#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>

class Sky {
public:
  void Init(const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &projection,
              const glm::vec3 &cameraPos, float luminosity = 1.0f);
  void Cleanup();

private:
  std::unique_ptr<Shader> shader;
  int indexCount = 0;

  bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ebo = BGFX_INVALID_HANDLE;
  TexHandle texture = kInvalidTex;

  // Sun billboard
  bgfx::VertexBufferHandle sunVbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle sunEbo = BGFX_INVALID_HANDLE;
  TexHandle sunTexture = kInvalidTex;

  static constexpr int SEGMENTS = 36;
  static constexpr float RADIUS = 3200.0f;
  static constexpr float BAND_BOTTOM = -1000.0f;
  static constexpr float BAND_TOP = 800.0f;
};

#endif

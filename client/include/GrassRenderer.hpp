#ifndef GRASS_RENDERER_HPP
#define GRASS_RENDERER_HPP

#include "TerrainParser.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GrassRenderer {
public:
  struct PushSource {
    glm::vec3 pos;
    float radius;
  };

  void Init();
  void Load(const TerrainData &data, int worldID, const std::string &dataPath,
            const std::vector<bool> *objectOccupancy = nullptr);
  void Render(const glm::mat4 &view, const glm::mat4 &projection, float time,
              const glm::vec3 &viewPos,
              const std::vector<PushSource> &pushSources = {});
  void Cleanup();

  void SetFogColor(const glm::vec3 &c) { fogColor = c; }
  void SetFogRange(float near_, float far_) {
    fogNear = near_;
    fogFar = far_;
  }
  void SetLuminosity(float l) { m_luminosity = l; }

private:
  struct GrassVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    float windWeight;
    float gridX;
    glm::vec3 color;
    float texLayer;
  };

  int indexCount = 0;

  glm::vec3 fogColor = glm::vec3(0.117f, 0.078f, 0.039f);
  float fogNear = 1500.0f;
  float fogFar = 3500.0f;
  float m_luminosity = 1.0f;
  int m_worldID = 0;
  float m_alphaMult = 1.0f;

  std::unique_ptr<Shader> shader;
  bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ebo = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_pushPosRadius = BGFX_INVALID_HANDLE;
  TexHandle grassTextures[3] = {kInvalidTex, kInvalidTex, kInvalidTex};
};

#endif // GRASS_RENDERER_HPP

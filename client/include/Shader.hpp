#ifndef SHADER_HPP
#define SHADER_HPP

#include <cstdio>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include <bgfx/bgfx.h>

// BGFX shader program wrapper — loads pre-compiled .bin shaders,
// caches uniform handles, and provides GLM-compatible setters.
class BgfxShader {
public:
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;

  // Load pre-compiled vertex + fragment shader binaries
  bool load(const char *vsPath, const char *fsPath) {
    bgfx::ShaderHandle vsh = loadBin(vsPath);
    bgfx::ShaderHandle fsh = loadBin(fsPath);
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
      if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
      if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
      return false;
    }
    program = bgfx::createProgram(vsh, fsh, true); // destroyShaders=true
    if (!bgfx::isValid(program)) {
      fprintf(stderr, "[BgfxShader] Failed to create program: %s + %s\n", vsPath, fsPath);
      return false;
    }
    return true;
  }

  // Factory: resolves shaders/ vs ../shaders/ path
  static std::unique_ptr<BgfxShader> Load(const std::string &vsName,
                                           const std::string &fsName) {
    std::ifstream test("shaders/" + vsName);
    std::string prefix = test.good() ? "shaders/" : "../shaders/";
    auto s = std::make_unique<BgfxShader>();
    if (!s->load((prefix + vsName).c_str(), (prefix + fsName).c_str()))
      return nullptr;
    return s;
  }

  void destroy() {
    for (auto &[name, handle] : m_uniformCache) {
      if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    }
    m_uniformCache.clear();
    if (bgfx::isValid(program)) {
      bgfx::destroy(program);
      program = BGFX_INVALID_HANDLE;
    }
  }

  // No use() — BGFX binds program at submit() time

  // Uniform setters — BGFX minimum granularity is vec4
  void setFloat(const char *name, float v) {
    float val[4] = {v, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Vec4), val);
  }

  void setVec2(const char *name, const glm::vec2 &v) {
    float val[4] = {v.x, v.y, 0.0f, 0.0f};
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Vec4), val);
  }

  void setVec3(const char *name, const glm::vec3 &v) {
    float val[4] = {v.x, v.y, v.z, 0.0f};
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Vec4), val);
  }

  void setVec4(const char *name, const glm::vec4 &v) {
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Vec4), glm::value_ptr(v));
  }

  void setMat3(const char *name, const glm::mat3 &m) {
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Mat3), glm::value_ptr(m));
  }

  void setMat4(const char *name, const glm::mat4 &m) {
    bgfx::setUniform(getOrCreate(name, bgfx::UniformType::Mat4), glm::value_ptr(m));
  }

  void setTexture(uint8_t stage, const char *name, bgfx::TextureHandle tex) {
    bgfx::setTexture(stage, getOrCreate(name, bgfx::UniformType::Sampler), tex);
  }

  // Point lights: pack as vec4 arrays (position.xyz + range in w, color.xyz + 0 in w)
  static constexpr int MAX_POINT_LIGHTS = 64;

  template <typename T>
  void uploadPointLights(int count, const T *lights) {
    // Pack positions (xyz) + range (w) into vec4 array
    float posData[MAX_POINT_LIGHTS * 4] = {};
    float colData[MAX_POINT_LIGHTS * 4] = {};
    for (int i = 0; i < count && i < MAX_POINT_LIGHTS; ++i) {
      posData[i * 4 + 0] = lights[i].position.x;
      posData[i * 4 + 1] = lights[i].position.y;
      posData[i * 4 + 2] = lights[i].position.z;
      posData[i * 4 + 3] = lights[i].range;
      colData[i * 4 + 0] = lights[i].color.x;
      colData[i * 4 + 1] = lights[i].color.y;
      colData[i * 4 + 2] = lights[i].color.z;
      colData[i * 4 + 3] = 0.0f;
    }
    int n = (count > 0) ? count : 1; // BGFX requires num >= 1
    bgfx::setUniform(getOrCreate("u_lightPosRange", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS),
                     posData, n);
    bgfx::setUniform(getOrCreate("u_lightColorArr", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS),
                     colData, n);
    float countVec[4] = {(float)count, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(getOrCreate("u_lightCount", bgfx::UniformType::Vec4), countVec);
  }

  void uploadPointLights(int count, const glm::vec3 *positions,
                         const glm::vec3 *colors, const float *ranges) {
    float posData[MAX_POINT_LIGHTS * 4] = {};
    float colData[MAX_POINT_LIGHTS * 4] = {};
    for (int i = 0; i < count && i < MAX_POINT_LIGHTS; ++i) {
      posData[i * 4 + 0] = positions[i].x;
      posData[i * 4 + 1] = positions[i].y;
      posData[i * 4 + 2] = positions[i].z;
      posData[i * 4 + 3] = ranges[i];
      colData[i * 4 + 0] = colors[i].x;
      colData[i * 4 + 1] = colors[i].y;
      colData[i * 4 + 2] = colors[i].z;
      colData[i * 4 + 3] = 0.0f;
    }
    int n = (count > 0) ? count : 1;
    bgfx::setUniform(getOrCreate("u_lightPosRange", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS),
                     posData, n);
    bgfx::setUniform(getOrCreate("u_lightColorArr", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS),
                     colData, n);
    float countVec[4] = {(float)count, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(getOrCreate("u_lightCount", bgfx::UniformType::Vec4), countVec);
  }

private:
  static bgfx::ShaderHandle loadBin(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
      fprintf(stderr, "[BgfxShader] Failed to open: %s\n", path);
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

  bgfx::UniformHandle getOrCreate(const char *name, bgfx::UniformType::Enum type,
                                   uint16_t num = 1) {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
      return it->second;
    bgfx::UniformHandle h = bgfx::createUniform(name, type, num);
    m_uniformCache[name] = h;
    return h;
  }

  std::unordered_map<std::string, bgfx::UniformHandle> m_uniformCache;
};

// Type alias so game code can use "Shader" in both paths
using Shader = BgfxShader;

#endif // SHADER_HPP

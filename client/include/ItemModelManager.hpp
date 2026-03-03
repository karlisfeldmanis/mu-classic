#ifndef MU_ITEM_MODEL_MANAGER_HPP
#define MU_ITEM_MODEL_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "ViewerCommon.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Shader;

struct ItemShadowMesh {
  GLuint vao = 0, vbo = 0;
  int vertexCount = 0;
};

struct LoadedItemModel {
  std::shared_ptr<BMDData> bmd;
  std::vector<MeshBuffers> meshes;           // Static buffers (bind pose)
  std::vector<ItemShadowMesh> shadowMeshes;  // Dynamic shadow projection buffers
  glm::vec3 transformedMin{0};               // AABB from bone-transformed vertices
  glm::vec3 transformedMax{0};
};

class ItemModelManager {
public:
  static void Init(Shader *shader, const std::string &dataPath);
  static LoadedItemModel *Get(const std::string &filename);
  static void RenderItemUI(const std::string &modelFile, int16_t defIndex,
                           int x, int y, int w, int h, bool hovered = false);
  static void RenderItemWorld(const std::string &filename, const glm::vec3 &pos,
                              const glm::mat4 &view, const glm::mat4 &proj,
                              float scale = 1.0f,
                              glm::vec3 rotation = glm::vec3(0),
                              int16_t defIndex = -1);

  // Main 5.2: ItemLight — returns BlendMesh index for weapons with glow (-1 = none)
  static int GetItemBlendMesh(int category, int itemIndex);
  // Shadow projection for ground items (same technique as monsters/NPCs)
  static void RenderItemWorldShadow(const std::string &filename,
                                    const glm::vec3 &pos,
                                    const glm::mat4 &view,
                                    const glm::mat4 &proj, float scale = 1.0f,
                                    glm::vec3 rotation = glm::vec3(0));

private:
  static std::map<std::string, LoadedItemModel> s_cache;
  static Shader *s_shader;
  static std::unique_ptr<Shader> s_shadowShader;
  static std::string s_dataPath;
  // Player.bmd skeleton for computing idle-pose bones for body part items
  static std::shared_ptr<BMDData> s_playerBmd;
  static std::vector<BoneWorldMatrix> s_playerIdleBones;
};

#endif // MU_ITEM_MODEL_MANAGER_HPP

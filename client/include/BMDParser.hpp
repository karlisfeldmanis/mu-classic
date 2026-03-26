#ifndef BMD_PARSER_HPP
#define BMD_PARSER_HPP

#include "BMDStructs.hpp"
#include <memory>
#include <string>

struct BMDData {
  char Version;
  std::string Name;
  std::vector<Mesh_t> Meshes;
  std::vector<Bone_t> Bones;
  std::vector<Action_t> Actions;
  glm::vec3 Min = glm::vec3(0.0f);
  glm::vec3 Max = glm::vec3(0.0f);
};

class BMDParser {
public:
  static std::unique_ptr<BMDData> Parse(const std::string &path,
                                         bool quiet = false);

private:
  static void Decrypt(uint8_t *dst, const uint8_t *src, size_t size);
};

#endif // BMD_PARSER_HPP

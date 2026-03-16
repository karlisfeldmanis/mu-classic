#ifndef FIRE_EFFECT_HPP
#define FIRE_EFFECT_HPP

#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Fire offset table: MU local-space offsets where fire spawns on each object
// type
struct FireOffsetEntry {
  int type;
  std::vector<glm::vec3> offsets; // MU local coords (x, y, z)
};

// Returns MU-local fire offsets for a given object type, or empty if no fire
// mapId is required because object types are map-specific (e.g. type 30 = stone
// in Lorencia but fireplace in Devias)
const std::vector<glm::vec3> &GetFireOffsets(int objectType, int mapId);

// Returns MU-local smoke offsets for a given object type, or empty if no smoke
const std::vector<glm::vec3> &GetSmokeOffsets(int objectType, int mapId);

// Returns the object type for a BMD filename, or -1 if not a fire/smoke object
int GetFireTypeFromFilename(const std::string &bmdFilename);

class FireEffect {
public:
  void Init(const std::string &effectDataPath);
  void ClearEmitters();
  void AddEmitter(const glm::vec3 &worldPos); // Fire emitter (GL world coords)
  void
  AddSmokeEmitter(const glm::vec3 &worldPos); // Smoke emitter (gray, slower)
  void
  AddWaterSmokeEmitter(const glm::vec3 &worldPos); // Water mist (blue tint)
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &projection);
  void Cleanup();

  int GetEmitterCount() const { return (int)emitters.size(); }
  int GetParticleCount() const { return (int)particles.size(); }

private:
  struct Emitter {
    glm::vec3 position;
    float spawnAccum = 0.0f;
    bool smoke = false; // true = gray smoke, false = orange fire
    bool water = false; // true = blue water mist
  };

  struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float scale;
    float rotation;
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
    bool isWater = false;
  };

  // Per-instance GPU data (must match vertex attribute layout)
  struct InstanceData {
    glm::vec3 worldPos;
    float scale;
    float rotation;
    float frame;
    glm::vec3 color;
    float alpha;
  };

  std::vector<Emitter> emitters;
  std::vector<Particle> particles;

  TexHandle fireTexture = kInvalidTex;
  TexHandle waterTexture = kInvalidTex;
  std::unique_ptr<Shader> billboardShader;
  bgfx::VertexBufferHandle quadVBO = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle quadEBO = BGFX_INVALID_HANDLE;
  // No instanceVBO — BGFX uses transient InstanceDataBuffer per frame
  void submitBatch(const std::vector<InstanceData> &batch, TexHandle tex);

  static constexpr int MAX_PARTICLES = 4096;
  static constexpr float PARTICLE_LIFETIME = 1.0f;
  static constexpr float SPAWN_RATE = 12.0f; // particles/sec per emitter
};

#endif // FIRE_EFFECT_HPP

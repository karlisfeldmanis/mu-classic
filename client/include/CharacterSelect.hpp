#ifndef MU_CHARACTER_SELECT_HPP
#define MU_CHARACTER_SELECT_HPP

#include "ServerConnection.hpp"
#include "Shader.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;
struct ImDrawList;

namespace CharacterSelect {

static constexpr int MAX_SLOTS = 4;

struct CharSlotEquip {
  uint8_t category = 0xFF; // 0xFF = empty/default
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;   // +0 to +15 enhancement (for glow rendering)
};

struct CharSlot {
  bool occupied = false;
  char name[11] = {};
  uint8_t classCode = 0; // 0=DW, 16=DK, 32=ELF, 48=MG
  uint16_t level = 0;
  // Equipment appearance from charSet encoding
  // 0=rightHand, 1=leftHand, 2=helm, 3=armor, 4=pants, 5=gloves, 6=boots
  CharSlotEquip equip[7];
  // Slot 7: wings (category 12: 0=Elf, 1=Heaven, 2=Satan, 3=Spirits, 4=Soul, 5=Dragon, 6=Darkness)
  CharSlotEquip wingEquip;
  // Slot 8: pet/mount (category 13: 0=Angel, 1=Imp, 2=Uniria, 3=Dinorant)
  CharSlotEquip petMount;
};

struct Context {
  ServerConnection *server;
  std::string dataPath;
  GLFWwindow *window = nullptr;
  // Callback to transition game state when character is selected
  std::function<void()> onCharSelected;
  // Callback for exit/quit
  std::function<void()> onExit;
  // Callback for fullscreen toggle (uses main.cpp's saved window geometry)
  std::function<void()> onToggleFullscreen;
};

void Init(const Context &ctx);
void Shutdown();

// Called when server sends character list (F3:00)
void SetCharacterList(const CharSlot *slots, int count);

// Called when server sends create result (F3:01)
void OnCreateResult(uint8_t result, const char *name, uint8_t slot,
                    uint8_t classCode);

// Called when server sends delete result (F3:02)
void OnDeleteResult(uint8_t result);

void Update(float dt);
void Render(int windowWidth, int windowHeight);

// Mouse/keyboard input
void OnMouseClick(double screenX, double screenY, int windowWidth,
                  int windowHeight);
void OnKeyPress(int key);
void OnCharInput(unsigned int codepoint);

// Query if creation modal is open (for input routing)
bool IsCreateModalOpen();

} // namespace CharacterSelect

#endif // MU_CHARACTER_SELECT_HPP

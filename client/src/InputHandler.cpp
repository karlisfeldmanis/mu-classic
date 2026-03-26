#include "InputHandler.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "ClientTypes.hpp"
#include "SoundManager.hpp"
#include "SystemMessageLog.hpp"
#include "HeroCharacter.hpp"
#include "InventoryUI.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "ObjectRenderer.hpp"
#include "RayPicker.hpp"
#include "ServerConnection.hpp"
#include "TextureLoader.hpp"
#include "UICoords.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <glm/glm.hpp>
#include <iostream>

// ── Stored context (set once via Init) ──
static InputContext s_ctxStore;
static const InputContext *s_ctx = &s_ctxStore;

// Heal spell cooldown (client-side lock to prevent packet spam)
static float s_healCooldown = 0.0f;
static constexpr float HEAL_COOLDOWN_TIME = 2.0f;

// Check if player has correct ammo for bow/crossbow. Returns true if OK or not ranged.
static bool CheckRangedAmmo() {
  if (!s_ctx->hero || !s_ctx->equipSlots) return true;
  if (s_ctx->hero->GetWeaponCategory() != 4) return true; // Not bow/crossbow

  auto &weapon = s_ctx->equipSlots[0]; // Right hand
  auto &ammo = s_ctx->equipSlots[1];   // Left hand (arrows/bolts)
  bool isCrossbow = (weapon.category == 4 &&
      ((weapon.itemIndex >= 8 && weapon.itemIndex <= 14) ||
       weapon.itemIndex == 16 || weapon.itemIndex == 18));

  bool correctAmmo = isCrossbow
      ? (ammo.category == 4 && ammo.itemIndex == 7 && ammo.quantity > 0)
      : (ammo.category == 4 && ammo.itemIndex == 15 && ammo.quantity > 0);

  if (!correctAmmo) {
    InventoryUI::ShowNotification(isCrossbow
        ? "This weapon requires Bolts!" : "This weapon requires Arrows!");
    SoundManager::Play(SOUND_ERROR01);
    return false;
  }
  return true;
}
static bool s_ctxInitialized = false;

// Pending NPC interaction (walk to NPC then open shop)
static int s_pendingNpcIdx = -1;

// Pending interactive object interaction (sit/pose — walk to object then trigger)
static int s_pendingInteractIdx = -1;
static constexpr float INTERACT_TRIGGER_RANGE = 100.0f; // Trigger sit/pose within this

// Distance thresholds for NPC interaction
static constexpr float NPC_INTERACT_RANGE =
    200.0f; // Open shop when within this range
static constexpr float NPC_STOP_OFFSET =
    150.0f; // Stop this far from NPC center
static constexpr float NPC_CLOSE_RANGE = 500.0f; // Auto-close shop beyond this

// Set true after first ProcessInput — prevents callbacks during early init
static bool s_gameReady = false;

// Skill quickslot toggle state (1-4 keys toggle RMC skill)
static int8_t s_savedRmcSkillId = -1; // Original RMC before quickslot override
static int s_activeQuickSlot = -1;    // Which slot (0-3) is active, -1=none

// Main 5.2 custom cursors loaded from OZT files in Data/Interface/
static GLFWcursor *s_cursorDefault = nullptr;     // Cursor.ozt
static GLFWcursor *s_cursorAttack = nullptr;      // CursorAttack.ozt
static GLFWcursor *s_cursorGet = nullptr;         // CursorGet.ozt
static GLFWcursor *s_cursorSitDown = nullptr;     // CursorSitDown.ozt
static GLFWcursor *s_cursorLeanAgainst = nullptr; // CursorLeanAgainst.ozt
static GLFWcursor *s_cursorTalk = nullptr;        // CursorTalk.ozt
static GLFWcursor *s_cursorDontMove = nullptr;    // CursorDontMove.OZT
static GLFWcursor *s_cursorPush = nullptr;        // CursorPush.ozt
static GLFWcursor *s_cursorEye = nullptr;         // CursorEye.ozt
static GLFWcursor *s_cursorRepair = nullptr;      // CursorRepair.OZT
static GLFWcursor *s_cursorAddIn = nullptr;       // CursorAddIn.OZT
static GLFWcursor *s_cursorFallback = nullptr;    // GLFW standard arrow (fallback)
static GLFWwindow *s_window = nullptr;

// Nearest-neighbor downscale RGBA image
static std::vector<unsigned char> DownscaleRGBA(const unsigned char *src,
                                                int srcW, int srcH,
                                                int dstW, int dstH) {
  std::vector<unsigned char> dst((size_t)dstW * dstH * 4);
  for (int y = 0; y < dstH; y++) {
    int sy = y * srcH / dstH;
    for (int x = 0; x < dstW; x++) {
      int sx = x * srcW / dstW;
      std::memcpy(&dst[(y * dstW + x) * 4], &src[(sy * srcW + sx) * 4], 4);
    }
  }
  return dst;
}

// Load an OZT file as a GLFW custom cursor.
// LoadOZTRaw V-flips TGA data — the result is already top-to-bottom which GLFW expects.
// For animated sprite sheets (e.g. 64x64 = 2x2 grid of 32x32), extract first frame.
// Cursors are scaled to 24x24 to match typical display DPI.
static constexpr int CURSOR_SIZE = 24;
static GLFWcursor *LoadOZTCursor(const std::string &path, int hotX = 0,
                                 int hotY = 0) {
  int w, h;
  auto pixels = TextureLoader::LoadOZTRaw(path, w, h);
  if (pixels.empty())
    return nullptr;
  int bpp = (int)pixels.size() / (w * h); // 3 (RGB) or 4 (RGBA)
  // GLFW requires RGBA — convert RGB to RGBA if needed
  std::vector<unsigned char> rgba;
  if (bpp == 3) {
    rgba.resize((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
      rgba[i * 4 + 0] = pixels[i * 3 + 0];
      rgba[i * 4 + 1] = pixels[i * 3 + 1];
      rgba[i * 4 + 2] = pixels[i * 3 + 2];
      rgba[i * 4 + 3] = 255;
    }
  } else {
    rgba = std::move(pixels);
  }
  // For sprite sheets larger than 32x32, extract first 32x32 frame (top-left)
  if (w > 32 && h > 32) {
    std::vector<unsigned char> frame(32 * 32 * 4);
    for (int y = 0; y < 32; y++)
      std::memcpy(&frame[y * 32 * 4], &rgba[y * w * 4], 32 * 4);
    rgba = std::move(frame);
    w = 32;
    h = 32;
  }
  // Scale hotspot proportionally
  int scaledHotX = hotX * CURSOR_SIZE / w;
  int scaledHotY = hotY * CURSOR_SIZE / h;
  // Downscale to target cursor size
  auto scaled = DownscaleRGBA(rgba.data(), w, h, CURSOR_SIZE, CURSOR_SIZE);
  GLFWimage img = {CURSOR_SIZE, CURSOR_SIZE, scaled.data()};
  return glfwCreateCursor(&img, scaledHotX, scaledHotY);
}

// ── GLFW callbacks ──

static void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
  if (!s_ctxInitialized)
    return;

  // Update NPC, Monster, and Ground Item hover state on cursor move
  // Block hover picking only when cursor is actually over a UI panel
  bool mouseOverUI = ImGui::GetIO().WantCaptureMouse ||
      (s_ctx->mouseOverUIPanel && *s_ctx->mouseOverUIPanel);
  if (!mouseOverUI) {
    *s_ctx->hoveredNpc = RayPicker::PickNpc(window, xpos, ypos);
    // Also check NPC label hover (only after game is fully initialized)
    if (*s_ctx->hoveredNpc < 0 && s_gameReady) {
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      glm::mat4 view = s_ctx->camera->GetViewMatrix();
      glm::mat4 proj =
          s_ctx->camera->GetProjectionMatrix((float)winW, (float)winH);
      glm::vec3 camPos = s_ctx->camera->GetPosition();
      *s_ctx->hoveredNpc = s_ctx->npcMgr->PickLabel(
          (float)xpos, (float)ypos, view, proj, winW, winH, camPos);
    }
    if (*s_ctx->hoveredNpc < 0) {
      // Ground items have higher pick priority than monsters so loot is easy
      *s_ctx->hoveredGroundItem = RayPicker::PickGroundItem(window, xpos, ypos);
      if (*s_ctx->hoveredGroundItem < 0) {
        *s_ctx->hoveredMonster = RayPicker::PickMonster(window, xpos, ypos);
      } else {
        *s_ctx->hoveredMonster = -1;
      }
    } else {
      *s_ctx->hoveredMonster = -1;
      *s_ctx->hoveredGroundItem = -1;
    }
  } else {
    *s_ctx->hoveredNpc = -1;
    *s_ctx->hoveredMonster = -1;
    *s_ctx->hoveredGroundItem = -1;
  }

  // Update cursor based on hover state (Main 5.2: CursorId switching)
  // Only use game cursors when in-game; default cursor over UI panels and char select
  if (s_window) {
    GLFWcursor *cursor = s_cursorDefault;
    if (s_gameReady && !mouseOverUI) {
      if (*s_ctx->hoveredMonster >= 0 && s_ctx->monsterMgr &&
          !s_ctx->monsterMgr->IsOwnSummon(*s_ctx->hoveredMonster)) {
        cursor = s_cursorAttack;
      } else if (*s_ctx->hoveredNpc >= 0) {
        cursor = s_cursorTalk;
      } else if (*s_ctx->hoveredGroundItem >= 0) {
        cursor = s_cursorGet;
      } else if (!s_ctx->hero || !s_ctx->hero->IsMounted()) {
        int interactHit =
            RayPicker::PickInteractiveObject(window, xpos, ypos);
        if (interactHit >= 0 && s_ctx->objectRenderer) {
          const auto &objs =
              s_ctx->objectRenderer->GetInteractiveObjects();
          if (interactHit < (int)objs.size()) {
            if (objs[interactHit].action ==
                ObjectRenderer::InteractType::SIT)
              cursor = s_cursorSitDown;
            else
              cursor = s_cursorLeanAgainst;
          }
        }
      }
    }
    if (!cursor)
      cursor = s_cursorFallback;
    glfwSetCursor(s_window, cursor);
  }
}

static void scroll_callback(GLFWwindow *window, double xoffset,
                            double yoffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

  // Chat log scroll: consume scroll if cursor is over the log
  double mx, my;
  glfwGetCursorPos(window, &mx, &my);
  if (SystemMessageLog::HandleScroll((float)mx, (float)my, (float)yoffset))
    return;

  // Suppress zoom when cursor is over any UI panel (quest dialog, inventory, etc.)
  if (s_ctx->mouseOverUIPanel && *s_ctx->mouseOverUIPanel)
    return;

  if (s_gameReady) {
    s_ctx->camera->ProcessMouseScroll(yoffset);
    // Persist zoom to server
    uint16_t z = static_cast<uint16_t>(s_ctx->camera->GetZoom() * 10.0f);
    s_ctx->server->SendCameraZoom(z);
  }
}

static void HandlePickupClick(GLFWwindow *window, double mx, double my) {
  // Allow pickup with inventory/charInfo open (MU Online allows this)
  // Only block if currently dragging an item
  if (InventoryUI::IsDragging())
    return;

  if (*s_ctx->hoveredGroundItem != -1) {
    int bestIdx = *s_ctx->hoveredGroundItem;
    float distToHero = glm::distance(s_ctx->hero->GetPosition(),
                                     s_ctx->groundItems[bestIdx].position);

    if (distToHero < 150.0f) {
      // Close enough, pick up immediately
      s_ctx->hero->CancelAttack();
      s_ctx->hero->StopMoving();
      s_ctx->server->SendPickup(s_ctx->groundItems[bestIdx].dropIndex);
      std::cout << "[Pickup] Sent direct pickup for index "
                << s_ctx->groundItems[bestIdx].dropIndex << " (Close range)"
                << std::endl;
      s_ctx->hero->ClearPendingPickup();
    } else {
      // Too far, move to it and set pending pickup
      s_ctx->hero->CancelAttack();
      s_ctx->hero->MoveTo(s_ctx->groundItems[bestIdx].position);
      s_ctx->hero->SetPendingPickup(bestIdx);
      std::cout << "[Pickup] Moving to item index "
                << s_ctx->groundItems[bestIdx].dropIndex << std::endl;
    }
  }
}

static bool IsGuardNpc(uint16_t npcType) {
  return (npcType >= 245 && npcType <= 249) ||
         npcType == 256 ||
         (npcType >= 310 && npcType <= 312);
}

static void OpenNpcDialog(int npcIdx, const NpcInfo &info) {
  if (IsGuardNpc(info.type)) {
    // Guard NPCs open quest dialog — always start at list view
    *s_ctx->questDialogOpen = true;
    *s_ctx->questDialogNpcIndex = npcIdx;
    *s_ctx->questDialogSelected = -1;
    s_ctx->hero->StopMoving();
    // Tell server to pause guard patrol
    s_ctx->server->SendNpcInteract(info.type, true);
  } else {
    printf("[Input] Opening shop for NPC idx=%d type=%d name=%s\n",
           npcIdx, info.type, info.name.c_str());
    s_ctx->server->SendShopOpen(info.type);
  }
}

static void HandleNpcInteraction(int npcIdx) {
  *s_ctx->selectedNpc = npcIdx;
  s_ctx->hero->CancelAttack();
  s_ctx->hero->ClearPendingPickup();

  NpcInfo info = s_ctx->npcMgr->GetNpcInfo(npcIdx);
  float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);

  if (dist < NPC_INTERACT_RANGE) {
    OpenNpcDialog(npcIdx, info);
    s_pendingNpcIdx = -1;
  } else {
    // Walk to a point NPC_STOP_OFFSET away from the NPC, toward the hero
    glm::vec3 heroPos = s_ctx->hero->GetPosition();
    glm::vec3 dir = heroPos - info.position;
    dir.y = 0;
    float len = glm::length(dir);
    if (len > 0.01f) {
      glm::vec3 stopPos = info.position + (dir / len) * NPC_STOP_OFFSET;
      s_ctx->hero->MoveTo(stopPos);
    } else {
      s_ctx->hero->MoveTo(info.position);
    }
    s_pendingNpcIdx = npcIdx;
  }
}

// --- Click-to-move mouse handler ---

static void mouse_button_callback(GLFWwindow *window, int button, int action,
                                  int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
  if (!s_ctxInitialized || !s_gameReady)
    return;

  // Block world interactions while Game Menu, skill learning, teleport, or mount toggle is active
  if (s_ctx->showGameMenu && *s_ctx->showGameMenu)
    return;
  if (s_ctx->isLearningSkill && *s_ctx->isLearningSkill)
    return;
  if (s_ctx->teleportingToTown && *s_ctx->teleportingToTown)
    return;
  if (s_ctx->mountToggling && *s_ctx->mountToggling)
    return;

  // Allow HUD button clicks even when quest dialog/log is open
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);
      if (InventoryUI::HandlePanelClick(vx, vy))
        return;
    }
  }

  // Block world clicks while quest dialog is open (modal)
  if (s_ctx->questDialogOpen && *s_ctx->questDialogOpen)
    return;
  // Quest log: only block clicks that land on the panel itself
  if (s_ctx->showQuestLog && *s_ctx->showQuestLog) {
    if (s_ctx->mouseOverUIPanel && *s_ctx->mouseOverUIPanel)
      return;
  }

  // Click-to-move on left click (NPC click takes priority)
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    bool overUI = ImGui::GetIO().WantCaptureMouse ||
                  (s_ctx->mouseOverUIPanel && *s_ctx->mouseOverUIPanel);
    if (!overUI) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);

      // Check system message log tabs first
      if (SystemMessageLog::HandleClick((float)mx, (float)my))
        return;

      // Check if click is on a UI panel or HUD first
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);
      if (InventoryUI::HandlePanelClick(vx, vy))
        return;

      // Cancel sit/pose on any world click — character should stand up
      if (s_ctx->hero->IsSittingOrPosing()) {
        s_ctx->hero->CancelSitPose();
        s_pendingInteractIdx = -1; // Prevent ProcessInput from re-triggering sit
        glm::vec3 target;
        if (RayPicker::ScreenToTerrain(window, mx, my, target)) {
          if (RayPicker::IsWalkable(target.x, target.z)) {
            s_ctx->hero->MoveTo(target);
            s_ctx->clickEffect->Show(target);
          }
        }
        return;
      }

      // Highest priority interactions: NPC > Monster > Ground Item > Movement
      int npcHit = RayPicker::PickNpc(window, mx, my);

      // Also check NPC label click (2D screen-space hit test)
      if (npcHit < 0 && s_gameReady) {
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);
        glm::mat4 view = s_ctx->camera->GetViewMatrix();
        glm::mat4 proj =
            s_ctx->camera->GetProjectionMatrix((float)winW, (float)winH);
        glm::vec3 camPos = s_ctx->camera->GetPosition();
        npcHit = s_ctx->npcMgr->PickLabel((float)mx, (float)my, view, proj,
                                          winW, winH, camPos);
      }

      if (npcHit >= 0) {
        HandleNpcInteraction(npcHit);
      } else {
        // Preserve selectedNpc while shop is open (needed for auto-close
        // distance check)
        if (!*s_ctx->shopOpen) {
          *s_ctx->selectedNpc = -1;
        }
        s_pendingNpcIdx = -1;
        // Re-pick at click time for reliable priority (NPC > Item > Monster)
        int itemHit = RayPicker::PickGroundItem(window, mx, my);
        int monHit =
            (itemHit < 0) ? RayPicker::PickMonster(window, mx, my) : -1;
        // Update hover state to match click picks
        *s_ctx->hoveredGroundItem = itemHit;
        if (itemHit >= 0)
          *s_ctx->hoveredMonster = -1;
        else
          *s_ctx->hoveredMonster = monHit;

        if (itemHit >= 0) {
          HandlePickupClick(window, mx, my);
        } else if (monHit >= 0 &&
                   !s_ctx->monsterMgr->IsSummon(s_ctx->monsterMgr->GetServerIndex(monHit))) {
          // Start attack — dismount happens later when the attack actually fires
          if (!CheckRangedAmmo()) {
            // Block attack — wrong or missing ammo for bow/crossbow
          } else {
            MonsterInfo info = s_ctx->monsterMgr->GetMonsterInfo(monHit);
            s_ctx->hero->AttackMonster(monHit, info.position);
            s_ctx->hero->ClearPendingPickup();
          }
        } else {
          // Check interactive world objects (chairs, pose boxes)
          // Skip if already sitting/posing or mounted — can't pose while riding
          int interactHit = (s_ctx->hero->IsSittingOrPosing() ||
                             s_ctx->hero->IsMounted())
              ? -1
              : RayPicker::PickInteractiveObject(window, mx, my);
          if (interactHit >= 0 && s_ctx->objectRenderer) {
            const auto &objs =
                s_ctx->objectRenderer->GetInteractiveObjects();
            const auto &obj = objs[interactHit];
            float dist = glm::distance(s_ctx->hero->GetPosition(),
                                       obj.worldPos);
            s_ctx->hero->CancelAttack();
            s_ctx->hero->ClearPendingPickup();
            if (dist < INTERACT_TRIGGER_RANGE) {
              // Close enough — sit/pose immediately
              bool isSit =
                  (obj.action == ObjectRenderer::InteractType::SIT);
              s_ctx->hero->StartSitPose(isSit, obj.facingAngle,
                                        obj.alignToObject, obj.worldPos);
              s_pendingInteractIdx = -1;
            } else {
              // Walk to object, trigger on arrival
              s_ctx->hero->MoveTo(obj.worldPos);
              s_pendingInteractIdx = interactHit;
            }
          } else {
          // Ground click — move to terrain
          // Block movement cancel before hit frame for melee (prevents animation cancel exploit)
          // Ranged (bow/crossbow) always cancellable — no melee exploit risk
          bool swingBlocked =
              s_ctx->hero->GetAttackState() == AttackState::SWINGING &&
              !s_ctx->hero->HasRegisteredHit() &&
              !(s_ctx->hero->HasWeapon() && s_ctx->hero->GetWeaponCategory() == 4);
          if (swingBlocked) {
            // Melee: can't cancel before hit frame — must commit to the swing
          } else {
            // Cancel attack target when clicking terrain to move
            if (s_ctx->hero->GetAttackTarget() >= 0)
              s_ctx->hero->CancelAttack();
            s_ctx->hero->ClearPendingPickup(); // Cancel pickup if manually moving
            s_pendingInteractIdx = -1;         // Cancel pending interact
            glm::vec3 target;
            if (RayPicker::ScreenToTerrain(window, mx, my, target)) {
              if (RayPicker::IsWalkable(target.x, target.z)) {
                s_ctx->hero->MoveTo(target);
                s_ctx->clickEffect->Show(target);
              }
            } else {
            }
          }
          }
        }
      }
    }
  }

  // Track right mouse held state for continuous skill casting
  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (s_ctx->rightMouseHeld)
      *s_ctx->rightMouseHeld = (action == GLFW_PRESS);
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);

      // Try UI panel right-click first (sell, orb use)
      if (!InventoryUI::HandlePanelRightClick(vx, vy)) {
        // Block world skill casting when cursor is over a UI panel
        if (s_ctx->mouseOverUIPanel && *s_ctx->mouseOverUIPanel)
          return;
        // Not on UI panel — try skill attack on monster
        if (s_ctx->rmcSkillId && *s_ctx->rmcSkillId >= 0) {
          uint8_t skillId = (uint8_t)*s_ctx->rmcSkillId;
          int cost = InventoryUI::GetSkillResourceCost(skillId);
          bool isDK = s_ctx->hero && s_ctx->hero->GetClass() == 16;
          int currentResource = isDK ? (s_ctx->serverAG ? *s_ctx->serverAG : 0)
                                     : (s_ctx->serverMP ? *s_ctx->serverMP : 0);
          if (s_ctx->hero && s_ctx->hero->IsInSafeZone()) {
            InventoryUI::ShowNotification("Cannot use skills in safe zone!");
            SoundManager::Play(SOUND_ERROR01);
          } else if (currentResource < cost) {
            InventoryUI::ShowNotification(isDK ? "Not enough AG!" : "Not enough Mana!");
            SoundManager::Play(SOUND_ERROR01);
          } else {
            // Dismount before using any skill (only autoattack allowed on mount)
            if (s_ctx->hero->IsMounted())
              s_ctx->hero->UnequipMount();
            if (skillId == 6) {
            // Teleport — ground-targeted (Main 5.2: AT_SKILL_TELEPORT)
            glm::vec3 target;
            if (RayPicker::ScreenToTerrain(window, mx, my, target)) {
              if (RayPicker::IsWalkable(target.x, target.z)) {
                uint8_t gridX = (uint8_t)(target.z / 100.0f);
                uint8_t gridY = (uint8_t)(target.x / 100.0f);
                s_ctx->server->SendTeleport(gridX, gridY);
                s_ctx->hero->TeleportTo(target);
                s_ctx->hero->ClearPendingPickup();
              }
            }
          } else if (skillId >= 26 && skillId <= 28) {
            // Elf buff skills — self-cast, instant (no animation)
            if (skillId == 26 && s_healCooldown > 0.0f) {
              InventoryUI::ShowNotification("Heal is on cooldown!");
              SoundManager::Play(SOUND_ERROR01);
            } else {
              s_ctx->server->SendSkillAttack(0xFFFF, skillId, 0, 0);
              // Heal uses warm heart sound, Defense/Damage buffs use mystical evil sound
              SoundManager::Play(skillId == 26 ? SOUND_HEART : SOUND_SOULBARRIER);
              if (s_ctx->serverMP) *s_ctx->serverMP -= cost;
              if (skillId == 26) s_healCooldown = HEAL_COOLDOWN_TIME;
            }
          } else if (skillId >= 30 && skillId <= 35) {
            // Summon skills — self-cast, no target needed
            // Re-casting same summon = toggle off (server handles despawn)
            s_ctx->server->SendSkillAttack(0xFFFF, skillId, 0, 0);
            if (!(s_ctx->monsterMgr && s_ctx->monsterMgr->HasOwnSummon())) {
              glm::vec3 heroPos = s_ctx->hero->GetPosition();
              s_ctx->hero->CastSelfAoE(skillId, heroPos);
            }
            SoundManager::Play(SOUND_MAGIC);
            if (s_ctx->serverMP) *s_ctx->serverMP -= cost;
          } else if (skillId == 8 || skillId == 9 || skillId == 10 || skillId == 12 || skillId == 14
                     || skillId == 41 || skillId == 42 || skillId == 43) {
            // AoE skills: DW spells + DK melee AoE (Twisting Slash, Rageful Blow, Death Stab)
            // GCD blocks spam, but allow interrupting normal melee attacks
            bool gcdReady = s_ctx->hero->GetGlobalCooldown() <= 0.0f ||
                            s_ctx->hero->GetActiveSkillId() == 0;
            if (gcdReady) {
              int monHit = RayPicker::PickMonster(window, mx, my);
              if (monHit >= 0) {
                MonsterInfo info = s_ctx->monsterMgr->GetMonsterInfo(monHit);
                s_ctx->hero->SkillAttackMonster(monHit, info.position, skillId);
              } else {
                glm::vec3 heroPos = s_ctx->hero->GetPosition();
                // DK melee AoE: always caster-centered
                bool isMeleeAoE = (skillId == 41 || skillId == 42 || skillId == 43);
                glm::vec3 groundTarget = heroPos;
                if (!isMeleeAoE)
                  RayPicker::ScreenToTerrain(window, mx, my, groundTarget);
                s_ctx->hero->CastSelfAoE(skillId, isMeleeAoE ? heroPos : groundTarget);
                // Caster-centered: Evil Spirit/Hellfire/Inferno/DK melee AoE
                // Directional: Twister/Flash
                bool casterCentered = (skillId == 9 || skillId == 10 || skillId == 14 || isMeleeAoE);
                float atkX = casterCentered ? heroPos.x : groundTarget.x;
                float atkZ = casterCentered ? heroPos.z : groundTarget.z;
                if (skillId == 12) {
                  // Flash: delay damage until beam spawns at frame 7.0
                  s_ctx->hero->SetPendingAquaPacket(0xFFFF, skillId, atkX, atkZ);
                } else {
                  s_ctx->server->SendSkillAttack(0xFFFF, skillId, atkX, atkZ);
                }
                // Optimistically deduct resource to prevent spam before server reply
                if (isDK && s_ctx->serverAG) *s_ctx->serverAG -= cost;
                else if (s_ctx->serverMP) *s_ctx->serverMP -= cost;
              }
            }
            s_ctx->hero->ClearPendingPickup();
          } else {
            int monHit = RayPicker::PickMonster(window, mx, my);
            if (monHit >= 0) {
              MonsterInfo info = s_ctx->monsterMgr->GetMonsterInfo(monHit);
              s_ctx->hero->SkillAttackMonster(monHit, info.position, skillId);
              s_ctx->hero->ClearPendingPickup();
            }
          }
          }
        }
      }
    }
  }

  // Mouse up: handle drag release
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    if (InventoryUI::IsDragging()) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);
      InventoryUI::HandlePanelMouseUp(window, vx, vy);
    }
  }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  // Note: do NOT check WantCaptureKeyboard here — it blocks game hotkeys
  // when ImGui panels have focus. Only text-input widgets need that guard.

  // Don't process game hotkeys during character select
  if (!s_gameReady)
    return;

  // Command terminal: Enter opens, Escape closes
  if (action == GLFW_PRESS && key == GLFW_KEY_ENTER &&
      s_ctx->showCommandTerminal && !*s_ctx->showCommandTerminal &&
      !ImGui::GetIO().WantTextInput) {
    *s_ctx->showCommandTerminal = true;
    return;
  }

  // Block game hotkeys while typing in command terminal or ImGui text
  if (ImGui::GetIO().WantTextInput)
    return;

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_C) {
      *s_ctx->showCharInfo = !*s_ctx->showCharInfo;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    if (key == GLFW_KEY_I) {
      *s_ctx->showInventory = !*s_ctx->showInventory;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    if (key == GLFW_KEY_S) {
      *s_ctx->showSkillWindow = !*s_ctx->showSkillWindow;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    if (key == GLFW_KEY_M) {
      *s_ctx->showMapWindow = !*s_ctx->showMapWindow;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    // T key removed — teleport/warp is done through the Map panel (M key)
    if (key == GLFW_KEY_L) {
      *s_ctx->showQuestLog = !*s_ctx->showQuestLog;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    if (key == GLFW_KEY_Q)
      InventoryUI::ConsumeQuickSlotItem(0);
    if (key == GLFW_KEY_W)
      InventoryUI::ConsumeQuickSlotItem(1);
    if (key == GLFW_KEY_E)
      InventoryUI::ConsumeQuickSlotItem(2);
    if (key == GLFW_KEY_R)
      InventoryUI::ConsumeQuickSlotItem(3);
    // Skill hotkeys 1-4: toggle RMC skill (press to activate, press again to restore)
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_4) {
      int idx = key - GLFW_KEY_1;
      if (s_ctx->skillBar[idx] != -1) {
        if (s_activeQuickSlot == idx) {
          // Same slot pressed again → restore original RMC
          *s_ctx->rmcSkillId = s_savedRmcSkillId;
          s_activeQuickSlot = -1;
          SoundManager::Play(SOUND_INTERFACE01);
        } else {
          // Different slot or first press → save original and activate
          if (s_activeQuickSlot == -1)
            s_savedRmcSkillId = *s_ctx->rmcSkillId;
          *s_ctx->rmcSkillId = s_ctx->skillBar[idx];
          s_activeQuickSlot = idx;
          SoundManager::Play(SOUND_INTERFACE01);
        }
      }
    }
    // Skill hotkeys 5-9, 0: permanent assignment (unchanged)
    if (key >= GLFW_KEY_5 && key <= GLFW_KEY_9) {
      int idx = key - GLFW_KEY_1;
      if (s_ctx->skillBar[idx] != -1)
        *s_ctx->rmcSkillId = s_ctx->skillBar[idx];
    }
    if (key == GLFW_KEY_0) {
      if (s_ctx->skillBar[9] != -1)
        *s_ctx->rmcSkillId = s_ctx->skillBar[9];
    }
    if (key == GLFW_KEY_ESCAPE) {
      SoundManager::Play(SOUND_CLICK01);
      // Close command terminal first if open
      if (s_ctx->showCommandTerminal && *s_ctx->showCommandTerminal) {
        *s_ctx->showCommandTerminal = false;
      } else if (*s_ctx->showGameMenu) {
        // Menu already open — close it
        *s_ctx->showGameMenu = false;
      } else {
        // First close any open panels; only open game menu if nothing was open
        bool closedSomething = false;
        if (*s_ctx->shopOpen) {
          *s_ctx->shopOpen = false;
          *s_ctx->selectedNpc = -1;
          closedSomething = true;
        }
        if (*s_ctx->showCharInfo) {
          *s_ctx->showCharInfo = false;
          closedSomething = true;
        }
        if (*s_ctx->showInventory) {
          *s_ctx->showInventory = false;
          closedSomething = true;
        }
        if (*s_ctx->showSkillWindow) {
          *s_ctx->showSkillWindow = false;
          closedSomething = true;
        }
        if (*s_ctx->showMapWindow) {
          *s_ctx->showMapWindow = false;
          closedSomething = true;
        }
        if (*s_ctx->questLogSelected >= 0) {
          *s_ctx->questLogSelected = -1;
          closedSomething = true;
        } else if (*s_ctx->showQuestLog) {
          *s_ctx->showQuestLog = false;
          closedSomething = true;
        }
        if (*s_ctx->questDialogOpen) {
          // Tell server to resume guard patrol
          if (*s_ctx->questDialogNpcIndex >= 0 &&
              *s_ctx->questDialogNpcIndex < s_ctx->npcMgr->GetNpcCount()) {
            NpcInfo qi = s_ctx->npcMgr->GetNpcInfo(*s_ctx->questDialogNpcIndex);
            s_ctx->server->SendNpcInteract(qi.type, false);
          }
          *s_ctx->questDialogOpen = false;
          *s_ctx->questDialogNpcIndex = -1;
          *s_ctx->questDialogSelected = -1;
          *s_ctx->selectedNpc = -1;
          closedSomething = true;
        }
        if (!closedSomething) {
          *s_ctx->showGameMenu = true;
        }
      }
    }
  }

  // TAB hold: show minimap while held, hide on release
  if (key == GLFW_KEY_TAB) {
    if (action == GLFW_PRESS) {
      *s_ctx->showMinimap = true;
    } else if (action == GLFW_RELEASE) {
      *s_ctx->showMinimap = false;
    }
  }
}

static void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

// ── Public API ──

void InputHandler::Init(const InputContext &ctx) {
  s_ctxStore = ctx;
  s_ctx = &s_ctxStore;
  s_ctxInitialized = true;
}

void InputHandler::RegisterCallbacks(GLFWwindow *window) {
  s_window = window;

  // Load Main 5.2 custom cursors from OZT files
  std::string ifDir = s_ctx->dataPath + "/Interface/";
  s_cursorDefault = LoadOZTCursor(ifDir + "Cursor.ozt");
  s_cursorAttack = LoadOZTCursor(ifDir + "CursorAttack.ozt");
  s_cursorGet = LoadOZTCursor(ifDir + "CursorGet.ozt");
  s_cursorSitDown = LoadOZTCursor(ifDir + "CursorSitDown.ozt");
  s_cursorLeanAgainst = LoadOZTCursor(ifDir + "CursorLeanAgainst.ozt");
  s_cursorTalk = LoadOZTCursor(ifDir + "CursorTalk.ozt");
  s_cursorDontMove = LoadOZTCursor(ifDir + "CursorDontMove.OZT");
  s_cursorPush = LoadOZTCursor(ifDir + "CursorPush.ozt");
  s_cursorEye = LoadOZTCursor(ifDir + "CursorEye.ozt");
  s_cursorRepair = LoadOZTCursor(ifDir + "CursorRepair.OZT");
  s_cursorAddIn = LoadOZTCursor(ifDir + "CursorAddIn.OZT");
  // Standard arrow as fallback if OZT loading fails
  s_cursorFallback = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
  if (!s_cursorDefault)
    s_cursorDefault = s_cursorFallback;
  if (!s_cursorAttack)
    s_cursorAttack = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
  if (!s_cursorTalk)
    s_cursorTalk = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
  if (!s_cursorGet)
    s_cursorGet = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetCharCallback(window, char_callback);
}

void InputHandler::ResetGameReady() { s_gameReady = false; }

int InputHandler::GetActiveQuickSlot() { return s_activeQuickSlot; }

void InputHandler::RestoreQuickSlotState() {
  // Reset first — each character has its own state
  s_activeQuickSlot = -1;
  s_savedRmcSkillId = -1;

  if (!s_ctx->rmcSkillId || !s_ctx->skillBar)
    return;
  int8_t rmc = *s_ctx->rmcSkillId;
  if (rmc < 0)
    return;
  // Check if current RMC matches any of slots 0-3
  for (int i = 0; i < 4; i++) {
    if (s_ctx->skillBar[i] == rmc) {
      s_activeQuickSlot = i;
      return;
    }
  }
}

// --- Process input: hero movement + auto-pickup ---

void InputHandler::ProcessInput(GLFWwindow *window, float deltaTime) {
  s_gameReady = true;

  // Tick heal cooldown
  if (s_healCooldown > 0.0f)
    s_healCooldown -= deltaTime;

  bool wasMoving = s_ctx->hero->IsMoving();
  s_ctx->hero->ProcessMovement(deltaTime);

  // Auto-pickup logic: check if we reached a pending item
  int pendingIdx = s_ctx->hero->GetPendingPickup();
  if (pendingIdx != -1) {
    if (pendingIdx >= 0 && pendingIdx < s_ctx->maxGroundItems &&
        s_ctx->groundItems[pendingIdx].active) {
      float dist = glm::distance(s_ctx->hero->GetPosition(),
                                 s_ctx->groundItems[pendingIdx].position);
      if (dist < 150.0f) {
        s_ctx->server->SendPickup(s_ctx->groundItems[pendingIdx].dropIndex);
        std::cout << "[Pickup] REACHED: Auto-picking item index "
                  << s_ctx->groundItems[pendingIdx].dropIndex << std::endl;
        s_ctx->hero->ClearPendingPickup();
      }
    } else {
      s_ctx->hero->ClearPendingPickup(); // Item no longer active
    }
  }

  // Pending NPC interaction: open shop/dialog when hero reaches the NPC
  if (s_pendingNpcIdx >= 0 && s_pendingNpcIdx < s_ctx->npcMgr->GetNpcCount()) {
    NpcInfo info = s_ctx->npcMgr->GetNpcInfo(s_pendingNpcIdx);
    float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);
    if (dist < NPC_INTERACT_RANGE) {
      OpenNpcDialog(s_pendingNpcIdx, info);
      s_pendingNpcIdx = -1;
    }
  }

  // Pending interactive object: trigger sit/pose when hero reaches the object
  // Cancel if mounted — can't pose while riding
  if (s_pendingInteractIdx >= 0 && s_ctx->hero && s_ctx->hero->IsMounted())
    s_pendingInteractIdx = -1;
  if (s_pendingInteractIdx >= 0 && s_ctx->objectRenderer) {
    const auto &objs = s_ctx->objectRenderer->GetInteractiveObjects();
    if (s_pendingInteractIdx < (int)objs.size()) {
      const auto &obj = objs[s_pendingInteractIdx];
      float dist =
          glm::distance(s_ctx->hero->GetPosition(), obj.worldPos);
      if (dist < INTERACT_TRIGGER_RANGE) {
        bool isSit = (obj.action == ObjectRenderer::InteractType::SIT);
        s_ctx->hero->StartSitPose(isSit, obj.facingAngle,
                                  obj.alignToObject, obj.worldPos);
        s_pendingInteractIdx = -1;
      } else if (!s_ctx->hero->IsMoving()) {
        // Stopped moving but didn't reach — cancel
        s_pendingInteractIdx = -1;
      }
    } else {
      s_pendingInteractIdx = -1;
    }
  }

  // Auto-close shop when hero walks too far from the NPC
  if (*s_ctx->shopOpen && *s_ctx->selectedNpc >= 0 &&
      *s_ctx->selectedNpc < s_ctx->npcMgr->GetNpcCount()) {
    NpcInfo info = s_ctx->npcMgr->GetNpcInfo(*s_ctx->selectedNpc);
    float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);
    if (dist > NPC_CLOSE_RANGE) {
      *s_ctx->shopOpen = false;
      *s_ctx->selectedNpc = -1;
    }
  }

  // Hide click effect when hero stops moving
  if (wasMoving && !s_ctx->hero->IsMoving())
    s_ctx->clickEffect->Hide();

  // Camera follows hero (Continuous)
  s_ctx->camera->SetPosition(s_ctx->hero->GetPosition());
}

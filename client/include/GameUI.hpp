#ifndef GAME_UI_HPP
#define GAME_UI_HPP

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "UITexture.hpp"
#include <string>
#include <cstdint>
#include <algorithm>

class GameUI {
public:
    void Init(const std::string &dataPath, GLFWwindow *window);
    void Cleanup();

    // Called each frame inside the ImGui NewFrame..Render block
    void Render();

    // Update button hover states (call each frame before Render)
    void UpdateHover();

    // Input: returns true if UI consumed the click
    bool ProcessMouseClick(float screenX, float screenY);

    // State setters
    void SetHP(int current, int max) { m_hp = current; m_maxHp = max; }
    void SetMP(int current, int max) { m_mp = current; m_maxMp = max; }
    void SetExperience(uint64_t current, uint64_t next, uint64_t prevLevel) {
        m_xp = current; m_nextXp = next; m_prevLevelXp = prevLevel;
    }
    void SetLevel(int level) { m_level = level; }
    void SetLevelUpPoints(int pts) { m_levelUpPoints = pts; }
    void SetGold(int zen) { m_gold = zen; }

    // Button click flags (one-shot, polled by main.cpp)
    bool WantsToggleStatPanel() const { return m_toggleStatPanel; }
    bool WantsToggleInventory() const { return m_toggleInventory; }
    bool WantsToggleFriend() const { return m_toggleFriend; }
    bool WantsToggleOptions() const { return m_toggleOptions; }
    void ClearButtonFlags() {
        m_toggleStatPanel = false;
        m_toggleInventory = false;
        m_toggleFriend = false;
        m_toggleOptions = false;
    }

    // Check if a screen position is over the toolbar area (for click suppression)
    bool IsOverToolbar(float screenX, float screenY) const;

private:
    // Texture loading helpers
    UITexture LoadOZJUI(const std::string &path);
    UITexture LoadOZTUI(const std::string &path);

    // Virtual 640x480 coordinate conversion
    float ConvertX(float x) const;
    float ConvertY(float y) const;
    float ScreenToVirtualX(float sx) const;
    float ScreenToVirtualY(float sy) const;

    // Draw textured quad in virtual coords (handles OZT V-flip automatically)
    void DrawImage(const UITexture &tex, float vx, float vy, float vw, float vh,
                   ImVec2 uvMin = {0, 0}, ImVec2 uvMax = {1, 1},
                   ImU32 tint = IM_COL32_WHITE);

    // Render sub-components
    void RenderToolbarFrame();
    void RenderHPGauge();
    void RenderMPGauge();
    void RenderExperienceBar();
    void RenderButtons();
    void RenderTextOverlays();

    // Textures
    UITexture m_texMenu1;      // left toolbar 256x51
    UITexture m_texMenu2;      // center toolbar 128x51
    UITexture m_texMenu3;      // right toolbar 256x51
    UITexture m_texGaugeRed;   // HP fill (64x64 container, 45x39 visible)
    UITexture m_texGaugeBlue;  // MP fill
    UITexture m_texExbar;      // XP bar fill
    UITexture m_texBtn[4];     // ChaInfo, Inventory, Friend, Window

    GLFWwindow *m_window = nullptr;

    // Game state
    int m_hp = 0, m_maxHp = 1;
    int m_mp = 0, m_maxMp = 1;
    uint64_t m_xp = 0, m_nextXp = 1, m_prevLevelXp = 0;
    int m_level = 1;
    int m_levelUpPoints = 0;
    int m_gold = 0;

    // Button states: 0=normal, 1=hover, 2=pressed
    int m_btnStates[4] = {};

    // One-shot toggle flags
    bool m_toggleStatPanel = false;
    bool m_toggleInventory = false;
    bool m_toggleFriend = false;
    bool m_toggleOptions = false;

    // Layout constants (640x480 virtual coords, from NewUIMainFrameWindow.cpp)
    static constexpr float TOOLBAR_Y = 429.0f;      // 480 - 51
    static constexpr float TOOLBAR_H = 51.0f;
    static constexpr float HP_X = 158.0f;
    static constexpr float HP_Y = 432.0f;
    static constexpr float GAUGE_W = 45.0f;
    static constexpr float GAUGE_H = 39.0f;
    static constexpr float MP_X = 437.0f;
    static constexpr float MP_Y = 432.0f;
    static constexpr float XP_X = 2.0f;
    static constexpr float XP_Y = 473.0f;
    static constexpr float XP_MAX_W = 629.0f;
    static constexpr float XP_H = 4.0f;
    static constexpr float BTN_START_X = 519.0f;
    static constexpr float BTN_Y = 429.0f;
    static constexpr float BTN_W = 30.0f;
    static constexpr float BTN_H = 41.0f;
};

#endif // GAME_UI_HPP

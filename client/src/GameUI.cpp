#include "GameUI.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <cstdio>

// --- Texture loading with dimension tracking ---

UITexture GameUI::LoadOZJUI(const std::string &path) {
  UITexture tex;
  tex.isOZT = false;
  tex.id = TextureLoader::LoadOZJ(path);
  if (TexValid(tex.id)) {
    printf("[GameUI] OZJ %s: %dx%d (id=%zu)\n", path.c_str(), tex.width,
           tex.height, (size_t)TexImID(tex.id));
  } else {
    printf("[GameUI] FAILED to load OZJ: %s\n", path.c_str());
  }
  return tex;
}

UITexture GameUI::LoadOZTUI(const std::string &path) {
  UITexture tex;
  tex.isOZT = true;
  tex.id = TextureLoader::LoadOZT(path);
  if (TexValid(tex.id)) {
    printf("[GameUI] OZT %s: %dx%d (id=%zu)\n", path.c_str(), tex.width,
           tex.height, (size_t)TexImID(tex.id));
  } else {
    printf("[GameUI] FAILED to load OZT: %s\n", path.c_str());
  }
  return tex;
}

void GameUI::Init(const std::string &dataPath, GLFWwindow *window) {
  m_window = window;
  std::string ifPath = dataPath + "/Interface/";

  // Toolbar frame (OZJ - no alpha needed)
  m_texMenu1 = LoadOZJUI(ifPath + "newui_menu01.OZJ");
  m_texMenu2 = LoadOZJUI(ifPath + "newui_menu02.OZJ");
  m_texMenu3 = LoadOZJUI(ifPath + "newui_menu03.OZJ");

  // Gauge fills (OZJ - may be 64x64 with 45x39 visible area)
  m_texGaugeRed = LoadOZJUI(ifPath + "newui_menu_red.OZJ");
  m_texGaugeBlue = LoadOZJUI(ifPath + "newui_menu_blue.OZJ");
  m_texExbar = LoadOZJUI(ifPath + "newui_Exbar.OZJ");

  // Toolbar buttons (OZT for alpha transparency)
  m_texBtn[0] = LoadOZTUI(ifPath + "newui_menu_Bt01.OZT");
  m_texBtn[1] = LoadOZTUI(ifPath + "newui_menu_Bt02.OZT");
  m_texBtn[2] = LoadOZTUI(ifPath + "newui_menu_Bt03.OZT");
  m_texBtn[3] = LoadOZTUI(ifPath + "newui_menu_Bt04.OZT");
}

void GameUI::Cleanup() {
  auto del = [](UITexture &t) {
    t.Destroy();
  };
  del(m_texMenu1);
  del(m_texMenu2);
  del(m_texMenu3);
  del(m_texGaugeRed);
  del(m_texGaugeBlue);
  del(m_texExbar);
  for (auto &t : m_texBtn)
    del(t);
}

// --- Coordinate conversion ---

float GameUI::ConvertX(float x) const {
  int winW, winH;
  glfwGetWindowSize(m_window, &winW, &winH);
  return x * (float)winW / 640.0f;
}

float GameUI::ConvertY(float y) const {
  int winW, winH;
  glfwGetWindowSize(m_window, &winW, &winH);
  return y * (float)winH / 480.0f;
}

float GameUI::ScreenToVirtualX(float sx) const {
  int winW, winH;
  glfwGetWindowSize(m_window, &winW, &winH);
  return sx * 640.0f / (float)winW;
}

float GameUI::ScreenToVirtualY(float sy) const {
  int winW, winH;
  glfwGetWindowSize(m_window, &winW, &winH);
  return sy * 480.0f / (float)winH;
}

// --- Drawing helper ---
// Handles OZT V-flip: OZT textures are flipped in the loader for 3D OpenGL
// rendering, but ImGui expects v=0 at top. We flip V coordinates back for OZT
// textures.

void GameUI::DrawImage(const UITexture &tex, float vx, float vy, float vw,
                       float vh, ImVec2 uvMin, ImVec2 uvMax, ImU32 tint) {
  if (!TexValid(tex.id))
    return;
  auto *dl = ImGui::GetForegroundDrawList();
  ImVec2 pMin(ConvertX(vx), ConvertY(vy));
  ImVec2 pMax(ConvertX(vx + vw), ConvertY(vy + vh));

  // OZT textures are V-flipped for 3D rendering; flip V back for ImGui 2D
  if (tex.isOZT) {
    float tmpMinY = uvMin.y;
    float tmpMaxY = uvMax.y;
    uvMin.y = 1.0f - tmpMaxY;
    uvMax.y = 1.0f - tmpMinY;
  }

  dl->AddImage(ImTextureRef((ImTextureID)TexImID(tex.id)), pMin, pMax, uvMin,
               uvMax, tint);
}

// --- Toolbar frame ---

void GameUI::RenderToolbarFrame() {
  DrawImage(m_texMenu1, 0.0f, TOOLBAR_Y, 256.0f, TOOLBAR_H);
  DrawImage(m_texMenu2, 256.0f, TOOLBAR_Y, 128.0f, TOOLBAR_H);
  DrawImage(m_texMenu3, 384.0f, TOOLBAR_Y, 256.0f, TOOLBAR_H);
}

// --- HP gauge (fill from bottom via UV crop) ---
// Reference: NewUIMainFrameWindow.cpp RenderLifeMana() line 285-301
// Gauge textures may be 64x64 with only 45x39 of usable data in top-left.
// UV must be scaled to only sample the valid region.

void GameUI::RenderHPGauge() {
  if (m_maxHp <= 0 || !TexValid(m_texGaugeRed.id))
    return;
  float fEmpty =
      (float)(m_maxHp - std::clamp(m_hp, 0, m_maxHp)) / (float)m_maxHp;

  float cropY = HP_Y + fEmpty * GAUGE_H;
  float cropH = GAUGE_H - fEmpty * GAUGE_H;
  if (cropH <= 0.5f)
    return;

  // Scale UV to only cover the 45x39 gauge region within the (potentially
  // larger) texture
  float uMax =
      (m_texGaugeRed.width > 0) ? GAUGE_W / (float)m_texGaugeRed.width : 1.0f;
  float vFull =
      (m_texGaugeRed.height > 0) ? GAUGE_H / (float)m_texGaugeRed.height : 1.0f;
  float vStart = fEmpty * vFull;

  ImVec2 uvMin(0.0f, vStart);
  ImVec2 uvMax(uMax, vFull);
  DrawImage(m_texGaugeRed, HP_X, cropY, GAUGE_W, cropH, uvMin, uvMax);
}

// --- MP gauge ---

void GameUI::RenderMPGauge() {
  if (m_maxMp <= 0 || !TexValid(m_texGaugeBlue.id))
    return;
  float fEmpty =
      (float)(m_maxMp - std::clamp(m_mp, 0, m_maxMp)) / (float)m_maxMp;

  float cropY = MP_Y + fEmpty * GAUGE_H;
  float cropH = GAUGE_H - fEmpty * GAUGE_H;
  if (cropH <= 0.5f)
    return;

  float uMax =
      (m_texGaugeBlue.width > 0) ? GAUGE_W / (float)m_texGaugeBlue.width : 1.0f;
  float vFull = (m_texGaugeBlue.height > 0)
                    ? GAUGE_H / (float)m_texGaugeBlue.height
                    : 1.0f;
  float vStart = fEmpty * vFull;

  ImVec2 uvMin(0.0f, vStart);
  ImVec2 uvMax(uMax, vFull);
  DrawImage(m_texGaugeBlue, MP_X, cropY, GAUGE_W, cropH, uvMin, uvMax);
}

// --- Experience bar ---
// Reference UV: u=0..6/8, v=0..1 (texture is 8x4, uses 6px of width)

void GameUI::RenderExperienceBar() {
  if (!TexValid(m_texExbar.id))
    return;
  float frac = 0.0f;
  if (m_nextXp > m_prevLevelXp) {
    frac = (float)(m_xp - m_prevLevelXp) / (float)(m_nextXp - m_prevLevelXp);
  }
  frac = std::clamp(frac, 0.0f, 1.0f);

  float barW = frac * XP_MAX_W;
  if (barW < 1.0f)
    return;

  float uMax = (m_texExbar.width > 0) ? 6.0f / (float)m_texExbar.width : 1.0f;
  DrawImage(m_texExbar, XP_X, XP_Y, barW, XP_H, {0.0f, 0.0f}, {uMax, 1.0f});
}

// --- Toolbar buttons ---
// OZT sprite sheets with vertical states (up/hover/down)
// Actual number of states determined from texture dimensions

void GameUI::RenderButtons() {
  for (int i = 0; i < 4; i++) {
    if (!TexValid(m_texBtn[i].id))
      continue;

    float x = BTN_START_X + i * BTN_W;
    int state = m_btnStates[i]; // 0=up, 1=hover, 2=down

    // Determine states from texture: each state should be ~BTN_H pixels tall
    int numStates = 3;
    if (m_texBtn[i].height > 0) {
      numStates = m_texBtn[i].height / (int)BTN_H;
      if (numStates < 1)
        numStates = 1;
      if (numStates > 4)
        numStates = 4;
    }
    if (state >= numStates)
      state = 0;

    float stateV = (float)state / (float)numStates;
    float stateVEnd = (float)(state + 1) / (float)numStates;

    // UV x: button content may not fill full texture width
    float uMax =
        (m_texBtn[i].width > 0) ? BTN_W / (float)m_texBtn[i].width : 1.0f;
    if (uMax > 1.0f)
      uMax = 1.0f;

    ImVec2 uvMin(0.0f, stateV);
    ImVec2 uvMax(uMax, stateVEnd);
    DrawImage(m_texBtn[i], x, BTN_Y, BTN_W, BTN_H, uvMin, uvMax);
  }
}

// --- Text overlays ---

void GameUI::RenderTextOverlays() {
  auto *dl = ImGui::GetForegroundDrawList();

  // HP number centered on gauge
  {
    char txt[16];
    snprintf(txt, sizeof(txt), "%d", std::max(m_hp, 0));
    ImVec2 sz = ImGui::CalcTextSize(txt);
    float cx = ConvertX(HP_X + GAUGE_W / 2.0f) - sz.x / 2.0f;
    float cy = ConvertY(HP_Y + GAUGE_H / 2.0f) - sz.y / 2.0f;
    dl->AddText(ImVec2(cx + 1, cy + 1), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(ImVec2(cx, cy), IM_COL32(255, 255, 255, 230), txt);
  }

  // MP number centered on gauge (only show if MP system active)
  if (m_maxMp > 0) {
    char txt[16];
    snprintf(txt, sizeof(txt), "%d", std::max(m_mp, 0));
    ImVec2 sz = ImGui::CalcTextSize(txt);
    float cx = ConvertX(MP_X + GAUGE_W / 2.0f) - sz.x / 2.0f;
    float cy = ConvertY(MP_Y + GAUGE_H / 2.0f) - sz.y / 2.0f;
    dl->AddText(ImVec2(cx + 1, cy + 1), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(ImVec2(cx, cy), IM_COL32(255, 255, 255, 230), txt);
  }

  // Level badge (left area of toolbar)
  {
    char txt[16];
    snprintf(txt, sizeof(txt), "Lv.%d", m_level);
    float lx = ConvertX(8.0f);
    float ly = ConvertY(TOOLBAR_Y + 6.0f);
    dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(ImVec2(lx, ly), IM_COL32(255, 220, 100, 255), txt);
  }

  // Gold display
  if (m_gold > 0) {
    char txt[32];
    snprintf(txt, sizeof(txt), "%d Zen", m_gold);
    float gx = ConvertX(8.0f);
    float gy = ConvertY(TOOLBAR_Y + 22.0f);
    dl->AddText(ImVec2(gx + 1, gy + 1), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(ImVec2(gx, gy), IM_COL32(255, 220, 80, 255), txt);
  }

  // Stat points indicator
  if (m_levelUpPoints > 0) {
    char txt[16];
    snprintf(txt, sizeof(txt), "+%d pts", m_levelUpPoints);
    float px = ConvertX(8.0f);
    float py = ConvertY(TOOLBAR_Y + 38.0f);
    dl->AddText(ImVec2(px + 1, py + 1), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(ImVec2(px, py), IM_COL32(100, 255, 100, 255), txt);
  }
}

// --- Main render ---

void GameUI::Render() {
  RenderToolbarFrame();
  RenderHPGauge();
  RenderMPGauge();
  RenderExperienceBar();
  RenderButtons();
  RenderTextOverlays();
}

// --- Hover update ---

void GameUI::UpdateHover() {
  double mx, my;
  glfwGetCursorPos(m_window, &mx, &my);
  float vx = ScreenToVirtualX((float)mx);
  float vy = ScreenToVirtualY((float)my);

  bool mouseDown =
      glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  for (int i = 0; i < 4; i++) {
    float bx = BTN_START_X + i * BTN_W;
    bool inBtn =
        (vx >= bx && vx < bx + BTN_W && vy >= BTN_Y && vy < BTN_Y + BTN_H);

    if (inBtn && mouseDown)
      m_btnStates[i] = 2;
    else if (inBtn)
      m_btnStates[i] = 1;
    else
      m_btnStates[i] = 0;
  }
}

// --- Mouse click handling ---

bool GameUI::ProcessMouseClick(float screenX, float screenY) {
  float vx = ScreenToVirtualX(screenX);
  float vy = ScreenToVirtualY(screenY);

  for (int i = 0; i < 4; i++) {
    float bx = BTN_START_X + i * BTN_W;
    if (vx >= bx && vx < bx + BTN_W && vy >= BTN_Y && vy < BTN_Y + BTN_H) {
      switch (i) {
      case 0:
        m_toggleStatPanel = true;
        break;
      case 1:
        m_toggleInventory = true;
        break;
      case 2:
        m_toggleFriend = true;
        break;
      case 3:
        m_toggleOptions = true;
        break;
      }
      return true;
    }
  }
  return false;
}

bool GameUI::IsOverToolbar(float screenX, float screenY) const {
  float vy = ScreenToVirtualY(screenY);
  return vy >= TOOLBAR_Y;
}

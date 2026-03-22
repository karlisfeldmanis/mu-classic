#include "HUD.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

// Format number with space as thousands separator: 1234 -> "1 234"
static void FormatNumber(char *buf, size_t bufSize, int value) {
  if (value < 0) {
    snprintf(buf, bufSize, "%d", value);
    return;
  }
  if (value < 1000) {
    snprintf(buf, bufSize, "%d", value);
    return;
  }
  if (value < 1000000) {
    snprintf(buf, bufSize, "%d %03d", value / 1000, value % 1000);
    return;
  }
  snprintf(buf, bufSize, "%d %03d %03d", value / 1000000, (value / 1000) % 1000,
           value % 1000);
}

void HUD::Init(const std::string &assetPath, GLFWwindow *window) {
  m_window = window;
  x = 0;
  y = HUD_TOP;
  w = 1280.0f;
  h = HUD_HEIGHT;

  m_texBase = UITexture::Load(assetPath + "/UI_HUD_Base.png");
  m_texLife = UITexture::Load(assetPath + "/UI_HUD_LIFE.png");
  m_texMana = UITexture::Load(assetPath + "/UI_HUD_MANA.png");
  m_texXPFill = UITexture::Load(assetPath + "/ActionBarsView_I2B0.png");
  m_texXPBg = UITexture::Load(assetPath + "/ActionBarsView_I2AC.png");
  m_texMenuIcons = UITexture::Load(assetPath + "/UI_MainMenuIcons.png");

  printf("[HUD] Loaded textures:\n");
  printf("  Base:  %s (%dx%d)\n", m_texBase ? "OK" : "FAIL", m_texBase.width,
         m_texBase.height);
  printf("  Life:  %s (%dx%d)\n", m_texLife ? "OK" : "FAIL", m_texLife.width,
         m_texLife.height);
  printf("  Mana:  %s (%dx%d)\n", m_texMana ? "OK" : "FAIL", m_texMana.width,
         m_texMana.height);
  printf("  XP:    %s (%dx%d)\n", m_texXPFill ? "OK" : "FAIL",
         m_texXPFill.width, m_texXPFill.height);
  printf("  Icons: %s (%dx%d)\n", m_texMenuIcons ? "OK" : "FAIL",
         m_texMenuIcons.width, m_texMenuIcons.height);
}

void HUD::Cleanup() {
  m_texBase.Destroy();
  m_texLife.Destroy();
  m_texMana.Destroy();
  m_texXPFill.Destroy();
  m_texXPBg.Destroy();
  m_texMenuIcons.Destroy();
}

void HUD::Update(const MockData &data) {
  m_hp = data.hp;
  m_maxHp = std::max(data.maxHp, 1);
  m_mp = data.mp;
  m_maxMp = std::max(data.maxMp, 1);
  m_classId = data.classId;
  m_level = data.level;
  m_levelUpPoints = data.levelUpPoints;
  m_gold = data.gold;

  m_hpFrac = (float)std::clamp(data.hp, 0, data.maxHp) / (float)m_maxHp;
  m_mpFrac = (float)std::clamp(data.mp, 0, data.maxMp) / (float)m_maxMp;

  if (data.nextLevelXp > data.prevLevelXp) {
    m_xpFrac = (float)(data.xp - data.prevLevelXp) /
               (float)(data.nextLevelXp - data.prevLevelXp);
    m_xpFrac = std::clamp(m_xpFrac, 0.0f, 1.0f);
  } else {
    m_xpFrac = 0.0f;
  }

  // Advance gem shimmer animation (~12 fps)
  m_animTimer += 1.0f / 60.0f;
  if (m_animTimer >= 0.083f) {
    m_animTimer -= 0.083f;
    m_gemFrame = (m_gemFrame + 1) % GEM_FRAMES;
  }
}

bool HUD::HandleMouseMove(float vx, float vy) {
  m_hoveredIcon = -1;
  if (!visible)
    return false;

  for (int i = 0; i < MENU_ICON_COUNT; i++) {
    float ix = ICON_X_START + i * (ICON_SIZE + ICON_GAP);
    float iy = ICON_Y_POS;
    if (vx >= ix && vx < ix + ICON_SIZE && vy >= iy && vy < iy + ICON_SIZE) {
      m_hoveredIcon = i;
      break;
    }
  }
  return HitTest(vx, vy);
}

bool HUD::HandleMouseDown(float vx, float vy) {
  if (!visible)
    return false;

  for (int i = 0; i < MENU_ICON_COUNT; i++) {
    float ix = ICON_X_START + i * (ICON_SIZE + ICON_GAP);
    float iy = ICON_Y_POS;
    if (vx >= ix && vx < ix + ICON_SIZE && vy >= iy && vy < iy + ICON_SIZE) {
      m_pressedIcon = i;
      return true;
    }
  }
  return HitTest(vx, vy);
}

bool HUD::HandleMouseUp(float vx, float vy) {
  if (!visible)
    return false;

  if (m_pressedIcon >= 0) {
    int pressed = m_pressedIcon;
    m_pressedIcon = -1;

    float ix = ICON_X_START + pressed * (ICON_SIZE + ICON_GAP);
    float iy = ICON_Y_POS;
    if (vx >= ix && vx < ix + ICON_SIZE && vy >= iy && vy < iy + ICON_SIZE) {
      switch (pressed) {
      case 0:
        if (onToggleCharInfo)
          onToggleCharInfo();
        break;
      case 1:
        if (onToggleInventory)
          onToggleInventory();
        break;
      case 2:
        if (onToggleParty)
          onToggleParty();
        break;
      case 3:
        if (onToggleOptions)
          onToggleOptions();
        break;
      }
      return true;
    }
  }
  return false;
}

void HUD::Render(ImDrawList *dl, const UICoords &coords) {
  if (!visible)
    return;

  // Gems render behind frame (frame has transparent diamond cutouts)
  RenderGemOrb(dl, coords, m_texLife, m_hpFrac, m_gemFrame, HP_ORB_X, HP_ORB_Y);

  // MP orb - Hidden for DK (Class 16)
  if (m_classId != 16) {
    RenderGemOrb(dl, coords, m_texMana, m_mpFrac, m_gemFrame, MP_ORB_X,
                 MP_ORB_Y);
  }

  // Frame on top (alpha cutouts let gems show through)
  RenderFrame(dl, coords);

  // Overlays on top of frame
  RenderXPBar(dl, coords);
  RenderMenuIcons(dl, coords);
  RenderTextOverlays(dl, coords);
}

void HUD::RenderFrame(ImDrawList *dl, const UICoords &coords) {
  if (m_texBase) {
    DrawImage(dl, coords, m_texBase, 0.0f, HUD_TOP, 1280.0f, HUD_HEIGHT);
  } else {
    DrawRect(dl, coords, 0.0f, HUD_TOP, 1280.0f, HUD_HEIGHT,
             IM_COL32(20, 22, 30, 230));
  }
}

void HUD::RenderGemOrb(ImDrawList *dl, const UICoords &coords,
                       const UITexture &tex, float frac, int animFrame,
                       float orbX, float orbY) {
  if (!tex || frac < 0.01f)
    return;

  // Sprite sheet cell for current animation frame
  int col = animFrame % GEM_COLS;
  int row = animFrame / GEM_COLS;

  float cellU = 1.0f / (float)GEM_COLS;
  float cellV = 1.0f / (float)GEM_ROWS;

  // Full cell UV bounds
  float u0 = col * cellU;
  float u1 = (col + 1) * cellU;
  float v0 = row * cellV;
  float v1 = (row + 1) * cellV;

  // Vertical clipping: fill from bottom up
  // frac=1.0 -> full gem visible, frac=0.5 -> bottom half only
  float fEmpty = 1.0f - frac;
  float clipV0 = v0 + fEmpty * (v1 - v0);
  float clipY = orbY + fEmpty * ORB_H;
  float clipH = ORB_H * frac;

  if (clipH < 0.5f)
    return;

  DrawImage(dl, coords, tex, orbX, clipY, ORB_W, clipH, {u0, clipV0}, {u1, v1});
}

void HUD::RenderXPBar(ImDrawList *dl, const UICoords &coords) {
  // Background
  if (m_texXPBg) {
    DrawImage(dl, coords, m_texXPBg, XP_X, XP_Y, XP_W, XP_H);
  }
  // Fill
  if (m_xpFrac > 0.01f) {
    float fillW = XP_W * m_xpFrac;
    if (m_texXPFill) {
      DrawImage(dl, coords, m_texXPFill, XP_X, XP_Y, fillW, XP_H, {0.0f, 0.0f},
                {m_xpFrac, 1.0f});
    } else {
      DrawRect(dl, coords, XP_X, XP_Y, fillW, XP_H, IM_COL32(0, 200, 200, 200));
    }
  }
}

void HUD::RenderMenuIcons(ImDrawList *dl, const UICoords &coords) {
  if (!m_texMenuIcons)
    return;

  static const int iconIndices[MENU_ICON_COUNT] = {
      ICON_IDX_CHAR, ICON_IDX_INV, ICON_IDX_PARTY, ICON_IDX_OPT};

  for (int i = 0; i < MENU_ICON_COUNT; i++) {
    float ix = ICON_X_START + i * (ICON_SIZE + ICON_GAP);
    float iy = ICON_Y_POS;

    int iconIdx = iconIndices[i];
    bool hovered = (i == m_hoveredIcon);
    bool pressed = (i == m_pressedIcon);

    // Top row = normal, bottom row = hover/pressed
    int row = (hovered || pressed) ? 1 : 0;

    float uMin = iconIdx / ICONS_PER_ROW;
    float uMax = (iconIdx + 1) / ICONS_PER_ROW;
    float vMin = row * 0.5f;
    float vMax = (row + 1) * 0.5f;

    DrawImage(dl, coords, m_texMenuIcons, ix, iy, ICON_SIZE, ICON_SIZE,
              {uMin, vMin}, {uMax, vMax});
  }
}

void HUD::RenderTextOverlays(ImDrawList *dl, const UICoords &coords) {
  ImFont *font = hudFont ? hudFont : ImGui::GetFont();
  float fontSize = font->LegacySize * ImGui::GetIO().FontGlobalScale;

  // Helper: draw centered text with shadow using the HUD font
  auto drawCentered = [&](float vx, float vy, float vw, const char *text,
                          ImU32 color) {
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    float cx = coords.ToScreenX(vx + vw * 0.5f) - sz.x * 0.5f;
    float cy = coords.ToScreenY(vy) - sz.y * 0.5f;
    dl->AddText(font, fontSize, ImVec2(cx + 1, cy + 1), IM_COL32(0, 0, 0, 200),
                text);
    dl->AddText(font, fontSize, ImVec2(cx, cy), color, text);
  };

  // HP value - centered on HP orb
  {
    char txt[32];
    snprintf(txt, sizeof(txt), "%d / %d", std::max(m_hp, 0), m_maxHp);
    drawCentered(HP_ORB_X, HP_ORB_Y + ORB_H * 0.58f, ORB_W, txt,
                 IM_COL32(255, 220, 220, 230));
  }

  // MP value - centered on MP orb (Hidden for DK)
  if (m_maxMp > 0 && m_classId != 16) {
    char txt[32];
    snprintf(txt, sizeof(txt), "%d / %d", std::max(m_mp, 0), m_maxMp);
    drawCentered(MP_ORB_X, MP_ORB_Y + ORB_H * 0.58f, ORB_W, txt,
                 IM_COL32(220, 220, 255, 230));
  }

  // Level and XP info - centered above XP bar
  {
    char txt[64];
    int xpPct = (int)(m_xpFrac * 100.0f);
    snprintf(txt, sizeof(txt), "Lv.%d  -  %d%%", m_level, xpPct);
    drawCentered(XP_X, XP_Y - 4.0f, XP_W, txt, IM_COL32(220, 200, 100, 220));
  }
}

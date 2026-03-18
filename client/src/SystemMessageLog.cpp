#include "SystemMessageLog.hpp"
#include "SoundManager.hpp"
#include <cstdarg>
#include <cstdio>
#include <deque>

namespace SystemMessageLog {

static inline ImU32 ScaleAlpha(ImU32 col, float a) {
  uint8_t origA = (col >> 24) & 0xFF;
  uint8_t newA = (uint8_t)(origA * a);
  return (col & 0x00FFFFFF) | ((ImU32)newA << 24);
}

struct LogMessage {
  std::string text;
  ImU32 color;
  MessageCategory category;
};

static constexpr int MAX_MESSAGES = 500;
static constexpr int MAX_VISIBLE = 7;
static constexpr float LINE_H = 15.0f;
static constexpr float TAB_H = 18.0f;
static constexpr float SCROLLBAR_W = 4.0f;
static constexpr float CONTAINER_W = 320.0f;
static constexpr float CONTAINER_H = TAB_H + MAX_VISIBLE * LINE_H + 6.0f;

static std::deque<LogMessage> s_messages;
static int s_activeTab = 0;    // 0=General(all), 1=Combat, 2=System
static int s_scrollOffset = 0; // 0 = bottom (newest), positive = scrolled up
static bool s_userScrolled = false; // true if user has scrolled up

// Tab hit-test (screen coords, set during Render)
static float s_tabX[3], s_tabY, s_tabW[3], s_tabEndY;
// Log area bounds (for scroll hit-test)
static float s_logX, s_logY, s_logW, s_logH;
static float s_hoverAlpha = 0.0f;    // 0 = hidden, 1 = fully visible
static float s_activityTimer = 0.0f; // Seconds since last new message
static constexpr float FADE_SHOW_TIME =
    5.0f; // Stay visible this long after last message
static constexpr float FADE_OUT_TIME = 1.5f; // Fade out duration

// Count messages matching current tab filter
static int CountFiltered() {
  int count = 0;
  for (auto &m : s_messages) {
    if (s_activeTab == 0 || (s_activeTab == 1 && m.category == MSG_COMBAT) ||
        (s_activeTab == 2 && m.category == MSG_SYSTEM))
      count++;
  }
  return count;
}

void Init() {
  s_messages.clear();
  s_activeTab = 0;
  s_scrollOffset = 0;
  s_userScrolled = false;
}

void Log(MessageCategory cat, ImU32 color, const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  LogMessage msg;
  msg.text = buf;
  msg.color = color;
  msg.category = cat;
  s_messages.push_back(std::move(msg));

  if ((int)s_messages.size() > MAX_MESSAGES)
    s_messages.pop_front();

  // Auto-scroll to bottom unless user has scrolled up
  if (!s_userScrolled)
    s_scrollOffset = 0;

  // Reset activity timer — makes log visible
  s_activityTimer = 0.0f;

  // Play error sound for red-tinted error messages (R>=200, G<120, B<120)
  uint8_t r = (color >> 0) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = (color >> 16) & 0xFF;
  if (r >= 200 && g < 120 && b < 120 && cat != 1)
    SoundManager::Play(SOUND_ERROR01);
}

void LogSilent(MessageCategory cat, ImU32 color, const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  LogMessage msg;
  msg.text = buf;
  msg.color = color;
  msg.category = cat;
  s_messages.push_back(std::move(msg));

  if ((int)s_messages.size() > MAX_MESSAGES)
    s_messages.pop_front();
  // Don't auto-scroll (used for bulk history load)
}

void Update(float /*deltaTime*/) {
  // No-op: messages are persistent now (no fade)
}

void Render(ImDrawList *dl, ImFont *font, float screenW, float screenH,
            float hudBarHeight, float mouseX, float mouseY) {
  float uiScale = ImGui::GetIO().FontGlobalScale;
  float sTabH = TAB_H * uiScale;
  float sLineH = LINE_H * uiScale;
  float sContW = CONTAINER_W * uiScale;
  float sContH = sTabH + MAX_VISIBLE * sLineH + 6.0f * uiScale;
  float sFontSz = 11.0f * uiScale;
  float cx = 8.0f;
  float cy = screenH - sContH - 30.0f; // raised above XP bar

  // Store log area bounds for scroll hit-test
  s_logX = cx;
  s_logY = cy;
  s_logW = sContW;
  s_logH = sContH;

  float dt = ImGui::GetIO().DeltaTime;
  s_activityTimer += dt;

  // Compute activity alpha: fully visible during show time, fade out after
  float activityAlpha = 1.0f;
  if (s_activityTimer > FADE_SHOW_TIME) {
    float fadeProgress = (s_activityTimer - FADE_SHOW_TIME) / FADE_OUT_TIME;
    activityAlpha = std::max(0.0f, 1.0f - fadeProgress);
  }

  // Track hover for scrollbar and to override fade
  bool hovered = (mouseX >= cx && mouseX <= cx + sContW && mouseY >= cy &&
                  mouseY <= cy + sContH);
  float fadeSpeed = 6.0f;
  if (hovered)
    s_hoverAlpha = std::min(1.0f, s_hoverAlpha + dt * fadeSpeed);
  else
    s_hoverAlpha = std::max(0.0f, s_hoverAlpha - dt * fadeSpeed);

  // Show fully when hovered, otherwise use activity fade
  float alpha = std::max(activityAlpha, s_hoverAlpha);
  if (alpha < 0.01f)
    return; // Completely invisible, skip rendering

  // Tabs — only show on hover
  const char *tabNames[] = {"General", "Combat", "System"};
  float tabX = cx + 4.0f;
  s_tabY = cy;
  s_tabEndY = cy + sTabH;

  for (int t = 0; t < 3; t++) {
    ImVec2 ts = font->CalcTextSizeA(sFontSz, FLT_MAX, 0, tabNames[t]);
    float tw = ts.x + 12.0f * uiScale;
    s_tabX[t] = tabX;
    s_tabW[t] = tw;

    if (s_hoverAlpha > 0.01f) {
      float ta = s_hoverAlpha;
      bool active = (t == s_activeTab);
      ImU32 tabCol = ScaleAlpha(active ? IM_COL32(255, 200, 50, 230)
                                       : IM_COL32(150, 150, 150, 130),
                                ta);

      if (active)
        dl->AddRectFilled(ImVec2(tabX, cy + 1), ImVec2(tabX + tw, cy + sTabH),
                          ScaleAlpha(IM_COL32(255, 200, 50, 20), ta), 2.0f);

      dl->AddText(font, sFontSz, ImVec2(tabX + 6, cy + (sTabH - ts.y) * 0.5f),
                  tabCol, tabNames[t]);

      if (active) {
        dl->AddLine(ImVec2(tabX + 2, cy + sTabH - 1),
                    ImVec2(tabX + tw - 2, cy + sTabH - 1), tabCol, 1.5f);
      }
    }
    tabX += tw + 2.0f;
  }

  // Message area
  float msgTop = cy + sTabH + 2.0f;
  float msgBottom = cy + sContH - 2.0f;

  // Collect ALL filtered messages
  struct VisMsg {
    const char *text;
    ImU32 color;
  };
  std::vector<VisMsg> filtered;
  filtered.reserve(s_messages.size());

  for (auto &m : s_messages) {
    if (s_activeTab == 1 && m.category != MSG_COMBAT)
      continue;
    if (s_activeTab == 2 && m.category != MSG_SYSTEM)
      continue;
    filtered.push_back({m.text.c_str(), m.color});
  }

  int totalFiltered = (int)filtered.size();
  int maxScroll = std::max(0, totalFiltered - MAX_VISIBLE);

  // Clamp scroll offset
  if (s_scrollOffset > maxScroll)
    s_scrollOffset = maxScroll;
  if (s_scrollOffset < 0)
    s_scrollOffset = 0;

  // If at bottom, clear user-scrolled flag
  if (s_scrollOffset == 0)
    s_userScrolled = false;

  // Render messages: newest at bottom, scroll offset moves view up
  // startIdx = first message to show (from bottom of filtered list)
  int endIdx = totalFiltered - s_scrollOffset;      // exclusive
  int startIdx = std::max(0, endIdx - MAX_VISIBLE); // inclusive

  for (int i = startIdx; i < endIdx; i++) {
    float y = msgTop + (float)(i - startIdx) * sLineH;
    if (y + sLineH > msgBottom)
      break;

    ImU32 shadowCol = ScaleAlpha(IM_COL32(0, 0, 0, 140), alpha);
    dl->AddText(font, sFontSz, ImVec2(cx + 5, y + 1), shadowCol,
                filtered[i].text);
    dl->AddText(font, sFontSz, ImVec2(cx + 4, y),
                ScaleAlpha(filtered[i].color, alpha), filtered[i].text);
  }

  // Scrollbar (WoW-style thin gold bar) — only on hover
  if (totalFiltered > MAX_VISIBLE && s_hoverAlpha > 0.01f) {
    float sbAlpha = s_hoverAlpha; // fade with hover
    float trackX = cx + sContW - SCROLLBAR_W - 2.0f;
    float trackTop = msgTop;
    float trackH = msgBottom - msgTop;

    // Track background
    dl->AddRectFilled(ImVec2(trackX, trackTop),
                      ImVec2(trackX + SCROLLBAR_W, trackTop + trackH),
                      ScaleAlpha(IM_COL32(30, 30, 30, 80), sbAlpha), 2.0f);

    // Thumb
    float thumbRatio = (float)MAX_VISIBLE / (float)totalFiltered;
    float thumbH = std::max(12.0f, trackH * thumbRatio);
    float scrollRatio =
        (maxScroll > 0) ? (float)(maxScroll - s_scrollOffset) / (float)maxScroll
                        : 0.0f;
    float thumbY = trackTop + scrollRatio * (trackH - thumbH);

    dl->AddRectFilled(ImVec2(trackX, thumbY),
                      ImVec2(trackX + SCROLLBAR_W, thumbY + thumbH),
                      ScaleAlpha(IM_COL32(255, 200, 50, 160), sbAlpha), 2.0f);
  }
}

bool HandleScroll(float mx, float my, float scrollDelta) {
  // Check if mouse is over the log area
  if (mx < s_logX || mx > s_logX + s_logW || my < s_logY ||
      my > s_logY + s_logH)
    return false;

  // Scroll: positive delta = scroll up (show older), negative = scroll down
  int lines = (int)(scrollDelta);
  if (lines == 0)
    lines = (scrollDelta > 0) ? 1 : -1;

  s_scrollOffset += lines;
  int totalFiltered = CountFiltered();
  int maxScroll = std::max(0, totalFiltered - MAX_VISIBLE);
  if (s_scrollOffset > maxScroll)
    s_scrollOffset = maxScroll;
  if (s_scrollOffset < 0)
    s_scrollOffset = 0;

  if (s_scrollOffset > 0)
    s_userScrolled = true;
  else
    s_userScrolled = false;

  return true;
}

bool HandleClick(float mx, float my) {
  if (my < s_tabY || my > s_tabEndY)
    return false;
  for (int t = 0; t < 3; t++) {
    if (mx >= s_tabX[t] && mx < s_tabX[t] + s_tabW[t]) {
      s_activeTab = t;
      s_scrollOffset = 0;
      s_userScrolled = false;
      return true;
    }
  }
  return false;
}

} // namespace SystemMessageLog

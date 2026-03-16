#include "UIWidget.hpp"

void UIWidget::DrawImage(ImDrawList* dl, const UICoords& coords,
                          const UITexture& tex, float vx, float vy, float vw, float vh,
                          ImVec2 uvMin, ImVec2 uvMax, ImU32 tint) {
    if (!TexValid(tex.id)) return;

    ImVec2 pMin(coords.ToScreenX(vx), coords.ToScreenY(vy));
    ImVec2 pMax(coords.ToScreenX(vx + vw), coords.ToScreenY(vy + vh));

    // OZT textures are V-flipped by the loader for 3D OpenGL rendering.
    // For ImGui 2D rendering, flip V coordinates back.
    if (tex.isOZT) {
        float tmpMinY = uvMin.y;
        float tmpMaxY = uvMax.y;
        uvMin.y = 1.0f - tmpMaxY;
        uvMax.y = 1.0f - tmpMinY;
    }

    dl->AddImage(ImTextureRef((ImTextureID)TexImID(tex.id)), pMin, pMax, uvMin, uvMax, tint);
}

void UIWidget::DrawText(ImDrawList* dl, const UICoords& coords,
                         float vx, float vy, const char* text,
                         ImU32 color, ImU32 shadowColor) {
    float sx = coords.ToScreenX(vx);
    float sy = coords.ToScreenY(vy);
    dl->AddText(ImVec2(sx + 1, sy + 1), shadowColor, text);
    dl->AddText(ImVec2(sx, sy), color, text);
}

void UIWidget::DrawTextCentered(ImDrawList* dl, const UICoords& coords,
                                 float vx, float vy, float vw, const char* text,
                                 ImU32 color, ImU32 shadowColor) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    float cx = coords.ToScreenX(vx + vw / 2.0f) - sz.x / 2.0f;
    float cy = coords.ToScreenY(vy);
    dl->AddText(ImVec2(cx + 1, cy + 1), shadowColor, text);
    dl->AddText(ImVec2(cx, cy), color, text);
}

void UIWidget::DrawRect(ImDrawList* dl, const UICoords& coords,
                         float vx, float vy, float vw, float vh, ImU32 color) {
    ImVec2 pMin(coords.ToScreenX(vx), coords.ToScreenY(vy));
    ImVec2 pMax(coords.ToScreenX(vx + vw), coords.ToScreenY(vy + vh));
    dl->AddRectFilled(pMin, pMax, color);
}

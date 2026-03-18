# UI System

The UI system is a hybrid of custom OpenGL textured quad rendering and ImGui for complex interaction panels.

## Character Selection

A high-fidelity standalone scene for character management and creation.

- **Scene Rendering**: Standalone terrain, world objects, and grass rendering.
- **Dynamic Lighting**: Sun direction with warm directional light and point lights from scene objects.
- **Projected Shadows**: Real-time shadow projection on terrain for character and equipment.
- **Face Portrait FBO**: Character creation screen features a high-resolution face preview rendered to an off-screen Framebuffer Object (FBO) with auto-framing AABB logic.
- **Emote System**: Interactive class-specific emotes (Salute for males, Greeting for Elf) that blend with idle animations.
- **Equipment Preview**: Full rendering of equipped armor, weapons, and animated wings.

## Character Creation Flow

The creation process is a synchronized handshake between the client UI and the server database.

1. **Class Selection**: Choose between Dark Knight (DK) or Dark Wizard (DW).
2. **Name Validation**: Real-time server-side check for name availability (`SUB_CHARCREATE`).
3. **Stat Initialization**: Database seeds starting stats based on class (e.g., DK starts with 28 STR, DW with 30 ENE).
4. **Transition**: Seamless hand-off from the character scene to the game world.

## Skill System & Combat HUD

Skills are class-resticted abilities with unique costs and ranges.

- **Resource Costs**: DK skills consume **AG (Ability Gauge)**; DW spells consume **Mana**.
- **Combat Feedback**: Real-time damage numbers with color coding (White: Normal, Yellow: Critical, Blue: Excellent).
- **Skill Types**:
    - **Physical (DK)**: Twisting Slash (AoE), Death Stab (Splash), Rageful Blow.
    - **Wizardry (DW)**: Evil Spirit (Screen AoE), Hellfire, Aqua Beam (Line), Teleport (Utility).
- **Stun/Status**: Twister and Evil Spirit apply a brief movement stun (`stormTime`) to targets.
- **DoT**: Poison spells apply a 10-second server-side debuff.

## Font System

Two-font design: readable body text paired with a WoW-style decorative title font.

| Font | File | Use | Base Size |
|------|------|-----|-----------|
| Verdana | `/System/Library/Fonts/Supplemental/Verdana.ttf` | Body text, tooltips, labels, stats | 14px |
| Cinzel | `fonts/Cinzel.ttf` (bundled) | Panel titles, region names, headers | 16px (titles), 28px (region) |

- **`g_fontDefault`**: Verdana — used for all readable text (inventory, tooltips, system messages, nameplates)
- **`g_fontBold`**: Cinzel — used for panel headers and short decorative text
- **`g_fontRegion`**: Cinzel at 28px — used for region name display overlay
- Fonts loaded via ImGui atlas at init, scaled by macOS content scale factor

## Fullscreen Scaling

All UI scales correctly between windowed and fullscreen modes via two mechanisms:

### UICoords Dynamic Offsets
`UICoords::SetCenteredScale(0.7f)` applies a centered 70% scale to all panel coordinates. Offsets are computed **dynamically** per-call from the current window size — never cached — so fullscreen toggling works without re-initialization.

### FontGlobalScale
`ImGui::GetIO().FontGlobalScale = winH / 768.0f` is set each frame before `ImGui::NewFrame()`. This auto-scales simple `AddText(pos, col, text)` calls. However, explicit-size `AddText(font, fontSize, ...)` calls and container dimensions require manual `* uiScale` multiplication:

```cpp
float uiScale = ImGui::GetIO().FontGlobalScale;
float fontSize = 13.0f * uiScale;  // Scale explicit font sizes
float barW = 60.0f * uiScale;      // Scale container dimensions
```

Files with manual uiScale: `GroundItemRenderer.cpp`, `SystemMessageLog.cpp`, `MonsterManagerRender.cpp`, `InventoryUI.cpp`, `InventoryUITooltip.cpp`, `HUD.cpp`, `main.cpp` (quest/menu panels).

## Camera System

Isometric camera pivoting around the player position, implemented in `Camera.cpp`.

- **Zoom**: Mouse scroll, range 400 (close-up) to 1200 (zoomed out), default 800, step 40/scroll
- **Smooth zoom**: `Zoom` lerps toward `TargetZoom` at rate `5.0 * deltaTime`
- **Rotation**: Right-click drag, pitch constrained -89° to -5°
- **State persistence**: Zoom, yaw, pitch saved to `camera_save.txt`

## Diablo HUD (Resource Orbs)

The main HUD is inspired by Diablo, featuring large circular orbs for primary resources.

- **HP Orb**: Red liquid fill reflecting player Health.
- **Mana/AG Orb**: Blue liquid for DW (Mana) or Purple for DK (Ability Gauge).
- **Orb Rendering**: 
    - Gradient-filled circles with specular highlights.
    - Floating text overlays for current/max values.
    - Scaling based on `g_uiPanelScale` (derived from window height).

## Quickbar Expansion

The quickbar supports expanded slots for both skills and consumables.

- **Skill Slots (1-10)**: Support for up to 10 assignable skills.
- **Potion Slots (Q, W, E)**: 3 dedicated slots for quick-access consumables.
- **Cooldown Display**: Semi-transparent radial overlay on icons during cooldown.
- **Hotkeys**: Standard 0.97d keys (1-9 for skills, Q/W/E/R for potions).

## Region Name Display

When entering a new map or region, a stylized nameplate pops up.

- **Texture-based**: Uses OZT images from `Data/Local/[Lang]/ImgsMapName/`.
- **4-State Animation**: `HIDE` → `FADEIN` → `SHOW` (2s hold) → `FADEOUT`.
- **Main 5.2 Implementation**: Matches original `CUIMapName` behavior.

## Notification System

A central screen notification area for non-blocking feedback.

- **Feedback**: Used for "HP is full", "Level Up", and inventory messages.
- **Animation**: Fades out after 1 second.
- **Style**: Dark translucent background with colored text.

## System Message Log

A reactive, filtered log for game events and combat feedback.

- **Categorization**: Modular tabs for "General", "Combat", and "System" messages.
- **Persistence**: Supports up to 500 historical messages with a thin WoW-style scrollbar.
- **Auto-Fade**: Log area remains visible during activity and fades out after 5 seconds of idle time.
- **Interactive**: Hovering "wakes up" the log for scrolling and tab switching.

## Ambient Creature System (Boids)

Procedural life rendered using specialized Boid AI.

- **Bird Flocking**: Atmospheric birds with Cohesion, Separation, and Alignment behaviors.
- **Dynamic AI**: Birds transition between Flying, Landing, Resting (Ground), and Taking Off.
- **Environmental Critters**: Dungeon bats with erratic flight and interactive fish on water tiles.
- **Falling Leaves**: Sinusoidal drifting leaf particles in Lorencia and Devias.

## Media & Diagnostics

- **Screenshot & GIF**: High-performance JPEG capture (TurboJPEG) and animated GIF recording with frame-diffing optimization.
- **Live Diagnostics**: Toggleable overlays for nearby objects and terrain attributes.

## Modules

| Module | Purpose |
|--------|---------|
| `InventoryUI.cpp` | Main UI orchestrator (Panels, HUD, Quickbar) |
| `CharacterSelect.cpp` | Character selection scene and creation UI |
| `SystemMessageLog.cpp` | Persistent message log with tab filtering |
| `BoidManager.cpp` | Ambient creature AI (birds, bats, fish, leaves) |
| `Screenshot.cpp` | JPEG/GIF capture and recording system |
| `CombatHandler.cpp` | Server-side skill logic and damage math |
| `QuestHandler.cpp` | Mission state and reward distribution |
| `HUD.cpp` | Layout and rendering of the Diablo HUD |
| `GameUI.cpp` | General UI texture and widget helpers |
| `UITexture.cpp` | Low-level UI texture loading and management |
| `UIWidget.cpp` | UI primitives (buttons, slots, bars) |
| `UICoords.hpp` | Coordinate system mapping for responsive UI |

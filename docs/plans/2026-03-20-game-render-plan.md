# game_render Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create game_render abstraction layer with software backend, wired into the game loop for camera, horizon, and frame presentation.

**Architecture:** Facade pattern mirroring `menu_render`. Opaque `GameRenderer` struct dispatches to `GameRendererSoftware`. Global `g_pGameRenderer` pointer (consistent with codebase's global pattern for `scrbuf`, `screen_pointer`, etc.). Software backend delegates to existing rendering functions.

**Tech Stack:** C, SDL3. No test framework — verification is build + run game.

**Design doc:** `docs/plans/2026-03-20-game-render-design.md`

**Note on scope:** This plan creates all files with complete implementations, then wires camera, horizon, and frame presentation into the game loop. Track polygon routing (`game_render_quad` in `DrawTrack3`/`subdivide`) and car rendering (`game_render_draw_car` in `DrawCars`) are implemented in the software backend but **not wired into the game loop yet** — that deeper refactor is a follow-up plan. DRY extraction into `render_common` is also deferred until the GPU backend is added (both software backends already share code by calling the same underlying functions).

---

## Phase 1: Scaffolding

### Task 1: Create game_render.h

**Files:**
- Create: `PROJECTS/ROLLER/game_render.h`

**Step 1: Write the header**

```c
#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"

typedef enum {
    GAME_RENDER_GPU,
    GAME_RENDER_SOFTWARE
} GameRenderMode;

typedef struct GameRenderer GameRenderer;

// Lifecycle
GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window);
void game_render_destroy(GameRenderer *renderer);
void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode);
GameRenderMode game_render_get_mode(GameRenderer *renderer);

// Frame lifecycle
void game_render_begin_frame(GameRenderer *renderer);
void game_render_end_frame(GameRenderer *renderer);

// Viewport
void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h);

// Camera
void game_render_set_camera(GameRenderer *renderer,
                            int viewMode, int carIdx, int chaseCamIdx);

// Asset loading — track textures
int game_render_load_track_textures(GameRenderer *renderer,
                                    uint8 *texture_vga, int gfx_size);
void game_render_free_track_textures(GameRenderer *renderer);

// Asset loading — car meshes
void game_render_load_car_mesh(GameRenderer *renderer, int carIdx,
                               const tColor *palette);
void game_render_free_car_mesh(GameRenderer *renderer, int carIdx);

// Asset loading — horizon
int game_render_load_horizon(GameRenderer *renderer, uint8 *horizon_data);
void game_render_free_horizon(GameRenderer *renderer);

// Asset loading — sprite blocks (HUD)
int game_render_load_blocks(GameRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette);
void game_render_free_blocks(GameRenderer *renderer, int slot);

// Draw — polygon (track, buildings, particles, clouds)
void game_render_quad(GameRenderer *renderer,
                      tPolyParams *poly,
                      const uint8 *texture_data,
                      int gfx_size,
                      const uint8 *palette_remap);

// Draw — car mesh
void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          int yaw, int pitch, int roll,
                          float worldX, float worldY, float worldZ,
                          int animFrame, const uint8 *color_remap);

// Draw — horizon
void game_render_draw_horizon(GameRenderer *renderer);

// Draw — HUD sprites
void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette);

// Palette
void game_render_set_palette(GameRenderer *renderer, const tColor *palette);

// Fade
void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames);
int game_render_fade_active(GameRenderer *renderer);
void game_render_fade_wait(GameRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx);

#endif
```

**Step 2: Commit**

```bash
git add PROJECTS/ROLLER/game_render.h
git commit -m "feat: add game_render.h public API header"
```

---

### Task 2: Create game_render_software.h

**Files:**
- Create: `PROJECTS/ROLLER/game_render_software.h`

**Step 1: Write the internal header**

Follow `menu_render_software.h` pattern. Every public API function has a corresponding `game_render_sw_*` function.

```c
#ifndef GAME_RENDER_SOFTWARE_H
#define GAME_RENDER_SOFTWARE_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"

typedef struct GameRendererSoftware GameRendererSoftware;

// Lifecycle
GameRendererSoftware *game_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window);
void game_render_sw_destroy(GameRendererSoftware *sw);

// Frame lifecycle
void game_render_sw_begin_frame(GameRendererSoftware *sw);
void game_render_sw_end_frame(GameRendererSoftware *sw);

// Viewport
void game_render_sw_set_viewport(GameRendererSoftware *sw,
                                 int x, int y, int w, int h);

// Camera
void game_render_sw_set_camera(GameRendererSoftware *sw,
                               int viewMode, int carIdx, int chaseCamIdx);

// Asset loading
int game_render_sw_load_track_textures(GameRendererSoftware *sw,
                                       uint8 *texture_vga, int gfx_size);
void game_render_sw_free_track_textures(GameRendererSoftware *sw);
void game_render_sw_load_car_mesh(GameRendererSoftware *sw, int carIdx,
                                  const tColor *palette);
void game_render_sw_free_car_mesh(GameRendererSoftware *sw, int carIdx);
int game_render_sw_load_horizon(GameRendererSoftware *sw,
                                uint8 *horizon_data);
void game_render_sw_free_horizon(GameRendererSoftware *sw);
int game_render_sw_load_blocks(GameRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette);
void game_render_sw_free_blocks(GameRendererSoftware *sw, int slot);

// Draw calls
void game_render_sw_quad(GameRendererSoftware *sw, tPolyParams *poly,
                         const uint8 *texture_data, int gfx_size,
                         const uint8 *palette_remap);
void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             int yaw, int pitch, int roll,
                             float worldX, float worldY, float worldZ,
                             int animFrame, const uint8 *color_remap);
void game_render_sw_draw_horizon(GameRendererSoftware *sw);
void game_render_sw_sprite(GameRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette);

// Palette
void game_render_sw_set_palette(GameRendererSoftware *sw,
                                const tColor *palette);

// Fade
void game_render_sw_begin_fade(GameRendererSoftware *sw, int direction,
                               int durationFrames);
int game_render_sw_fade_active(GameRendererSoftware *sw);
void game_render_sw_fade_wait(GameRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx);

#endif
```

**Step 2: Commit**

```bash
git add PROJECTS/ROLLER/game_render_software.h
git commit -m "feat: add game_render_software.h internal header"
```

---

### Task 3: Create game_render_software.c

**Files:**
- Create: `PROJECTS/ROLLER/game_render_software.c`

**Step 1: Write the full software backend**

Pattern: mirrors `menu_render_software.c`. Most functions are thin wrappers or no-ops. Asset loading is no-op because data lives in globals (`texture_vga`, `CarDesigns[]`, etc.). Draw calls delegate to existing functions.

```c
#include "game_render_software.h"
#include "3d.h"
#include "func2.h"
#include "func3.h"
#include "view.h"
#include "horizon.h"
#include "car.h"
#include "polytex.h"
#include "roller.h"
#include "sound.h"

#include <stdlib.h>
#include <math.h>

#define GAME_RENDER_MAX_BLOCK_SLOTS 32

struct GameRendererSoftware {
    int fadeInPending;
    tBlockHeader *blocks[GAME_RENDER_MAX_BLOCK_SLOTS];
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

GameRendererSoftware *game_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window) {
    (void)device;
    (void)window;
    GameRendererSoftware *sw = calloc(1, sizeof(GameRendererSoftware));
    return sw;
}

void game_render_sw_destroy(GameRendererSoftware *sw) {
    free(sw);
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void game_render_sw_begin_frame(GameRendererSoftware *sw) {
    (void)sw;
}

void game_render_sw_end_frame(GameRendererSoftware *sw) {
    if (sw->fadeInPending) {
        sw->fadeInPending = 0;
        palette_brightness = 0;
        for (int i = 0; i < 256; i++) {
            pal_addr[i].byR = 0;
            pal_addr[i].byB = 0;
            pal_addr[i].byG = 0;
        }
        fade_palette(32);
        return;
    }
    g_bPaletteSet = true;
    UpdateSDLWindow();
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

void game_render_sw_set_viewport(GameRendererSoftware *sw,
                                 int x, int y, int w, int h) {
    (void)sw;
    (void)w;
    (void)h;
    // Set screen_pointer to the viewport origin within scrbuf.
    // Existing rendering code writes relative to screen_pointer.
    screen_pointer = scrbuf + (y * winw) + x;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void game_render_sw_set_camera(GameRendererSoftware *sw,
                               int viewMode, int carIdx, int chaseCamIdx) {
    (void)sw;
    calculateview(viewMode, carIdx, chaseCamIdx);
}

// ---------------------------------------------------------------------------
// Asset loading — all no-ops for software (data lives in globals)
// ---------------------------------------------------------------------------

int game_render_sw_load_track_textures(GameRendererSoftware *sw,
                                       uint8 *texture_vga, int gfx_size) {
    (void)sw; (void)texture_vga; (void)gfx_size;
    return 0;
}

void game_render_sw_free_track_textures(GameRendererSoftware *sw) {
    (void)sw;
}

void game_render_sw_load_car_mesh(GameRendererSoftware *sw, int carIdx,
                                  const tColor *palette) {
    (void)sw; (void)carIdx; (void)palette;
}

void game_render_sw_free_car_mesh(GameRendererSoftware *sw, int carIdx) {
    (void)sw; (void)carIdx;
}

int game_render_sw_load_horizon(GameRendererSoftware *sw,
                                uint8 *horizon_data) {
    (void)sw; (void)horizon_data;
    return 0;
}

void game_render_sw_free_horizon(GameRendererSoftware *sw) {
    (void)sw;
}

int game_render_sw_load_blocks(GameRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette) {
    (void)palette;
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS)
        sw->blocks[slot] = blocks;
    return 0;
}

void game_render_sw_free_blocks(GameRendererSoftware *sw, int slot) {
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS)
        sw->blocks[slot] = NULL;
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

void game_render_sw_quad(GameRendererSoftware *sw, tPolyParams *poly,
                         const uint8 *texture_data, int gfx_size,
                         const uint8 *palette_remap) {
    (void)sw;
    (void)palette_remap;
    if (poly->iSurfaceType & 0x100) { // SURFACE_FLAG_APPLY_TEXTURE
        int texIdx = poly->iSurfaceType & 0xFF;
        POLYTEX((uint8 *)texture_data, screen_pointer, poly, texIdx, gfx_size);
    } else {
        POLYFLAT(screen_pointer, poly);
    }
}

void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             int yaw, int pitch, int roll,
                             float worldX, float worldY, float worldZ,
                             int animFrame, const uint8 *color_remap) {
    (void)sw;
    (void)yaw; (void)pitch; (void)roll;
    (void)animFrame; (void)color_remap;
    // Compute distance from camera to car for LOD.
    // DisplayCar reads car state from global Car[] array.
    extern float viewx, viewy, viewz;
    float dx = worldX - viewx;
    float dy = worldY - viewy;
    float dz = worldZ - viewz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    DisplayCar(carIdx, screen_pointer, dist);
}

void game_render_sw_draw_horizon(GameRendererSoftware *sw) {
    (void)sw;
    DrawHorizon(screen_pointer);
}

void game_render_sw_sprite(GameRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette) {
    (void)palette;
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS && sw->blocks[slot])
        display_block(scrbuf, sw->blocks[slot], blockIdx, x, y,
                      transparentColorIndex);
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

void game_render_sw_set_palette(GameRendererSoftware *sw,
                                const tColor *palette) {
    (void)sw;
    for (int i = 0; i < 256; i++)
        pal_addr[i] = palette[i];
}

// ---------------------------------------------------------------------------
// Fade
// ---------------------------------------------------------------------------

void game_render_sw_begin_fade(GameRendererSoftware *sw, int direction,
                               int durationFrames) {
    (void)durationFrames;
    if (direction) {
        sw->fadeInPending = 1;
    } else {
        fade_palette(0);
    }
}

int game_render_sw_fade_active(GameRendererSoftware *sw) {
    (void)sw;
    return 0; // blocking fade completes immediately
}

void game_render_sw_fade_wait(GameRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx) {
    (void)sw;
    (void)redraw_fn;
    (void)ctx;
}
```

**Step 2: Commit**

```bash
git add PROJECTS/ROLLER/game_render_software.c
git commit -m "feat: add game_render software backend"
```

---

### Task 4: Create game_render.c

**Files:**
- Create: `PROJECTS/ROLLER/game_render.c`

**Step 1: Write the facade dispatcher**

Follow `menu_render.c` pattern exactly. Software-only for now — the `if (mode == SOFTWARE)` checks make adding the GPU backend trivial later. Asset load/free functions call the software backend unconditionally (same as `menu_render.c` calls both backends).

```c
#include "game_render.h"
#include "game_render_software.h"

#include <stdlib.h>

struct GameRenderer {
    GameRenderMode mode;
    GameRendererSoftware *sw;
    SDL_GPUDevice *device;
    SDL_Window *window;
};

GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window) {
    GameRenderer *r = calloc(1, sizeof(GameRenderer));
    r->device = device;
    r->window = window;
    r->sw = game_render_sw_create(device, window);
    r->mode = GAME_RENDER_SOFTWARE;
    return r;
}

void game_render_destroy(GameRenderer *renderer) {
    game_render_sw_destroy(renderer->sw);
    free(renderer);
}

void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode) {
    (void)renderer;
    (void)mode;
    // Only software mode available currently
}

GameRenderMode game_render_get_mode(GameRenderer *renderer) {
    return renderer->mode;
}

// Frame lifecycle

void game_render_begin_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_frame(renderer->sw);
}

void game_render_end_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_end_frame(renderer->sw);
}

// Viewport

void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_viewport(renderer->sw, x, y, w, h);
}

// Camera

void game_render_set_camera(GameRenderer *renderer,
                            int viewMode, int carIdx, int chaseCamIdx) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_camera(renderer->sw, viewMode, carIdx, chaseCamIdx);
}

// Asset loading

int game_render_load_track_textures(GameRenderer *renderer,
                                    uint8 *texture_vga, int gfx_size) {
    return game_render_sw_load_track_textures(renderer->sw,
                                              texture_vga, gfx_size);
}

void game_render_free_track_textures(GameRenderer *renderer) {
    game_render_sw_free_track_textures(renderer->sw);
}

void game_render_load_car_mesh(GameRenderer *renderer, int carIdx,
                               const tColor *palette) {
    game_render_sw_load_car_mesh(renderer->sw, carIdx, palette);
}

void game_render_free_car_mesh(GameRenderer *renderer, int carIdx) {
    game_render_sw_free_car_mesh(renderer->sw, carIdx);
}

int game_render_load_horizon(GameRenderer *renderer, uint8 *horizon_data) {
    return game_render_sw_load_horizon(renderer->sw, horizon_data);
}

void game_render_free_horizon(GameRenderer *renderer) {
    game_render_sw_free_horizon(renderer->sw);
}

int game_render_load_blocks(GameRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette) {
    return game_render_sw_load_blocks(renderer->sw, slot, blocks, palette);
}

void game_render_free_blocks(GameRenderer *renderer, int slot) {
    game_render_sw_free_blocks(renderer->sw, slot);
}

// Draw calls

void game_render_quad(GameRenderer *renderer, tPolyParams *poly,
                      const uint8 *texture_data, int gfx_size,
                      const uint8 *palette_remap) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_quad(renderer->sw, poly, texture_data, gfx_size,
                            palette_remap);
}

void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          int yaw, int pitch, int roll,
                          float worldX, float worldY, float worldZ,
                          int animFrame, const uint8 *color_remap) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_car(renderer->sw, carIdx, yaw, pitch, roll,
                                worldX, worldY, worldZ, animFrame,
                                color_remap);
}

void game_render_draw_horizon(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_horizon(renderer->sw);
}

void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_sprite(renderer->sw, slot, blockIdx, x, y,
                              transparentColorIndex, palette);
}

// Palette

void game_render_set_palette(GameRenderer *renderer, const tColor *palette) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_palette(renderer->sw, palette);
}

// Fade

void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_fade(renderer->sw, direction, durationFrames);
}

int game_render_fade_active(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        return game_render_sw_fade_active(renderer->sw);
    return 0;
}

void game_render_fade_wait(GameRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_fade_wait(renderer->sw, redraw_fn, ctx);
}
```

**Step 2: Commit**

```bash
git add PROJECTS/ROLLER/game_render.c
git commit -m "feat: add game_render facade dispatcher"
```

---

### Task 5: Update build systems and verify compilation

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `Makefile`

**Step 1: Add source files to CMakeLists.txt**

Insert after the `menu_render_software.c` line (line 43):

```
    PROJECTS/ROLLER/game_render.c
    PROJECTS/ROLLER/game_render_software.c
```

**Step 2: Add source files to Makefile**

Insert after the `menu_render_software.c \` line (line 53):

```
        PROJECTS/ROLLER/game_render.c \
        PROJECTS/ROLLER/game_render_software.c \
```

**Step 3: Build**

```bash
cmake --build build 2>&1 | head -50
```

Fix any compilation errors. Common issues:
- Missing includes — check that `view.h` declares `calculateview`, `horizon.h` declares `DrawHorizon`, `car.h` declares `DisplayCar`
- `viewx`/`viewy`/`viewz` extern declarations — these are in `3d.h` or `func2.h`; the `extern float viewx, viewy, viewz;` in `game_render_software.c` may need adjustment if they're declared differently (e.g., as `double` or in a different header)

**Step 4: Commit**

```bash
git add CMakeLists.txt Makefile
git commit -m "build: add game_render source files to all build systems"
```

---

## Phase 2: Integration

### Task 6: Add global GameRenderer and wire lifecycle into play_game

**Files:**
- Modify: `PROJECTS/ROLLER/3d.h` — add extern declaration
- Modify: `PROJECTS/ROLLER/3d.c` — add global, create in `play_game_init`, destroy in `play_game_uninit`

**Step 1: Add extern to 3d.h**

Near the other extern declarations (near `extern uint8 *scrbuf`):

```c
#include "game_render.h"

extern GameRenderer *g_pGameRenderer;
```

**Step 2: Add global definition in 3d.c**

Near the other global definitions (near `uint8 *scrbuf = NULL`):

```c
GameRenderer *g_pGameRenderer = NULL;
```

**Step 3: Create in play_game_init**

In `play_game_init()`, after SDL resources are available (the function already has access to the GPU device and window via globals in `roller.h`). Add near the end of initialization, after `loadtrack`/`InitCars`:

```c
// Find the existing references to s_pGPUDevice and s_pWindow.
// They're declared in roller.h or accessible via extern.
// If they're static in roller.c, use the SDL globals or pass them differently.
// The menu_render_create call elsewhere shows how they're accessed.
extern SDL_GPUDevice *s_pGPUDevice;
extern SDL_Window *s_pWindow;
g_pGameRenderer = game_render_create(s_pGPUDevice, s_pWindow);
```

> **Note:** Check how `menu_render_create` gets `s_pGPUDevice` and `s_pWindow`. Follow the same pattern. If they're static in `roller.c`, there may be accessor functions or they may be passed through a different mechanism. Grep for `menu_render_create` to find the call site and see how the device/window are provided.

**Step 4: Destroy in play_game_uninit**

In `play_game_uninit()`, before freeing other resources:

```c
game_render_destroy(g_pGameRenderer);
g_pGameRenderer = NULL;
```

**Step 5: Build and verify**

```bash
cmake --build build 2>&1 | head -50
```

Run the game, start a race, exit. Verify no crashes on init or cleanup.

**Step 6: Commit**

```bash
git add PROJECTS/ROLLER/3d.h PROJECTS/ROLLER/3d.c
git commit -m "feat: wire game_render lifecycle into play_game"
```

---

### Task 7: Route camera and horizon through game_render in draw_road

**Files:**
- Modify: `PROJECTS/ROLLER/3d.c` — `draw_road` function

**Step 1: Find draw_road**

Search for `void draw_road` in `3d.c` (around line 967). The function body has this sequence:

```c
screen_pointer = pScrPtr;
// ... subscale setup ...
calculateview(uiViewMode, iCarIdx, iChaseCamIdx);
DrawHorizon(pScrPtr);
CalcVisibleTrack(iCarIdx, uiViewMode);
DrawCars(iCarIdx, uiViewMode);
CalcVisibleBuildings();
DrawTrack3(pScrPtr, iChaseCamIdx, iCarIdx);
```

**Step 2: Replace calculateview**

Change:
```c
calculateview(uiViewMode, iCarIdx, iChaseCamIdx);
```
To:
```c
game_render_set_camera(g_pGameRenderer, uiViewMode, iCarIdx, iChaseCamIdx);
```

**Step 3: Replace DrawHorizon**

Change:
```c
DrawHorizon(pScrPtr);
```
To:
```c
game_render_draw_horizon(g_pGameRenderer);
```

**Step 4: Add include if needed**

`3d.c` should already include `3d.h` which now includes `game_render.h`. If not, add:
```c
#include "game_render.h"
```

**Step 5: Build and verify**

```bash
cmake --build build 2>&1 | head -50
```

Run the game, start a race. The horizon and camera should render identically to before — the software backend calls the same underlying functions.

**Step 6: Commit**

```bash
git add PROJECTS/ROLLER/3d.c
git commit -m "feat: route camera and horizon through game_render"
```

---

### Task 8: Route frame presentation through game_render

**Files:**
- Modify: `PROJECTS/ROLLER/3d.c` — `updatescreen` function and game loop

**Step 1: Find updatescreen**

Search for `void updatescreen` in `3d.c` (around line 699).

**Step 2: Add begin_frame at the top**

At the very start of `updatescreen()`, after local variable declarations:

```c
game_render_begin_frame(g_pGameRenderer);
```

**Step 3: Add end_frame at the bottom**

At the very end of `updatescreen()`, after `init_animate_ads()` or the last statement:

```c
game_render_end_frame(g_pGameRenderer);
```

**Step 4: Find and remove duplicate UpdateSDLWindow call**

The software backend's `end_frame` calls `UpdateSDLWindow()`. Search for where `UpdateSDLWindow()` is called in the game frame path (NOT in menu code). Common locations:
- After `game_copypic` in `updatescreen`
- In the `play_game` main loop
- In `game_copypic` itself

Remove or guard the duplicate call so the frame isn't presented twice. Use grep:
```bash
grep -n "UpdateSDLWindow" PROJECTS/ROLLER/3d.c
```

If `UpdateSDLWindow` is called from `game_copypic` in `3d.c`, you may need to conditionally skip it when `g_pGameRenderer` is active, or restructure so `game_copypic` only does the copy/overlay work without presenting.

> **Important:** This is the trickiest integration point. The goal is: `game_render_end_frame` is the ONLY place that calls `UpdateSDLWindow` during gameplay. If the game calls it from multiple places, you need to consolidate. Test carefully — if the screen goes black or renders double, the UpdateSDLWindow call is missing or duplicated.

**Step 5: Build and verify**

```bash
cmake --build build 2>&1 | head -50
```

Run the game, start a race. Frame presentation should work identically.

**Step 6: Commit**

```bash
git add PROJECTS/ROLLER/3d.c
git commit -m "feat: route frame presentation through game_render"
```

---

## Future Work (not in this plan)

### Track polygon routing
Thread `g_pGameRenderer` through `DrawTrack3` and `subdivide`. Replace every `POLYTEX`/`POLYFLAT` call with `game_render_quad(g_pGameRenderer, ...)`. The software backend calls the same rasterizers. This is a mechanical but large refactor (~20-40 call sites across `drawtrk3.c`, `building.c`, and the subdivide functions).

### Car rendering routing
Replace `DisplayCar` calls in `DrawCars` with `game_render_draw_car(g_pGameRenderer, ...)`. Requires mapping DrawCars' computed distance to the world-space API or adding distance as a parameter.

### HUD sprite routing
Wire `game_render_load_blocks` / `game_render_sprite` into the HUD rendering code (`test_panel`, speed display, minimap).

### Fade/palette routing
Replace `fade_palette` calls in the game loop with `game_render_begin_fade` / `game_render_fade_wait`.

### DRY extraction (render_common)
When the GPU backend is added, extract shared code into `render_common.h/.c`:
- Frame presentation (`UpdateSDLWindow` internals → `render_present_indexed`)
- Car mesh → GPU conversion (from `menu_render_gpu.c`)
- Fade logic
- Sprite blitting
Both `menu_render` and `game_render` software backends already share code by calling the same underlying functions, so this extraction only becomes valuable when GPU code needs sharing.

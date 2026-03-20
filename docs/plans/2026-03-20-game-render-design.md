# game_render: Game Rendering Abstraction Layer

## Goal

Abstract the game's rendering pipeline behind a backend-switchable API, mirroring the existing `menu_render` pattern. The primary goal is to eventually have a GPU backend that replaces the software polygon rasterizers with actual GPU draw calls. Initial implementation is software-only.

## Design Decisions

- **Abstraction level: mid-level primitive submission.** The game loop still orchestrates what to draw (horizon, track chunks, cars), but submits primitives through `game_render` instead of calling rasterizers directly. This is the natural seam where software and GPU backends diverge.
- **Camera owned by game_render.** `game_render_set_camera()` configures the view; polygon submissions are in world space. Software backend calls existing `calculateview()` internally. GPU backend would build a MVP matrix.
- **Single polygon primitive.** One `game_render_quad()` call handles all polygon geometry (track, buildings, particles, clouds). The backend reads `iSurfaceType` flags to choose textured vs flat rasterization.
- **Car meshes are separate.** Cars are full 3D meshes with per-frame transforms, not individual quads. The API takes world-space position and orientation; the backend handles rendering.
- **Hybrid asset management.** Bulk assets (track textures, car meshes) go through load/free lifecycle. Palette and remap tables are passed per-draw-call since they change frequently.
- **game_render owns the display pipeline.** `end_frame` handles everything: software backend does indexed-to-RGBA conversion and SDL blit (absorbing `UpdateSDLWindow`). GPU backend would present its own swapchain.
- **Viewports from the start.** `set_viewport` supports split-screen by being called before each view's geometry submission.
- **HUD sprites go through game_render.** Keeps the rendering path consistent and makes GPU backend transition cleaner.
- **DRY with menu_render.** Sprite blitting, frame presentation, fade logic, and car mesh GPU conversion are extracted into shared `render_common` infrastructure.

## API

```c
#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"

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

## File Structure

| File | Purpose |
|------|---------|
| `game_render.h` | Public API |
| `game_render.c` | Facade/dispatcher (routes to software or GPU backend) |
| `game_render_software.h` | Software backend internal header |
| `game_render_software.c` | Software backend (wraps existing rasterizers) |
| `render_common.h` | Shared rendering infrastructure header |
| `render_common.c` | Shared code: sprites, frame presentation, fade, car mesh GPU conversion |

## Shared Infrastructure (render_common)

Code extracted from `menu_render` and `roller.c` to be reused by both renderers:

- **Sprite blitting** — `display_block` wrapper used by both menu and game software backends
- **Frame presentation** — indexed-to-RGBA conversion + SDL GPU transfer buffer + swapchain blit (currently in `UpdateSDLWindow`)
- **Fade logic** — palette fade math
- **Car mesh GPU conversion** — `tCarDesign` to GPU vertex/index buffers + texture atlas (currently in `menu_render_gpu.c`)

## What Stays Separate

- **Track mesh loading** — menu builds a simplified bird's-eye overview; game needs per-chunk detail with LOD subdivision
- **Polygon rasterization** — game only (subdivide/POLYFLAT/POLYTEX)
- **Layer system** — menu only (background/preview/foreground compositing)

## Software Backend Strategy

The software backend wraps existing code with minimal changes:

- `game_render_quad` → calls `subdivide()` / `POLYTEX` / `POLYFLAT`
- `game_render_draw_car` → calls existing `DisplayCar` machinery
- `game_render_draw_horizon` → calls existing `DrawHorizon`
- `game_render_sprite` → calls `display_block`
- `game_render_set_camera` → calls `calculateview()`
- `game_render_end_frame` → calls `render_present_indexed()` (extracted from `UpdateSDLWindow`)

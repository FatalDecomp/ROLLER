#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"
#include "scene_render.h"
#include "crt_filter.h"

typedef struct SceneRendererGPU SceneRendererGPU;

typedef enum {
    GAME_RENDER_GPU,
    GAME_RENDER_SOFTWARE
} GameRenderMode;

typedef SceneTextureHandle TextureHandle;
#define TEXTURE_HANDLE_INVALID SCENE_TEXTURE_HANDLE_INVALID

typedef SceneRenderVertex GameRenderVertex;
typedef SceneRenderCamera GameRenderCamera;
typedef SceneRenderProjection GameRenderProjection;

typedef struct GameRenderCarPose {
    tVec3 position;
    int yaw;
    int pitch;
    int roll;
} GameRenderCarPose;

typedef struct GameRenderCarOptions {
    int anim_frame;
    const uint8 *color_remap;
} GameRenderCarOptions;

#define GAME_RENDER_SUBDIVIDE_TYPE_AUTO SCENE_RENDER_SUBDIVIDE_TYPE_AUTO
#define GAME_RENDER_SUBDIVIDE_TYPE_CLOUD SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD
#define GAME_RENDER_SUBDIVIDE_TYPE_BUILDING SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING
#define GAME_RENDER_SUBDIVIDE_TYPE_SIGN SCENE_RENDER_SUBDIVIDE_TYPE_SIGN

/* GPU-only routing flags set by building.c.  Both use bits above 15 so they
 * cannot appear in building polygon data (uiTex is 16-bit).  SW renderer
 * ignores them entirely. */
#define SURFACE_FLAG_GPU_IS_SIGN 0x00100000  /* real advert-sign quad (bit 20 / BOUNCE_20) */
#define SURFACE_FLAG_GPU_IS_TREE 0x00400000  /* camera-facing billboard tree (bit 22 / WALL_22) */

typedef struct GameRenderer GameRenderer;

// Lifecycle
GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window);
void game_render_destroy(GameRenderer *renderer);
void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode);
GameRenderMode game_render_get_mode(const GameRenderer *renderer);
void game_render_set_force_gpu_load(GameRenderer *renderer, bool force);
void game_render_set_particle_depth(GameRenderer *renderer, float ndcZ);
void game_render_set_particle_depth_pervertex(GameRenderer *renderer, const float ndcZ[4]);
void game_render_set_split_screen(GameRenderer *renderer, bool split);
bool game_render_is_split_screen(const GameRenderer *renderer);
void game_render_set_debug_overlay(GameRenderer *renderer, DebugOverlay *overlay);
void game_render_set_crt_filter(GameRenderer *renderer, CRTFilter *filter);

// Frame lifecycle
void game_render_begin_frame(GameRenderer *renderer);
void game_render_end_frame(GameRenderer *renderer);

// Mirror pass (rearview / side view): in GPU mode, the scene calls made
// between begin/end (with a secondary camera already set via
// game_render_set_camera/set_projection) render natively on the GPU into a
// small offscreen target instead of falling back to SW. end_mirror_pass
// flushes that offscreen render and caches the resulting texture; call
// composite_mirror_pass afterward (once the real on-screen destination rect
// is known -- SW globals winw/winh/xbase are typically restored to their
// full-window values by then) to blit it onto the current frame.
// scrSizeRatio: the same fraction 3d.c just divided scr_size by for the
// mirror window (e.g. 0.25 for the 1/4-size rearview mirror, 0.5 for the
// 1/2-size side mirror). The GPU's FOV/aspect math is coupled to the current
// viewport size, so the mirror's queued geometry needs that same reference
// scaled down by this ratio, or its effective FOV comes out wrong -- SW just
// draws the same FOV into a smaller pixel buffer, but GPU normalizes FOV by
// viewport size, so leaving the viewport unset (i.e. sized for the main
// window) makes the mirror's picture significantly narrower/zoomed-in than
// intended. Restored to the default (main-window) viewport in end_mirror_pass.
// texW/texH: desired offscreen render-target pixel size.
// screenX/Y/W/H: on-screen destination rect in the same logical pixel space
// as winw/winh. flipH: true for a true left-right mirror image (rearview);
// false for the side-view mirror. borderColorIdx: palette index for the
// thin border drawn around the picture.
// In SW mode these are all no-ops (the existing SW mirbuf path is used).
void game_render_begin_mirror_pass(GameRenderer *renderer, float scrSizeRatio);
void game_render_end_mirror_pass(GameRenderer *renderer, int texW, int texH);
void game_render_composite_mirror_pass(GameRenderer *renderer,
                                       int screenX, int screenY,
                                       int screenW, int screenH,
                                       bool flipH, int borderColorIdx);

// CINEMA cheat letterbox (default behaviour): same secondary-view-then-
// composite pattern as the mirror pass above, giving GPU mode a real
// letterboxed 3D view (with opaque black bars) matching SW's own cinema
// mode instead of always rendering full-size. In SW mode these are no-ops
// (the existing SW scrbuf-shrink path is used). screenX/Y/W/H should be
// winx/winy/winw/winh (the cinema sub-rect within the fixed XMAX x YMAX
// reference frame).
void game_render_begin_cinema_pass(GameRenderer *renderer, int winwCinema, int winhCinema);
void game_render_end_cinema_pass(GameRenderer *renderer, int texW, int texH);
void game_render_composite_cinema_view(GameRenderer *renderer,
                                       int screenX, int screenY,
                                       int screenW, int screenH);

// Widescreen support ("(native)" Render Scale options, debug_overlay.c):
// widens the GPU's own 3D camera viewport to the real window's native aspect
// ratio (no bars) at a FIXED height (refWinH -- pass the same winh normal
// single-player mode uses), without touching the SW/HUD reference frame at
// all, so the HUD stays pixel-for-pixel identical to normal mode instead of
// blurry/stretched (see game_render_begin_cinema_native's comment for why
// this is safe: only vertical centering is coupled to a SW-side reference in
// build_mvp, not horizontal). Available independently of the CINEMA cheat --
// 3d.c calls this from both the normal single-player path and the CINEMA-
// active path. game_render_reset_cinema_native() MUST be called
// unconditionally every frame before deciding whether this frame is a
// Native frame -- its state persists across scene_render_gpu_end_frame,
// unlike the letterbox pass above.
void game_render_begin_cinema_native(GameRenderer *renderer, int refWinH);
void game_render_reset_cinema_native(GameRenderer *renderer);

// 2-player split screen: each player's queued draws (game_render_draw_car/
// quad_world/etc, called with that player's own camera/projection already
// set via draw_road) render into their own offscreen slot instead of sharing
// the single main draw queue -- which used to make both players' geometry
// accumulate together and render overlapping -- then get composited into
// their half of the frame as a plain (non-flipped, borderless) screen quad.
// slot: 0 for player 1, 1 for player 2 (each keeps its own persistent
// texture so flushing player 2 doesn't overwrite player 1's not-yet-
// composited picture).
// texW/texH: offscreen render-target pixel size (pass that player's own
// winw/winh at the point their draw_road call ran).
// frameW/frameH: the FULL output frame size (XMAX/YMAX), used to convert
// screenX/Y/W/H into NDC -- NOT winw/winh, which in 2P mode are sized to a
// single player's half rather than the full frame.
// In SW mode this is a no-op (the existing SW path already handles 2P).
void game_render_flush_player_view(GameRenderer *renderer, int slot,
                                   int texW, int texH,
                                   int frameW, int frameH,
                                   int screenX, int screenY,
                                   int screenW, int screenH);

// Call once before both players' draw_road() calls and once after both
// flush_player_view calls. Scales the GPU viewport the same way
// begin/end_mirror_pass do (see that comment): 2-player mode halves scr_size
// relative to single-player's default, so the FOV needs the same 640x400
// baseline scaled by that same ratio while queuing each player's draws,
// restored to the default afterward.
void game_render_begin_2p_pass(GameRenderer *renderer);
void game_render_end_2p_pass(GameRenderer *renderer);

// Call right before EACH player's own draw_road() (not just once for both):
// snapshots textured-particle state so the following flush_player_view call
// can tell "this player's own scene" (including real smoke/explosions, which
// must NOT leak into the final composited frame with wrong per-view screen
// coordinates) apart from an earlier player's still-pending composite quad.
void game_render_secondary_view_will_queue(GameRenderer *renderer);

// Call right before EACH player's own draw_road(), alongside
// game_render_secondary_view_will_queue(). slot: 0=P1, 1=P2. Lets per-car GPU
// state keyed only by carIdx (e.g. the car name-tag scrY smoothing) also key
// on which player's view is currently being queued.
void game_render_set_active_view_slot(GameRenderer *renderer, int slot);

// SW draws the divider between the two player views by clearing a few rows
// of scrbuf directly; GPU mode's 3D content no longer comes from scrbuf (it's
// composited from each player's own offscreen render), so that clear isn't
// visible there -- draw an explicit flat black bar instead. Call after both
// game_render_flush_player_view calls. frameW/frameH: XMAX/YMAX. dividerY/
// dividerH: same rect SW clears (winh, 4).
// In SW mode this is a no-op.
void game_render_draw_2p_divider(GameRenderer *renderer,
                                 int frameW, int frameH,
                                 int dividerY, int dividerH);

// Darkens the 3D scene behind the in-race pause/options menu, matching SW's
// blankwindow()+shade-level-3 effect in func2.c's display_paused(). Call once
// per frame while the pause overlay is active (game_req != 0), before or
// after display_paused() -- ordering doesn't matter since this draws into the
// 3D scene at NDC z=0 (always wins depth test) while the menu text still
// goes through the separate HUD/scrbuf compositing pass drawn last.
// In SW mode this is a no-op (SW already handles it).
void game_render_draw_pause_darken(GameRenderer *renderer);

// Viewport
void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h);
void game_render_set_target(GameRenderer *renderer, uint8 *buffer,
                            int stride, int width, int height);

// Camera
void game_render_set_camera(GameRenderer *renderer,
                            const GameRenderCamera *camera);

// Projection
void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj);

// Unified texture loading
// tex_idx: use TEXTURE_BANK_* constants.
// gfx_size determines layout: 0=64x64, 1=32x32.
// Returned handles are GameRenderer-level texture-bank tokens. The renderer
// translates them to the active backend's internal slot at draw time.
TextureHandle game_render_load_texture(GameRenderer *renderer,
                                       uint8 *pixelData,
                                       int width, int height,
                                       int tex_idx, int gfx_size);
void game_render_free_texture(GameRenderer *renderer,
                              TextureHandle handle);

// Look up the GameRenderer-level handle registered for a texture bank index.
TextureHandle game_render_get_texture_handle(GameRenderer *renderer,
                                             int tex_idx);

// Asset loading — sprite blocks (HUD)
TextureHandle game_render_load_blocks(GameRenderer *renderer, int slot,
                                      tBlockHeader *blocks,
                                      const tColor *palette);
void game_render_free_blocks(GameRenderer *renderer, int slot);

// Draw — polygon (track, buildings, particles, clouds)
// Pass TEXTURE_HANDLE_INVALID for flat (untextured) polygons.
void game_render_quad_screen(GameRenderer *renderer,
                      tPolyParams *poly,
                      TextureHandle handle,
                      const uint8 *palette_remap);

// Always routes through the SW rasterizer into scrbuf (via screen_pointer),
// regardless of render mode -- for HUD-style screen overlays that don't need
// GPU depth-testing/occlusion (unlike game_render_quad_screen's particle
// path) and should composite via the shared HUD overlay buffer the same way
// car-name-tag text already does. Correctly handles split-screen modes with
// no extra work, since it just writes pixels wherever screen_pointer already
// points (set per-view by draw_road()) instead of needing to be captured
// inside an isolated per-view GPU render pass.
void game_render_quad_screen_hud(GameRenderer *renderer,
                                 tPolyParams *poly,
                                 TextureHandle handle,
                                 const uint8 *palette_remap);

// Direct access to the underlying GPU sub-renderer for GPU-only callers that
// need scene_render_gpu.h functions not otherwise wrapped here (e.g. name-tag
// billboards in game_render_hardware.c, which need scene_render_gpu_upload_rgba/
// screen_quad_textured/set_particle_ndcz directly). Returns NULL in SW mode or
// if the GPU device is unavailable.
SceneRendererGPU *game_render_get_gpu(GameRenderer *renderer);

// Direct access to the underlying GPU device, for GPU-only callers that need
// to create/release their own textures directly (e.g. name-tag label texture
// caching in game_render_hardware.c). Returns NULL in SW mode.
SDL_GPUDevice *game_render_get_device(GameRenderer *renderer);

// Set the clip-space depth (NDC Z in [0,1]) for the next game_render_quad_screen
// call so GPU particles are depth-tested against scene geometry.
// Must be called before each particle quad.  Ignored in SW mode.
void game_render_set_particle_depth(GameRenderer *renderer, float ndcZ);
// Per-vertex variant: ndcZ[4] maps to quad vertices v0..v3.  Consumed after one call.
// Use for elongated trails (type 1) where head and tail are at different depths.
void game_render_set_particle_depth_pervertex(GameRenderer *renderer, const float ndcZ[4]);

// Draw — world-space quad (GPU-ready interface)
// verts must point to exactly 4 GameRenderVertex entries.
// surfaceFlags carries the same bitfield as tPolyParams.iSurfaceType.
// subThreshold is the legacy `subdivides[i] * subscale` depth threshold;
// when >0 and the polygon's nearest projected Z meets/exceeds it, the
// renderer skips subdivide and rasterizes via POLYTEX/POLYFLAT directly
// (matches the legacy subdivide-vs-game_render_quad_screen branch). Pass 0.0f
// to always subdivide.
void game_render_quad_world(GameRenderer *renderer,
                            const GameRenderVertex *verts,
                            TextureHandle handle,
                            int surfaceFlags,
                            float subThreshold);
void game_render_quad_world_subdivide_type(GameRenderer *renderer,
                                           const GameRenderVertex *verts,
                                           TextureHandle handle,
                                           int surfaceFlags,
                                           int subdivideType,
                                           float subThreshold);

// Draw — car mesh
void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          const GameRenderCarPose *pose,
                          const GameRenderCarOptions *options);

// Draw — sky/horizon background
void game_render_draw_sky(GameRenderer *renderer,
                          const GameRenderCamera *camera,
                          const GameRenderProjection *projection);

// Draw — HUD sprites
void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette);

// Draw — HUD print_block (scaled sprite blit to dest pointer)
void game_render_print_block(GameRenderer *renderer, int slot, int blockIdx,
                             uint8 *pDest);

// Palette
void game_render_set_palette(GameRenderer *renderer, const tColor *palette);

// Fade
void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames);
int game_render_fade_active(GameRenderer *renderer);

#endif

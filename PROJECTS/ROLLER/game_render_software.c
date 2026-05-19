#include "game_render_software.h"
#include "3d.h"
#include "drawtrk3.h"
#include "transfrm.h"
#include "graphics.h"
#include "func2.h"
#include "func3.h"
#include "horizon.h"
#include "car.h"
#include "polytex.h"
#include "roller.h"
#include "sound.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GAME_RENDER_MAX_BLOCK_SLOTS 32
#define GAME_RENDER_MAX_TEXTURE_SLOTS 32

// Slot table entry for pixel textures
typedef struct {
    uint8 *pixels;
    int width;
    int height;
    int tex_idx;
    int gfx_size;
    int in_use;
} TextureSlot;

struct GameRendererSoftware {
    GameRenderCamera camera;
    GameRenderProjection proj;
    int screenWidth;
    int screenHeight;
    int fadeInPending;
    tBlockHeader *blocks[GAME_RENDER_MAX_BLOCK_SLOTS];
    TextureHandle blockHandles[GAME_RENDER_MAX_BLOCK_SLOTS];

    // Texture slot table
    TextureSlot texSlots[GAME_RENDER_MAX_TEXTURE_SLOTS];
    // Map tex_idx → TextureHandle (for game_render_get_texture_handle)
    TextureHandle texIdxToHandle[32];
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

static void game_render_sw_start_pending_fade_in(GameRendererSoftware *sw) {
    if (!sw->fadeInPending)
        return;

    sw->fadeInPending = 0;
    palette_brightness = 0;
    for (int i = 0; i < 256; i++) {
        pal_addr[i].byR = 0;
        pal_addr[i].byB = 0;
        pal_addr[i].byG = 0;
    }
    fade_palette_begin(32);
}

void game_render_sw_begin_frame(GameRendererSoftware *sw) {
    (void)sw;
}

void game_render_sw_end_frame(GameRendererSoftware *sw) {
    game_render_sw_start_pending_fade_in(sw);
    if (fade_palette_active())
        fade_palette_update();
    UpdateSDLWindow();
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

void game_render_sw_set_viewport(GameRendererSoftware *sw,
                                 int x, int y, int w, int h) {
    sw->screenWidth = w;
    sw->screenHeight = h;
    // Set screen_pointer to the viewport origin within scrbuf.
    // Existing rendering code writes relative to screen_pointer.
    screen_pointer = scrbuf + (y * sw->screenWidth) + x;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void game_render_sw_set_camera(GameRendererSoftware *sw,
                               const GameRenderCamera *camera) {
    extern float viewx, viewy, viewz;
    extern float fcos, fsin;
    extern int VIEWDIST;
    sw->camera = *camera;
    viewx = camera->viewX;
    viewy = camera->viewY;
    viewz = camera->viewZ;
    fcos = camera->cosYaw;
    fsin = camera->sinYaw;
    VIEWDIST = (int)camera->fovScale;
}

void game_render_sw_set_projection(GameRendererSoftware *sw,
                                   const GameRenderProjection *proj) {
    extern float vk1, vk2, vk3, vk4, vk5, vk6, vk7, vk8, vk9;
    extern int scr_size, xbase, ybase, gfx_size;
    sw->proj = *proj;
    // Write through to globals for legacy code (subdivide, POLYTEX, etc.)
    vk1 = proj->view[0][0]; vk2 = proj->view[0][1]; vk3 = proj->view[0][2];
    vk4 = proj->view[1][0]; vk5 = proj->view[1][1]; vk6 = proj->view[1][2];
    vk7 = proj->view[2][0]; vk8 = proj->view[2][1]; vk9 = proj->view[2][2];
    scr_size = proj->screenScale;
    xbase = proj->centerX;
    ybase = proj->centerY;
    gfx_size = proj->texHalfRes;
}

// ---------------------------------------------------------------------------
// Asset loading
// ---------------------------------------------------------------------------

TextureHandle game_render_sw_load_texture(GameRendererSoftware *sw,
                                          uint8 *pixelData,
                                          int width, int height,
                                          int tex_idx, int gfx_size) {
    // Free any existing handle for this tex_idx first
    if (tex_idx >= 0 && tex_idx < 32) {
        TextureHandle old = sw->texIdxToHandle[tex_idx];
        if (old != TEXTURE_HANDLE_INVALID)
            game_render_sw_free_texture(sw, old);
    }

    // Find a free slot (skip 0 — reserved for TEXTURE_HANDLE_INVALID)
    for (int i = 1; i < GAME_RENDER_MAX_TEXTURE_SLOTS; i++) {
        if (!sw->texSlots[i].in_use) {
            sw->texSlots[i].pixels   = pixelData;
            sw->texSlots[i].width    = width;
            sw->texSlots[i].height   = height;
            sw->texSlots[i].tex_idx  = tex_idx;
            sw->texSlots[i].gfx_size = gfx_size;
            sw->texSlots[i].in_use   = 1;

            // Register the handle for this tex_idx
            if (tex_idx >= 0 && tex_idx < 32)
                sw->texIdxToHandle[tex_idx] = i;

            return (TextureHandle)i;
        }
    }
    return TEXTURE_HANDLE_INVALID;
}

void game_render_sw_free_texture(GameRendererSoftware *sw,
                                 TextureHandle handle) {
    if (handle <= 0 || handle >= GAME_RENDER_MAX_TEXTURE_SLOTS)
        return;
    int tex_idx = sw->texSlots[handle].tex_idx;
    if (tex_idx >= 0 && tex_idx < 32
        && sw->texIdxToHandle[tex_idx] == handle)
        sw->texIdxToHandle[tex_idx] = TEXTURE_HANDLE_INVALID;
    memset(&sw->texSlots[handle], 0, sizeof(TextureSlot));
}

TextureHandle game_render_sw_get_texture_handle(GameRendererSoftware *sw,
                                                 int tex_idx) {
    if (tex_idx >= 0 && tex_idx < 32)
        return sw->texIdxToHandle[tex_idx];
    return TEXTURE_HANDLE_INVALID;
}

TextureHandle game_render_sw_load_blocks(GameRendererSoftware *sw, int slot,
                                         tBlockHeader *blocks,
                                         const tColor *palette) {
    (void)palette;
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS) {
        sw->blocks[slot] = blocks;
        sw->blockHandles[slot] = (TextureHandle)(slot + 1);
        return sw->blockHandles[slot];
    }
    return TEXTURE_HANDLE_INVALID;
}

void game_render_sw_free_blocks(GameRendererSoftware *sw, int slot) {
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS) {
        sw->blocks[slot] = NULL;
        sw->blockHandles[slot] = TEXTURE_HANDLE_INVALID;
    }
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

void game_render_sw_quad_screen(GameRendererSoftware *sw, tPolyParams *poly,
                         TextureHandle handle,
                         const uint8 *palette_remap) {
    (void)palette_remap;
    if (handle > 0 && handle < GAME_RENDER_MAX_TEXTURE_SLOTS
        && sw->texSlots[handle].in_use) {
        TextureSlot *slot = &sw->texSlots[handle];
        POLYTEX(slot->pixels, screen_pointer, poly,
                slot->tex_idx, slot->gfx_size);
    } else {
        POLYFLAT(screen_pointer, poly);
    }
}

void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             const GameRenderCarPose *pose,
                             const GameRenderCarOptions *options) {
    if (!sw || !pose)
        return;
    // Compute distance from camera to car for LOD using the explicit pose.
    const GameRenderCamera *cam = &sw->camera;
    float dx = pose->position.fX - cam->viewX;
    float dy = pose->position.fY - cam->viewY;
    float dz = pose->position.fZ - cam->viewZ;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    CarRenderPose car_pose = {
        .position = pose->position,
        .yaw = pose->yaw,
        .pitch = pose->pitch,
        .roll = pose->roll,
    };
    CarRenderOptions car_options = {
        .anim_frame = options ? options->anim_frame : 0,
        .color_remap = options ? options->color_remap : NULL,
    };
    DisplayCarWithPose(carIdx, screen_pointer, dist, &car_pose, &car_options);
}

void game_render_sw_draw_sky(GameRendererSoftware *sw,
                             const GameRenderCamera *camera,
                             const GameRenderProjection *projection) {
    if (!sw || !camera || !projection)
        return;
    game_render_sw_set_camera(sw, camera);
    game_render_sw_set_projection(sw, projection);
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

void game_render_sw_print_block(GameRendererSoftware *sw, int slot,
                                int blockIdx, uint8 *pDest) {
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS && sw->blocks[slot])
        print_block(pDest, sw->blocks[slot], blockIdx);
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
        fade_palette_begin(0);
    }
}

int game_render_sw_fade_active(GameRendererSoftware *sw) {
    return sw->fadeInPending || fade_palette_active();
}

void game_render_sw_fade_wait(GameRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx) {
    while (game_render_sw_fade_active(sw)) {
        if (redraw_fn)
            redraw_fn(ctx);
        game_render_sw_start_pending_fade_in(sw);
        if (fade_palette_active())
            fade_palette_update();
        UpdateSDLWindow();
        if (game_render_sw_fade_active(sw))
            SDL_Delay(1);
    }
}

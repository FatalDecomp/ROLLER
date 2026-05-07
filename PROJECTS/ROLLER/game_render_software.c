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

/* SubpolyType values passed to dodivide for texture routing.
 * -1 = wide wall (2048x1024), 0 = standard track (1024x1024),
 * 666 = building (other_texture[] lookup). */
#define SUBPOLY_WALL     (-1)
#define SUBPOLY_STANDARD   0
#define SUBPOLY_BUILDING 666

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

void game_render_sw_quad_world(GameRendererSoftware *sw,
                               const GameRenderVertex *verts,
                               TextureHandle handle,
                               int surfaceFlags) {
    const GameRenderCamera *cam = &sw->camera;
    const GameRenderProjection *proj = &sw->proj;
    float vx[4], vy[4], vz[4];
    int clippedCount = 0;

    // World → view space transform using view matrix
    for (int i = 0; i < 4; i++) {
        float dx = verts[i].x - cam->viewX;
        float dy = verts[i].y - cam->viewY;
        float dz = verts[i].z - cam->viewZ;

        vx[i] = dx * proj->view[0][0] + dy * proj->view[1][0] + dz * proj->view[2][0];
        vy[i] = dx * proj->view[0][1] + dy * proj->view[1][1] + dz * proj->view[2][1];
        vz[i] = dx * proj->view[0][2] + dy * proj->view[1][2] + dz * proj->view[2][2];
    }

    // Populate tPolyParams with screen-space vertices
    tPolyParams poly;
    poly.iSurfaceType = surfaceFlags;
    poly.uiNumVerts = 4;

    for (int i = 0; i < 4; i++) {
        float z = vz[i];
        if (z < 80.0f) {
            z = 80.0f;
            clippedCount++;
        }

        int sx = (int)(vx[i] * (int)cam->fovScale / z + proj->centerX);
        int sy = (int)(vy[i] * (int)cam->fovScale / z + proj->centerY);

        poly.vertices[i].x = (proj->screenScale * sx) >> 6;
        poly.vertices[i].y = (proj->screenScale * (199 - sy)) >> 6;
    }

    /* Skip fully-clipped polygons */
    if (clippedCount >= 4)
        return;

    /* Flat (untextured) polygons: rasterize directly */
    if (handle == TEXTURE_HANDLE_INVALID) {
        POLYFLAT(screen_pointer, &poly);
        return;
    }

    /* Routing: building and wall textures need subdivide for their
     * special layouts (other_texture[] lookup and 2048x1024 wide).
     * All other textures (car, cargen, standard track) use POLYTEX
     * directly — same as the screen-space path. */
    if (handle > 0 && handle < GAME_RENDER_MAX_TEXTURE_SLOTS
        && sw->texSlots[handle].in_use) {
        int tex_idx = sw->texSlots[handle].tex_idx;
        if (tex_idx == TEXTURE_BANK_BUILDING) {
            subdivide(screen_pointer, &poly,
                      vx[0], vy[0], vz[0],
                      vx[1], vy[1], vz[1],
                      vx[2], vy[2], vz[2],
                      vx[3], vy[3], vz[3],
                      SUBPOLY_BUILDING, proj->texHalfRes);
            return;
        }
        if ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE)
            && tex_idx == TEXTURE_BANK_TRACK) {
            subdivide(screen_pointer, &poly,
                      vx[0], vy[0], vz[0],
                      vx[1], vy[1], vz[1],
                      vx[2], vy[2], vz[2],
                      vx[3], vy[3], vz[3],
                      SUBPOLY_WALL, proj->texHalfRes);
            return;
        }
        /* Car, cargen, standard track, and all other textures. */
        POLYTEX(sw->texSlots[handle].pixels, screen_pointer,
                &poly, tex_idx, sw->texSlots[handle].gfx_size);
        return;
    }
    /* No valid handle with pixel data: treated as flat. */
    POLYFLAT(screen_pointer, &poly);
}

void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             int yaw, int pitch, int roll,
                             float worldX, float worldY, float worldZ,
                             int animFrame, const uint8 *color_remap) {
    (void)yaw; (void)pitch; (void)roll;
    (void)animFrame; (void)color_remap;
    // Compute distance from camera to car for LOD.
    // DisplayCar reads car state from global Car[] array.
    const GameRenderCamera *cam = &sw->camera;
    float dx = worldX - cam->viewX;
    float dy = worldY - cam->viewY;
    float dz = worldZ - cam->viewZ;
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

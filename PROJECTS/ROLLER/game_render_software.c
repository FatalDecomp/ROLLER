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

void game_render_sw_subdivide_view_quad(uint8 *pDest, tPolyParams *poly,
                                        const float viewCoords[4][3],
                                        int subdivideType, int texHalfRes) {
    subdivide(pDest, poly,
              viewCoords[0][0], viewCoords[0][1], viewCoords[0][2],
              viewCoords[1][0], viewCoords[1][1], viewCoords[1][2],
              viewCoords[2][0], viewCoords[2][1], viewCoords[2][2],
              viewCoords[3][0], viewCoords[3][1], viewCoords[3][2],
              subdivideType, texHalfRes);
}

void game_render_sw_quad_world(GameRendererSoftware *sw,
                               const GameRenderVertex *verts,
                               TextureHandle handle,
                               int surfaceFlags,
                               float subThreshold) {
    game_render_sw_quad_world_subdivide_type(sw, verts, handle, surfaceFlags,
                                             GAME_RENDER_SUBDIVIDE_TYPE_AUTO,
                                             subThreshold);
}

void game_render_sw_quad_world_subdivide_type(GameRendererSoftware *sw,
                                              const GameRenderVertex *verts,
                                              TextureHandle handle,
                                              int surfaceFlags,
                                              int subdivideType,
                                              float subThreshold) {
    const GameRenderCamera *cam = &sw->camera;
    const GameRenderProjection *proj = &sw->proj;

    /* Routing: building textures use other_texture[] lookup,
     * wall polygons (and wide-textured roads/lanes) use wide 2048x1024
     * layout, everything else uses standard 1024x1024. The subpoly type
     * also selects the projection precision recipe — buildings use the
     * legacy floor/int-math recipe, tracks/walls/standard use the legacy
     * drawtrk3 double+round recipe. Wide-texture roads only land in the
     * wall path when wide_on is enabled (mirrors legacy case 5 dispatch). */
    int subpolyType;
    if (subdivideType != GAME_RENDER_SUBDIVIDE_TYPE_AUTO) {
        subpolyType = subdivideType;
    } else if (handle > 0 && handle < GAME_RENDER_MAX_TEXTURE_SLOTS
        && sw->texSlots[handle].in_use
        && sw->texSlots[handle].tex_idx == TEXTURE_BANK_BUILDING) {
        subpolyType = SUBPOLY_BUILDING;
    } else if ((surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) && wide_on) {
        subpolyType = SUBPOLY_WALL;
    } else {
        subpolyType = SUBPOLY_STANDARD;
    }

    tPolyParams poly;
    poly.iSurfaceType = surfaceFlags;
    poly.uiNumVerts = 4;

    float subVx[4], subVy[4], subVz[4];

    if (subpolyType == SUBPOLY_BUILDING) {
        /* Building recipe: floor(world − view) in double, integer-truncate
         * after matrix product, integer-math projection, skip-all-clipped. */
        int iVx[4], iVy[4], iVz[4];
        int clippedCount = 0;
        for (int i = 0; i < 4; i++) {
            double dx = floor((double)verts[i].x - cam->viewX);
            double dy = floor((double)verts[i].y - cam->viewY);
            double dz = floor((double)verts[i].z - cam->viewZ);
            iVx[i] = (int)(dx * proj->view[0][0] + dy * proj->view[1][0] + dz * proj->view[2][0]);
            iVy[i] = (int)(dx * proj->view[0][1] + dy * proj->view[1][1] + dz * proj->view[2][1]);
            iVz[i] = (int)(dx * proj->view[0][2] + dy * proj->view[1][2] + dz * proj->view[2][2]);
        }
        int viewDist = (int)cam->fovScale;
        for (int i = 0; i < 4; i++) {
            if (iVz[i] < 80) {
                iVz[i] = 80;
                clippedCount++;
            }
            int xp = iVx[i] * viewDist / iVz[i] + proj->centerX;
            int yp = iVy[i] * viewDist / iVz[i] + proj->centerY;
            poly.vertices[i].x = (proj->screenScale * xp) >> 6;
            poly.vertices[i].y = (proj->screenScale * (199 - yp)) >> 6;
            subVx[i] = (float)iVx[i];
            subVy[i] = (float)iVy[i];
            subVz[i] = (float)iVz[i];
        }
        if (clippedCount >= 4)
            return;
    } else {
        /* Track/wall recipe (legacy drawtrk3): world − view in double (no floor),
         * matrix product cast to float for X/Y, double Z preserved for clip
         * and perspective division (rounded int separately for subdivide Z),
         * round-to-int after double projection, no skip-all-clipped.
         *
         * Car polygons use explicit subpoly types 3+ for car texture routing.
         * DisplayCar's legacy projection differs subtly from drawtrk3: it
         * truncates projected X/Y with d2i() and passes raw view-Z into
         * subdivide(). Preserve that recipe so routing car meshes through the
         * world API does not move their screen-space silhouette. */
        int useCarProjection = subpolyType >= 3;
        double viewDist = (double)cam->fovScale;
        for (int i = 0; i < 4; i++) {
            /* Float subtraction first (matches legacy: tVec3.fX - viewx),
             * then promote to double for matrix product. */
            double dx = (double)(verts[i].x - cam->viewX);
            double dy = (double)(verts[i].y - cam->viewY);
            double dz = (double)(verts[i].z - cam->viewZ);
            float fVx = (float)(dx * proj->view[0][0] + dy * proj->view[1][0] + dz * proj->view[2][0]);
            float fVy = (float)(dx * proj->view[0][1] + dy * proj->view[1][1] + dz * proj->view[2][1]);
            double dCameraZ = dx * proj->view[0][2] + dy * proj->view[1][2] + dz * proj->view[2][2];
            float fVz = (float)dCameraZ;
            float fProjectedZ = fVz;
            if (fProjectedZ < 80.0f) fProjectedZ = 80.0f;
            double dInvZ = 1.0 / (double)fProjectedZ;
            int xp;
            int yp;
            if (useCarProjection) {
                xp = (int)(viewDist * (double)fVx * dInvZ + (double)proj->centerX);
                yp = (int)(dInvZ * (viewDist * (double)fVy) + (double)proj->centerY);
            } else {
                xp = (int)round(viewDist * (double)fVx * dInvZ + (double)proj->centerX);
                yp = (int)round(dInvZ * (viewDist * (double)fVy) + (double)proj->centerY);
            }
            poly.vertices[i].x = (xp * proj->screenScale) >> 6;
            poly.vertices[i].y = (proj->screenScale * (199 - yp)) >> 6;
            subVx[i] = fVx;
            subVy[i] = fVy;
            subVz[i] = useCarProjection ? fVz : (float)((int)round(dCameraZ));
        }
    }

    /* Wide-texture polygons need the 2x UV anchor (legacy set_starts(1u)
     * before each wide poly, set_starts(0) after). */
    if (subpolyType == SUBPOLY_WALL) set_starts(1u);

    /* Subdivide-vs-direct dispatch: when caller provided a threshold and
     * the polygon's nearest projected Z exceeds it, render directly via
     * POLYTEX/POLYFLAT — mirrors the legacy
     * `if (subdivides[i] * subscale > min_z) subdivide else game_render_quad_screen`
     * branch in drawtrk3.c. */
    int useDirect = 0;
    if (subThreshold > 0.0f) {
        float minZ = subVz[0];
        if (subVz[1] < minZ) minZ = subVz[1];
        if (subVz[2] < minZ) minZ = subVz[2];
        if (subVz[3] < minZ) minZ = subVz[3];
        if (subThreshold <= minZ)
            useDirect = 1;
    }

    if (useDirect && handle == TEXTURE_HANDLE_INVALID) {
        POLYFLAT(screen_pointer, &poly);
    } else if (useDirect) {
        TextureSlot *slot = &sw->texSlots[handle];
        POLYTEX(slot->pixels, screen_pointer, &poly,
                slot->tex_idx, slot->gfx_size);
    } else {
        subdivide(screen_pointer, &poly,
                  subVx[0], subVy[0], subVz[0],
                  subVx[1], subVy[1], subVz[1],
                  subVx[2], subVy[2], subVz[2],
                  subVx[3], subVy[3], subVz[3],
                  subpolyType, proj->texHalfRes);
    }

    if (subpolyType == SUBPOLY_WALL) set_starts(0);
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

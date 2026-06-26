#include "game_render.h"
#include "game_render_software.h"
#include "game_render_hardware.h"
#include "scene_render_gpu.h"
#include "3d.h"
#include "func2.h"
#include "horizon.h"
#include "drawtrk3.h"
#include "sound.h"
#include "car.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

struct GameRenderer {
    GameRenderMode mode;
    GameRenderMode pendingMode;  /* applied at next begin_frame boundary */
    bool           pendingModeSet;
    GameRendererSoftware *sw;
    SceneRenderer *scene;
    SceneRendererGPU *gpu; /* cached from scene at creation; NULL if GPU unavailable */
    SDL_GPUDevice *device;
    SDL_Window *window;
    GameRendererHardware *hw;
    int hudW, hudH;
    bool mirrorPass;  /* true during mirror buffer render — routes scene to SW */
    bool splitScreen; /* GPU mode only: SW quads also run, HUD pass covers left half */
};

static TextureHandle game_render_texture_handle_from_index(int tex_idx) {
    if (tex_idx < 0 || tex_idx >= 32)
        return TEXTURE_HANDLE_INVALID;
    return tex_idx + 1;
}

static int game_render_texture_index_from_handle(TextureHandle handle) {
    if (handle <= TEXTURE_HANDLE_INVALID || handle > 32)
        return -1;
    return handle - 1;
}

GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window) {
    GameRenderer *r = calloc(1, sizeof(GameRenderer));
    if (!r)
        return NULL;
    r->device = device;
    r->window = window;
    r->sw = game_render_sw_create(device, window);
    r->scene = scene_render_create(device, window);
    r->gpu   = scene_render_get_gpu(r->scene);
    r->mode = GAME_RENDER_SOFTWARE;
    r->hw = game_render_hw_create(device);
    return r;
}

void game_render_destroy(GameRenderer *renderer) {
    if (!renderer)
        return;
    game_render_hw_destroy(renderer->hw);
    scene_render_destroy(renderer->scene);
    game_render_sw_destroy(renderer->sw);
    free(renderer);
}

void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode) {
    if (!renderer)
        return;
    /* Defer the actual switch to the next begin_frame so it never fires
     * mid-frame while a GPU command buffer is open. */
    renderer->pendingMode    = mode;
    renderer->pendingModeSet = true;
    /* Split screen is GPU-only; turn it off when leaving GPU mode. */
    if (mode != GAME_RENDER_GPU && renderer->splitScreen)
        game_render_set_split_screen(renderer, false);
}

void game_render_set_debug_overlay(GameRenderer *renderer, DebugOverlay *overlay) {
    if (!renderer)
        return;
    scene_render_set_debug_overlay(renderer->scene, overlay);
}

GameRenderMode game_render_get_mode(GameRenderer *renderer) {
    return renderer->mode;
}

void game_render_set_split_screen(GameRenderer *renderer, bool split) {
    if (!renderer) return;
    renderer->splitScreen = split;
    scene_render_set_split_screen(renderer->scene, split);
}

bool game_render_is_split_screen(GameRenderer *renderer) {
    return renderer && renderer->splitScreen;
}

// Frame lifecycle

void game_render_begin_frame(GameRenderer *renderer) {
    if (renderer->pendingModeSet) {
        /* When switching GPU→SW, cancel any open command buffer before the
         * GPU path is abandoned.  This guards against the edge case where a
         * begin_frame acquired a cmdBuf but end_frame was never reached. */
        if (renderer->pendingMode == GAME_RENDER_SOFTWARE) {
            scene_render_gpu_cancel_frame(renderer->gpu);
            /* GPU mode never updates palette_brightness (it renders its own
             * brightness independently), so it stays 0 from setpal().  Force
             * it to full brightness now so the first SW frame is visible.
             * Skip this if a palette fade is already in progress (e.g. the
             * end-of-race fade-out), so we don't stomp a running animation. */
            if (!fade_palette_active())
                palette_brightness = 32;
        }
        renderer->mode           = renderer->pendingMode;
        renderer->pendingModeSet = false;
        scene_render_set_use_gpu(renderer->scene, renderer->mode == GAME_RENDER_GPU);
    }
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_frame(renderer->sw);
    else if (renderer->mode == GAME_RENDER_GPU) {
        int hudW = renderer->hudW > 0 ? renderer->hudW : 640;
        int hudH = renderer->hudH > 0 ? renderer->hudH : 400;
        if (scrbuf)
            memset(scrbuf, 0, (size_t)hudW * hudH);
        scene_render_gpu_begin_frame(renderer->gpu);
    }
}

void game_render_end_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE) {
        game_render_sw_end_frame(renderer->sw);
    } else if (renderer->mode == GAME_RENDER_GPU) {
        if (fade_palette_active())
            fade_palette_update();
        game_render_hw_draw_fps_overlay();
        scene_render_gpu_set_hud_buffer(renderer->gpu,
                                        scrbuf,
                                        renderer->hudW > 0 ? renderer->hudW : 640,
                                        renderer->hudH > 0 ? renderer->hudH : 400);
        scene_render_gpu_end_frame(renderer->gpu);
    }
}

void game_render_begin_mirror_pass(GameRenderer *renderer) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU) return;
    renderer->mirrorPass = true;
    scene_render_set_use_gpu(renderer->scene, false);
}

void game_render_end_mirror_pass(GameRenderer *renderer) {
    if (!renderer) return;
    renderer->mirrorPass = false;
    if (renderer->mode == GAME_RENDER_GPU) {
        scene_render_set_use_gpu(renderer->scene, true);
        if (renderer->gpu) scene_render_gpu_discard_queued(renderer->gpu);
    }
}

// Viewport

void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h) {
    if (!renderer)
        return;
    renderer->hudW = w;
    renderer->hudH = h;
    game_render_sw_set_viewport(renderer->sw, x, y, w, h);
    scene_render_set_viewport(renderer->scene, x, y, w, h);
}

void game_render_set_target(GameRenderer *renderer, uint8 *pixBuf,
                            int stride, int width, int height) {
    if (!renderer)
        return;
    scene_render_set_target(renderer->scene, pixBuf, stride, width, height);
}

// Camera

void game_render_set_camera(GameRenderer *renderer,
                            const GameRenderCamera *camera) {
    if (!renderer || !camera)
        return;
    /* Always update SW globals — legacy code (car name labels, clouds) reads
     * viewx/viewy/viewz, vk1-vk9, etc. regardless of render mode. */
    game_render_sw_set_camera(renderer->sw, camera);
    scene_render_set_camera(renderer->scene, camera);
}

// Projection

void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj) {
    if (!renderer || !proj)
        return;
    /* Always update SW globals — legacy code reads vk1-vk9, xbase/ybase,
     * scr_size etc. regardless of render mode. */
    game_render_sw_set_projection(renderer->sw, proj);
    scene_render_set_projection(renderer->scene, proj);
}

// Asset loading

TextureHandle game_render_load_texture(GameRenderer *renderer,
                                       uint8 *pixelData,
                                       int width, int height,
                                       int tex_idx, int gfx_size) {
    if (!renderer)
        return TEXTURE_HANDLE_INVALID;
    TextureHandle swHandle = game_render_sw_load_texture(renderer->sw, pixelData,
                                                         width, height, tex_idx, gfx_size);
    TextureHandle sceneHandle = scene_render_load_texture(renderer->scene, pixelData,
                                                          width, height, tex_idx, gfx_size);
    if (sceneHandle == TEXTURE_HANDLE_INVALID && swHandle == TEXTURE_HANDLE_INVALID)
        return TEXTURE_HANDLE_INVALID;
    return game_render_texture_handle_from_index(tex_idx);
}

void game_render_free_texture(GameRenderer *renderer,
                              TextureHandle handle) {
    if (!renderer)
        return;
    int tex_idx = game_render_texture_index_from_handle(handle);
    if (tex_idx < 0)
        return;
    scene_render_free_texture(renderer->scene,
                              scene_render_get_texture_handle(renderer->scene, tex_idx));
    game_render_sw_free_texture(renderer->sw,
                                game_render_sw_get_texture_handle(renderer->sw, tex_idx));
}

TextureHandle game_render_get_texture_handle(GameRenderer *renderer,
                                             int tex_idx) {
    if (!renderer)
        return TEXTURE_HANDLE_INVALID;
    if (scene_render_get_texture_handle(renderer->scene, tex_idx) != TEXTURE_HANDLE_INVALID ||
        game_render_sw_get_texture_handle(renderer->sw, tex_idx) != TEXTURE_HANDLE_INVALID)
        return game_render_texture_handle_from_index(tex_idx);
    return TEXTURE_HANDLE_INVALID;
}

TextureHandle game_render_load_blocks(GameRenderer *renderer, int slot,
                                      tBlockHeader *blocks,
                                      const tColor *pal) {
    if (!renderer)
        return TEXTURE_HANDLE_INVALID;
    return game_render_sw_load_blocks(renderer->sw, slot, blocks, pal);
}

void game_render_free_blocks(GameRenderer *renderer, int slot) {
    if (!renderer)
        return;
    game_render_sw_free_blocks(renderer->sw, slot);
}

// Draw calls

void game_render_quad_screen(GameRenderer *renderer, tPolyParams *poly,
                      TextureHandle handle,
                      const uint8 *palette_remap) {
    if (!renderer)
        return;

    /* GPU mode + flat colour: route to the dedicated particle pipeline so particles
     * are drawn at full render resolution instead of the scrbuf SW overlay. */
    if (renderer->mode == GAME_RENDER_GPU && renderer->gpu
        && !renderer->mirrorPass && handle == TEXTURE_HANDLE_INVALID) {
        int colorIdx = poly->iSurfaceType & 0xFF;
        if (palette_remap)
            colorIdx = palette_remap[colorIdx] & 0xFF;
        const tColor *c = &palette[colorIdx];
        float cr = (float)c->byR / 63.0f;
        float cg = (float)c->byG / 63.0f;
        float cb = (float)c->byB / 63.0f;
        float ca = (poly->iSurfaceType & SURFACE_FLAG_TRANSPARENT) ? 0.5f : 1.0f;
        float ww = (float)winw, wh = (float)winh;
        float ndcX[4], ndcY[4];
        for (int i = 0; i < 4; i++) {
            ndcX[i] = (float)poly->vertices[i].x / ww * 2.0f - 1.0f;
            ndcY[i] = 1.0f - (float)poly->vertices[i].y / wh * 2.0f;
        }
        scene_render_gpu_screen_quad_flat(renderer->gpu, ndcX, ndcY, cr, cg, cb, ca);
        return;
    }

    /* SW path: scrbuf overlay (also used for textured screen quads in GPU mode). */
    int tex_idx = game_render_texture_index_from_handle(handle);
    TextureHandle swHandle = tex_idx >= 0
        ? game_render_sw_get_texture_handle(renderer->sw, tex_idx)
        : TEXTURE_HANDLE_INVALID;
    game_render_sw_quad_screen(renderer->sw, poly, swHandle, palette_remap);
}

void game_render_quad_world(GameRenderer *renderer,
                            const GameRenderVertex *verts,
                            TextureHandle handle,
                            int surfaceFlags,
                            float subThreshold) {
    game_render_quad_world_subdivide_type(renderer, verts, handle, surfaceFlags,
                                          GAME_RENDER_SUBDIVIDE_TYPE_AUTO,
                                          subThreshold);
}

void game_render_quad_world_subdivide_type(GameRenderer *renderer,
                                           const GameRenderVertex *verts,
                                           TextureHandle handle,
                                           int surfaceFlags,
                                           int subdivideType,
                                           float subThreshold) {
    if (!renderer)
        return;
    SceneRenderLegacyQuadOptions options = {
        .subdivideType = subdivideType,
        .subThreshold = subThreshold,
    };
    int tex_idx = game_render_texture_index_from_handle(handle);
    SceneTextureHandle sceneHandle = tex_idx >= 0
        ? scene_render_get_texture_handle(renderer->scene, tex_idx)
        : SCENE_TEXTURE_HANDLE_INVALID;
    scene_render_quad_world_legacy(renderer->scene, verts, sceneHandle,
                                   surfaceFlags, options);
}

void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          const GameRenderCarPose *pose,
                          const GameRenderCarOptions *options) {
    if (!renderer || !pose)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE || renderer->mirrorPass)
        game_render_sw_draw_car(renderer->sw, carIdx, pose, options);
    else if (renderer->mode == GAME_RENDER_GPU) {
        if (renderer->splitScreen)
            game_render_sw_draw_car(renderer->sw, carIdx, pose, options);
        if (renderer->hw && renderer->gpu) {
            game_render_hw_draw_car(renderer->hw, renderer->gpu, carIdx, pose, options);
            game_render_hw_draw_car_name_tag(carIdx, pose);
        }
        CarRenderPose sw_pose = { pose->position, pose->yaw, pose->pitch, pose->roll };
        DisplayCarSmoke(carIdx, &sw_pose);
    }
}

/* Clip the NDC screen rectangle against the sky half-plane (S-H, 1 clip plane).
 * hx/hy: horizon point in NDC. nx/ny: sky-side normal (unnormalized, NDC-space).
 * Returns number of output vertices (0-5) in out_x/out_y. */
static int clip_sky_poly_ndc(float hx, float hy, float nx, float ny,
                              float out_x[5], float out_y[5])
{
    static const float cx[4] = {-1.f, +1.f, +1.f, -1.f};
    static const float cy[4] = {-1.f, -1.f, +1.f, +1.f};
    int n = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        float d0 = nx*(cx[i]-hx) + ny*(cy[i]-hy);
        float d1 = nx*(cx[j]-hx) + ny*(cy[j]-hy);
        if (d0 >= 0.f) { out_x[n] = cx[i]; out_y[n] = cy[i]; n++; }
        if ((d0 > 0.f) != (d1 > 0.f)) {
            float t = d0 / (d0 - d1);
            out_x[n] = cx[i] + t*(cx[j]-cx[i]);
            out_y[n] = cy[i] + t*(cy[j]-cy[i]);
            n++;
        }
    }
    return n;
}

void game_render_draw_sky(GameRenderer *renderer,
                          const GameRenderCamera *camera,
                          const GameRenderProjection *projection) {
    if (!renderer || !camera || !projection)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE || renderer->mirrorPass) {
        game_render_sw_draw_sky(renderer->sw, camera, projection);
        return;
    }
    if (renderer->mode == GAME_RENDER_GPU) {
        /* Sky colour index 0x91 (145): DrawHorizon hard-codes bySkyColor = 0x91.
         * .pal files store 6-bit VGA DAC values (0-63); divide by 63 to normalise. */
        {
            const tColor *sky = &palette[0x91];
            scene_render_gpu_set_sky_color(renderer->gpu,
                sky->byR / 63.0f,
                sky->byG / 63.0f,
                sky->byB / 63.0f);
        }

        /* Set up SW view globals (viewx/viewy/viewz, vk1-vk9, xbase/ybase, etc.)
         * Always done here — both for clouds and so that ybase/winh are current
         * for the horizon fraction computation below.
         * displayclouds() does NOT write to any screen buffer, so NULL is safe. */
        game_render_sw_set_camera(renderer->sw, camera);
        game_render_sw_set_projection(renderer->sw, projection);

        /* Compute horizon fraction (sky height as a proportion of the viewport)
         * using the same formula as DrawHorizon() in horizon.c.
         * ybase and winh are now current from set_projection above. */
        {
            int   elevMasked  = worldelev & 0x3FFF;
            float fSinElev    = tsin[elevMasked];
            int   groundColor = -1;
            float skyPolyX[5], skyPolyY[5]; int skyPolyN = 0; bool skyAnyGround = false;

            if ((textures_off & TEX_OFF_HORIZON) == 0) {
                if (fSinElev < -0.7f) {
                    /* All ground — clear with ground, no sky quad needed. */
                    groundColor  = (int)(uint8)HorizonColour[front_sec];
                    skyAnyGround = true;
                } else if (fSinElev <= 0.7f) {
                    int   tiltMasked   = worldtilt & 0x3FFF;
                    float fCosElev     = tcos[elevMasked];
                    float fCosTiltVal  = tcos[tiltMasked];
                    float fNegSinTilt  = -tsin[tiltMasked];
                    bool  groundOnTop  = (fCosElev < 0.0f);
                    double dViewDistTan = (double)VIEWDIST * ptan[elevMasked];

                    /* Horizon point in screen pixels (Y-down, origin top-left). */
                    float ww = (float)winw, wh = (float)winh;
                    float hx_pix = (float)((dViewDistTan * fNegSinTilt + xbase) * scr_size / 64);
                    float hy_pix = (float)(((199 - ybase) + dViewDistTan * fCosTiltVal) * scr_size / 64);

                    /* Horizon point in NDC. */
                    float hx = (ww > 0.f) ? 2.f*hx_pix/ww - 1.f : 0.f;
                    float hy = (wh > 0.f) ? 1.f - 2.f*hy_pix/wh : 0.f;

                    /* Sky-side NDC normal derived from screen-space sky normal
                     * (fNegSinTilt, -fCosTiltVal) with aspect-ratio correction:
                     *   nx_ndc = fNegSinTilt * winw
                     *   ny_ndc = fCosTiltVal * winh
                     * Flip both when groundOnTop (inverted camera). */
                    float nx = -fNegSinTilt * ww;
                    float ny =  fCosTiltVal * wh;
                    if (groundOnTop) { nx = -nx; ny = -ny; }

                    /* Clip screen rectangle against sky half-plane. */
                    skyPolyN = clip_sky_poly_ndc(hx, hy, nx, ny, skyPolyX, skyPolyY);

                    /* anyGround: any NDC corner on the ground side. */
                    static const float ccx[4] = {-1.f,+1.f,+1.f,-1.f};
                    static const float ccy[4] = {-1.f,-1.f,+1.f,+1.f};
                    for (int i = 0; i < 4; i++) {
                        if (nx*(ccx[i]-hx) + ny*(ccy[i]-hy) < 0.f)
                            { skyAnyGround = true; break; }
                    }

                    groundColor = (int)(uint8)HorizonColour[front_sec];
                }
                /* fSinElev > 0.7: all sky — groundColor stays -1. */
            }

            /* Pack polygon into float[5][2] for the GPU call. */
            float skyPoly[5][2];
            for (int i = 0; i < skyPolyN; i++) {
                skyPoly[i][0] = skyPolyX[i];
                skyPoly[i][1] = skyPolyY[i];
            }
            scene_render_gpu_set_horizon(renderer->gpu, groundColor, skyAnyGround,
                                         (const float (*)[2])skyPoly, skyPolyN);
        }

        if ((textures_off & TEX_OFF_CLOUDS) == 0)
            displayclouds(NULL);
    }
}

void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *pal) {
    game_render_sw_sprite(renderer->sw, slot, blockIdx, x, y,
                          transparentColorIndex, pal);
}

void game_render_print_block(GameRenderer *renderer, int slot, int blockIdx,
                             uint8 *pDest) {
    game_render_sw_print_block(renderer->sw, slot, blockIdx, pDest);
}

// Palette

void game_render_set_palette(GameRenderer *renderer, const tColor *pal) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_palette(renderer->sw, pal);
}

// Fade

void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames) {
    if (renderer->mode == GAME_RENDER_SOFTWARE) {
        game_render_sw_begin_fade(renderer->sw, direction, durationFrames);
    } else {
        if (direction)
            fade_audio_restore();
        else
            fade_palette_begin(0); /* locks keyboard; fade_active stays true until complete */
    }
}

int game_render_fade_active(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        return game_render_sw_fade_active(renderer->sw);
    return fade_palette_active();
}

void game_render_set_texture_filter(GameRenderer *renderer, int filter) {
    if (renderer) scene_render_gpu_set_texture_filter(renderer->gpu, filter);
}

void game_render_set_trilinear(GameRenderer *renderer, bool enabled) {
    if (renderer) scene_render_gpu_set_trilinear(renderer->gpu, enabled);
}

void game_render_set_anisotropy_level(GameRenderer *renderer, int level) {
    if (renderer) scene_render_gpu_set_anisotropy_level(renderer->gpu, level);
}

void game_render_set_lod_bias(GameRenderer *renderer, float bias) {
    if (renderer) scene_render_gpu_set_lod_bias(renderer->gpu, bias);
}

void game_render_set_render_scale(GameRenderer *renderer, float scale) {
    if (renderer) scene_render_gpu_set_render_scale(renderer->gpu, scale);
}

void game_render_set_fog_density(GameRenderer *renderer, float density) {
    if (renderer) scene_render_gpu_set_fog_density(renderer->gpu, density);
}

void game_render_set_fog_color(GameRenderer *renderer, float fr, float fg, float fb) {
    if (renderer) scene_render_gpu_set_fog_color(renderer->gpu, fr, fg, fb);
}

void game_render_set_gamma(GameRenderer *renderer, float gamma) {
    if (renderer) scene_render_gpu_set_gamma(renderer->gpu, gamma);
}

void game_render_set_antialiasing(GameRenderer *renderer, int level) {
    if (renderer) scene_render_gpu_set_msaa(renderer->gpu, level);
}

void game_render_set_fog_start(GameRenderer *renderer, float start) {
    if (renderer) scene_render_gpu_set_fog_start(renderer->gpu, start);
}

void game_render_set_saturation(GameRenderer *renderer, float saturation) {
    if (renderer) scene_render_gpu_set_saturation(renderer->gpu, saturation);
}

void game_render_set_contrast(GameRenderer *renderer, float contrast) {
    if (renderer) scene_render_gpu_set_contrast(renderer->gpu, contrast);
}

void game_render_set_vignette(GameRenderer *renderer, float strength) {
    if (renderer) scene_render_gpu_set_vignette(renderer->gpu, strength);
}

void game_render_set_fov_multiplier(GameRenderer *renderer, float mult) {
    if (renderer) scene_render_gpu_set_fov_multiplier(renderer->gpu, mult);
}

void game_render_set_wireframe(GameRenderer *renderer, bool enabled) {
    if (renderer) scene_render_gpu_set_wireframe(renderer->gpu, enabled);
}

void game_render_set_cull_mode(GameRenderer *renderer, int mode) {
    if (renderer) scene_render_gpu_set_cull_mode(renderer->gpu, mode);
}

void game_render_set_brightness(GameRenderer *renderer, float brightness) {
    if (renderer) scene_render_gpu_set_brightness(renderer->gpu, brightness);
}

void game_render_set_vsync(GameRenderer *renderer, bool enabled) {
    if (renderer) scene_render_gpu_set_vsync(renderer->gpu, enabled);
}

void game_render_set_crt_filter(GameRenderer *renderer, CRTFilter *filter) {
    if (renderer) scene_render_gpu_set_crt_filter(renderer->gpu, filter);
}

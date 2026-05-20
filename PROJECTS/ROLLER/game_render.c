#include "game_render.h"
#include "game_render_software.h"
#include "3d.h"
#include "func2.h"
#include "roller.h"

#include <stdlib.h>

struct GameRenderer {
    GameRenderMode mode;
    GameRendererSoftware *sw;
    SceneRenderer *scene;
    SDL_GPUDevice *device;
    SDL_Window *window;
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
    r->mode = GAME_RENDER_SOFTWARE;
    return r;
}

void game_render_destroy(GameRenderer *renderer) {
    if (!renderer)
        return;
    scene_render_destroy(renderer->scene);
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
    if (!renderer)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_viewport(renderer->sw, x, y, w, h);
    scene_render_set_viewport(renderer->scene, x, y, w, h);
}

void game_render_set_target(GameRenderer *renderer, uint8 *buffer,
                            int stride, int width, int height) {
    if (!renderer)
        return;
    scene_render_set_target(renderer->scene, buffer, stride, width, height);
}

// Camera

void game_render_set_camera(GameRenderer *renderer,
                            const GameRenderCamera *camera) {
    if (!renderer || !camera)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_camera(renderer->sw, camera);
    scene_render_set_camera(renderer->scene, camera);
}

// Projection

void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj) {
    if (!renderer || !proj)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE)
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
                                      const tColor *palette) {
    if (!renderer)
        return TEXTURE_HANDLE_INVALID;
    return game_render_sw_load_blocks(renderer->sw, slot, blocks, palette);
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
    if (renderer->mode == GAME_RENDER_SOFTWARE) {
        int tex_idx = game_render_texture_index_from_handle(handle);
        TextureHandle swHandle = tex_idx >= 0
            ? game_render_sw_get_texture_handle(renderer->sw, tex_idx)
            : TEXTURE_HANDLE_INVALID;
        game_render_sw_quad_screen(renderer->sw, poly, swHandle, palette_remap);
    }
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
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_car(renderer->sw, carIdx, pose, options);
}

void game_render_draw_sky(GameRenderer *renderer,
                          const GameRenderCamera *camera,
                          const GameRenderProjection *projection) {
    if (!renderer || !camera || !projection)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_sky(renderer->sw, camera, projection);
}

void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_sprite(renderer->sw, slot, blockIdx, x, y,
                              transparentColorIndex, palette);
}

void game_render_print_block(GameRenderer *renderer, int slot, int blockIdx,
                             uint8 *pDest) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_print_block(renderer->sw, slot, blockIdx, pDest);
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

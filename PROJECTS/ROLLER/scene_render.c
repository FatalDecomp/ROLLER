#include "scene_render.h"
#include "scene_render_software.h"
#include "scene_render_gpu.h"

#include <stdbool.h>
#include <stdlib.h>

struct SceneRenderer {
    SceneRendererSoftware *sw;
    SceneRendererGPU      *gpu;
    SDL_GPUDevice *device;
    SDL_Window *window;
    bool use_gpu;   /* route quads to GPU when true, SW when false */
    bool use_split; /* when true, route quads to BOTH SW and GPU simultaneously */
};

SceneRenderer *scene_render_create(SDL_GPUDevice *device, SDL_Window *window) {
    SceneRenderer *r = calloc(1, sizeof(SceneRenderer));
    if (!r)
        return NULL;
    r->device = device;
    r->window = window;
    r->sw = scene_render_sw_create(device, window);
    if (!r->sw) {
        free(r);
        return NULL;
    }
    r->gpu = scene_render_gpu_create(device, window);
    return r;
}

void scene_render_destroy(SceneRenderer *renderer) {
    if (!renderer)
        return;
    scene_render_sw_destroy(renderer->sw);
    if (renderer->gpu)
        scene_render_gpu_destroy(renderer->gpu);
    free(renderer);
}

void scene_render_set_target(SceneRenderer *renderer, uint8 *buffer,
                             int stride, int width, int height) {
    if (!renderer)
        return;
    scene_render_sw_set_target(renderer->sw, buffer, stride, width, height);
}

void scene_render_set_viewport(SceneRenderer *renderer,
                               int x, int y, int w, int h) {
    if (!renderer)
        return;
    scene_render_sw_set_viewport(renderer->sw, x, y, w, h);
    if (renderer->gpu)
        scene_render_gpu_set_viewport(renderer->gpu, x, y, w, h);
}

void scene_render_set_camera(SceneRenderer *renderer,
                             const SceneRenderCamera *camera) {
    if (!renderer || !camera)
        return;
    scene_render_sw_set_camera(renderer->sw, camera);
    if (renderer->gpu)
        scene_render_gpu_set_camera(renderer->gpu, camera);
}

void scene_render_set_projection(SceneRenderer *renderer,
                                 const SceneRenderProjection *projection) {
    if (!renderer || !projection)
        return;
    scene_render_sw_set_projection(renderer->sw, projection);
    if (renderer->gpu)
        scene_render_gpu_set_projection(renderer->gpu, projection);
}

SceneTextureHandle scene_render_load_texture(SceneRenderer *renderer,
                                             uint8 *pixelData,
                                             int width, int height,
                                             int tex_idx,
                                             int texHalfRes) {
    if (!renderer)
        return SCENE_TEXTURE_HANDLE_INVALID;
    SceneTextureHandle swh = scene_render_sw_load_texture(renderer->sw, pixelData,
                                                          width, height,
                                                          tex_idx, texHalfRes);
    if (renderer->gpu)
        scene_render_gpu_load_texture(renderer->gpu, pixelData, width, height,
                                      tex_idx, texHalfRes);
    return swh;
}

void scene_render_free_texture(SceneRenderer *renderer,
                               SceneTextureHandle handle) {
    if (!renderer)
        return;
    scene_render_sw_free_texture(renderer->sw, handle);
    if (renderer->gpu)
        scene_render_gpu_free_texture(renderer->gpu, handle);
}

SceneTextureHandle scene_render_get_texture_handle(SceneRenderer *renderer,
                                                   int tex_idx) {
    if (!renderer)
        return SCENE_TEXTURE_HANDLE_INVALID;
    return scene_render_sw_get_texture_handle(renderer->sw, tex_idx);
}

void scene_render_set_use_gpu(SceneRenderer *renderer, bool use_gpu) {
    if (renderer)
        renderer->use_gpu = use_gpu;
}

void scene_render_set_split_screen(SceneRenderer *renderer, bool split) {
    if (!renderer) return;
    renderer->use_split = split;
    if (renderer->gpu)
        scene_render_gpu_set_split_screen(renderer->gpu, split);
}

void scene_render_set_debug_overlay(SceneRenderer *renderer, DebugOverlay *overlay) {
    if (renderer && renderer->gpu)
        scene_render_gpu_set_debug_overlay(renderer->gpu, overlay);
}

void scene_render_quad_world_legacy(SceneRenderer *renderer,
                                    const SceneRenderVertex verts[4],
                                    SceneTextureHandle texture,
                                    int surfaceFlags,
                                    SceneRenderLegacyQuadOptions options) {
    if (!renderer || !verts)
        return;
    if (renderer->gpu && renderer->use_gpu)
        scene_render_gpu_quad_world_legacy(renderer->gpu, verts, texture,
                                           surfaceFlags, options);
    if (!renderer->use_gpu || renderer->use_split)
        scene_render_sw_quad_world_legacy(renderer->sw, verts, texture,
                                          surfaceFlags, options);
}

SceneRendererGPU *scene_render_get_gpu(SceneRenderer *renderer) {
    return renderer ? renderer->gpu : NULL;
}

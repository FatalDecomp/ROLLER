#include "scene_render_software.h"
#include "3d.h"
#include "drawtrk3.h"
#include "graphics.h"
#include "func2.h"
#include "polytex.h"
#include "roller.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCENE_RENDER_MAX_TEXTURE_SLOTS 32

/* SubpolyType values passed to dodivide for texture routing.
 * -1 = wide wall (2048x1024), 0 = standard track (1024x1024),
 * 666 = building (other_texture[] lookup). */
#define SUBPOLY_WALL     (-1)
#define SUBPOLY_STANDARD   0
#define SUBPOLY_BUILDING 666

typedef struct {
    uint8 *pixels;
    int width;
    int height;
    int tex_idx;
    int texHalfRes;
    int in_use;
} SceneTextureSlot;

struct SceneRendererSoftware {
    SceneRenderCamera camera;
    SceneRenderProjection proj;

    uint8 *targetBuffer;
    int targetStride;
    int targetWidth;
    int targetHeight;
    int viewportX;
    int viewportY;
    int viewportW;
    int viewportH;

    SceneTextureSlot texSlots[SCENE_RENDER_MAX_TEXTURE_SLOTS];
    SceneTextureHandle texIdxToHandle[32];
};

static void scene_render_sw_bind_target(SceneRendererSoftware *sw) {
    if (!sw || !sw->targetBuffer)
        return;
    screen_pointer = sw->targetBuffer + sw->viewportY * sw->targetStride + sw->viewportX;
}

SceneRendererSoftware *scene_render_sw_create(SDL_GPUDevice *device,
                                              SDL_Window *window) {
    (void)device;
    (void)window;
    SceneRendererSoftware *sw = calloc(1, sizeof(SceneRendererSoftware));
    return sw;
}

void scene_render_sw_destroy(SceneRendererSoftware *sw) {
    free(sw);
}

void scene_render_sw_set_target(SceneRendererSoftware *sw, uint8 *buffer,
                                int stride, int width, int height) {
    if (!sw)
        return;
    sw->targetBuffer = buffer;
    sw->targetStride = stride;
    sw->targetWidth = width;
    sw->targetHeight = height;
    if (sw->viewportW == 0 && sw->viewportH == 0) {
        sw->viewportX = 0;
        sw->viewportY = 0;
        sw->viewportW = width;
        sw->viewportH = height;
    }
    scene_render_sw_bind_target(sw);
}

void scene_render_sw_set_viewport(SceneRendererSoftware *sw,
                                  int x, int y, int w, int h) {
    if (!sw)
        return;
    sw->viewportX = x;
    sw->viewportY = y;
    sw->viewportW = w;
    sw->viewportH = h;
    scene_render_sw_bind_target(sw);
}

void scene_render_sw_set_camera(SceneRendererSoftware *sw,
                                const SceneRenderCamera *camera) {
    extern float viewx, viewy, viewz;
    extern float fcos, fsin;
    extern int VIEWDIST;
    if (!sw || !camera)
        return;
    sw->camera = *camera;
    viewx = camera->viewX;
    viewy = camera->viewY;
    viewz = camera->viewZ;
    fcos = camera->cosYaw;
    fsin = camera->sinYaw;
    VIEWDIST = (int)camera->fovScale;
}

void scene_render_sw_set_projection(SceneRendererSoftware *sw,
                                    const SceneRenderProjection *proj) {
    extern float vk1, vk2, vk3, vk4, vk5, vk6, vk7, vk8, vk9;
    extern int scr_size, xbase, ybase, gfx_size;
    if (!sw || !proj)
        return;
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

SceneTextureHandle scene_render_sw_load_texture(SceneRendererSoftware *sw,
                                                uint8 *pixelData,
                                                int width, int height,
                                                int tex_idx, int texHalfRes) {
    if (!sw)
        return SCENE_TEXTURE_HANDLE_INVALID;

    if (tex_idx >= 0 && tex_idx < 32) {
        SceneTextureHandle old = sw->texIdxToHandle[tex_idx];
        if (old != SCENE_TEXTURE_HANDLE_INVALID)
            scene_render_sw_free_texture(sw, old);
    }

    for (int i = 1; i < SCENE_RENDER_MAX_TEXTURE_SLOTS; i++) {
        if (!sw->texSlots[i].in_use) {
            sw->texSlots[i].pixels = pixelData;
            sw->texSlots[i].width = width;
            sw->texSlots[i].height = height;
            sw->texSlots[i].tex_idx = tex_idx;
            sw->texSlots[i].texHalfRes = texHalfRes;
            sw->texSlots[i].in_use = 1;
            if (tex_idx >= 0 && tex_idx < 32)
                sw->texIdxToHandle[tex_idx] = i;
            return (SceneTextureHandle)i;
        }
    }
    return SCENE_TEXTURE_HANDLE_INVALID;
}

void scene_render_sw_free_texture(SceneRendererSoftware *sw,
                                  SceneTextureHandle handle) {
    if (!sw || handle <= 0 || handle >= SCENE_RENDER_MAX_TEXTURE_SLOTS)
        return;
    int tex_idx = sw->texSlots[handle].tex_idx;
    if (tex_idx >= 0 && tex_idx < 32 && sw->texIdxToHandle[tex_idx] == handle)
        sw->texIdxToHandle[tex_idx] = SCENE_TEXTURE_HANDLE_INVALID;
    memset(&sw->texSlots[handle], 0, sizeof(SceneTextureSlot));
}

SceneTextureHandle scene_render_sw_get_texture_handle(SceneRendererSoftware *sw,
                                                      int tex_idx) {
    if (!sw)
        return SCENE_TEXTURE_HANDLE_INVALID;
    if (tex_idx >= 0 && tex_idx < 32)
        return sw->texIdxToHandle[tex_idx];
    return SCENE_TEXTURE_HANDLE_INVALID;
}

void scene_render_sw_quad_world_legacy(SceneRendererSoftware *sw,
                                       const SceneRenderVertex *verts,
                                       SceneTextureHandle handle,
                                       int surfaceFlags,
                                       SceneRenderLegacyQuadOptions options) {
    if (!sw || !verts || !sw->targetBuffer)
        return;

    scene_render_sw_bind_target(sw);

    const SceneRenderCamera *cam = &sw->camera;
    const SceneRenderProjection *proj = &sw->proj;

    int useCloudProjection = options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD;
    int subpolyType;
    if (useCloudProjection) {
        subpolyType = SUBPOLY_STANDARD;
    } else if (options.subdivideType != SCENE_RENDER_SUBDIVIDE_TYPE_AUTO) {
        subpolyType = options.subdivideType;
    } else if (handle > 0 && handle < SCENE_RENDER_MAX_TEXTURE_SLOTS
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
        int useCarProjection = subpolyType >= 3;
        double viewDist = (double)cam->fovScale;
        for (int i = 0; i < 4; i++) {
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
            if (useCarProjection || useCloudProjection) {
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
            subVz[i] = (useCarProjection || useCloudProjection) ? fVz : (float)((int)round(dCameraZ));
        }
    }

    if (subpolyType == SUBPOLY_WALL) set_starts(1u);

    int useDirect = 0;
    if (options.subThreshold > 0.0f) {
        float minZ = subVz[0];
        if (subVz[1] < minZ) minZ = subVz[1];
        if (subVz[2] < minZ) minZ = subVz[2];
        if (subVz[3] < minZ) minZ = subVz[3];
        if (options.subThreshold <= minZ)
            useDirect = 1;
    }

    if (useDirect && handle == SCENE_TEXTURE_HANDLE_INVALID) {
        POLYFLAT(screen_pointer, &poly);
    } else if (useDirect) {
        SceneTextureSlot *slot = &sw->texSlots[handle];
        POLYTEX(slot->pixels, screen_pointer, &poly,
                slot->tex_idx, slot->texHalfRes);
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

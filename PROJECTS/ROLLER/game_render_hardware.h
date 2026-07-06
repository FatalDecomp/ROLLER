#ifndef GAME_RENDER_HARDWARE_H
#define GAME_RENDER_HARDWARE_H

#include <SDL3/SDL.h>
#include "scene_render_gpu.h"

/* Forward declarations (defined in game_render.h) */
typedef struct GameRenderCarPose    GameRenderCarPose;
typedef struct GameRenderCarOptions GameRenderCarOptions;

typedef struct GameRendererHardware GameRendererHardware;

GameRendererHardware *game_render_hw_create(SDL_GPUDevice *device);
void                  game_render_hw_destroy(GameRendererHardware *r);

void game_render_hw_draw_car(GameRendererHardware       *r,
                              SceneRendererGPU           *scene,
                              int                         carIdx,
                              const GameRenderCarPose    *pose,
                              const GameRenderCarOptions *options);

/* Draw the 2D name-tag overlay for one car in GPU mode.
 * Must be called after game_render_hw_draw_car, before NamesLeft is decremented.
 * viewSlot: which player's view is currently being queued (0=P1, 1=P2 in
 * 2-player mode; 0 otherwise) -- keys the per-car scrY smoothing state so one
 * player's view doesn't inherit the other's camera-relative computation. */
void game_render_hw_draw_car_name_tag(int carIdx, const GameRenderCarPose *pose, int viewSlot);

/* Draw FPS counter into scrbuf at the corner specified by g_iFpsDisplay (1-4; 0=off). */
void game_render_hw_draw_fps_overlay(void);

#endif /* GAME_RENDER_HARDWARE_H */

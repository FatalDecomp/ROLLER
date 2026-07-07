#ifndef GAME_RENDER_HW_H
#define GAME_RENDER_HW_H

/* GPU-specific settings for GameRenderer.  Include this header (instead of
 * game_render.h) in files that need to tweak hardware-renderer parameters.
 * game_render.h is included transitively so callers only need one include. */

#include "game_render.h"

typedef struct GameRenderer GameRenderer;

/* Texture filtering: 0=nearest, 1=bilinear, 2=anisotropic */
void game_render_set_texture_filter(GameRenderer *renderer, int filter);
/* Trilinear mip blending */
void game_render_set_trilinear(GameRenderer *renderer, bool enabled);
/* Debug: clamp sampler to mip 0 only (isolates Adreno mipmap layout bug) */
void game_render_set_disable_mipmaps(GameRenderer *renderer, bool disabled);
/* Anisotropy level: 0=2x, 1=4x, 2=8x, 3=16x */
void game_render_set_anisotropy_level(GameRenderer *renderer, int level);
/* Mip LOD bias: negative=sharper, positive=blurrier */
void game_render_set_lod_bias(GameRenderer *renderer, float bias);
/* Internal render scale: 1.0=native, 2.0=4x pixels (SSAA) */
void game_render_set_render_scale(GameRenderer *renderer, float scale);
/* Exponential-squared fog coefficient: 0.0=off */
void game_render_set_fog_density(GameRenderer *renderer, float density);
/* Fog colour: linear RGB [0..1] each channel */
void game_render_set_fog_color(GameRenderer *renderer, float fr, float fg, float fb);
/* Output gamma: 1.0=neutral, <1=brighter, >1=darker */
void game_render_set_gamma(GameRenderer *renderer, float gamma);
/* MSAA level: 0=off, 1=2x, 2=4x, 3=8x */
void game_render_set_antialiasing(GameRenderer *renderer, int level);
/* V-sync */
void game_render_set_vsync(GameRenderer *renderer, bool enabled);
/* Fog start: view-space depth before which fog is suppressed */
void game_render_set_fog_start(GameRenderer *renderer, float start);
/* Colour saturation: 0=greyscale, 1=neutral, >1=boosted */
void game_render_set_saturation(GameRenderer *renderer, float saturation);
/* Contrast: 0=flat grey, 1=neutral, >1=high contrast */
void game_render_set_contrast(GameRenderer *renderer, float contrast);
/* Vignette: 0=off, higher=darker edges */
void game_render_set_vignette(GameRenderer *renderer, float strength);
/* FOV multiplier on top of game camera: 1.0=native, <1=zoom in, >1=zoom out */
void game_render_set_fov_multiplier(GameRenderer *renderer, float mult);
/* Wireframe: true=line fill, false=solid */
void game_render_set_wireframe(GameRenderer *renderer, bool enabled);
/* Per-chunk static geometry cache kill-switch: false=every quad always live (debug only, not saved) */
void game_render_set_chunk_cache_enabled(GameRenderer *renderer, bool enabled);
/* Cull mode: 0=default, 1=none, 2=back, 3=front (debug only, not saved) */
void game_render_set_cull_mode(GameRenderer *renderer, int mode);
/* Additive brightness offset: 0.0=neutral, positive=brighter, negative=darker */
void game_render_set_brightness(GameRenderer *renderer, float brightness);

#endif /* GAME_RENDER_HW_H */

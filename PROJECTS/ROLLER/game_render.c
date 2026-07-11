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
#include "roller.h"    /* g_fMirrorFov */

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
    bool mirrorPass;    /* true between begin/end_mirror_pass */
    SDL_GPUTexture *pendingMirrorTex; /* set by end_mirror_pass, consumed by composite_mirror_pass */
    SDL_GPUTexture *pendingCinemaTex; /* set by end_cinema_pass, consumed by composite_cinema_view --
                                       * see game_render_begin_cinema_pass. Safe to share secondary-view
                                       * slot 0 with the mirror pass: CINEMA, mirror, and 2-player are
                                       * mutually exclusive per frame (3d.c's Play_View/player_type==2
                                       * branches both exit before the CINEMA branch is ever reached). */
    bool splitScreen;   /* GPU mode only: SW quads also run, HUD pass covers left half */
    bool forceGpuLoad;  /* always upload textures to GPU even in SW mode (set for intro) */
    int  activeViewSlot; /* which secondary-view slot (0=P1, 1=P2) is currently being
                          * queued in 2-player mode -- see game_render_set_active_view_slot */
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
    /* GPU textures are only uploaded when the race/session starts in GPU mode.
     * If they were skipped (started in SW), switching to GPU mid-session used
     * to just be blocked outright (would've rendered with an empty atlas).
     * Instead, lazily upload every already-loaded texture's still-live pixel
     * data to the GPU atlas now, so the switch can proceed safely. */
    if (mode == GAME_RENDER_GPU && !scene_render_get_gpu_load_enabled(renderer->scene))
        scene_render_reload_gpu_textures(renderer->scene);
    /* Defer the actual switch to the next begin_frame so it never fires
     * mid-frame while a GPU command buffer is open. */
    renderer->pendingMode    = mode;
    renderer->pendingModeSet = true;
    /* Split screen is GPU-only; turn it off when leaving GPU mode. */
    if (mode != GAME_RENDER_GPU && renderer->splitScreen)
        game_render_set_split_screen(renderer, false);
}

void game_render_set_force_gpu_load(GameRenderer *renderer, bool force) {
    if (renderer)
        renderer->forceGpuLoad = force;
}

void game_render_set_debug_overlay(GameRenderer *renderer, DebugOverlay *overlay) {
    if (!renderer)
        return;
    scene_render_set_debug_overlay(renderer->scene, overlay);
}

GameRenderMode game_render_get_mode(const GameRenderer *renderer) {
    return renderer->mode;
}

void game_render_set_split_screen(GameRenderer *renderer, bool split) {
    if (!renderer) return;
    renderer->splitScreen = split;
    scene_render_set_split_screen(renderer->scene, split);
}

bool game_render_is_split_screen(const GameRenderer *renderer) {
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
        /* Name-tag EMA smoothing state goes stale whenever GPU mode isn't the
         * one actually drawing tags -- reset on every switch (either
         * direction) so resuming GPU mode never blends in from a stale
         * position (see game_render_hw_reset_name_tags doc comment). */
        game_render_hw_reset_name_tags();
    }
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_frame(renderer->sw);
    else if (renderer->mode == GAME_RENDER_GPU) {
        /* Clear the FULL fixed-size scrbuf allocation (SCRBUF_MAX_PIXELS),
         * not just this frame's iHudW*iHudH -- hudW/hudH (via "Cinema
         * Native") can change from one frame to the next (toggling on/off,
         * or the computed logical resolution shifting), and this clear runs
         * BEFORE that decision is made for the current frame (see 3d.c's
         * CINEMA branch, which calls game_render_begin_cinema_native/
         * game_render_reset_cinema_native well after game_render_begin_
         * frame). Clearing only iHudW*iHudH-worth of bytes using whichever
         * frame's dimensions happened to be set last left stale rows from
         * an earlier configuration behind, read back with a mismatched row
         * stride -- visible as flickering/melted HUD text. The full clear
         * is always safe (scrbuf is allocated at exactly this size, see
         * SCRBUF_MAX_PIXELS) and cheap (a quarter-megabyte memset). */
        if (scrbuf)
            memset(scrbuf, 0, SCRBUF_MAX_PIXELS);
        scene_render_gpu_begin_frame(renderer->gpu);
    }
}

void game_render_end_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE) {
        game_render_sw_end_frame(renderer->sw);
    } else if (renderer->mode == GAME_RENDER_GPU) {
        int iHudW = renderer->hudW > 0 ? renderer->hudW : (XMAX > 0 ? XMAX : 640);
        int iHudH = renderer->hudH > 0 ? renderer->hudH : (YMAX > 0 ? YMAX : 400);
        if (fade_palette_active())
            fade_palette_update();
        game_render_hw_draw_fps_overlay();
        scene_render_gpu_set_hud_buffer(renderer->gpu,
                                        scrbuf,
                                        iHudW,
                                        iHudH);
        scene_render_gpu_end_frame(renderer->gpu);
    }
}

void game_render_begin_mirror_pass(GameRenderer *renderer, float scrSizeRatio) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU) return;
    renderer->mirrorPass = true;
    if (renderer->gpu) {
        /* Snapshot texParticle counts NOW, before the mirror's draw_road()
         * queues anything (including real smoke/particles) -- see
         * scene_render_gpu_secondary_view_will_queue's comment. */
        scene_render_gpu_secondary_view_will_queue(renderer->gpu);
        /* See header comment: build_mvp's FOV/aspect is normalized by the
         * current GPU viewport size (defaults to 640x400 when never set,
         * which is the case throughout normal gameplay). The mirror's
         * queued draws need that same baseline scaled down by the same
         * fraction scr_size was just divided by. g_fMirrorFov is a user
         * debug-overlay tuning knob on top of that: since a LARGER viewport
         * value here means smaller per-pixel FOV (more of the world fits in
         * the same NDC range), increasing g_fMirrorFov widens the mirror's
         * effective FOV (more zoomed out); lowering it narrows/zooms in. */
        int vpW = (int)(640.0f * scrSizeRatio * g_fMirrorFov + 0.5f);
        int vpH = (int)(400.0f * scrSizeRatio * g_fMirrorFov + 0.5f);
        if (vpW < 1) vpW = 1;
        if (vpH < 1) vpH = 1;
        scene_render_gpu_set_viewport(renderer->gpu, 0, 0, vpW, vpH);
    }
}

void game_render_end_mirror_pass(GameRenderer *renderer, int texW, int texH) {
    if (!renderer) return;
    renderer->mirrorPass = false;
    renderer->pendingMirrorTex = NULL;
    if (renderer->mode == GAME_RENDER_GPU && renderer->gpu) {
        renderer->pendingMirrorTex = scene_render_gpu_flush_secondary_view(renderer->gpu, 0, texW, texH);
        /* Restore the default (main-window) viewport for the main scene's
         * own queued draws that follow. */
        scene_render_gpu_set_viewport(renderer->gpu, 0, 0, 0, 0);
    }
}

/* Composite the texture captured by end_mirror_pass onto the current frame
 * as a screen-space quad, once the real on-screen destination rect is known.
 * Queued at NDC z=0 (always passes depth test, no depth write) so it draws
 * over the main scene like a HUD element. flipH reverses which screen-space
 * corner samples which UV corner, producing a true left-right mirror image
 * without needing a separate flipped draw path. */
void game_render_composite_mirror_pass(GameRenderer *renderer,
                                       int screenX, int screenY,
                                       int screenW, int screenH,
                                       bool flipH, int borderColorIdx) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    SDL_GPUTexture *tex = renderer->pendingMirrorTex;
    renderer->pendingMirrorTex = NULL;
    if (!tex) return;

    float ww = (float)winw, wh = (float)winh;
    if (ww <= 0.0f || wh <= 0.0f) return;

    scene_render_gpu_set_particle_ndcz(renderer->gpu, 0.0f);

    /* Border: flat quad slightly larger than the picture, drawn first so the
     * picture (drawn second, same z) paints over its middle. */
    const int kBorder = 2;
    float bl = (float)(screenX - kBorder), br = (float)(screenX + screenW + kBorder);
    float bt = (float)(screenY - kBorder), bb = (float)(screenY + screenH + kBorder);
    float bx[4] = { br/ww*2.0f-1.0f, bl/ww*2.0f-1.0f, bl/ww*2.0f-1.0f, br/ww*2.0f-1.0f };
    float by[4] = { 1.0f-bt/wh*2.0f, 1.0f-bt/wh*2.0f, 1.0f-bb/wh*2.0f, 1.0f-bb/wh*2.0f };
    const tColor *bc = &palette[borderColorIdx & 0xFF];
    scene_render_gpu_screen_quad_flat(renderer->gpu, bx, by,
        bc->byR / 63.0f, bc->byG / 63.0f, bc->byB / 63.0f, 1.0f);

    /* Picture: v0/v1/v2/v3 = top-right/top-left/bottom-left/bottom-right
     * screen positions normally; swapping v0<->v1 and v2<->v3 flips which
     * screen corner samples which UV corner, i.e. mirrors the image. */
    float l = (float)screenX, r = (float)(screenX + screenW);
    float t = (float)screenY, b = (float)(screenY + screenH);
    float px[4], py[4];
    if (flipH) {
        px[0]=l; py[0]=t;  px[1]=r; py[1]=t;
        px[2]=r; py[2]=b;  px[3]=l; py[3]=b;
    } else {
        px[0]=r; py[0]=t;  px[1]=l; py[1]=t;
        px[2]=l; py[2]=b;  px[3]=r; py[3]=b;
    }
    float ndcX[4], ndcY[4];
    for (int i = 0; i < 4; i++) {
        ndcX[i] = px[i]/ww*2.0f - 1.0f;
        ndcY[i] = 1.0f - py[i]/wh*2.0f;
    }
    scene_render_gpu_screen_quad_textured(renderer->gpu, ndcX, ndcY, tex, 1.0f, 1.0f, 1.0f, 1.0f);
}

/* CINEMA cheat letterbox (default behaviour): SW renders the shrunk 3D view
 * directly into a sub-rect of scrbuf (the real framebuffer there), giving a
 * true letterboxed look. GPU has no equivalent -- the primary 3D pass always
 * renders full-size regardless of winw/winh, so the cheat's black bars never
 * appear (scrbuf's memset(0) bars become transparent HUD pixels instead of
 * opaque black -- see indexed_to_rgba). Fixed the same way the mirror/2P
 * secondary views already are: render the shrunk content into its own
 * texture via the existing secondary-view mechanism, then composite it as a
 * black-bordered quad into the correct sub-rect of the main frame. */
void game_render_begin_cinema_pass(GameRenderer *renderer, int winwCinema, int winhCinema) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU) return;
    if (renderer->gpu) {
        scene_render_gpu_secondary_view_will_queue(renderer->gpu);
        scene_render_gpu_set_viewport(renderer->gpu, 0, 0, winwCinema, winhCinema);
    }
}

void game_render_end_cinema_pass(GameRenderer *renderer, int texW, int texH) {
    if (!renderer) return;
    renderer->pendingCinemaTex = NULL;
    if (renderer->mode == GAME_RENDER_GPU && renderer->gpu) {
        renderer->pendingCinemaTex = scene_render_gpu_flush_secondary_view(renderer->gpu, 0, texW, texH);
        scene_render_gpu_set_viewport(renderer->gpu, 0, 0, 0, 0);
    }
}

/* Composite the texture captured by end_cinema_pass into the shrunk cinema
 * sub-rect of the main frame. Unlike composite_mirror_pass (which runs after
 * winw/winh have already been restored to the full frame, so dividing by
 * them is equivalent to dividing by XMAX/YMAX), CINEMA's composite runs
 * while winw/winh STILL describe the shrunk sub-rect itself -- dividing by
 * them here would be circular and just fill the whole screen again (the
 * exact bug being fixed). screenX/Y/W/H (= winx,winy,winw,winh) are pixel
 * coordinates within the FIXED XMAX x YMAX reference frame instead, exactly
 * like pMainScrPtr = &scrbuf[winw*winy] already treats scrbuf as a
 * fixed-size canvas with a sub-rect inside it. */
void game_render_composite_cinema_view(GameRenderer *renderer,
                                       int screenX, int screenY,
                                       int screenW, int screenH) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    SDL_GPUTexture *tex = renderer->pendingCinemaTex;
    renderer->pendingCinemaTex = NULL;
    if (!tex) return;

    float ww = (float)XMAX, wh = (float)YMAX;
    if (ww <= 0.0f || wh <= 0.0f) return;

    scene_render_gpu_set_particle_ndcz(renderer->gpu, 0.0f);

    /* Full-frame opaque black quad first: Pass 1's own clear colour is the
     * sky colour, not black, so this is what actually produces the bars.
     * The picture (drawn second, same NDC z) paints over its middle --
     * same technique as the mirror pass's border+picture pair above. */
    float fullX[4] = { 1.0f, -1.0f, -1.0f, 1.0f };
    float fullY[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
    scene_render_gpu_screen_quad_flat(renderer->gpu, fullX, fullY, 0.0f, 0.0f, 0.0f, 1.0f);

    float l = (float)screenX, r = (float)(screenX + screenW);
    float t = (float)screenY, b = (float)(screenY + screenH);
    float px[4] = { r, l, l, r };
    float py[4] = { t, t, b, b };
    float ndcX[4], ndcY[4];
    for (int i = 0; i < 4; i++) {
        ndcX[i] = px[i]/ww*2.0f - 1.0f;
        ndcY[i] = 1.0f - py[i]/wh*2.0f;
    }
    scene_render_gpu_screen_quad_textured(renderer->gpu, ndcX, ndcY, tex, 1.0f, 1.0f, 1.0f, 1.0f);
}

/* Widescreen support: fill the REAL window at its native aspect ratio -- no
 * bars, full use of ultrawide/triple-monitor width -- WITHOUT touching the
 * SW/HUD reference frame at all, so the HUD looks pixel-for-pixel identical
 * to normal single-player mode instead of blurry/stretched. Selected via the
 * "(native)" Render Scale options (debug_overlay.c), available at any time,
 * not just while the CINEMA cheat is active -- 3d.c calls this from BOTH the
 * normal single-player path and the CINEMA-active path when native is on.
 *
 * An earlier version computed a budget-constrained logical winw/winh/
 * scr_size for BOTH the 3D camera and the HUD canvas together (see git
 * history if ever needed) -- HUD glyphs/sprites are rendered by the game's
 * original sprite-scaling code into scrbuf at whatever that logical
 * resolution was, and since scrbuf is a fixed ~256000-byte allocation (see
 * SCRBUF_MAX_PIXELS), a wide aspect ratio forced the logical height well
 * below the normal 400px baseline -- then upscaling that smaller canvas to
 * fill a real window produced exactly the reported blur/zoom.
 *
 * The fix: build_mvp's vertical-centering correction (scene_render_gpu.c,
 * `shift_y = (vpH*0.5 - horizon_y) / (vpH*0.5)`) is the ONLY
 * place a SW-side reference value (winh, via ss/centerY) is coupled to the
 * GPU's own viewport size -- there is no equivalent horizontal coupling
 * (fovX is a pure multiplicative FOV term, no additive centering offset).
 * So the GPU's viewport HEIGHT must still equal the SW-side winh exactly
 * (as Part 1's letterbox pass already does), but its WIDTH can be freely
 * widened to match the real aspect ratio without any SW-side counterpart --
 * 3d.c's CINEMA branch now sets winw/winh/scr_size/xbase to the EXACT same
 * values normal single-player mode uses (so scrbuf and the whole HUD/name-
 * tag pipeline works exactly as it always has, budget concerns don't even
 * arise since that's exactly SCRBUF_MAX_PIXELS), and this function only
 * widens the GPU's OWN viewport width to the real aspect ratio at that same
 * (unchanged) height. The full-window HUD texture then only stretches
 * horizontally by the same mild ratio the 3D view widened by, not the
 * severe uniform upscale the budget-constrained approach needed.
 *
 * This state persists across scene_render_gpu_end_frame (unlike the
 * letterbox pass, which is consumed synchronously within begin/end_cinema_
 * pass) -- callers MUST call game_render_reset_cinema_native() every frame,
 * unconditionally, before deciding whether this frame is a Native frame, so
 * a previous frame's override can never leak into a frame that shouldn't
 * have it (normal windowed, mirror, 2P, letterbox-default, or Native-off). */
void game_render_begin_cinema_native(GameRenderer *renderer, int refWinH) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    if (!renderer->window || refWinH <= 0) return;
    int realW = 0, realH = 0;
    SDL_GetWindowSizeInPixels(renderer->window, &realW, &realH);
    if (realW <= 0 || realH <= 0) return;
    double ar = (double)realW / (double)realH;
    int wideVpW = (int)((double)refWinH * ar + 0.5);
    if (wideVpW < 1) wideVpW = 1;
    scene_render_gpu_set_viewport(renderer->gpu, 0, 0, wideVpW, refWinH);
}

void game_render_reset_cinema_native(GameRenderer *renderer) {
    if (!renderer) return;
    if (renderer->mode == GAME_RENDER_GPU && renderer->gpu)
        scene_render_gpu_set_viewport(renderer->gpu, 0, 0, 0, 0);
}

void game_render_flush_player_view(GameRenderer *renderer, int slot,
                                   int texW, int texH,
                                   int frameW, int frameH,
                                   int screenX, int screenY,
                                   int screenW, int screenH) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    SDL_GPUTexture *tex = scene_render_gpu_flush_secondary_view(renderer->gpu, slot, texW, texH);
    if (!tex) return;
    if (frameW <= 0 || frameH <= 0) return;

    float ww = (float)frameW, wh = (float)frameH;
    float l = (float)screenX, r = (float)(screenX + screenW);
    float t = (float)screenY, b = (float)(screenY + screenH);
    /* v0=top-right, v1=top-left, v2=bottom-left, v3=bottom-right (no flip,
     * no border -- just a plain half-screen picture). */
    float px[4] = { r, l, l, r };
    float py[4] = { t, t, b, b };
    float ndcX[4], ndcY[4];
    for (int i = 0; i < 4; i++) {
        ndcX[i] = px[i]/ww*2.0f - 1.0f;
        ndcY[i] = 1.0f - py[i]/wh*2.0f;
    }
    scene_render_gpu_set_particle_ndcz(renderer->gpu, 0.0f);
    scene_render_gpu_screen_quad_textured(renderer->gpu, ndcX, ndcY, tex, 1.0f, 1.0f, 1.0f, 1.0f);
}

void game_render_begin_2p_pass(GameRenderer *renderer) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    /* Unlike the mirror (where winw AND winh shrink together, preserving
     * aspect -- see game_render_begin_mirror_pass), 2-player mode keeps winw
     * at the FULL window width (XMAX) and only halves winh. scr_size is also
     * halved (128->64 SVGA, 64->32 non-SVGA).
     *
     * build_mvp's fovX/fovY = 2*fovScale*ss/vpW(or H), where ss tracks the
     * (already-halved) scr_size. Deriving the vpW/vpH that make this match
     * SW's own screenX/Y = K*scr_size/winw(or h) ratio:
     *   - height: winh AND scr_size both halve, so their ratio (and thus the
     *     needed vpH) is exactly half the 640x400 baseline -> vpH = 200.
     *   - width: winw is UNCHANGED but scr_size still halves, so the ratio
     *     needed is HALF of single-player's own baseline ratio -> vpW must
     *     stay at the FULL 640 baseline (not halved) so that dividing by the
     *     halved ss produces half the single-player fovX (wider effective
     *     horizontal FOV, matching the unchanged width).
     * Halving both (320x200, an earlier attempt) made fovX 2x too large,
     * visibly squishing/zooming the picture relative to SW. */
    scene_render_gpu_set_viewport(renderer->gpu, 0, 0, 640, 200);
}

void game_render_end_2p_pass(GameRenderer *renderer) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    scene_render_gpu_set_viewport(renderer->gpu, 0, 0, 0, 0);
}

/* Call right before EACH player's own draw_road() in 2-player mode (not just
 * once for both, unlike begin_2p_pass's viewport setup) -- see
 * scene_render_gpu_secondary_view_will_queue's comment for why the timing
 * must be this precise: each player's own scene (including real smoke/
 * particles) needs its own "before" snapshot. */
void game_render_secondary_view_will_queue(GameRenderer *renderer) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    scene_render_gpu_secondary_view_will_queue(renderer->gpu);
}

/* Call right before EACH player's own draw_road() in 2-player mode, alongside
 * game_render_secondary_view_will_queue(). Lets per-car GPU state that's keyed
 * only by carIdx (e.g. the name-tag scrY EMA) also key on which player's view
 * is currently being queued, so drawing car X's tag in P1's view doesn't feed
 * its smoothing off a value P2's view (a different camera entirely) wrote for
 * the same carIdx moments earlier -- see [[project_car_name_tags]]. */
void game_render_set_active_view_slot(GameRenderer *renderer, int slot) {
    if (!renderer) return;
    renderer->activeViewSlot = slot;
}

void game_render_draw_2p_divider(GameRenderer *renderer,
                                 int frameW, int frameH,
                                 int dividerY, int dividerH) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    if (frameW <= 0 || frameH <= 0) return;

    float ww = (float)frameW, wh = (float)frameH;
    float l = 0.0f, r = ww;
    float t = (float)dividerY, b = (float)(dividerY + dividerH);
    float ndcX[4] = { r/ww*2.0f-1.0f, l/ww*2.0f-1.0f, l/ww*2.0f-1.0f, r/ww*2.0f-1.0f };
    float ndcY[4] = { 1.0f-t/wh*2.0f, 1.0f-t/wh*2.0f, 1.0f-b/wh*2.0f, 1.0f-b/wh*2.0f };
    scene_render_gpu_set_particle_ndcz(renderer->gpu, 0.0f);
    scene_render_gpu_screen_quad_flat(renderer->gpu, ndcX, ndcY, 0.0f, 0.0f, 0.0f, 1.0f);
}

// Full-screen translucent black quad, matching SW's in-race pause-menu darken
// effect (blankwindow() + shade-level-3 palette remap in func2.c's
// display_paused()) -- SW's mechanism remaps whatever's already in scrbuf in
// place, which works there because scrbuf IS the rendered frame; in GPU mode
// scrbuf is just a throwaway HUD-overlay canvas (memset every frame, real 3D
// frame lives only on the GPU render target), so that trick is a no-op here.
// This draws an explicit darken quad into the GPU scene instead, and the HUD
// text pass (menu strings, drawn into scrbuf as usual) still composites on
// top of this afterward. Call once per frame while the pause overlay is
// active, after any 2-player flush_player_view calls have already run (see
// scene_render_gpu_screen_quad_darken for why the ordering matters in 2P mode).
void game_render_draw_pause_darken(GameRenderer *renderer) {
    if (!renderer || renderer->mode != GAME_RENDER_GPU || !renderer->gpu) return;
    scene_render_gpu_set_particle_ndcz(renderer->gpu, 0.0f);
    scene_render_gpu_screen_quad_darken(renderer->gpu, 0.8f);
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
    /* Only upload textures to the GPU renderer if the race will actually use it.
     * In pure software mode the GPU scene textures are never sampled, and
     * uploading every track/car tile (hundreds of mipmapped images, one GPU
     * submit each) on a cold device can flood the driver and trigger
     * VK_ERROR_DEVICE_LOST.  Target mode = the pending mode if a switch is
     * queued (set before the first begin_frame), otherwise the current mode. */
    GameRenderMode target = renderer->pendingModeSet ? renderer->pendingMode
                                                     : renderer->mode;
    scene_render_set_gpu_load_enabled(renderer->scene,
        renderer->forceGpuLoad || target == GAME_RENDER_GPU);
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

    /* GPU mode: route particles through the dedicated depth-tested pipeline so they
     * are occluded by solid geometry instead of blitting over everything via SW overlay. */
    if (renderer->mode == GAME_RENDER_GPU && renderer->gpu && !renderer->splitScreen) {
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
        if (handle == TEXTURE_HANDLE_INVALID) {
            scene_render_gpu_screen_quad_flat(renderer->gpu, ndcX, ndcY, cr, cg, cb, ca);
            return;
        }
        /* Tile index lives in the low byte of iSurfaceType, same as the SW path.
         * For cargen (tex_idx 18) use the particle-variant tiles where palette index 0 is
         * opaque white, so (texture × vertex_colour) gives the smoke/fire tint.  SW's POLYTEX
         * substitutes index 0 with the car's runtime colour; this is the GPU equivalent. */
        int tex_idx_gpu = game_render_texture_index_from_handle(handle);
        int tile_idx    = poly->iSurfaceType & SURFACE_MASK_TEXTURE_INDEX;
        SDL_GPUTexture *gpuTex = (tex_idx_gpu == 18)
            ? scene_render_gpu_get_particle_tile_texture(renderer->gpu, 18, tile_idx)
            : scene_render_gpu_get_tile_texture(renderer->gpu, tex_idx_gpu, tile_idx);
        if (gpuTex && scene_render_gpu_screen_quad_textured(renderer->gpu, ndcX, ndcY,
                                                            gpuTex, cr, cg, cb, ca))
            return;
        /* Tile unavailable — flat colour on the GPU depth-tested path. */
        scene_render_gpu_screen_quad_flat(renderer->gpu, ndcX, ndcY, cr, cg, cb, ca);
        return;
    }

    /* SW path: scrbuf overlay fallback. */
    int tex_idx = game_render_texture_index_from_handle(handle);
    TextureHandle swHandle = tex_idx >= 0
        ? game_render_sw_get_texture_handle(renderer->sw, tex_idx)
        : TEXTURE_HANDLE_INVALID;
    game_render_sw_quad_screen(renderer->sw, poly, swHandle, palette_remap);
}

/* Always routes through the SW rasterizer into screen_pointer (scrbuf),
 * regardless of render mode -- for HUD-style screen overlays that don't need
 * GPU depth-testing/occlusion (unlike real particles/smoke) and should
 * composite via the HUD overlay buffer the same way car-name-tag text
 * already does via mini_prt_string. Unlike game_render_quad_screen's GPU
 * particle path (which needs to be captured inside each secondary view's own
 * isolated render pass in split-screen modes -- see game_render_flush_player_
 * view/game_render_composite_mirror_pass), a plain pixel write into scrbuf
 * at the position screen_pointer already points to (set per-view by
 * draw_road()) just works, since the whole buffer composites in one shared
 * HUD pass regardless of how many 3D views share the frame. */
void game_render_quad_screen_hud(GameRenderer *renderer, tPolyParams *poly,
                                 TextureHandle handle,
                                 const uint8 *palette_remap) {
    if (!renderer)
        return;
    int tex_idx = game_render_texture_index_from_handle(handle);
    TextureHandle swHandle = tex_idx >= 0
        ? game_render_sw_get_texture_handle(renderer->sw, tex_idx)
        : TEXTURE_HANDLE_INVALID;
    game_render_sw_quad_screen(renderer->sw, poly, swHandle, palette_remap);
}

SceneRendererGPU *game_render_get_gpu(GameRenderer *renderer) {
    return renderer ? renderer->gpu : NULL;
}

SDL_GPUDevice *game_render_get_device(GameRenderer *renderer) {
    return renderer ? renderer->device : NULL;
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

void game_render_set_particle_depth(GameRenderer *renderer, float ndcZ)
{
    if (renderer && renderer->gpu)
        scene_render_gpu_set_particle_ndcz(renderer->gpu, ndcZ);
}

void game_render_set_particle_depth_pervertex(GameRenderer *renderer, const float ndcZ[4])
{
    if (renderer && renderer->gpu)
        scene_render_gpu_set_particle_ndcz_pervertex(renderer->gpu, ndcZ);
}

void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          const GameRenderCarPose *pose,
                          const GameRenderCarOptions *options) {
    if (!renderer || !pose)
        return;
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_car(renderer->sw, carIdx, pose, options);
    else if (renderer->mode == GAME_RENDER_GPU) {
        if (renderer->splitScreen)
            game_render_sw_draw_car(renderer->sw, carIdx, pose, options);
        if (renderer->hw && renderer->gpu) {
            game_render_hw_draw_car(renderer->hw, renderer->gpu, carIdx, pose, options);
            game_render_hw_draw_car_name_tag(carIdx, pose, renderer->activeViewSlot);
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
    if (renderer->mode == GAME_RENDER_SOFTWARE) {
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

void game_render_set_disable_mipmaps(GameRenderer *renderer, bool disabled) {
    if (renderer) scene_render_gpu_set_disable_mipmaps(renderer->gpu, disabled);
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

void game_render_set_emulate_software_track_borders(GameRenderer *renderer, bool enabled) {
    if (renderer) scene_render_gpu_set_emulate_software_track_darken_border(renderer->gpu, enabled);
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

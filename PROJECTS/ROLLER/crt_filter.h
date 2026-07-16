#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct CRTFilter CRTFilter;

typedef enum {
    CRT_FILTER_HYLLIAN = 0,         // scanlines + phosphor mask, Catmull-Rom resample
    CRT_FILTER_VGA_DOUBLE_SCAN = 1, // dosbox-staging "vga-1080p-fake-double-scan"
} CRTFilterMode;

CRTFilter *crt_filter_create(SDL_GPUDevice *device, SDL_Window *window);
void       crt_filter_destroy(CRTFilter *filter);
void       crt_filter_set_mode(CRTFilter *filter, CRTFilterMode mode);
CRTFilterMode crt_filter_get_mode(CRTFilter *filter);

// Apply CRT effect: reads srcTex (srcW x srcH), outputs to dstTex at the
// letterbox rect [dstX, dstY, dstW, dstH].  The entire dstTex is cleared
// black first so black bars outside the rect are correct.
// Must be called with an active command buffer; no render pass may be open.
void crt_filter_apply(CRTFilter       *filter,
                      SDL_GPUCommandBuffer *cmd,
                      SDL_GPUTexture  *srcTex,
                      Uint32           srcW,
                      Uint32           srcH,
                      SDL_GPUTexture  *dstTex,
                      Uint32           dstX,
                      Uint32           dstY,
                      Uint32           dstW,
                      Uint32           dstH);

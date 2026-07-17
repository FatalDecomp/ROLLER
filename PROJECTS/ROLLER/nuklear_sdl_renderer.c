/*
 * SDL3 adaptation of Nuklear 4.13.2's public-domain
 * demo/sdl_renderer/nuklear_sdl_renderer.h backend.
 */
#include "nuklear_sdl_renderer.h"

#include <stddef.h>
#include <stdlib.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_ZERO_COMMAND_MEMORY
#include "nuklear.h"

typedef struct {
  float afPosition[2];
  float afUV[2];
  SDL_FColor color;
} tNuklearSDLVertex;

struct NuklearSDLRenderer {
  SDL_Renderer *pRenderer;
  SDL_Texture *pFontTexture;
  struct nk_buffer commands;
  struct nk_draw_null_texture nullTexture;
};

NuklearSDLRenderer *nuklear_sdl_renderer_create(SDL_Renderer *pRenderer)
{
  NuklearSDLRenderer *pBackend;

  if (!pRenderer)
    return NULL;

  pBackend = calloc(1, sizeof(*pBackend));
  if (!pBackend)
    return NULL;

  pBackend->pRenderer = pRenderer;
  nk_buffer_init_default(&pBackend->commands);
  return pBackend;
}

bool nuklear_sdl_renderer_finish_font_atlas(NuklearSDLRenderer *pBackend,
                                            struct nk_font_atlas *pAtlas,
                                            const void *pPixels,
                                            int iWidth, int iHeight)
{
  if (!pBackend || !pAtlas || !pPixels || iWidth <= 0 || iHeight <= 0)
    return false;

  pBackend->pFontTexture = SDL_CreateTexture(
      pBackend->pRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
      iWidth, iHeight);
  if (!pBackend->pFontTexture) {
    SDL_Log("debug_overlay: failed to create Nuklear font texture: %s",
            SDL_GetError());
    return false;
  }

  if (!SDL_UpdateTexture(pBackend->pFontTexture, NULL, pPixels, iWidth * 4) ||
      !SDL_SetTextureBlendMode(pBackend->pFontTexture, SDL_BLENDMODE_BLEND) ||
      !SDL_SetTextureScaleMode(pBackend->pFontTexture, SDL_SCALEMODE_LINEAR)) {
    SDL_Log("debug_overlay: failed to configure Nuklear font texture: %s",
            SDL_GetError());
    SDL_DestroyTexture(pBackend->pFontTexture);
    pBackend->pFontTexture = NULL;
    return false;
  }

  nk_font_atlas_end(pAtlas, nk_handle_ptr(pBackend->pFontTexture),
                    &pBackend->nullTexture);
  return true;
}

bool nuklear_sdl_renderer_render(NuklearSDLRenderer *pBackend,
                                 struct nk_context *pContext,
                                 int iLogicalWidth, int iLogicalHeight,
                                 int iOutputWidth, int iOutputHeight)
{
  static const struct nk_draw_vertex_layout_element aVertexLayout[] = {
    { NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
      NK_OFFSETOF(tNuklearSDLVertex, afPosition) },
    { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
      NK_OFFSETOF(tNuklearSDLVertex, afUV) },
    { NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT,
      NK_OFFSETOF(tNuklearSDLVertex, color) },
    { NK_VERTEX_LAYOUT_END }
  };
  struct nk_convert_config config;
  struct nk_buffer vertexBuffer;
  struct nk_buffer indexBuffer;
  const struct nk_draw_command *pCommand;
  const nk_draw_index *pIndexOffset;
  tNuklearSDLVertex *pVertices;
  SDL_Rect savedClip = {0};
  bool bClipEnabled;
  bool bSuccess = true;
  float fScale;
  float fOffsetX;
  float fOffsetY;
  int iVertexCount;

  if (!pBackend || !pContext || iLogicalWidth <= 0 || iLogicalHeight <= 0)
    return false;

  if ((iOutputWidth <= 0 || iOutputHeight <= 0) &&
      !SDL_GetCurrentRenderOutputSize(pBackend->pRenderer,
                                      &iOutputWidth, &iOutputHeight))
    return false;
  if (iOutputWidth <= 0 || iOutputHeight <= 0)
    return false;

  SDL_zero(config);
  config.vertex_layout = aVertexLayout;
  config.vertex_size = sizeof(tNuklearSDLVertex);
  config.vertex_alignment = NK_ALIGNOF(tNuklearSDLVertex);
  config.tex_null = pBackend->nullTexture;
  config.circle_segment_count = 22;
  config.curve_segment_count = 22;
  config.arc_segment_count = 22;
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_ON;
  config.line_AA = NK_ANTI_ALIASING_ON;

  nk_buffer_init_default(&vertexBuffer);
  nk_buffer_init_default(&indexBuffer);
  if (nk_convert(pContext, &pBackend->commands, &vertexBuffer, &indexBuffer,
                 &config) != NK_CONVERT_SUCCESS) {
    SDL_Log("debug_overlay: Nuklear command conversion failed");
    bSuccess = false;
    goto cleanup;
  }

  fScale = (float)iOutputWidth / (float)iLogicalWidth;
  if ((float)iLogicalHeight * fScale > (float)iOutputHeight)
    fScale = (float)iOutputHeight / (float)iLogicalHeight;
  fOffsetX = ((float)iOutputWidth - (float)iLogicalWidth * fScale) * 0.5f;
  fOffsetY = ((float)iOutputHeight - (float)iLogicalHeight * fScale) * 0.5f;

  pVertices = (tNuklearSDLVertex *)nk_buffer_memory(&vertexBuffer);
  iVertexCount = (int)(vertexBuffer.needed / sizeof(tNuklearSDLVertex));
  for (int iVertex = 0; iVertex < iVertexCount; ++iVertex) {
    pVertices[iVertex].afPosition[0] =
        fOffsetX + pVertices[iVertex].afPosition[0] * fScale;
    pVertices[iVertex].afPosition[1] =
        fOffsetY + pVertices[iVertex].afPosition[1] * fScale;
  }

  bClipEnabled = SDL_RenderClipEnabled(pBackend->pRenderer);
  if (!SDL_GetRenderClipRect(pBackend->pRenderer, &savedClip))
    SDL_zero(savedClip);

  pIndexOffset = (const nk_draw_index *)nk_buffer_memory_const(&indexBuffer);
  nk_draw_foreach(pCommand, pContext, &pBackend->commands) {
    SDL_Rect clip;

    if (!pCommand->elem_count)
      continue;

    clip.x = (int)(fOffsetX + pCommand->clip_rect.x * fScale);
    clip.y = (int)(fOffsetY + pCommand->clip_rect.y * fScale);
    clip.w = (int)(pCommand->clip_rect.w * fScale + 0.5f);
    clip.h = (int)(pCommand->clip_rect.h * fScale + 0.5f);
    if (clip.x < 0) {
      clip.w += clip.x;
      clip.x = 0;
    }
    if (clip.y < 0) {
      clip.h += clip.y;
      clip.y = 0;
    }
    if (clip.x + clip.w > iOutputWidth)
      clip.w = iOutputWidth - clip.x;
    if (clip.y + clip.h > iOutputHeight)
      clip.h = iOutputHeight - clip.y;

    if (clip.w > 0 && clip.h > 0) {
      SDL_SetRenderClipRect(pBackend->pRenderer, &clip);
      if (!SDL_RenderGeometryRaw(
              pBackend->pRenderer, (SDL_Texture *)pCommand->texture.ptr,
              pVertices[0].afPosition, sizeof(tNuklearSDLVertex),
              &pVertices[0].color, sizeof(tNuklearSDLVertex),
              pVertices[0].afUV, sizeof(tNuklearSDLVertex), iVertexCount,
              pIndexOffset, (int)pCommand->elem_count,
              sizeof(nk_draw_index))) {
        SDL_Log("debug_overlay: SDL_RenderGeometryRaw failed: %s",
                SDL_GetError());
        bSuccess = false;
      }
    }

    pIndexOffset += pCommand->elem_count;
  }

  if (bClipEnabled)
    SDL_SetRenderClipRect(pBackend->pRenderer, &savedClip);
  else
    SDL_SetRenderClipRect(pBackend->pRenderer, NULL);

cleanup:
  nk_clear(pContext);
  nk_buffer_clear(&pBackend->commands);
  nk_buffer_free(&vertexBuffer);
  nk_buffer_free(&indexBuffer);
  return bSuccess;
}

void nuklear_sdl_renderer_destroy(NuklearSDLRenderer *pBackend)
{
  if (!pBackend)
    return;

  SDL_DestroyTexture(pBackend->pFontTexture);
  nk_buffer_free(&pBackend->commands);
  free(pBackend);
}

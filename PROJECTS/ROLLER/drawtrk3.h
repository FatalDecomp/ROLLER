#ifndef _ROLLER_DRAWTRK3_H
#define _ROLLER_DRAWTRK3_H
//-------------------------------------------------------------------------------------------------
#include "polyf.h"
#include "types.h"
#include "3d.h"
#include "editor_surface.h"
//-------------------------------------------------------------------------------------------------

extern int showsub;
extern int view_limit;
extern int divtype;
extern int NextSect[MAX_TRACK_CHUNKS];
extern int tex_hgt;
extern int polyysize;
extern int polyxsize;
extern uint8 *subptr;
extern int fliptype;
extern int subpolytype;
extern tPolyParams *subpoly;
extern int tex_wid;
extern int flatpol;
extern tPolyParams RoadPoly;
extern int start_sect;
extern int gap_size;
extern int first_size;
extern int TrackSize;
extern int backwards;
extern int next_front;
extern int mid_sec;
extern int back_sec;
extern int front_sec;
extern int VisibleHumans;
extern int min_sub_size;
extern int NamesLeft;
extern int CarsLeft;
extern int VisibleCars;
extern int num_pols;
extern int small_poly;

//-------------------------------------------------------------------------------------------------

int CalcVisibleTrack(int iCarIdx, unsigned int uiViewMode);
#if defined(ROLLER_EDITOR_CORE)
int CalcVisibleTrackEditor(unsigned int uiViewMode);
#endif
void DrawTrack3(uint8 *pScrPtr, int iChaseCamIdx, int iCarIdx,
                const GameRenderCamera *camera,
                const GameRenderProjection *projection);
int facing_ok(float fX0, float fY0, float fZ0,
              float fX1, float fY1, float fZ1,
              float fX2, float fY2, float fZ2,
              float fX3, float fY3, float fZ3);
void set_starts(unsigned int uiType);
bool drawtrk3_editor_texture_atlas(uint32_t uiTextureSet,
                                   tEdTextureAtlas *pAtlas);
bool drawtrk3_init_editor_material_table(tEdMaterialTable *pTable,
                                         tEdMaterial *pStorage,
                                         uint32_t uiCapacity);
bool drawtrk3_emit_surface_to_renderer(
    GameRenderer *pRenderer,
    const GameRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT],
    const tEdSurfaceInfo *pInfo);
bool drawtrk3_emit_full_track(tEdMaterialTable *pMaterials,
                              tEdEmitSurfaceFn pfnEmit,
                              void *pUserData);
/*
 * E4A-S6. The scenery half of the canonical stream: every placed building and
 * advert panel, walked by building index with no camera, visible set, depth
 * sort, or backface cull, at the yaw the track file recorded rather than the
 * viewer-facing one the renderer uses for billboards. Surfaces E4A-S3
 * classifies ROLLER_ED_CONTENT_RUNTIME_SCENERY are not emitted, so an exporter
 * never sees one. Conventions: ADR 0005.
 */
bool drawtrk3_emit_full_scenery(tEdMaterialTable *pMaterials,
                                tEdEmitSurfaceFn pfnEmit,
                                void *pUserData);
void drawtrk3_editor_selection_set(uint32_t uiFirstChunkId,
                                   uint32_t uiLastChunkId,
                                   uint16_t unSurfaceClass,
                                   uint8_t byHighlightColour);
void drawtrk3_editor_selection_clear(void);
/* Syncs the renderer's selection from the facade overlay state; call once per
 * editor frame, before rendering. */
void drawtrk3_editor_apply_overlay_selection(void);
/* Draws the enabled E3A-S4 helper overlays. Editor render path only; the
 * helpers are not track content and never reach the canonical emitter. */
void drawtrk3_editor_draw_helpers(GameRenderer *pRenderer);

/*
 * E3A-S7. Draws the facade's reference mesh, if one is set and
 * SHOW_REFERENCE_MESH is on. It goes through the same world-quad path the
 * track uses, so the renderer depth-composes it against the scene rather than
 * pasting it over the top.
 */
void drawtrk3_editor_draw_reference_mesh(GameRenderer *pRenderer);

//-------------------------------------------------------------------------------------------------
#endif

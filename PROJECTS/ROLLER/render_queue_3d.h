#ifndef _ROLLER_RENDER_QUEUE_3D_H
#define _ROLLER_RENDER_QUEUE_3D_H
//-------------------------------------------------------------------------------------------------
#include "3d.h"
//-------------------------------------------------------------------------------------------------

#define RENDER_QUEUE_3D_CAPACITY 6500
#define RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY 14

// Only implemented gameplay-3D command kinds are named here. Legacy priorities stay on the
// temporary compatibility path until their renderers migrate to typed queue submission APIs.
typedef enum
{
  RENDER_COMMAND_3D_KIND_START_LIGHT = 0
} RenderCommand3DKind;

typedef struct
{
  tTrackZOrderEntry entries[RENDER_QUEUE_3D_CAPACITY];
  int count;
} RenderQueue3D;

//-------------------------------------------------------------------------------------------------

RenderQueue3D *render_queue_3d_global(void);
void render_queue_3d_clear(RenderQueue3D *pQueue);
tTrackZOrderEntry *render_queue_3d_entries(RenderQueue3D *pQueue);
int render_queue_3d_count(const RenderQueue3D *pQueue);
void render_queue_3d_set_legacy_count(RenderQueue3D *pQueue, int iCount);

// Temporary compatibility API for unmigrated legacy render priorities. Priority 14 is reserved for
// start lights and must use render_queue_3d_add_start_light().
tTrackZOrderEntry *render_queue_3d_add_legacy_priority(RenderQueue3D *pQueue,
                                                       int iLegacyPriority,
                                                       int iChunkIdx,
                                                       float fZDepth);

tTrackZOrderEntry *render_queue_3d_add_start_light(RenderQueue3D *pQueue,
                                                   int iLightIdx,
                                                   float fZDepth);

int render_queue_3d_compare_legacy_z_order(const void *pCommand1, const void *pCommand2);
void render_queue_3d_sort(RenderQueue3D *pQueue);

//-------------------------------------------------------------------------------------------------
#endif

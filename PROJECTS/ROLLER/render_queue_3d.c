#include "render_queue_3d.h"
#include <stdlib.h>
//-------------------------------------------------------------------------------------------------

static RenderQueue3D g_RenderQueue3D;

//-------------------------------------------------------------------------------------------------

RenderQueue3D *render_queue_3d_global(void)
{
  return &g_RenderQueue3D;
}

//-------------------------------------------------------------------------------------------------

void render_queue_3d_clear(RenderQueue3D *pQueue)
{
  pQueue->count = 0;
}

//-------------------------------------------------------------------------------------------------

tTrackZOrderEntry *render_queue_3d_entries(RenderQueue3D *pQueue)
{
  return pQueue->entries;
}

//-------------------------------------------------------------------------------------------------

int render_queue_3d_count(const RenderQueue3D *pQueue)
{
  return pQueue->count;
}

//-------------------------------------------------------------------------------------------------

void render_queue_3d_set_legacy_count(RenderQueue3D *pQueue, int iCount)
{
  pQueue->count = iCount;
}

//-------------------------------------------------------------------------------------------------

static tTrackZOrderEntry *render_queue_3d_add(RenderQueue3D *pQueue,
                                              int iLegacyPriority,
                                              int iChunkIdx,
                                              float fZDepth)
{
  tTrackZOrderEntry *pEntry;

  if (pQueue->count >= RENDER_QUEUE_3D_CAPACITY)
    return NULL;

  pEntry = &pQueue->entries[pQueue->count];
  pEntry->nRenderPriority = (int16)iLegacyPriority;
  pEntry->nChunkIdx = (int16)iChunkIdx;
  pEntry->fZDepth = fZDepth;
  ++pQueue->count;

  return pEntry;
}

//-------------------------------------------------------------------------------------------------

tTrackZOrderEntry *render_queue_3d_add_legacy_priority(RenderQueue3D *pQueue,
                                                       int iLegacyPriority,
                                                       int iChunkIdx,
                                                       float fZDepth)
{
  switch (iLegacyPriority) {
  case RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY:
  case RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY:
    return NULL;
  }

  return render_queue_3d_add(pQueue, iLegacyPriority, iChunkIdx, fZDepth);
}

//-------------------------------------------------------------------------------------------------

tTrackZOrderEntry *render_queue_3d_add_building(RenderQueue3D *pQueue,
                                                int iBuildingIdx,
                                                float fZDepth)
{
  (void)RENDER_COMMAND_3D_KIND_BUILDING;
  return render_queue_3d_add(pQueue, RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY, iBuildingIdx, fZDepth);
}

//-------------------------------------------------------------------------------------------------

tTrackZOrderEntry *render_queue_3d_add_start_light(RenderQueue3D *pQueue,
                                                   int iLightIdx,
                                                   float fZDepth)
{
  (void)RENDER_COMMAND_3D_KIND_START_LIGHT;
  return render_queue_3d_add(pQueue, RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY, iLightIdx, fZDepth);
}

//-------------------------------------------------------------------------------------------------

int render_queue_3d_compare_legacy_z_order(const void *pCommand1, const void *pCommand2)
{
  const tTrackZOrderEntry *pTrackZ1 = (const tTrackZOrderEntry *)pCommand1;
  const tTrackZOrderEntry *pTrackZ2 = (const tTrackZOrderEntry *)pCommand2;
  const float fZCmp1 = pTrackZ1->fZDepth;
  const float fZCmp2 = pTrackZ2->fZDepth;
  const int iRenderPriorityCmp1 = pTrackZ1->nRenderPriority;
  const int iRenderPriorityCmp2 = pTrackZ2->nRenderPriority;

  if (fZCmp1 < (double)fZCmp2)
    return -1;
  if (fZCmp1 == fZCmp2) {
    if (iRenderPriorityCmp1 == iRenderPriorityCmp2)
      return 0;
    if (iRenderPriorityCmp1 >= iRenderPriorityCmp2)
      return -1;
  }
  return 1;
}

//-------------------------------------------------------------------------------------------------

void render_queue_3d_sort(RenderQueue3D *pQueue)
{
  qsort(pQueue->entries, pQueue->count, sizeof(pQueue->entries[0]), render_queue_3d_compare_legacy_z_order);
}

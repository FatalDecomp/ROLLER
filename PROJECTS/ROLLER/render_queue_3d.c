#include "render_queue_3d.h"
#include <stdlib.h>
//-------------------------------------------------------------------------------------------------

static RenderQueue3D g_RenderQueue3D;

typedef struct
{
  tTrackZOrderEntry entry;
  RenderCommand3D command;
  int has_typed_command;
} RenderQueue3DSortEntry;

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

const RenderCommand3D *render_queue_3d_command_at(const RenderQueue3D *pQueue, int iIndex)
{
  if (iIndex < 0 || iIndex >= pQueue->count)
    return NULL;
  if (!pQueue->has_typed_command[iIndex])
    return NULL;
  return &pQueue->commands[iIndex];
}

//-------------------------------------------------------------------------------------------------

int render_queue_3d_count(const RenderQueue3D *pQueue)
{
  return pQueue->count;
}

//-------------------------------------------------------------------------------------------------

void render_queue_3d_set_legacy_count(RenderQueue3D *pQueue, int iCount)
{
  int iCommandIndex;

  if (iCount < 0)
    iCount = 0;
  if (iCount > RENDER_QUEUE_3D_CAPACITY)
    iCount = RENDER_QUEUE_3D_CAPACITY;

  for (iCommandIndex = 0; iCommandIndex < iCount; ++iCommandIndex)
    pQueue->has_typed_command[iCommandIndex] = 0;

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
  pQueue->has_typed_command[pQueue->count] = 0;
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
  case RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY:
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
  tTrackZOrderEntry *pEntry = render_queue_3d_add(pQueue, RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY, iBuildingIdx, fZDepth);
  if (pEntry != NULL) {
    RenderCommand3D *pCommand = &pQueue->commands[pQueue->count - 1];
    pCommand->kind = RENDER_COMMAND_3D_KIND_BUILDING;
    pCommand->payload.building.building_idx = iBuildingIdx;
    pCommand->payload.building.depth = fZDepth;
    pQueue->has_typed_command[pQueue->count - 1] = 1;
  }
  return pEntry;
}

tTrackZOrderEntry *render_queue_3d_add_car(RenderQueue3D *pQueue,
                                           int iCarIdx,
                                           float fZDepth,
                                           const GameRenderCarPose *pPose,
                                           const GameRenderCarOptions *pOptions)
{
  tTrackZOrderEntry *pEntry = render_queue_3d_add(pQueue, RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY, iCarIdx, fZDepth);
  if (pEntry != NULL) {
    RenderCommand3D *pCommand = &pQueue->commands[pQueue->count - 1];
    pCommand->kind = RENDER_COMMAND_3D_KIND_CAR;
    pCommand->payload.car.car_idx = iCarIdx;
    pCommand->payload.car.depth = fZDepth;
    if (pPose != NULL) {
      pCommand->payload.car.pose = *pPose;
    } else {
      pCommand->payload.car.pose.position.fX = 0.0f;
      pCommand->payload.car.pose.position.fY = 0.0f;
      pCommand->payload.car.pose.position.fZ = 0.0f;
      pCommand->payload.car.pose.yaw = 0;
      pCommand->payload.car.pose.pitch = 0;
      pCommand->payload.car.pose.roll = 0;
    }
    if (pOptions != NULL) {
      pCommand->payload.car.options = *pOptions;
    } else {
      pCommand->payload.car.options.anim_frame = 0;
      pCommand->payload.car.options.color_remap = NULL;
    }
    pQueue->has_typed_command[pQueue->count - 1] = 1;
  }
  return pEntry;
}

//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------

tTrackZOrderEntry *render_queue_3d_add_start_light(RenderQueue3D *pQueue,
                                                   int iLightIdx,
                                                   float fZDepth)
{
  tTrackZOrderEntry *pEntry = render_queue_3d_add(pQueue, RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY, iLightIdx, fZDepth);
  if (pEntry != NULL) {
    RenderCommand3D *pCommand = &pQueue->commands[pQueue->count - 1];
    pCommand->kind = RENDER_COMMAND_3D_KIND_START_LIGHT;
    pCommand->payload.start_light.light_idx = iLightIdx;
    pCommand->payload.start_light.depth = fZDepth;
    pQueue->has_typed_command[pQueue->count - 1] = 1;
  }
  return pEntry;
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

static int render_queue_3d_compare_sort_entries(const void *pCommand1, const void *pCommand2)
{
  const RenderQueue3DSortEntry *pSortEntry1 = (const RenderQueue3DSortEntry *)pCommand1;
  const RenderQueue3DSortEntry *pSortEntry2 = (const RenderQueue3DSortEntry *)pCommand2;
  return render_queue_3d_compare_legacy_z_order(&pSortEntry1->entry, &pSortEntry2->entry);
}

//-------------------------------------------------------------------------------------------------

void render_queue_3d_sort(RenderQueue3D *pQueue)
{
  int iCommandIndex;
  RenderQueue3DSortEntry *pSortEntries;

  if (pQueue->count <= 1)
    return;

  pSortEntries = (RenderQueue3DSortEntry *)malloc(sizeof(*pSortEntries) * pQueue->count);
  if (pSortEntries == NULL) {
    qsort(pQueue->entries, pQueue->count, sizeof(pQueue->entries[0]), render_queue_3d_compare_legacy_z_order);
    for (iCommandIndex = 0; iCommandIndex < pQueue->count; ++iCommandIndex)
      pQueue->has_typed_command[iCommandIndex] = 0;
    return;
  }

  for (iCommandIndex = 0; iCommandIndex < pQueue->count; ++iCommandIndex) {
    pSortEntries[iCommandIndex].entry = pQueue->entries[iCommandIndex];
    pSortEntries[iCommandIndex].command = pQueue->commands[iCommandIndex];
    pSortEntries[iCommandIndex].has_typed_command = pQueue->has_typed_command[iCommandIndex];
  }

  qsort(pSortEntries, pQueue->count, sizeof(pSortEntries[0]), render_queue_3d_compare_sort_entries);

  for (iCommandIndex = 0; iCommandIndex < pQueue->count; ++iCommandIndex) {
    pQueue->entries[iCommandIndex] = pSortEntries[iCommandIndex].entry;
    pQueue->commands[iCommandIndex] = pSortEntries[iCommandIndex].command;
    pQueue->has_typed_command[iCommandIndex] = pSortEntries[iCommandIndex].has_typed_command;
  }

  free(pSortEntries);
}

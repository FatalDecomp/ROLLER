#ifndef _ROLLER_RENDER_QUEUE_3D_H
#define _ROLLER_RENDER_QUEUE_3D_H
//-------------------------------------------------------------------------------------------------
#include "3d.h"
//-------------------------------------------------------------------------------------------------

#define RENDER_QUEUE_3D_CAPACITY 6500
#define RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY 11
#define RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY 13
#define RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY 14

// Only implemented gameplay-3D command kinds are named here. Legacy priorities stay on the
// temporary compatibility path until their renderers migrate to typed queue submission APIs.
typedef enum
{
  RENDER_COMMAND_3D_KIND_BUILDING = 0,
  RENDER_COMMAND_3D_KIND_CAR,
  RENDER_COMMAND_3D_KIND_START_LIGHT
} RenderCommand3DKind;

typedef struct
{
  int building_idx;
  float depth;
} RenderCommand3DBuilding;

typedef struct
{
  int car_idx;
  float depth;
  GameRenderCarPose pose;
  GameRenderCarOptions options;
} RenderCommand3DCar;

typedef struct
{
  int light_idx;
  float depth;
} RenderCommand3DStartLight;

typedef struct
{
  RenderCommand3DKind kind;
  union
  {
    RenderCommand3DBuilding building;
    RenderCommand3DCar car;
    RenderCommand3DStartLight start_light;
  } payload;
} RenderCommand3D;


typedef struct
{
  tTrackZOrderEntry entries[RENDER_QUEUE_3D_CAPACITY];
  RenderCommand3D commands[RENDER_QUEUE_3D_CAPACITY];
  int has_typed_command[RENDER_QUEUE_3D_CAPACITY];
  int count;
} RenderQueue3D;

//-------------------------------------------------------------------------------------------------

RenderQueue3D *render_queue_3d_global(void);
void render_queue_3d_clear(RenderQueue3D *pQueue);
tTrackZOrderEntry *render_queue_3d_entries(RenderQueue3D *pQueue);
const RenderCommand3D *render_queue_3d_command_at(const RenderQueue3D *pQueue, int iIndex);
int render_queue_3d_count(const RenderQueue3D *pQueue);
void render_queue_3d_set_legacy_count(RenderQueue3D *pQueue, int iCount);

// Temporary compatibility API for unmigrated legacy render priorities. Priorities with named
// command APIs must use those APIs instead.
tTrackZOrderEntry *render_queue_3d_add_legacy_priority(RenderQueue3D *pQueue,
                                                       int iLegacyPriority,
                                                       int iChunkIdx,
                                                       float fZDepth);

tTrackZOrderEntry *render_queue_3d_add_car(RenderQueue3D *pQueue,
                                           int iCarIdx,
                                           float fZDepth,
                                           const GameRenderCarPose *pPose,
                                           const GameRenderCarOptions *pOptions);

tTrackZOrderEntry *render_queue_3d_add_building(RenderQueue3D *pQueue,
                                                int iBuildingIdx,
                                                float fZDepth);


tTrackZOrderEntry *render_queue_3d_add_start_light(RenderQueue3D *pQueue,
                                                   int iLightIdx,
                                                   float fZDepth);

int render_queue_3d_compare_legacy_z_order(const void *pCommand1, const void *pCommand2);
void render_queue_3d_sort(RenderQueue3D *pQueue);

//-------------------------------------------------------------------------------------------------
#endif

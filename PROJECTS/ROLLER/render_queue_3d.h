#ifndef _ROLLER_RENDER_QUEUE_3D_H
#define _ROLLER_RENDER_QUEUE_3D_H
//-------------------------------------------------------------------------------------------------
#include "3d.h"
//-------------------------------------------------------------------------------------------------

#define RENDER_QUEUE_3D_CAPACITY 6500
#define RENDER_QUEUE_3D_LEFT_WALL_LEGACY_PRIORITY 0
#define RENDER_QUEUE_3D_RIGHT_WALL_LEGACY_PRIORITY 1
#define RENDER_QUEUE_3D_GROUND_LEGACY_PRIORITY 2
#define RENDER_QUEUE_3D_LEFT_LOWER_WALL_LEGACY_PRIORITY 3
#define RENDER_QUEUE_3D_RIGHT_LOWER_WALL_LEGACY_PRIORITY 4
#define RENDER_QUEUE_3D_ROAD_CENTER_LEGACY_PRIORITY 5
#define RENDER_QUEUE_3D_LEFT_LANE_LEGACY_PRIORITY 6
#define RENDER_QUEUE_3D_RIGHT_LANE_LEGACY_PRIORITY 7
#define RENDER_QUEUE_3D_LEFT_HIGH_WALL_LEGACY_PRIORITY 8
#define RENDER_QUEUE_3D_RIGHT_HIGH_WALL_LEGACY_PRIORITY 9
#define RENDER_QUEUE_3D_ROOF_LEGACY_PRIORITY 10
#define RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY 11
#define RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY 13
#define RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY 14

// Only implemented gameplay-3D command kinds are named here. Gameplay 3D submissions use
// typed queue APIs so raw legacy priority writes cannot leak back into drawtrk3.c.
typedef enum
{
  RENDER_COMMAND_3D_KIND_BUILDING = 0,
  RENDER_COMMAND_3D_KIND_CAR,
  RENDER_COMMAND_3D_KIND_GROUND_SURFACE,
  RENDER_COMMAND_3D_KIND_LOWER_WALL_SURFACE,
  RENDER_COMMAND_3D_KIND_ROAD_SURFACE,
  RENDER_COMMAND_3D_KIND_ROOF_SURFACE,
  RENDER_COMMAND_3D_KIND_START_LIGHT,
  RENDER_COMMAND_3D_KIND_WALL_SURFACE
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
  int section_idx;
  float depth;
} RenderCommand3DGroundSurface;

typedef enum
{
  RENDER_COMMAND_3D_WALL_SURFACE_LEFT = 0,
  RENDER_COMMAND_3D_WALL_SURFACE_RIGHT
} RenderCommand3DWallSurfaceSide;

typedef enum
{
  RENDER_COMMAND_3D_WALL_SURFACE_BASIC = 0,
  RENDER_COMMAND_3D_WALL_SURFACE_HIGH
} RenderCommand3DWallSurfaceVariant;

typedef struct
{
  int section_idx;
  float depth;
  RenderCommand3DWallSurfaceSide side;
  RenderCommand3DWallSurfaceVariant variant;
} RenderCommand3DWallSurface;

typedef struct
{
  int section_idx;
  float depth;
  RenderCommand3DWallSurfaceSide side;
} RenderCommand3DLowerWallSurface;

typedef enum
{
  RENDER_COMMAND_3D_ROAD_SURFACE_CENTER = 0,
  RENDER_COMMAND_3D_ROAD_SURFACE_LEFT_LANE,
  RENDER_COMMAND_3D_ROAD_SURFACE_RIGHT_LANE
} RenderCommand3DRoadSurfaceKind;

typedef struct
{
  int section_idx;
  float depth;
  RenderCommand3DRoadSurfaceKind surface_kind;
} RenderCommand3DRoadSurface;

typedef enum
{
  RENDER_COMMAND_3D_ROOF_SURFACE_NEXT_SECTION = 0,
  RENDER_COMMAND_3D_ROOF_SURFACE_CURRENT_SECTION
} RenderCommand3DRoofSurfaceVariant;

typedef struct
{
  int section_idx;
  float depth;
  RenderCommand3DRoofSurfaceVariant variant;
} RenderCommand3DRoofSurface;

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
    RenderCommand3DGroundSurface ground_surface;
    RenderCommand3DLowerWallSurface lower_wall_surface;
    RenderCommand3DRoadSurface road_surface;
    RenderCommand3DRoofSurface roof_surface;
    RenderCommand3DStartLight start_light;
    RenderCommand3DWallSurface wall_surface;
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

void render_queue_3d_add_ground(RenderQueue3D *pQueue,
                                 int iSectionIdx,
                                 float fZDepth);

void render_queue_3d_add_next_section_roof(RenderQueue3D *pQueue,
                                            int iSectionIdx,
                                            float fZDepth);

void render_queue_3d_add_current_section_roof(RenderQueue3D *pQueue,
                                               int iSectionIdx,
                                               float fZDepth);

void render_queue_3d_add_left_wall(RenderQueue3D *pQueue,
                                    int iSectionIdx,
                                    float fZDepth);

void render_queue_3d_add_right_wall(RenderQueue3D *pQueue,
                                     int iSectionIdx,
                                     float fZDepth);

void render_queue_3d_add_left_high_wall(RenderQueue3D *pQueue,
                                         int iSectionIdx,
                                         float fZDepth);

void render_queue_3d_add_right_high_wall(RenderQueue3D *pQueue,
                                          int iSectionIdx,
                                          float fZDepth);

void render_queue_3d_add_left_lower_wall(RenderQueue3D *pQueue,
                                          int iSectionIdx,
                                          float fZDepth);

void render_queue_3d_add_right_lower_wall(RenderQueue3D *pQueue,
                                           int iSectionIdx,
                                           float fZDepth);

void render_queue_3d_add_road_center(RenderQueue3D *pQueue,
                                      int iSectionIdx,
                                      float fZDepth);

void render_queue_3d_add_left_lane(RenderQueue3D *pQueue,
                                    int iSectionIdx,
                                    float fZDepth);

void render_queue_3d_add_right_lane(RenderQueue3D *pQueue,
                                     int iSectionIdx,
                                     float fZDepth);

void render_queue_3d_add_car(RenderQueue3D *pQueue,
                              int iCarIdx,
                              float fZDepth,
                              const GameRenderCarPose *pPose,
                              const GameRenderCarOptions *pOptions);

void render_queue_3d_add_building(RenderQueue3D *pQueue,
                                   int iBuildingIdx,
                                   float fZDepth);


void render_queue_3d_add_start_light(RenderQueue3D *pQueue,
                                      int iLightIdx,
                                      float fZDepth);

int render_queue_3d_compare_legacy_z_order(const void *pCommand1, const void *pCommand2);
void render_queue_3d_sort(RenderQueue3D *pQueue);

//-------------------------------------------------------------------------------------------------
#endif

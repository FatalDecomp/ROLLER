#include "render_queue_3d.h"
#include <assert.h>
#include <stddef.h>

static void test_sort_matches_legacy_zcmp(void)
{
  RenderQueue3D queue;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_road_center(&queue, 100, 10.0f);
  render_queue_3d_add_unmigrated_legacy_priority(&queue, 2, 101, 4.0f);
  render_queue_3d_add_building(&queue, 102, 10.0f);
  render_queue_3d_add_left_lane(&queue, 103, 10.0f);

  render_queue_3d_sort(&queue);

  assert(render_queue_3d_count(&queue) == 4);
  assert(queue.entries[0].nChunkIdx == 101);
  assert(queue.entries[1].nChunkIdx == 102);
  assert(queue.entries[2].nChunkIdx == 103);
  assert(queue.entries[3].nChunkIdx == 100);
}

static void test_road_lane_priority_mapping_payloads(void)
{
  RenderQueue3D queue;
  const RenderCommand3D *command;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_road_center(&queue, 17, 123.0f);
  render_queue_3d_add_left_lane(&queue, 18, 124.0f);
  render_queue_3d_add_right_lane(&queue, 19, 125.0f);

  assert(render_queue_3d_count(&queue) == 3);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_ROAD_CENTER_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 17);
  assert(queue.entries[0].fZDepth == 123.0f);
  command = render_queue_3d_command_at(&queue, 0);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_ROAD_SURFACE);
  assert(command->payload.road_surface.surface_kind == RENDER_COMMAND_3D_ROAD_SURFACE_CENTER);
  assert(command->payload.road_surface.section_idx == 17);
  assert(command->payload.road_surface.depth == 123.0f);

  assert(queue.entries[1].nRenderPriority == RENDER_QUEUE_3D_LEFT_LANE_LEGACY_PRIORITY);
  assert(queue.entries[1].nChunkIdx == 18);
  assert(queue.entries[1].fZDepth == 124.0f);
  command = render_queue_3d_command_at(&queue, 1);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_ROAD_SURFACE);
  assert(command->payload.road_surface.surface_kind == RENDER_COMMAND_3D_ROAD_SURFACE_LEFT_LANE);
  assert(command->payload.road_surface.section_idx == 18);
  assert(command->payload.road_surface.depth == 124.0f);

  assert(queue.entries[2].nRenderPriority == RENDER_QUEUE_3D_RIGHT_LANE_LEGACY_PRIORITY);
  assert(queue.entries[2].nChunkIdx == 19);
  assert(queue.entries[2].fZDepth == 125.0f);
  command = render_queue_3d_command_at(&queue, 2);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_ROAD_SURFACE);
  assert(command->payload.road_surface.surface_kind == RENDER_COMMAND_3D_ROAD_SURFACE_RIGHT_LANE);
  assert(command->payload.road_surface.section_idx == 19);
  assert(command->payload.road_surface.depth == 125.0f);
}


static void test_wall_priority_mapping_payloads(void)
{
  RenderQueue3D queue;
  const RenderCommand3D *command;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_left_wall(&queue, 20, 200.0f);
  render_queue_3d_add_right_wall(&queue, 21, 201.0f);
  render_queue_3d_add_left_high_wall(&queue, 22, 202.0f);
  render_queue_3d_add_right_high_wall(&queue, 23, 203.0f);

  assert(render_queue_3d_count(&queue) == 4);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_LEFT_WALL_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 20);
  assert(queue.entries[0].fZDepth == 200.0f);
  command = render_queue_3d_command_at(&queue, 0);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_WALL_SURFACE);
  assert(command->payload.wall_surface.section_idx == 20);
  assert(command->payload.wall_surface.depth == 200.0f);
  assert(command->payload.wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_LEFT);
  assert(command->payload.wall_surface.variant == RENDER_COMMAND_3D_WALL_SURFACE_BASIC);

  assert(queue.entries[1].nRenderPriority == RENDER_QUEUE_3D_RIGHT_WALL_LEGACY_PRIORITY);
  assert(queue.entries[1].nChunkIdx == 21);
  assert(queue.entries[1].fZDepth == 201.0f);
  command = render_queue_3d_command_at(&queue, 1);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_WALL_SURFACE);
  assert(command->payload.wall_surface.section_idx == 21);
  assert(command->payload.wall_surface.depth == 201.0f);
  assert(command->payload.wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_RIGHT);
  assert(command->payload.wall_surface.variant == RENDER_COMMAND_3D_WALL_SURFACE_BASIC);

  assert(queue.entries[2].nRenderPriority == RENDER_QUEUE_3D_LEFT_HIGH_WALL_LEGACY_PRIORITY);
  assert(queue.entries[2].nChunkIdx == 22);
  assert(queue.entries[2].fZDepth == 202.0f);
  command = render_queue_3d_command_at(&queue, 2);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_WALL_SURFACE);
  assert(command->payload.wall_surface.section_idx == 22);
  assert(command->payload.wall_surface.depth == 202.0f);
  assert(command->payload.wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_LEFT);
  assert(command->payload.wall_surface.variant == RENDER_COMMAND_3D_WALL_SURFACE_HIGH);

  assert(queue.entries[3].nRenderPriority == RENDER_QUEUE_3D_RIGHT_HIGH_WALL_LEGACY_PRIORITY);
  assert(queue.entries[3].nChunkIdx == 23);
  assert(queue.entries[3].fZDepth == 203.0f);
  command = render_queue_3d_command_at(&queue, 3);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_WALL_SURFACE);
  assert(command->payload.wall_surface.section_idx == 23);
  assert(command->payload.wall_surface.depth == 203.0f);
  assert(command->payload.wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_RIGHT);
  assert(command->payload.wall_surface.variant == RENDER_COMMAND_3D_WALL_SURFACE_HIGH);
}

static void test_lower_wall_priority_mapping_payloads(void)
{
  RenderQueue3D queue;
  const RenderCommand3D *command;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_left_lower_wall(&queue, 30, 300.0f);
  render_queue_3d_add_right_lower_wall(&queue, 31, 301.0f);

  assert(render_queue_3d_count(&queue) == 2);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_LEFT_LOWER_WALL_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 30);
  assert(queue.entries[0].fZDepth == 300.0f);
  command = render_queue_3d_command_at(&queue, 0);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_LOWER_WALL_SURFACE);
  assert(command->payload.lower_wall_surface.section_idx == 30);
  assert(command->payload.lower_wall_surface.depth == 300.0f);
  assert(command->payload.lower_wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_LEFT);

  assert(queue.entries[1].nRenderPriority == RENDER_QUEUE_3D_RIGHT_LOWER_WALL_LEGACY_PRIORITY);
  assert(queue.entries[1].nChunkIdx == 31);
  assert(queue.entries[1].fZDepth == 301.0f);
  command = render_queue_3d_command_at(&queue, 1);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_LOWER_WALL_SURFACE);
  assert(command->payload.lower_wall_surface.section_idx == 31);
  assert(command->payload.lower_wall_surface.depth == 301.0f);
  assert(command->payload.lower_wall_surface.side == RENDER_COMMAND_3D_WALL_SURFACE_RIGHT);
}

static void test_building_priority_mapping(void)
{
  RenderQueue3D queue;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_building(&queue, 17, 123.0f);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 17);
  assert(queue.entries[0].fZDepth == 123.0f);
  assert(render_queue_3d_count(&queue) == 1);
}

static void test_start_light_priority_mapping(void)
{
  RenderQueue3D queue;
  render_queue_3d_clear(&queue);

  render_queue_3d_add_start_light(&queue, 2, 42.0f);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 2);
  assert(queue.entries[0].fZDepth == 42.0f);
  assert(render_queue_3d_count(&queue) == 1);
}

static void test_car_priority_mapping_payload(void)
{
  RenderQueue3D queue;
  const RenderCommand3D *command;
  const uint8 color_remap[2] = {3, 9};
  const GameRenderCarPose pose = {
    .position = {10.0f, 20.0f, 30.0f},
    .yaw = 100,
    .pitch = 200,
    .roll = 300,
  };
  const GameRenderCarOptions options = {
    .anim_frame = 7,
    .color_remap = color_remap,
  };
  render_queue_3d_clear(&queue);

  render_queue_3d_add_car(&queue, 5, 321.0f, &pose, &options);

  assert(queue.entries[0].nRenderPriority == RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY);
  assert(queue.entries[0].nChunkIdx == 5);
  assert(queue.entries[0].fZDepth == 321.0f);
  assert(render_queue_3d_count(&queue) == 1);

  command = render_queue_3d_command_at(&queue, 0);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_CAR);
  assert(command->payload.car.car_idx == 5);
  assert(command->payload.car.depth == 321.0f);
  assert(command->payload.car.pose.position.fX == 10.0f);
  assert(command->payload.car.pose.position.fY == 20.0f);
  assert(command->payload.car.pose.position.fZ == 30.0f);
  assert(command->payload.car.pose.yaw == 100);
  assert(command->payload.car.pose.pitch == 200);
  assert(command->payload.car.pose.roll == 300);
  assert(command->payload.car.options.anim_frame == 7);
  assert(command->payload.car.options.color_remap == color_remap);
}

static void test_sort_keeps_typed_payloads_with_sorted_entries(void)
{
  RenderQueue3D queue;
  const RenderCommand3D *command;
  const GameRenderCarPose pose = {
    .position = {1.0f, 2.0f, 3.0f},
    .yaw = 4,
    .pitch = 5,
    .roll = 6,
  };
  const GameRenderCarOptions options = {
    .anim_frame = 8,
    .color_remap = NULL,
  };
  render_queue_3d_clear(&queue);

  render_queue_3d_add_car(&queue, 4, 20.0f, &pose, &options);
  render_queue_3d_add_unmigrated_legacy_priority(&queue, 2, 101, 10.0f);
  render_queue_3d_add_right_lane(&queue, 77, 30.0f);

  render_queue_3d_sort(&queue);

  assert(render_queue_3d_count(&queue) == 3);
  assert(queue.entries[0].nChunkIdx == 101);
  assert(render_queue_3d_command_at(&queue, 0) == NULL);
  assert(queue.entries[1].nRenderPriority == RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY);
  assert(queue.entries[1].nChunkIdx == 4);
  command = render_queue_3d_command_at(&queue, 1);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_CAR);
  assert(command->payload.car.car_idx == 4);
  assert(command->payload.car.depth == 20.0f);
  assert(command->payload.car.pose.roll == 6);
  assert(command->payload.car.options.anim_frame == 8);
  assert(queue.entries[2].nRenderPriority == RENDER_QUEUE_3D_RIGHT_LANE_LEGACY_PRIORITY);
  assert(queue.entries[2].nChunkIdx == 77);
  command = render_queue_3d_command_at(&queue, 2);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_ROAD_SURFACE);
  assert(command->payload.road_surface.surface_kind == RENDER_COMMAND_3D_ROAD_SURFACE_RIGHT_LANE);
  assert(command->payload.road_surface.section_idx == 77);
  assert(command->payload.road_surface.depth == 30.0f);
}

int main(int argc, const char **argv, const char **envp)
{
  (void)argc;
  (void)argv;
  (void)envp;
  test_sort_matches_legacy_zcmp();
  test_road_lane_priority_mapping_payloads();
  test_wall_priority_mapping_payloads();
  test_lower_wall_priority_mapping_payloads();
  test_building_priority_mapping();
  test_start_light_priority_mapping();
  test_car_priority_mapping_payload();
  test_sort_keeps_typed_payloads_with_sorted_entries();
  return 0;
}

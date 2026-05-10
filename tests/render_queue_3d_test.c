#include "render_queue_3d.h"
#include <assert.h>
#include <stddef.h>

static void test_sort_matches_legacy_zcmp(void) {
  RenderQueue3D queue;
  render_queue_3d_clear(&queue);

  assert(render_queue_3d_add_legacy_priority(&queue, 5, 100, 10.0f) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 2, 101, 4.0f) != NULL);
  assert(render_queue_3d_add_building(&queue, 102, 10.0f) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 6, 103, 10.0f) != NULL);

  render_queue_3d_sort(&queue);

  assert(render_queue_3d_count(&queue) == 4);
  assert(queue.entries[0].nChunkIdx == 101);
  assert(queue.entries[1].nChunkIdx == 102);
  assert(queue.entries[2].nChunkIdx == 103);
  assert(queue.entries[3].nChunkIdx == 100);
}

static void test_building_priority_mapping_and_legacy_guard(void) {
  RenderQueue3D queue;
  tTrackZOrderEntry *entry;
  render_queue_3d_clear(&queue);

  entry = render_queue_3d_add_building(&queue, 17, 123.0f);
  assert(entry != NULL);
  assert(entry->nRenderPriority == RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY);
  assert(entry->nChunkIdx == 17);
  assert(entry->fZDepth == 123.0f);
  assert(render_queue_3d_count(&queue) == 1);

  assert(render_queue_3d_add_legacy_priority(
             &queue, RENDER_QUEUE_3D_BUILDING_LEGACY_PRIORITY, 7, 123.0f) ==
         NULL);
  assert(render_queue_3d_count(&queue) == 1);
}

static void test_start_light_priority_mapping_and_legacy_guard(void) {
  RenderQueue3D queue;
  tTrackZOrderEntry *entry;
  render_queue_3d_clear(&queue);

  entry = render_queue_3d_add_start_light(&queue, 2, 42.0f);
  assert(entry != NULL);
  assert(entry->nRenderPriority == RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY);
  assert(entry->nChunkIdx == 2);
  assert(entry->fZDepth == 42.0f);
  assert(render_queue_3d_count(&queue) == 1);

  assert(render_queue_3d_add_legacy_priority(
             &queue, RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY, 7, 42.0f) ==
         NULL);
  assert(render_queue_3d_count(&queue) == 1);
}

static void test_car_priority_mapping_payload_and_legacy_guard(void) {
  RenderQueue3D queue;
  tTrackZOrderEntry *entry;
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

  entry = render_queue_3d_add_car(&queue, 5, 321.0f, &pose, &options);
  assert(entry != NULL);
  assert(entry->nRenderPriority == RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY);
  assert(entry->nChunkIdx == 5);
  assert(entry->fZDepth == 321.0f);
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

  assert(render_queue_3d_add_legacy_priority(
             &queue, RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY, 7, 321.0f) == NULL);
  assert(render_queue_3d_count(&queue) == 1);
}

static void test_sort_keeps_typed_car_payload_with_sorted_entry(void) {
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

  assert(render_queue_3d_add_car(&queue, 4, 20.0f, &pose, &options) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 2, 101, 10.0f) != NULL);

  render_queue_3d_sort(&queue);

  assert(render_queue_3d_count(&queue) == 2);
  assert(queue.entries[0].nChunkIdx == 101);
  assert(render_queue_3d_command_at(&queue, 0) == NULL);
  assert(queue.entries[1].nRenderPriority ==
         RENDER_QUEUE_3D_CAR_LEGACY_PRIORITY);
  assert(queue.entries[1].nChunkIdx == 4);
  command = render_queue_3d_command_at(&queue, 1);
  assert(command != NULL);
  assert(command->kind == RENDER_COMMAND_3D_KIND_CAR);
  assert(command->payload.car.car_idx == 4);
  assert(command->payload.car.depth == 20.0f);
  assert(command->payload.car.pose.roll == 6);
  assert(command->payload.car.options.anim_frame == 8);
}

int main(int argc, const char **argv, const char **envp) {
  (void)argc;
  (void)argv;
  (void)envp;
  test_sort_matches_legacy_zcmp();
  test_building_priority_mapping_and_legacy_guard();
  test_start_light_priority_mapping_and_legacy_guard();
  test_car_priority_mapping_payload_and_legacy_guard();
  test_sort_keeps_typed_car_payload_with_sorted_entry();
  return 0;
}

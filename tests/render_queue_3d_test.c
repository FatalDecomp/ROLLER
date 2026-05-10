#include "render_queue_3d.h"
#include <assert.h>
#include <stddef.h>

static void test_sort_matches_legacy_zcmp(void)
{
  RenderQueue3D queue;
  render_queue_3d_clear(&queue);

  assert(render_queue_3d_add_legacy_priority(&queue, 5, 100, 10.0f) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 2, 101, 4.0f) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 13, 102, 10.0f) != NULL);
  assert(render_queue_3d_add_legacy_priority(&queue, 6, 103, 10.0f) != NULL);

  render_queue_3d_sort(&queue);

  assert(render_queue_3d_count(&queue) == 4);
  assert(queue.entries[0].nChunkIdx == 101);
  assert(queue.entries[1].nChunkIdx == 102);
  assert(queue.entries[2].nChunkIdx == 103);
  assert(queue.entries[3].nChunkIdx == 100);
}

static void test_start_light_priority_mapping_and_legacy_guard(void)
{
  RenderQueue3D queue;
  tTrackZOrderEntry *entry;
  render_queue_3d_clear(&queue);

  entry = render_queue_3d_add_start_light(&queue, 2, 42.0f);
  assert(entry != NULL);
  assert(entry->nRenderPriority == RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY);
  assert(entry->nChunkIdx == 2);
  assert(entry->fZDepth == 42.0f);
  assert(render_queue_3d_count(&queue) == 1);

  assert(render_queue_3d_add_legacy_priority(&queue,
                                             RENDER_QUEUE_3D_START_LIGHT_LEGACY_PRIORITY,
                                             7,
                                             42.0f) == NULL);
  assert(render_queue_3d_count(&queue) == 1);
}

int main(int argc, const char **argv, const char **envp)
{
  (void)argc;
  (void)argv;
  (void)envp;
  test_sort_matches_legacy_zcmp();
  test_start_light_priority_mapping_and_legacy_guard();
  return 0;
}

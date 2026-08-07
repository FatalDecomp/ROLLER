#ifndef ROLLER_EDITOR_TEST_CAR_H
#define ROLLER_EDITOR_TEST_CAR_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * E3A-S6. The editor's test car: one car, placed on an AI line at a chosen
 * chunk, drawn from the real car plans and the real car texture -- and no
 * simulation.
 *
 * E1-S6 sets numcars = 0 before the loader's first car-sized initialization
 * and skips placecars/testteaminit/initcarview, so nothing here may raise
 * numcars: doing so would hand the update loop a car to drive. Instead this
 * module writes Car[0] as pure render data and calls the renderer's car draw
 * directly, which reads Car[carIdx] but never numcars. The car therefore
 * exists for exactly as long as one frame's draw call and is never stepped.
 *
 * Car[0].nCurrChunk is deliberately left at -1. That is the renderer's own
 * "not on a chunk" value, and it makes DisplayCarWithPose zero every motion
 * offset and treat the pose as world space rather than chunk-local -- exactly
 * the two properties a static editor car needs. iLastValidChunk still names
 * the chunk underneath, so the car lights and shadows against the right
 * piece of track.
 */

/* Forward-declared so this header stays free of the renderer's own. */
typedef struct GameRenderer GameRenderer;

/*
 * Releases the editor's claim on the car slot and forgets which design was
 * prepared, so the next prepare reloads. Called from worker shutdown; safe
 * before anything has been prepared.
 */
void ed_test_car_reset(void);

/*
 * Makes uiDesign drawable: computes the shared car hitbox/size tables on
 * first use, loads the design's texture bank if a different one is current,
 * and registers it with the renderer.
 *
 * bAdvanced selects ROLLER's advanced-cars skin -- the `y*.bm` bank plus the
 * palette remap that recolours parts like the mirrors -- which the editor
 * spells as its Y model variants. The plan is identical either way, so this
 * is a skin rather than a design; it is still part of what "prepared" means,
 * because switching it reloads a different bank.
 *
 * Returns false and leaves the previous
 * design intact if the texture cannot be loaded -- the legacy loader reports
 * that through the recoverable core-error boundary rather than exiting
 * (E0-S7), and this is where that is turned back into a plain failure.
 */
bool ed_test_car_prepare(GameRenderer *pRenderer, uint32_t uiDesign,
                         bool bAdvanced);

/*
 * World placement for the car on uiAiLine at uiChunkId. Position comes from
 * the same ed_helper_ai_line_point() the AI-line overlay draws, so the car
 * sits on the line the editor shows. Orientation is the chunk's own
 * yaw/pitch/roll out of localdata, which is what the legacy editor rebuilt as
 * its yaw/pitch/roll matrices. bMillionPlus turns the car around, reproducing
 * the legacy toggle.
 */
bool ed_test_car_pose(uint32_t uiChunkId, uint32_t uiAiLine, bool bMillionPlus,
                      float afPositionOut[3], int32_t *piYaw, int32_t *piPitch,
                      int32_t *piRoll);

/*
 * Draws the overlay's test car, if SHOW_TEST_CAR is set and the selection
 * names a loaded chunk. One call per frame from the editor's render path.
 */
void ed_test_car_draw(GameRenderer *pRenderer);

#endif

#ifndef ROLLER_EDITOR_CAMERA_H
#define ROLLER_EDITOR_CAMERA_H

#include "editor_api.h"

#include <stdbool.h>

/* Internal editor camera state.  The facade supplies world-space positions
 * and degree angles; the legacy renderer consumes a 14-bit angular circle. */
void roller_ed_camera_reset(void);
void roller_ed_camera_set(const tEdCameraState *pCamera);
bool roller_ed_camera_apply(void);
int roller_ed_camera_degrees_to_angle(float fDegrees);

#endif

#pragma once

#include_next <dinput.h>

/*
 * ROLLER does not use DirectInput force feedback. Some wheel drivers hang when
 * SDL's DirectInput joystick backend resets or autocenters force-feedback
 * devices during SDL_OpenJoystick(). Keep SDL's DirectInput input path, but
 * make force feedback appear unavailable to SDL in this build.
 */
#ifdef DIDC_FORCEFEEDBACK
#undef DIDC_FORCEFEEDBACK
#endif
#define DIDC_FORCEFEEDBACK 0

#ifdef DISCL_EXCLUSIVE
#undef DISCL_EXCLUSIVE
#endif
#define DISCL_EXCLUSIVE DISCL_NONEXCLUSIVE

#ifdef IDirectInputDevice8_SendForceFeedbackCommand
#undef IDirectInputDevice8_SendForceFeedbackCommand
#endif
#define IDirectInputDevice8_SendForceFeedbackCommand(p, a) DI_OK

#ifdef IDirectInputDevice8_Unacquire
#undef IDirectInputDevice8_Unacquire
#endif
#define IDirectInputDevice8_Unacquire(p) DI_OK

#ifdef IDirectInputDevice8_Release
#undef IDirectInputDevice8_Release
#endif
#define IDirectInputDevice8_Release(p) 0

#ifdef IDirectInput8_Release
#undef IDirectInput8_Release
#endif
#define IDirectInput8_Release(p) 0

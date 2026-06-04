#ifndef _ROLLER_ROLLERINPUT_H
#define _ROLLER_ROLLERINPUT_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <SDL3/SDL.h>
//-------------------------------------------------------------------------------------------------

#define INPUT_NUM_ACTIONS 14

typedef enum
{
  INPUT_BINDING_NONE,
  INPUT_BINDING_KEYBOARD,
  INPUT_BINDING_JOYSTICK_BUTTON,
  INPUT_BINDING_JOYSTICK_AXIS,
  INPUT_BINDING_JOYSTICK_HAT
} eInputBindingType;

typedef enum
{
  INPUT_AXIS_CENTERED,
  INPUT_AXIS_PEDAL
} eInputAxisMode;

typedef struct
{
  SDL_Joystick *pJoystick;
  SDL_Gamepad *pGamepad;
  SDL_GUID guid;
  SDL_JoystickID joyId;
  uint16 unVendor;
  uint16 unProduct;
  uint16 unVersion;
  int iNumAxes;
  int iNumButtons;
  int iNumHats;
  int *piAxes;
  int *piMenuAxisRest;
  uint8 *pbyButtons;
  uint8 *pbyMenuPrevButtons;
  uint8 byMenuPrevGamepadButtons[SDL_GAMEPAD_BUTTON_COUNT];
  uint8 *pbyHats;
  char szName[128];
  char szPath[ROLLER_MAX_PATH];
  int iOrdinal;
  bool bGamepad;
#if defined(_WIN32)
  bool bWinMM;
  uint32 uWinMMId;
  uint8 byWinMMAxisMap[6];
  uint32 dwWinMMAxisMin[6];
  uint32 dwWinMMAxisMax[6];
#endif
} tInputDevice;

typedef struct
{
  eInputBindingType eType;
  int iDeviceRef;
  SDL_GUID guid;
  SDL_JoystickID joyId;
  uint16 unVendor;
  uint16 unProduct;
  uint16 unVersion;
  int iInputIndex;
  int iDirection;
  eInputAxisMode eAxisMode;
  int iDeadzone;
  int iThreshold;
  int iRestValue;
  bool bInvert;
  bool bGamepadInput;
  int iKeyScancode;
  int iOrdinal;
  char szName[128];
  char szPath[ROLLER_MAX_PATH];
} tInputBinding;

typedef struct
{
  int iRawValue;
  int iNormalizedValue;
  int iPressed;
} tInputBindingPreview;

extern tInputBinding g_inputBindings[INPUT_NUM_ACTIONS];

void InputInit(void);
void InputShutdown(void);
void InputHandleEvent(const SDL_Event *pEvent);
void InputUpdate(void);
void InputUpdateMenuControls(void);
void InputRefreshDevices(void);
void InputResetBindings(void);
void InputApplyDefaultGamepadBindings(void);
int InputLoadConfig(void);
void InputSaveConfig(void);
int InputGetActionPressed(int iAction);
int InputGetSteeringValue(int iPlayer);
void InputGetBindingName(const tInputBinding *pBinding, char *szOut, int iOutLen);
void InputGetActionBindingName(int iAction, char *szOut, int iOutLen);
void InputSetKeyboardBinding(int iAction, int iScancode);
void InputSetControllerBinding(int iAction, const tInputBinding *pBinding);
void InputBackupBindings(void);
void InputRestoreBindings(void);
void InputCaptureBegin(void);
int InputCapturePoll(int iAction, tInputBinding *pBindingOut);
void InputGetBindingPreview(const tInputBinding *pBinding, tInputBindingPreview *pPreview);
int InputGetDeviceCount(void);
const tInputDevice *InputGetDevice(int iDeviceRef);

//-------------------------------------------------------------------------------------------------
#endif

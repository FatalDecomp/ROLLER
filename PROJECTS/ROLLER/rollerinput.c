#include "rollerinput.h"
#include "3d.h"
#include "func2.h"
#include "roller.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//-------------------------------------------------------------------------------------------------

#define INPUT_DEFAULT_DEADZONE 8000
#define INPUT_DEFAULT_THRESHOLD 12000
#define INPUT_PEDAL_DEADZONE 4000
#define INPUT_STEERING_MAGNITUDE_MAX 0x102

typedef struct
{
  const char *szName;
  int iDefaultScancode;
} tInputActionInfo;

typedef struct
{
  SDL_JoystickID joyId;
  int iNumAxes;
  int iNumButtons;
  int iNumHats;
  int *piAxes;
  uint8 *pbyButtons;
  uint8 *pbyHats;
} tInputCaptureDevice;

//-------------------------------------------------------------------------------------------------

tInputBinding g_inputBindings[INPUT_NUM_ACTIONS];

static tInputDevice *s_pDevices = NULL;
static int s_iNumDevices = 0;
static bool s_bInitialized = false;
static tInputBinding s_backupBindings[INPUT_NUM_ACTIONS];
static tInputBinding s_releaseBinding;
static tInputCaptureDevice *s_pCaptureDevices = NULL;
static int s_iNumCaptureDevices = 0;
static bool s_bCaptureActive = false;
static bool s_bWaitingForRelease = false;

static const tInputActionInfo s_actionInfo[INPUT_NUM_ACTIONS] = {
  { "P1left", 44 },
  { "P1right", 45 },
  { "P1up", 20 },
  { "P1down", 33 },
  { "P1upgear", 19 },
  { "P1downgear", 32 },
  { "P2left", 79 },
  { "P2right", 80 },
  { "P2up", 73 },
  { "P2down", 77 },
  { "P2upgear", 72 },
  { "P2downgear", 76 },
  { "P1cheat", 21 },
  { "P2cheat", 79 }
};

static int InputReadButton(tInputBinding *pBinding);
static int InputReadHat(tInputBinding *pBinding);
static int InputGetAxisValueInDirection(tInputBinding *pBinding);

//-------------------------------------------------------------------------------------------------

static int InputClampInt(int iValue, int iMin, int iMax)
{
  if (iValue < iMin)
    return iMin;
  if (iValue > iMax)
    return iMax;
  return iValue;
}

//-------------------------------------------------------------------------------------------------

static int InputStringEqualsNoCase(const char *szA, const char *szB)
{
  while (*szA && *szB) {
    if (tolower((unsigned char)*szA) != tolower((unsigned char)*szB))
      return 0;
    ++szA;
    ++szB;
  }
  return *szA == *szB;
}

//-------------------------------------------------------------------------------------------------

static char *InputTrim(char *szText)
{
  char *szEnd;

  while (*szText && isspace((unsigned char)*szText))
    ++szText;

  szEnd = szText + strlen(szText);
  while (szEnd > szText && isspace((unsigned char)szEnd[-1]))
    --szEnd;
  *szEnd = '\0';

  return szText;
}

//-------------------------------------------------------------------------------------------------

static void InputCopyString(char *szDst, int iDstLen, const char *szSrc)
{
  if (iDstLen <= 0)
    return;

  if (!szSrc)
    szSrc = "";

  strncpy(szDst, szSrc, (size_t)iDstLen - 1);
  szDst[iDstLen - 1] = '\0';
}

//-------------------------------------------------------------------------------------------------

static int InputGuidIsZero(SDL_GUID guid)
{
  SDL_GUID zeroGuid;

  memset(&zeroGuid, 0, sizeof(zeroGuid));
  return memcmp(&guid, &zeroGuid, sizeof(guid)) == 0;
}

//-------------------------------------------------------------------------------------------------

static int InputGuidEquals(SDL_GUID guidA, SDL_GUID guidB)
{
  return memcmp(&guidA, &guidB, sizeof(guidA)) == 0;
}

//-------------------------------------------------------------------------------------------------

static void InputClearBinding(tInputBinding *pBinding)
{
  memset(pBinding, 0, sizeof(*pBinding));
  pBinding->eType = INPUT_BINDING_NONE;
  pBinding->iDeviceRef = -1;
  pBinding->iDirection = 1;
  pBinding->eAxisMode = INPUT_AXIS_CENTERED;
  pBinding->iDeadzone = INPUT_DEFAULT_DEADZONE;
  pBinding->iThreshold = INPUT_DEFAULT_THRESHOLD;
}

//-------------------------------------------------------------------------------------------------

void InputResetBindings(void)
{
  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i)
    InputClearBinding(&g_inputBindings[i]);
}

//-------------------------------------------------------------------------------------------------

static void InputCloseDevice(tInputDevice *pDevice)
{
  if (!pDevice)
    return;

  SDL_free(pDevice->piAxes);
  SDL_free(pDevice->pbyButtons);
  SDL_free(pDevice->pbyHats);

  if (pDevice->pGamepad)
    SDL_CloseGamepad(pDevice->pGamepad);
  else if (pDevice->pJoystick)
    SDL_CloseJoystick(pDevice->pJoystick);

  memset(pDevice, 0, sizeof(*pDevice));
}

//-------------------------------------------------------------------------------------------------

static void InputCloseAllDevices(void)
{
  for (int i = 0; i < s_iNumDevices; ++i)
    InputCloseDevice(&s_pDevices[i]);

  SDL_free(s_pDevices);
  s_pDevices = NULL;
  s_iNumDevices = 0;
}

//-------------------------------------------------------------------------------------------------
static void InputFreeCaptureSnapshot(void)
{
  for (int i = 0; i < s_iNumCaptureDevices; ++i) {
    SDL_free(s_pCaptureDevices[i].piAxes);
    SDL_free(s_pCaptureDevices[i].pbyButtons);
    SDL_free(s_pCaptureDevices[i].pbyHats);
  }

  SDL_free(s_pCaptureDevices);
  s_pCaptureDevices = NULL;
  s_iNumCaptureDevices = 0;
}

//-------------------------------------------------------------------------------------------------

static int InputGrowDeviceList(void)
{
  tInputDevice *pNewDevices;

  pNewDevices = (tInputDevice *)SDL_realloc(s_pDevices, sizeof(tInputDevice) * (size_t)(s_iNumDevices + 1));
  if (!pNewDevices)
    return 0;

  s_pDevices = pNewDevices;
  memset(&s_pDevices[s_iNumDevices], 0, sizeof(tInputDevice));
  return 1;
}

//-------------------------------------------------------------------------------------------------

static int InputOpenDevice(SDL_JoystickID joyId, int iOrdinal)
{
  const char *szName;
  const char *szPath;
  tInputDevice *pDevice;

  if (!InputGrowDeviceList())
    return 0;

  pDevice = &s_pDevices[s_iNumDevices];
  pDevice->bGamepad = SDL_IsGamepad(joyId);

  if (pDevice->bGamepad) {
    pDevice->pGamepad = SDL_OpenGamepad(joyId);
    if (!pDevice->pGamepad) {
      SDL_Log("InputOpenDevice: SDL_OpenGamepad failed: %s", SDL_GetError());
      InputCloseDevice(pDevice);
      return 0;
    }
    pDevice->pJoystick = SDL_GetGamepadJoystick(pDevice->pGamepad);
  } else {
    pDevice->pJoystick = SDL_OpenJoystick(joyId);
    if (!pDevice->pJoystick) {
      SDL_Log("InputOpenDevice: SDL_OpenJoystick failed: %s", SDL_GetError());
      InputCloseDevice(pDevice);
      return 0;
    }
  }

  if (!pDevice->pJoystick) {
    InputCloseDevice(pDevice);
    return 0;
  }

  pDevice->joyId = SDL_GetJoystickID(pDevice->pJoystick);
  if (!pDevice->joyId)
    pDevice->joyId = joyId;
  pDevice->guid = SDL_GetJoystickGUID(pDevice->pJoystick);
  pDevice->unVendor = SDL_GetJoystickVendor(pDevice->pJoystick);
  pDevice->unProduct = SDL_GetJoystickProduct(pDevice->pJoystick);
  pDevice->unVersion = SDL_GetJoystickProductVersion(pDevice->pJoystick);
  pDevice->iNumAxes = SDL_GetNumJoystickAxes(pDevice->pJoystick);
  pDevice->iNumButtons = SDL_GetNumJoystickButtons(pDevice->pJoystick);
  pDevice->iNumHats = SDL_GetNumJoystickHats(pDevice->pJoystick);
  pDevice->iOrdinal = iOrdinal;

  if (pDevice->iNumAxes < 0)
    pDevice->iNumAxes = 0;
  if (pDevice->iNumButtons < 0)
    pDevice->iNumButtons = 0;
  if (pDevice->iNumHats < 0)
    pDevice->iNumHats = 0;

  if (pDevice->iNumAxes > 0) {
    pDevice->piAxes = (int *)SDL_calloc((size_t)pDevice->iNumAxes, sizeof(int));
    if (!pDevice->piAxes) {
      InputCloseDevice(pDevice);
      return 0;
    }
  }
  if (pDevice->iNumButtons > 0) {
    pDevice->pbyButtons = (uint8 *)SDL_calloc((size_t)pDevice->iNumButtons, sizeof(uint8));
    if (!pDevice->pbyButtons) {
      InputCloseDevice(pDevice);
      return 0;
    }
  }
  if (pDevice->iNumHats > 0) {
    pDevice->pbyHats = (uint8 *)SDL_calloc((size_t)pDevice->iNumHats, sizeof(uint8));
    if (!pDevice->pbyHats) {
      InputCloseDevice(pDevice);
      return 0;
    }
  }

  szName = SDL_GetJoystickName(pDevice->pJoystick);
  if (!szName)
    szName = SDL_GetJoystickNameForID(joyId);
  szPath = SDL_GetJoystickPath(pDevice->pJoystick);
  if (!szPath)
    szPath = SDL_GetJoystickPathForID(joyId);
  InputCopyString(pDevice->szName, sizeof(pDevice->szName), szName);
  InputCopyString(pDevice->szPath, sizeof(pDevice->szPath), szPath);

  ++s_iNumDevices;
  return 1;
}

//-------------------------------------------------------------------------------------------------

static int InputBindingMatchesDevice(const tInputBinding *pBinding, const tInputDevice *pDevice)
{
  if (pBinding->joyId && pBinding->joyId == pDevice->joyId)
    return 1;

  if (!InputGuidIsZero(pBinding->guid) && !InputGuidEquals(pBinding->guid, pDevice->guid))
    return 0;

  if (pBinding->szPath[0] && pDevice->szPath[0] && strcmp(pBinding->szPath, pDevice->szPath) == 0)
    return 1;

  if (pBinding->unVendor && pBinding->unVendor != pDevice->unVendor)
    return 0;
  if (pBinding->unProduct && pBinding->unProduct != pDevice->unProduct)
    return 0;
  if (pBinding->unVersion && pBinding->unVersion != pDevice->unVersion)
    return 0;

  if (pBinding->szName[0] && pDevice->szName[0] && strcmp(pBinding->szName, pDevice->szName) != 0)
    return 0;

  if (pBinding->iOrdinal >= 0 && pBinding->iOrdinal != pDevice->iOrdinal)
    return 0;

  return !InputGuidIsZero(pBinding->guid) || pBinding->unVendor || pBinding->unProduct || pBinding->szName[0];
}

//-------------------------------------------------------------------------------------------------

static void InputResolveBindingDevice(tInputBinding *pBinding)
{
  if (pBinding->eType == INPUT_BINDING_NONE || pBinding->eType == INPUT_BINDING_KEYBOARD)
    return;

  if (pBinding->iDeviceRef >= 0 && pBinding->iDeviceRef < s_iNumDevices) {
    const tInputDevice *pDevice = &s_pDevices[pBinding->iDeviceRef];
    if (!pBinding->joyId || pBinding->joyId == pDevice->joyId)
      return;
  }

  pBinding->iDeviceRef = -1;
  for (int i = 0; i < s_iNumDevices; ++i) {
    if (InputBindingMatchesDevice(pBinding, &s_pDevices[i])) {
      pBinding->iDeviceRef = i;
      pBinding->joyId = s_pDevices[i].joyId;
      return;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void InputResolveAllBindings(void)
{
  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i)
    InputResolveBindingDevice(&g_inputBindings[i]);
}

//-------------------------------------------------------------------------------------------------

void InputRefreshDevices(void)
{
  SDL_JoystickID *pJoystickIds;
  int iCount = 0;

  if (!s_bInitialized)
    return;

  InputFreeCaptureSnapshot();
  s_bCaptureActive = false;
  s_bWaitingForRelease = false;
  InputCloseAllDevices();

  pJoystickIds = SDL_GetJoysticks(&iCount);
  for (int i = 0; pJoystickIds && i < iCount; ++i)
    InputOpenDevice(pJoystickIds[i], i);
  SDL_free(pJoystickIds);

  InputResolveAllBindings();
  InputUpdate();
}

//-------------------------------------------------------------------------------------------------

void InputInit(void)
{
  if (s_bInitialized)
    return;

  s_bInitialized = true;
  InputResetBindings();

  if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
    SDL_Log("InputInit: SDL_InitSubSystem failed: %s", SDL_GetError());
    return;
  }

  InputRefreshDevices();
  InputApplyDefaultGamepadBindings();
}

//-------------------------------------------------------------------------------------------------

void InputShutdown(void)
{
  if (!s_bInitialized)
    return;

  InputFreeCaptureSnapshot();
  s_bCaptureActive = false;
  s_bWaitingForRelease = false;
  InputCloseAllDevices();
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
  s_bInitialized = false;
}

//-------------------------------------------------------------------------------------------------

void InputHandleEvent(const SDL_Event *pEvent)
{
  if (!pEvent || !s_bInitialized)
    return;

  if (pEvent->type == SDL_EVENT_JOYSTICK_ADDED ||
      pEvent->type == SDL_EVENT_JOYSTICK_REMOVED ||
      pEvent->type == SDL_EVENT_GAMEPAD_ADDED ||
      pEvent->type == SDL_EVENT_GAMEPAD_REMOVED) {
    InputRefreshDevices();
  }
}

//-------------------------------------------------------------------------------------------------

void InputUpdate(void)
{
  if (!s_bInitialized)
    return;

  SDL_UpdateJoysticks();
  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
    tInputDevice *pDevice = &s_pDevices[iDevice];
    for (int i = 0; i < pDevice->iNumAxes; ++i)
      pDevice->piAxes[i] = SDL_GetJoystickAxis(pDevice->pJoystick, i);
    for (int i = 0; i < pDevice->iNumButtons; ++i)
      pDevice->pbyButtons[i] = SDL_GetJoystickButton(pDevice->pJoystick, i) ? 1 : 0;
    for (int i = 0; i < pDevice->iNumHats; ++i)
      pDevice->pbyHats[i] = SDL_GetJoystickHat(pDevice->pJoystick, i);
  }
}

//-------------------------------------------------------------------------------------------------

static tInputDevice *InputGetBindingDevice(tInputBinding *pBinding)
{
  InputResolveBindingDevice(pBinding);

  if (pBinding->iDeviceRef < 0 || pBinding->iDeviceRef >= s_iNumDevices)
    return NULL;

  return &s_pDevices[pBinding->iDeviceRef];
}

//-------------------------------------------------------------------------------------------------

static void InputCopyDeviceIdentity(tInputBinding *pBinding, const tInputDevice *pDevice)
{
  pBinding->iDeviceRef = (int)(pDevice - s_pDevices);
  pBinding->joyId = pDevice->joyId;
  pBinding->guid = pDevice->guid;
  pBinding->unVendor = pDevice->unVendor;
  pBinding->unProduct = pDevice->unProduct;
  pBinding->unVersion = pDevice->unVersion;
  pBinding->iOrdinal = pDevice->iOrdinal;
  InputCopyString(pBinding->szName, sizeof(pBinding->szName), pDevice->szName);
  InputCopyString(pBinding->szPath, sizeof(pBinding->szPath), pDevice->szPath);
}

//-------------------------------------------------------------------------------------------------
void InputSetKeyboardBinding(int iAction, int iScancode)
{
  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS)
    return;

  InputClearBinding(&g_inputBindings[iAction]);
  if (iScancode >= 0 && iScancode < 0x80)
    userkey[iAction] = iScancode;
}

//-------------------------------------------------------------------------------------------------

static int InputGetOppositeSteeringAction(int iAction)
{
  switch (iAction) {
    case USERKEY_P1LEFT:
      return USERKEY_P1RIGHT;
    case USERKEY_P1RIGHT:
      return USERKEY_P1LEFT;
    case USERKEY_P2LEFT:
      return USERKEY_P2RIGHT;
    case USERKEY_P2RIGHT:
      return USERKEY_P2LEFT;
    default:
      break;
  }

  return -1;
}

//-------------------------------------------------------------------------------------------------

void InputSetControllerBinding(int iAction, const tInputBinding *pBinding)
{
  int iOppositeAction;

  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS || !pBinding)
    return;

  if (userkey[iAction] < 0 || userkey[iAction] >= 0x80)
    userkey[iAction] = s_actionInfo[iAction].iDefaultScancode;
  g_inputBindings[iAction] = *pBinding;
  g_inputBindings[iAction].iKeyScancode = userkey[iAction];
  InputResolveBindingDevice(&g_inputBindings[iAction]);

  iOppositeAction = InputGetOppositeSteeringAction(iAction);
  if (iOppositeAction >= 0 &&
      pBinding->eType == INPUT_BINDING_JOYSTICK_AXIS &&
      pBinding->eAxisMode == INPUT_AXIS_CENTERED) {
    g_inputBindings[iOppositeAction] = *pBinding;
    g_inputBindings[iOppositeAction].iDirection = -g_inputBindings[iOppositeAction].iDirection;
    if (userkey[iOppositeAction] < 0 || userkey[iOppositeAction] >= 0x80)
      userkey[iOppositeAction] = s_actionInfo[iOppositeAction].iDefaultScancode;
    g_inputBindings[iOppositeAction].iKeyScancode = userkey[iOppositeAction];
    InputResolveBindingDevice(&g_inputBindings[iOppositeAction]);
  }
}

//-------------------------------------------------------------------------------------------------

void InputBackupBindings(void)
{
  memcpy(s_backupBindings, g_inputBindings, sizeof(s_backupBindings));
}

//-------------------------------------------------------------------------------------------------

void InputRestoreBindings(void)
{
  memcpy(g_inputBindings, s_backupBindings, sizeof(g_inputBindings));
  InputResolveAllBindings();
}

//-------------------------------------------------------------------------------------------------

static int InputIsSteeringAction(int iAction)
{
  return iAction == USERKEY_P1LEFT ||
    iAction == USERKEY_P1RIGHT ||
    iAction == USERKEY_P2LEFT ||
    iAction == USERKEY_P2RIGHT;
}

//-------------------------------------------------------------------------------------------------

static void InputCaptureWaitForRelease(const tInputBinding *pBinding)
{
  s_releaseBinding = *pBinding;
  s_bWaitingForRelease = pBinding->eType != INPUT_BINDING_NONE;
}

//-------------------------------------------------------------------------------------------------

static int InputCaptureReleaseStillActive(void)
{
  tInputBinding binding;
  int iThreshold;

  if (!s_bWaitingForRelease)
    return 0;

  binding = s_releaseBinding;
  switch (binding.eType) {
    case INPUT_BINDING_JOYSTICK_BUTTON:
      return InputReadButton(&binding);
    case INPUT_BINDING_JOYSTICK_HAT:
      return InputReadHat(&binding);
    case INPUT_BINDING_JOYSTICK_AXIS:
      iThreshold = binding.eAxisMode == INPUT_AXIS_PEDAL ? binding.iThreshold : binding.iDeadzone;
      if (iThreshold <= 0)
        iThreshold = INPUT_DEFAULT_THRESHOLD;
      return InputGetAxisValueInDirection(&binding) > iThreshold;
    default:
      break;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static const tInputCaptureDevice *InputFindCaptureDevice(SDL_JoystickID joyId)
{
  for (int i = 0; i < s_iNumCaptureDevices; ++i) {
    if (s_pCaptureDevices[i].joyId == joyId)
      return &s_pCaptureDevices[i];
  }

  return NULL;
}

//-------------------------------------------------------------------------------------------------

void InputCaptureBegin(void)
{
  InputFreeCaptureSnapshot();
  s_bWaitingForRelease = false;

  if (s_iNumDevices > 0) {
    s_pCaptureDevices = (tInputCaptureDevice *)SDL_calloc((size_t)s_iNumDevices, sizeof(tInputCaptureDevice));
    if (s_pCaptureDevices) {
      s_iNumCaptureDevices = s_iNumDevices;
      for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
        const tInputDevice *pDevice = &s_pDevices[iDevice];
        tInputCaptureDevice *pCaptureDevice = &s_pCaptureDevices[iDevice];

        pCaptureDevice->joyId = pDevice->joyId;
        pCaptureDevice->iNumAxes = pDevice->iNumAxes;
        pCaptureDevice->iNumButtons = pDevice->iNumButtons;
        pCaptureDevice->iNumHats = pDevice->iNumHats;

        if (pDevice->iNumAxes > 0) {
          pCaptureDevice->piAxes = (int *)SDL_malloc(sizeof(int) * (size_t)pDevice->iNumAxes);
          if (pCaptureDevice->piAxes)
            memcpy(pCaptureDevice->piAxes, pDevice->piAxes, sizeof(int) * (size_t)pDevice->iNumAxes);
        }
        if (pDevice->iNumButtons > 0) {
          pCaptureDevice->pbyButtons = (uint8 *)SDL_malloc(sizeof(uint8) * (size_t)pDevice->iNumButtons);
          if (pCaptureDevice->pbyButtons)
            memcpy(pCaptureDevice->pbyButtons, pDevice->pbyButtons, sizeof(uint8) * (size_t)pDevice->iNumButtons);
        }
        if (pDevice->iNumHats > 0) {
          pCaptureDevice->pbyHats = (uint8 *)SDL_malloc(sizeof(uint8) * (size_t)pDevice->iNumHats);
          if (pCaptureDevice->pbyHats)
            memcpy(pCaptureDevice->pbyHats, pDevice->pbyHats, sizeof(uint8) * (size_t)pDevice->iNumHats);
        }
      }
    }
  }

  s_bCaptureActive = true;
}

//-------------------------------------------------------------------------------------------------

int InputCapturePoll(int iAction, tInputBinding *pBindingOut)
{
  const int iAxisCaptureDelta = 12000;

  if (!pBindingOut || iAction < 0 || iAction >= INPUT_NUM_ACTIONS)
    return 0;

  if (!s_bCaptureActive)
    InputCaptureBegin();

  if (s_bWaitingForRelease) {
    if (InputCaptureReleaseStillActive())
      return 0;
    InputCaptureBegin();
    return 0;
  }

  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
    const tInputDevice *pDevice = &s_pDevices[iDevice];
    const tInputCaptureDevice *pCaptureDevice = InputFindCaptureDevice(pDevice->joyId);

    for (int iButton = 0; iButton < pDevice->iNumButtons; ++iButton) {
      int iWasDown = pCaptureDevice && pCaptureDevice->pbyButtons && iButton < pCaptureDevice->iNumButtons
        ? pCaptureDevice->pbyButtons[iButton] != 0
        : 0;
      if (pDevice->pbyButtons[iButton] && !iWasDown) {
        InputClearBinding(pBindingOut);
        pBindingOut->eType = INPUT_BINDING_JOYSTICK_BUTTON;
        pBindingOut->iInputIndex = iButton;
        pBindingOut->iKeyScancode = userkey[iAction];
        InputCopyDeviceIdentity(pBindingOut, pDevice);
        InputCaptureWaitForRelease(pBindingOut);
        return 1;
      }
    }

    for (int iHat = 0; iHat < pDevice->iNumHats; ++iHat) {
      int iWasHat = pCaptureDevice && pCaptureDevice->pbyHats && iHat < pCaptureDevice->iNumHats
        ? pCaptureDevice->pbyHats[iHat]
        : SDL_HAT_CENTERED;
      int iHatValue = pDevice->pbyHats[iHat];
      if (iHatValue && iHatValue != iWasHat) {
        int iHatDirection = iHatValue & ~iWasHat;
        if (!iHatDirection)
          iHatDirection = iHatValue;
        InputClearBinding(pBindingOut);
        pBindingOut->eType = INPUT_BINDING_JOYSTICK_HAT;
        pBindingOut->iInputIndex = iHat;
        pBindingOut->iDirection = iHatDirection;
        pBindingOut->iKeyScancode = userkey[iAction];
        InputCopyDeviceIdentity(pBindingOut, pDevice);
        InputCaptureWaitForRelease(pBindingOut);
        return 1;
      }
    }

    for (int iAxis = 0; iAxis < pDevice->iNumAxes; ++iAxis) {
      int iBaseValue = pCaptureDevice && pCaptureDevice->piAxes && iAxis < pCaptureDevice->iNumAxes
        ? pCaptureDevice->piAxes[iAxis]
        : 0;
      int iDelta = pDevice->piAxes[iAxis] - iBaseValue;
      if (abs(iDelta) >= iAxisCaptureDelta) {
        InputClearBinding(pBindingOut);
        pBindingOut->eType = INPUT_BINDING_JOYSTICK_AXIS;
        pBindingOut->iInputIndex = iAxis;
        pBindingOut->iDirection = iDelta < 0 ? -1 : 1;
        pBindingOut->eAxisMode = InputIsSteeringAction(iAction) ? INPUT_AXIS_CENTERED : INPUT_AXIS_PEDAL;
        pBindingOut->iDeadzone = pBindingOut->eAxisMode == INPUT_AXIS_PEDAL ? INPUT_PEDAL_DEADZONE : INPUT_DEFAULT_DEADZONE;
        pBindingOut->iThreshold = INPUT_DEFAULT_THRESHOLD;
        pBindingOut->iRestValue = pBindingOut->eAxisMode == INPUT_AXIS_PEDAL ? iBaseValue : 0;
        pBindingOut->iKeyScancode = userkey[iAction];
        InputCopyDeviceIdentity(pBindingOut, pDevice);
        InputCaptureWaitForRelease(pBindingOut);
        return 1;
      }
    }
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static void InputBindGamepadButton(int iAction, int iDeviceRef, SDL_GamepadButton eButton)
{
  tInputBinding *pBinding;

  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS || iDeviceRef < 0 || iDeviceRef >= s_iNumDevices)
    return;

  pBinding = &g_inputBindings[iAction];
  InputClearBinding(pBinding);
  pBinding->eType = INPUT_BINDING_JOYSTICK_BUTTON;
  pBinding->iInputIndex = (int)eButton;
  pBinding->bGamepadInput = true;
  pBinding->iKeyScancode = userkey[iAction];
  InputCopyDeviceIdentity(pBinding, &s_pDevices[iDeviceRef]);
}

//-------------------------------------------------------------------------------------------------

static void InputBindGamepadAxis(int iAction, int iDeviceRef, SDL_GamepadAxis eAxis, int iDirection, eInputAxisMode eAxisMode, int iRestValue)
{
  tInputBinding *pBinding;

  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS || iDeviceRef < 0 || iDeviceRef >= s_iNumDevices)
    return;

  pBinding = &g_inputBindings[iAction];
  InputClearBinding(pBinding);
  pBinding->eType = INPUT_BINDING_JOYSTICK_AXIS;
  pBinding->iInputIndex = (int)eAxis;
  pBinding->iDirection = iDirection < 0 ? -1 : 1;
  pBinding->eAxisMode = eAxisMode;
  pBinding->iRestValue = iRestValue;
  pBinding->iDeadzone = eAxisMode == INPUT_AXIS_PEDAL ? INPUT_PEDAL_DEADZONE : INPUT_DEFAULT_DEADZONE;
  pBinding->iThreshold = INPUT_DEFAULT_THRESHOLD;
  pBinding->bGamepadInput = true;
  pBinding->iKeyScancode = userkey[iAction];
  InputCopyDeviceIdentity(pBinding, &s_pDevices[iDeviceRef]);
}

//-------------------------------------------------------------------------------------------------

static int InputFindNthGamepad(int iGamepadIndex)
{
  int iFound = 0;

  for (int i = 0; i < s_iNumDevices; ++i) {
    if (!s_pDevices[i].bGamepad || !s_pDevices[i].pGamepad)
      continue;
    if (iFound == iGamepadIndex)
      return i;
    ++iFound;
  }

  return -1;
}

//-------------------------------------------------------------------------------------------------

void InputApplyDefaultGamepadBindings(void)
{
  for (int iPlayer = 0; iPlayer < 2; ++iPlayer) {
    int iDeviceRef = InputFindNthGamepad(iPlayer);
    int iActionBase = iPlayer == 0 ? USERKEY_P1LEFT : USERKEY_P2LEFT;
    int iCheatAction = iPlayer == 0 ? USERKEY_P1CHEAT : USERKEY_P2CHEAT;
    SDL_Gamepad *pGamepad;

    if (iDeviceRef < 0)
      continue;

    pGamepad = s_pDevices[iDeviceRef].pGamepad;
    InputBindGamepadAxis(iActionBase, iDeviceRef, SDL_GAMEPAD_AXIS_LEFTX, -1, INPUT_AXIS_CENTERED, 0);
    InputBindGamepadAxis(iActionBase + 1, iDeviceRef, SDL_GAMEPAD_AXIS_LEFTX, 1, INPUT_AXIS_CENTERED, 0);

    if (SDL_GamepadHasAxis(pGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
      InputBindGamepadAxis(iActionBase + 2, iDeviceRef, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1, INPUT_AXIS_PEDAL, 0);
    else
      InputBindGamepadButton(iActionBase + 2, iDeviceRef, SDL_GAMEPAD_BUTTON_SOUTH);

    if (SDL_GamepadHasAxis(pGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER))
      InputBindGamepadAxis(iActionBase + 3, iDeviceRef, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1, INPUT_AXIS_PEDAL, 0);
    else
      InputBindGamepadButton(iActionBase + 3, iDeviceRef, SDL_GAMEPAD_BUTTON_EAST);

    if (SDL_GamepadHasButton(pGamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
      InputBindGamepadButton(iActionBase + 4, iDeviceRef, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    else
      InputBindGamepadButton(iActionBase + 4, iDeviceRef, SDL_GAMEPAD_BUTTON_NORTH);

    if (SDL_GamepadHasButton(pGamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
      InputBindGamepadButton(iActionBase + 5, iDeviceRef, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    else
      InputBindGamepadButton(iActionBase + 5, iDeviceRef, SDL_GAMEPAD_BUTTON_WEST);

    InputBindGamepadButton(iCheatAction, iDeviceRef, SDL_GAMEPAD_BUTTON_START);
  }
}

//-------------------------------------------------------------------------------------------------

static int InputReadButton(tInputBinding *pBinding)
{
  tInputDevice *pDevice = InputGetBindingDevice(pBinding);

  if (!pDevice)
    return 0;

  if (pBinding->bGamepadInput && pDevice->pGamepad)
    return SDL_GetGamepadButton(pDevice->pGamepad, (SDL_GamepadButton)pBinding->iInputIndex) ? 1 : 0;

  if (pBinding->iInputIndex < 0 || pBinding->iInputIndex >= pDevice->iNumButtons)
    return 0;

  return pDevice->pbyButtons[pBinding->iInputIndex] != 0;
}

//-------------------------------------------------------------------------------------------------

static int InputReadHat(tInputBinding *pBinding)
{
  tInputDevice *pDevice = InputGetBindingDevice(pBinding);

  if (!pDevice || pBinding->iInputIndex < 0 || pBinding->iInputIndex >= pDevice->iNumHats)
    return 0;

  return (pDevice->pbyHats[pBinding->iInputIndex] & pBinding->iDirection) != 0;
}

//-------------------------------------------------------------------------------------------------

static int InputAxisIsGamepadTrigger(const tInputBinding *pBinding)
{
  return pBinding->bGamepadInput &&
    (pBinding->iInputIndex == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
     pBinding->iInputIndex == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
}

//-------------------------------------------------------------------------------------------------

static int InputReadAxisRaw(tInputBinding *pBinding)
{
  tInputDevice *pDevice = InputGetBindingDevice(pBinding);

  if (!pDevice)
    return 0;

  if (pBinding->bGamepadInput && pDevice->pGamepad)
    return SDL_GetGamepadAxis(pDevice->pGamepad, (SDL_GamepadAxis)pBinding->iInputIndex);

  if (pBinding->iInputIndex < 0 || pBinding->iInputIndex >= pDevice->iNumAxes)
    return 0;

  return pDevice->piAxes[pBinding->iInputIndex];
}

//-------------------------------------------------------------------------------------------------

static int InputGetAxisValueInDirection(tInputBinding *pBinding)
{
  int iRawValue = InputReadAxisRaw(pBinding);
  int iValue;

  if (pBinding->eAxisMode == INPUT_AXIS_PEDAL) {
    iValue = iRawValue - pBinding->iRestValue;
  } else {
    iValue = iRawValue;
  }

  if (pBinding->iDirection < 0)
    iValue = -iValue;
  if (pBinding->bInvert)
    iValue = -iValue;

  return iValue;
}

//-------------------------------------------------------------------------------------------------

static int InputGetAxisTravel(tInputBinding *pBinding)
{
  int iMinValue = InputAxisIsGamepadTrigger(pBinding) ? 0 : SDL_JOYSTICK_AXIS_MIN;
  int iMaxValue = SDL_JOYSTICK_AXIS_MAX;
  int iTravelLow = abs(pBinding->iRestValue - iMinValue);
  int iTravelHigh = abs(iMaxValue - pBinding->iRestValue);

  if (pBinding->eAxisMode == INPUT_AXIS_CENTERED)
    return SDL_JOYSTICK_AXIS_MAX;

  return iTravelLow > iTravelHigh ? iTravelLow : iTravelHigh;
}

//-------------------------------------------------------------------------------------------------

static int InputGetAxisMagnitude(tInputBinding *pBinding)
{
  int iValue;
  int iDeadzone;
  int iTravel;

  if (pBinding->eType != INPUT_BINDING_JOYSTICK_AXIS)
    return 0;

  iValue = InputGetAxisValueInDirection(pBinding);
  iDeadzone = pBinding->eAxisMode == INPUT_AXIS_PEDAL ? pBinding->iThreshold : pBinding->iDeadzone;
  if (iDeadzone < 0)
    iDeadzone = 0;
  if (iValue <= iDeadzone)
    return 0;

  iTravel = InputGetAxisTravel(pBinding);
  if (iTravel <= iDeadzone)
    return INPUT_STEERING_MAGNITUDE_MAX;

  return InputClampInt(((iValue - iDeadzone) * INPUT_STEERING_MAGNITUDE_MAX) / (iTravel - iDeadzone), 0, INPUT_STEERING_MAGNITUDE_MAX);
}

//-------------------------------------------------------------------------------------------------

static int InputReadAxisPressed(tInputBinding *pBinding)
{
  int iValue = InputGetAxisValueInDirection(pBinding);
  int iThreshold = pBinding->iThreshold;

  if (iThreshold <= 0)
    iThreshold = INPUT_DEFAULT_THRESHOLD;

  return iValue > iThreshold;
}

//-------------------------------------------------------------------------------------------------
void InputGetBindingPreview(const tInputBinding *pBinding, tInputBindingPreview *pPreview)
{
  tInputBinding binding;

  if (!pPreview)
    return;

  memset(pPreview, 0, sizeof(*pPreview));
  if (!pBinding)
    return;

  binding = *pBinding;
  switch (binding.eType) {
    case INPUT_BINDING_JOYSTICK_BUTTON:
      pPreview->iPressed = InputReadButton(&binding);
      break;
    case INPUT_BINDING_JOYSTICK_AXIS:
      pPreview->iRawValue = InputReadAxisRaw(&binding);
      pPreview->iNormalizedValue = InputGetAxisMagnitude(&binding);
      pPreview->iPressed = InputReadAxisPressed(&binding);
      break;
    case INPUT_BINDING_JOYSTICK_HAT:
      pPreview->iPressed = InputReadHat(&binding);
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static int InputReadControllerBinding(tInputBinding *pBinding)
{
  switch (pBinding->eType) {
    case INPUT_BINDING_KEYBOARD:
      return pBinding->iKeyScancode >= 0 && pBinding->iKeyScancode < 140 && keys[pBinding->iKeyScancode];
    case INPUT_BINDING_JOYSTICK_BUTTON:
      return InputReadButton(pBinding);
    case INPUT_BINDING_JOYSTICK_AXIS:
      return InputReadAxisPressed(pBinding);
    case INPUT_BINDING_JOYSTICK_HAT:
      return InputReadHat(pBinding);
    default:
      break;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

int InputGetActionPressed(int iAction)
{
  int iScancode;

  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS)
    return 0;

  iScancode = userkey[iAction];
  if (iScancode >= 0 && iScancode < 0x80 && iScancode < 140 && keys[iScancode])
    return 1;

  return InputReadControllerBinding(&g_inputBindings[iAction]);
}

//-------------------------------------------------------------------------------------------------

int InputGetSteeringValue(int iPlayer)
{
  int iLeftAction = iPlayer == 0 ? USERKEY_P1LEFT : USERKEY_P2LEFT;
  int iRightAction = iPlayer == 0 ? USERKEY_P1RIGHT : USERKEY_P2RIGHT;
  int iLeft = InputGetAxisMagnitude(&g_inputBindings[iLeftAction]);
  int iRight = InputGetAxisMagnitude(&g_inputBindings[iRightAction]);

  return iLeft - iRight;
}

//-------------------------------------------------------------------------------------------------

static int InputFindActionByName(const char *szName)
{
  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i) {
    if (InputStringEqualsNoCase(szName, s_actionInfo[i].szName))
      return i;
  }

  return -1;
}

//-------------------------------------------------------------------------------------------------

static int InputParseInt(const char *szValue, int iDefault)
{
  char *szEnd;
  long lValue;

  if (!szValue || !*szValue)
    return iDefault;

  lValue = strtol(szValue, &szEnd, 0);
  if (szEnd == szValue)
    return iDefault;

  return (int)lValue;
}

//-------------------------------------------------------------------------------------------------

static int InputParseBool(const char *szValue)
{
  if (!szValue)
    return 0;

  return strcmp(szValue, "1") == 0 ||
    InputStringEqualsNoCase(szValue, "true") ||
    InputStringEqualsNoCase(szValue, "yes");
}

//-------------------------------------------------------------------------------------------------

static void InputParseBindingField(tInputBinding *pBinding, const char *szKey, const char *szValue)
{
  if (InputStringEqualsNoCase(szKey, "guid")) {
    pBinding->guid = SDL_StringToGUID(szValue);
  } else if (InputStringEqualsNoCase(szKey, "name")) {
    InputCopyString(pBinding->szName, sizeof(pBinding->szName), szValue);
  } else if (InputStringEqualsNoCase(szKey, "path")) {
    InputCopyString(pBinding->szPath, sizeof(pBinding->szPath), szValue);
  } else if (InputStringEqualsNoCase(szKey, "vendor")) {
    pBinding->unVendor = (uint16)InputParseInt(szValue, 0);
  } else if (InputStringEqualsNoCase(szKey, "product")) {
    pBinding->unProduct = (uint16)InputParseInt(szValue, 0);
  } else if (InputStringEqualsNoCase(szKey, "version")) {
    pBinding->unVersion = (uint16)InputParseInt(szValue, 0);
  } else if (InputStringEqualsNoCase(szKey, "ordinal")) {
    pBinding->iOrdinal = InputParseInt(szValue, -1);
  } else if (InputStringEqualsNoCase(szKey, "index")) {
    pBinding->iInputIndex = InputParseInt(szValue, 0);
  } else if (InputStringEqualsNoCase(szKey, "dir")) {
    pBinding->iDirection = InputParseInt(szValue, 1) < 0 ? -1 : 1;
  } else if (InputStringEqualsNoCase(szKey, "mode")) {
    pBinding->eAxisMode = InputStringEqualsNoCase(szValue, "pedal") ? INPUT_AXIS_PEDAL : INPUT_AXIS_CENTERED;
  } else if (InputStringEqualsNoCase(szKey, "deadzone")) {
    pBinding->iDeadzone = InputParseInt(szValue, INPUT_DEFAULT_DEADZONE);
  } else if (InputStringEqualsNoCase(szKey, "threshold")) {
    pBinding->iThreshold = InputParseInt(szValue, INPUT_DEFAULT_THRESHOLD);
  } else if (InputStringEqualsNoCase(szKey, "rest")) {
    pBinding->iRestValue = InputParseInt(szValue, 0);
  } else if (InputStringEqualsNoCase(szKey, "invert")) {
    pBinding->bInvert = InputParseBool(szValue);
  } else if (InputStringEqualsNoCase(szKey, "standard")) {
    pBinding->bGamepadInput = InputParseBool(szValue);
  } else if (InputStringEqualsNoCase(szKey, "scancode")) {
    pBinding->iKeyScancode = InputParseInt(szValue, pBinding->iKeyScancode);
  }
}

//-------------------------------------------------------------------------------------------------

static void InputParseBindingValue(int iAction, char *szValue)
{
  char *szToken;
  tInputBinding binding;

  InputClearBinding(&binding);
  binding.iKeyScancode = userkey[iAction];
  binding.iOrdinal = -1;

  szToken = strtok(szValue, ";");
  if (!szToken)
    return;

  szToken = InputTrim(szToken);
  if (InputStringEqualsNoCase(szToken, "none")) {
    userkey[iAction] = s_actionInfo[iAction].iDefaultScancode;
    g_inputBindings[iAction] = binding;
    return;
  }
  if (InputStringEqualsNoCase(szToken, "keyboard")) {
    binding.eType = INPUT_BINDING_KEYBOARD;
  } else if (InputStringEqualsNoCase(szToken, "button")) {
    binding.eType = INPUT_BINDING_JOYSTICK_BUTTON;
  } else if (InputStringEqualsNoCase(szToken, "axis")) {
    binding.eType = INPUT_BINDING_JOYSTICK_AXIS;
  } else if (InputStringEqualsNoCase(szToken, "hat")) {
    binding.eType = INPUT_BINDING_JOYSTICK_HAT;
  } else {
    return;
  }

  while ((szToken = strtok(NULL, ";")) != NULL) {
    char *szEquals;
    char *szKey;
    char *szFieldValue;

    szToken = InputTrim(szToken);
    szEquals = strchr(szToken, '=');
    if (!szEquals)
      continue;
    *szEquals = '\0';
    szKey = InputTrim(szToken);
    szFieldValue = InputTrim(szEquals + 1);
    InputParseBindingField(&binding, szKey, szFieldValue);
  }

  if (binding.iKeyScancode >= 0 && binding.iKeyScancode < 0x80)
    userkey[iAction] = binding.iKeyScancode;

  if (binding.eType == INPUT_BINDING_KEYBOARD) {
    InputClearBinding(&g_inputBindings[iAction]);
    return;
  }

  g_inputBindings[iAction] = binding;
  InputResolveBindingDevice(&g_inputBindings[iAction]);
}

//-------------------------------------------------------------------------------------------------

static void InputMigrateLegacyKeyboardBindings(void)
{
  int iIgnoredLegacyJoystick = 0;

  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i) {
    if (userkey[i] >= 0x80) {
      userkey[i] = s_actionInfo[i].iDefaultScancode;
      iIgnoredLegacyJoystick = 1;
    }
  }

  if (iIgnoredLegacyJoystick)
    SDL_Log("Ignoring legacy joystick bindings from FATAL.INI; using keyboard defaults for those actions.");
}

//-------------------------------------------------------------------------------------------------

int InputLoadConfig(void)
{
  FILE *fp;
  char szLine[1024];

  InputResetBindings();

  fp = ROLLERfopen("ROLLER.INI", "r");
  if (!fp) {
    InputMigrateLegacyKeyboardBindings();
    InputApplyDefaultGamepadBindings();
    return 0;
  }

  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i)
    userkey[i] = s_actionInfo[i].iDefaultScancode;

  while (fgets(szLine, sizeof(szLine), fp)) {
    char *szText = InputTrim(szLine);
    char *szEquals;
    int iAction;

    if (!szText[0] || szText[0] == '#' || szText[0] == ';' || szText[0] == '[')
      continue;

    szEquals = strchr(szText, '=');
    if (!szEquals)
      continue;

    *szEquals = '\0';
    iAction = InputFindActionByName(InputTrim(szText));
    if (iAction < 0)
      continue;

    InputParseBindingValue(iAction, InputTrim(szEquals + 1));
  }

  fclose(fp);
  InputResolveAllBindings();
  return 1;
}

//-------------------------------------------------------------------------------------------------

static void InputWriteDeviceFields(FILE *fp, const tInputBinding *pBinding)
{
  char szGuid[64];

  SDL_GUIDToString(pBinding->guid, szGuid, sizeof(szGuid));
  fprintf(fp, ";guid=%s", szGuid);
  if (pBinding->szName[0])
    fprintf(fp, ";name=%s", pBinding->szName);
  if (pBinding->szPath[0])
    fprintf(fp, ";path=%s", pBinding->szPath);
  fprintf(fp, ";vendor=%u;product=%u;version=%u;ordinal=%d",
    pBinding->unVendor,
    pBinding->unProduct,
    pBinding->unVersion,
    pBinding->iOrdinal);
}

//-------------------------------------------------------------------------------------------------

void InputSaveConfig(void)
{
  FILE *fp = ROLLERfopen("ROLLER.INI", "w");

  if (!fp)
    return;

  fprintf(fp, "InputVersion=2\n");
  fprintf(fp, "[Input]\n");

  for (int i = 0; i < INPUT_NUM_ACTIONS; ++i) {
    const tInputBinding *pBinding = &g_inputBindings[i];
    int iScancode = userkey[i] >= 0 && userkey[i] < 0x80 ? userkey[i] : s_actionInfo[i].iDefaultScancode;

    fprintf(fp, "%s=", s_actionInfo[i].szName);
    switch (pBinding->eType) {
      case INPUT_BINDING_JOYSTICK_BUTTON:
        fprintf(fp, "button");
        InputWriteDeviceFields(fp, pBinding);
        fprintf(fp, ";index=%d;standard=%d;scancode=%d\n", pBinding->iInputIndex, pBinding->bGamepadInput ? 1 : 0, iScancode);
        break;
      case INPUT_BINDING_JOYSTICK_AXIS:
        fprintf(fp, "axis");
        InputWriteDeviceFields(fp, pBinding);
        fprintf(fp, ";index=%d;dir=%d;mode=%s;deadzone=%d;threshold=%d;rest=%d;invert=%d;standard=%d;scancode=%d\n",
          pBinding->iInputIndex,
          pBinding->iDirection,
          pBinding->eAxisMode == INPUT_AXIS_PEDAL ? "pedal" : "centered",
          pBinding->iDeadzone,
          pBinding->iThreshold,
          pBinding->iRestValue,
          pBinding->bInvert ? 1 : 0,
          pBinding->bGamepadInput ? 1 : 0,
          iScancode);
        break;
      case INPUT_BINDING_JOYSTICK_HAT:
        fprintf(fp, "hat");
        InputWriteDeviceFields(fp, pBinding);
        fprintf(fp, ";index=%d;dir=%d;scancode=%d\n", pBinding->iInputIndex, pBinding->iDirection, iScancode);
        break;
      default:
        fprintf(fp, "keyboard;scancode=%d\n", iScancode);
        break;
    }
  }

  fclose(fp);
}

//-------------------------------------------------------------------------------------------------

void InputGetBindingName(const tInputBinding *pBinding, char *szOut, int iOutLen)
{
  if (!szOut || iOutLen <= 0)
    return;

  szOut[0] = '\0';
  if (!pBinding || pBinding->eType == INPUT_BINDING_NONE) {
    InputCopyString(szOut, iOutLen, "Keyboard");
    return;
  }

  switch (pBinding->eType) {
    case INPUT_BINDING_KEYBOARD:
      if (pBinding->iKeyScancode >= 0 && pBinding->iKeyScancode < 140 && keyname[pBinding->iKeyScancode])
        snprintf(szOut, (size_t)iOutLen, "%s", keyname[pBinding->iKeyScancode]);
      else
        snprintf(szOut, (size_t)iOutLen, "Keyboard %d", pBinding->iKeyScancode);
      break;
    case INPUT_BINDING_JOYSTICK_BUTTON:
      snprintf(szOut, (size_t)iOutLen, "button %d", pBinding->iInputIndex);
      break;
    case INPUT_BINDING_JOYSTICK_AXIS:
      snprintf(szOut, (size_t)iOutLen, "axis %d %s", pBinding->iInputIndex, pBinding->iDirection < 0 ? "-" : "+");
      break;
    case INPUT_BINDING_JOYSTICK_HAT:
      snprintf(szOut, (size_t)iOutLen, "hat %d", pBinding->iInputIndex);
      break;
    default:
      InputCopyString(szOut, iOutLen, "Unbound");
      break;
  }
}

//-------------------------------------------------------------------------------------------------
void InputGetActionBindingName(int iAction, char *szOut, int iOutLen)
{
  int iKey;

  if (!szOut || iOutLen <= 0)
    return;

  szOut[0] = '\0';
  if (iAction < 0 || iAction >= INPUT_NUM_ACTIONS) {
    InputCopyString(szOut, iOutLen, "Unbound");
    return;
  }

  if (g_inputBindings[iAction].eType != INPUT_BINDING_NONE) {
    InputGetBindingName(&g_inputBindings[iAction], szOut, iOutLen);
    return;
  }

  iKey = userkey[iAction];
  if (iKey >= 0 && iKey < 140 && keyname[iKey]) {
    InputCopyString(szOut, iOutLen, keyname[iKey]);
    return;
  }

  snprintf(szOut, (size_t)iOutLen, "Keyboard %d", iKey);
}

//-------------------------------------------------------------------------------------------------

static int InputRawAxisToLegacyValue(int iValue)
{
  return ((iValue + 32768) * 10000) / 65536;
}

//-------------------------------------------------------------------------------------------------

int InputGetLegacyJoySlot(int iSlot, int *piButton1, int *piButton2, int *piXAxis, int *piYAxis)
{
  tInputDevice *pDevice;

  if (piButton1)
    *piButton1 = 0;
  if (piButton2)
    *piButton2 = 0;
  if (piXAxis)
    *piXAxis = 5000;
  if (piYAxis)
    *piYAxis = 5000;

  if (iSlot < 0 || iSlot >= s_iNumDevices)
    return 0;

  pDevice = &s_pDevices[iSlot];
  if (pDevice->bGamepad && pDevice->pGamepad) {
    if (piButton1)
      *piButton1 = SDL_GetGamepadButton(pDevice->pGamepad, SDL_GAMEPAD_BUTTON_SOUTH) ? 1 : 0;
    if (piButton2)
      *piButton2 = SDL_GetGamepadButton(pDevice->pGamepad, SDL_GAMEPAD_BUTTON_EAST) ? 1 : 0;
    if (piXAxis)
      *piXAxis = InputRawAxisToLegacyValue(SDL_GetGamepadAxis(pDevice->pGamepad, SDL_GAMEPAD_AXIS_LEFTY));
    if (piYAxis)
      *piYAxis = InputRawAxisToLegacyValue(SDL_GetGamepadAxis(pDevice->pGamepad, SDL_GAMEPAD_AXIS_LEFTX));
    return 1;
  }

  if (piButton1 && pDevice->iNumButtons > 0)
    *piButton1 = pDevice->pbyButtons[0] != 0;
  if (piButton2 && pDevice->iNumButtons > 1)
    *piButton2 = pDevice->pbyButtons[1] != 0;
  if (piXAxis && pDevice->iNumAxes > 1)
    *piXAxis = InputRawAxisToLegacyValue(pDevice->piAxes[1]);
  if (piYAxis && pDevice->iNumAxes > 0)
    *piYAxis = InputRawAxisToLegacyValue(pDevice->piAxes[0]);

  return 1;
}

//-------------------------------------------------------------------------------------------------

int InputGetDeviceCount(void)
{
  return s_iNumDevices;
}

//-------------------------------------------------------------------------------------------------

const tInputDevice *InputGetDevice(int iDeviceRef)
{
  if (iDeviceRef < 0 || iDeviceRef >= s_iNumDevices)
    return NULL;

  return &s_pDevices[iDeviceRef];
}

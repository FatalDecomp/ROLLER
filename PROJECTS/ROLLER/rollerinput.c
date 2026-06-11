#include "rollerinput.h"
#include "3d.h"
#include "func2.h"
#include "menu_render.h"
#include "roller.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif
//-------------------------------------------------------------------------------------------------

#define INPUT_DEFAULT_DEADZONE 8000
#define INPUT_DEFAULT_THRESHOLD 12000
#define INPUT_AXIS_CAPTURE_DELTA 12000
#define INPUT_TRIGGER_CAPTURE_DELTA 6000
#define INPUT_PEDAL_DEADZONE 4000
#define INPUT_STEERING_MAGNITUDE_MAX 0x102
#define INPUT_MENU_AXIS_DEADZONE 12000
#define INPUT_MENU_REPEAT_INITIAL_MS 280
#define INPUT_MENU_REPEAT_MS 90
#if defined(_WIN32)
#define INPUT_WINMM_AXIS_COUNT 6
#define INPUT_WINMM_REFRESH_MS 1000
#define INPUT_HOTPLUG_REFRESH_DELAY_MS 250
#else
#define INPUT_HOTPLUG_REFRESH_DELAY_MS 0
#endif

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
  int iGamepadAxes[SDL_GAMEPAD_AXIS_COUNT];
  uint8 byGamepadButtons[SDL_GAMEPAD_BUTTON_COUNT];
} tInputCaptureDevice;

typedef struct
{
  bool bDown;
  uint64 ullNextRepeatMs;
} tInputMenuKeyState;

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
static tInputMenuKeyState s_menuUpState;
static tInputMenuKeyState s_menuDownState;
static tInputMenuKeyState s_menuLeftState;
static tInputMenuKeyState s_menuRightState;
static tInputMenuKeyState s_menuAcceptState;
static tInputMenuKeyState s_menuBackState;
static tInputMenuKeyState s_menuAnyButtonState;
static tInputMenuKeyState s_menuQuitYesState;
static tInputMenuKeyState s_menuQuitCancelState;
static bool s_bMenuWaitForRelease = false;
static bool s_bDeviceRefreshPending = false;
static uint64 s_ullDeviceRefreshMs = 0;
#if defined(_WIN32)
static uint32 s_uWinMMDeviceMask = 0;
static uint64 s_ullNextWinMMRefreshMs = 0;
static bool s_bRefreshingDevices = false;
static eInputWindowsBackend s_eWindowsBackend = INPUT_WINDOWS_BACKEND_SDL_DINPUT;
#endif

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
  { "P2cheat", 71 }
};

static int InputReadButton(tInputBinding *pBinding);
static int InputReadHat(tInputBinding *pBinding);
static int InputReadAxisRaw(tInputBinding *pBinding);
static int InputGetAxisValueInDirection(tInputBinding *pBinding);
static int InputMenuAnyButtonContext(void);
static int InputMenuGamepadButtonIsAccept(SDL_GamepadButton eButton);
static int InputMenuGamepadButtonIsBack(SDL_GamepadButton eButton);
static int InputMenuGamepadButtonIsCupSwitch(SDL_GamepadButton eButton);
static int InputMenuGamepadButtonDown(tInputDevice *pDevice, SDL_GamepadButton eButton);
static void InputMenuRememberAxisRests(void);
static void InputMenuRememberButtonStates(void);
static void InputMenuResetKeyStates(void);

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

#if defined(_WIN32)
static int InputUsingSDLDirectInput(void)
{
  return s_eWindowsBackend == INPUT_WINDOWS_BACKEND_SDL_DINPUT;
}

//-------------------------------------------------------------------------------------------------

static void InputApplyWindowsBackendHint(void)
{
  SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_DIRECTINPUT, InputUsingSDLDirectInput() ? "1" : "0", SDL_HINT_OVERRIDE);
}

//-------------------------------------------------------------------------------------------------

static Uint32 InputGetSDLInitFlags(void)
{
  if (InputUsingSDLDirectInput())
    return SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD;

  return 0;
}
#endif

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
  SDL_free(pDevice->piMenuAxisRest);
  SDL_free(pDevice->pbyButtons);
  SDL_free(pDevice->pbyMenuPrevButtons);
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

static int InputAllocateDeviceState(tInputDevice *pDevice)
{
  if (pDevice->iNumAxes > 0) {
    pDevice->piAxes = (int *)SDL_calloc((size_t)pDevice->iNumAxes, sizeof(int));
    if (!pDevice->piAxes) {
      InputCloseDevice(pDevice);
      return 0;
    }
    pDevice->piMenuAxisRest = (int *)SDL_calloc((size_t)pDevice->iNumAxes, sizeof(int));
    if (!pDevice->piMenuAxisRest) {
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
    pDevice->pbyMenuPrevButtons = (uint8 *)SDL_calloc((size_t)pDevice->iNumButtons, sizeof(uint8));
    if (!pDevice->pbyMenuPrevButtons) {
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

  return 1;
}

//-------------------------------------------------------------------------------------------------

static int InputOpenDevice(SDL_JoystickID joyId, int iOrdinal)
{
  bool bGamepad = SDL_IsGamepad(joyId);
  const char *szName;
  const char *szPath;
  tInputDevice *pDevice;

#if defined(_WIN32)
  if (!bGamepad && !InputUsingSDLDirectInput())
    return 0;
#endif

  if (!InputGrowDeviceList())
    return 0;

  pDevice = &s_pDevices[s_iNumDevices];
  pDevice->bGamepad = bGamepad;

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

  if (!InputAllocateDeviceState(pDevice))
    return 0;

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

#if defined(_WIN32)
enum
{
  INPUT_WINMM_AXIS_X,
  INPUT_WINMM_AXIS_Y,
  INPUT_WINMM_AXIS_Z,
  INPUT_WINMM_AXIS_R,
  INPUT_WINMM_AXIS_U,
  INPUT_WINMM_AXIS_V
};

//-------------------------------------------------------------------------------------------------

static int InputReadWinMMInfo(UINT uJoyId, JOYINFOEX *pInfo)
{
  memset(pInfo, 0, sizeof(*pInfo));
  pInfo->dwSize = sizeof(*pInfo);
  pInfo->dwFlags = JOY_RETURNALL | JOY_RETURNPOVCTS;
  return joyGetPosEx(uJoyId, pInfo) == MMSYSERR_NOERROR;
}

//-------------------------------------------------------------------------------------------------

static SDL_GUID InputMakeWinMMGuid(const JOYCAPSA *pCaps, UINT uJoyId)
{
  SDL_GUID guid;
  uint32 uHash = 2166136261u;
  const unsigned char *pName = (const unsigned char *)pCaps->szPname;

  memset(&guid, 0, sizeof(guid));
  while (*pName) {
    uHash ^= (uint32)*pName++;
    uHash *= 16777619u;
  }

  guid.data[0] = 'W';
  guid.data[1] = 'M';
  guid.data[2] = 'M';
  guid.data[3] = 0;
  guid.data[4] = (uint8)(pCaps->wMid & 0xFF);
  guid.data[5] = (uint8)((pCaps->wMid >> 8) & 0xFF);
  guid.data[6] = (uint8)(pCaps->wPid & 0xFF);
  guid.data[7] = (uint8)((pCaps->wPid >> 8) & 0xFF);
  guid.data[8] = (uint8)(uJoyId & 0xFF);
  guid.data[9] = (uint8)((uJoyId >> 8) & 0xFF);
  guid.data[10] = (uint8)(uHash & 0xFF);
  guid.data[11] = (uint8)((uHash >> 8) & 0xFF);
  guid.data[12] = (uint8)((uHash >> 16) & 0xFF);
  guid.data[13] = (uint8)((uHash >> 24) & 0xFF);
  guid.data[14] = (uint8)(pCaps->wNumAxes & 0xFF);
  guid.data[15] = (uint8)(pCaps->wNumButtons & 0xFF);
  return guid;
}

//-------------------------------------------------------------------------------------------------

static void InputAddWinMMAxis(tInputDevice *pDevice, uint8 byAxisMap, uint32 dwMin, uint32 dwMax)
{
  int iAxis = pDevice->iNumAxes;

  if (iAxis >= INPUT_WINMM_AXIS_COUNT || dwMax <= dwMin)
    return;

  pDevice->byWinMMAxisMap[iAxis] = byAxisMap;
  pDevice->dwWinMMAxisMin[iAxis] = dwMin;
  pDevice->dwWinMMAxisMax[iAxis] = dwMax;
  ++pDevice->iNumAxes;
}

//-------------------------------------------------------------------------------------------------

static DWORD InputGetWinMMAxisValue(const JOYINFOEX *pInfo, uint8 byAxisMap)
{
  switch (byAxisMap) {
    case INPUT_WINMM_AXIS_X: return pInfo->dwXpos;
    case INPUT_WINMM_AXIS_Y: return pInfo->dwYpos;
    case INPUT_WINMM_AXIS_Z: return pInfo->dwZpos;
    case INPUT_WINMM_AXIS_R: return pInfo->dwRpos;
    case INPUT_WINMM_AXIS_U: return pInfo->dwUpos;
    case INPUT_WINMM_AXIS_V: return pInfo->dwVpos;
    default: break;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static int InputNormalizeWinMMAxis(DWORD dwValue, uint32 dwMin, uint32 dwMax)
{
  uint64 ullRange;
  int iValue;

  if (dwMax <= dwMin)
    return 0;

  if (dwValue < dwMin)
    dwValue = dwMin;
  if (dwValue > dwMax)
    dwValue = dwMax;

  ullRange = (uint64)(dwMax - dwMin);
  iValue = (int)((((uint64)(dwValue - dwMin) * 65535u) + (ullRange / 2u)) / ullRange) - 32768;
  return InputClampInt(iValue, SDL_JOYSTICK_AXIS_MIN, SDL_JOYSTICK_AXIS_MAX);
}

//-------------------------------------------------------------------------------------------------

static uint8 InputWinMMPOVToSDLHat(DWORD dwPOV)
{
  DWORD dwSector;

  if (dwPOV == JOY_POVCENTERED || dwPOV >= 36000)
    return SDL_HAT_CENTERED;

  dwSector = ((dwPOV + 2250) % 36000) / 4500;
  switch (dwSector) {
    case 0: return SDL_HAT_UP;
    case 1: return SDL_HAT_RIGHTUP;
    case 2: return SDL_HAT_RIGHT;
    case 3: return SDL_HAT_RIGHTDOWN;
    case 4: return SDL_HAT_DOWN;
    case 5: return SDL_HAT_LEFTDOWN;
    case 6: return SDL_HAT_LEFT;
    case 7: return SDL_HAT_LEFTUP;
    default: break;
  }

  return SDL_HAT_CENTERED;
}

//-------------------------------------------------------------------------------------------------

static void InputUpdateWinMMDevice(tInputDevice *pDevice)
{
  JOYINFOEX info;

  if (!InputReadWinMMInfo((UINT)pDevice->uWinMMId, &info)) {
    for (int i = 0; i < pDevice->iNumAxes; ++i)
      pDevice->piAxes[i] = 0;
    for (int i = 0; i < pDevice->iNumButtons; ++i)
      pDevice->pbyButtons[i] = 0;
    for (int i = 0; i < pDevice->iNumHats; ++i)
      pDevice->pbyHats[i] = SDL_HAT_CENTERED;
    return;
  }

  for (int i = 0; i < pDevice->iNumAxes; ++i) {
    pDevice->piAxes[i] = InputNormalizeWinMMAxis(
      InputGetWinMMAxisValue(&info, pDevice->byWinMMAxisMap[i]),
      pDevice->dwWinMMAxisMin[i],
      pDevice->dwWinMMAxisMax[i]);
  }
  for (int i = 0; i < pDevice->iNumButtons; ++i)
    pDevice->pbyButtons[i] = (info.dwButtons & (1u << i)) ? 1 : 0;
  if (pDevice->iNumHats > 0)
    pDevice->pbyHats[0] = InputWinMMPOVToSDLHat(info.dwPOV);
}

//-------------------------------------------------------------------------------------------------

static uint32 InputGetWinMMConnectedMask(void)
{
  UINT uNumDevs = joyGetNumDevs();
  uint32 uMask = 0;

  if (uNumDevs > 32)
    uNumDevs = 32;

  for (UINT uJoyId = 0; uJoyId < uNumDevs; ++uJoyId) {
    JOYCAPSA caps;
    JOYINFOEX info;

    if (joyGetDevCapsA(uJoyId, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
      continue;
    if (!InputReadWinMMInfo(uJoyId, &info))
      continue;
    uMask |= (uint32)(1u << uJoyId);
  }

  return uMask;
}

//-------------------------------------------------------------------------------------------------

static int InputOpenWinMMDevice(UINT uJoyId)
{
  JOYCAPSA caps;
  JOYINFOEX info;
  tInputDevice *pDevice;

  if (joyGetDevCapsA(uJoyId, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
    return 0;
  if (!InputReadWinMMInfo(uJoyId, &info))
    return 0;
  if (!InputGrowDeviceList())
    return 0;

  pDevice = &s_pDevices[s_iNumDevices];
  pDevice->bWinMM = true;
  pDevice->uWinMMId = (uint32)uJoyId;
  pDevice->joyId = (SDL_JoystickID)(0x80000000u | (uJoyId + 1u));
  pDevice->guid = InputMakeWinMMGuid(&caps, uJoyId);
  pDevice->unVendor = (uint16)caps.wMid;
  pDevice->unProduct = (uint16)caps.wPid;
  pDevice->unVersion = 0;
  pDevice->iOrdinal = (int)uJoyId;
  pDevice->iNumButtons = caps.wNumButtons > 32 ? 32 : caps.wNumButtons;
  pDevice->iNumHats = (caps.wCaps & JOYCAPS_HASPOV) ? 1 : 0;

  if (caps.wNumAxes > 0)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_X, caps.wXmin, caps.wXmax);
  if (caps.wNumAxes > 1)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_Y, caps.wYmin, caps.wYmax);
  if ((caps.wCaps & JOYCAPS_HASZ) && pDevice->iNumAxes < caps.wNumAxes)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_Z, caps.wZmin, caps.wZmax);
  if ((caps.wCaps & JOYCAPS_HASR) && pDevice->iNumAxes < caps.wNumAxes)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_R, caps.wRmin, caps.wRmax);
  if ((caps.wCaps & JOYCAPS_HASU) && pDevice->iNumAxes < caps.wNumAxes)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_U, caps.wUmin, caps.wUmax);
  if ((caps.wCaps & JOYCAPS_HASV) && pDevice->iNumAxes < caps.wNumAxes)
    InputAddWinMMAxis(pDevice, INPUT_WINMM_AXIS_V, caps.wVmin, caps.wVmax);

  if (!InputAllocateDeviceState(pDevice))
    return 0;

  InputCopyString(pDevice->szName, sizeof(pDevice->szName), caps.szPname);
  snprintf(pDevice->szPath, sizeof(pDevice->szPath), "winmm:%u:%s", (unsigned)uJoyId, caps.szPname);
  InputUpdateWinMMDevice(pDevice);

  ++s_iNumDevices;
  return 1;
}

//-------------------------------------------------------------------------------------------------

static void InputOpenWinMMDevices(void)
{
  UINT uNumDevs = joyGetNumDevs();

  if (uNumDevs > 32)
    uNumDevs = 32;

  s_uWinMMDeviceMask = InputGetWinMMConnectedMask();
  s_ullNextWinMMRefreshMs = SDL_GetTicks() + INPUT_WINMM_REFRESH_MS;
  for (UINT uJoyId = 0; uJoyId < uNumDevs; ++uJoyId) {
    if (s_uWinMMDeviceMask & (uint32)(1u << uJoyId))
      InputOpenWinMMDevice(uJoyId);
  }
}

//-------------------------------------------------------------------------------------------------

static int InputMaybeRefreshWinMMDevices(void)
{
  uint64 ullNowMs;
  uint32 uMask;

  if (s_bRefreshingDevices)
    return 0;

  ullNowMs = SDL_GetTicks();
  if (ullNowMs < s_ullNextWinMMRefreshMs)
    return 0;

  s_ullNextWinMMRefreshMs = ullNowMs + INPUT_WINMM_REFRESH_MS;
  uMask = InputGetWinMMConnectedMask();
  if (uMask == s_uWinMMDeviceMask)
    return 0;

  InputRefreshDevices();
  return 1;
}
#endif

//-------------------------------------------------------------------------------------------------

static int InputBindingMatchesDevice(const tInputBinding *pBinding, const tInputDevice *pDevice)
{
  if (pBinding->joyId && pBinding->joyId == pDevice->joyId)
    return 1;

  if (!InputGuidIsZero(pBinding->guid) && !InputGuidEquals(pBinding->guid, pDevice->guid)) {
#if defined(_WIN32)
    if (pDevice->bWinMM &&
        pBinding->szName[0] &&
        pDevice->szName[0] &&
        strcmp(pBinding->szName, pDevice->szName) == 0 &&
        (pBinding->iOrdinal < 0 || pBinding->iOrdinal == pDevice->iOrdinal)) {
      return 1;
    }
#endif
    return 0;
  }

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

static void InputCancelPendingDeviceRefresh(void)
{
  s_bDeviceRefreshPending = false;
  s_ullDeviceRefreshMs = 0;
}

//-------------------------------------------------------------------------------------------------

static void InputRequestDeviceRefresh(void)
{
  s_bDeviceRefreshPending = true;
  s_ullDeviceRefreshMs = SDL_GetTicks() + INPUT_HOTPLUG_REFRESH_DELAY_MS;
}

//-------------------------------------------------------------------------------------------------

#if defined(_WIN32)
static int InputSDLDeviceListMatchesOpenDevices(SDL_JoystickID *pJoystickIds, int iCount)
{
  if (iCount > 0 && !pJoystickIds)
    return 0;
  if (s_iNumDevices != iCount)
    return 0;

  for (int i = 0; i < iCount; ++i) {
    int iFound = 0;
    for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
      if (!s_pDevices[iDevice].bWinMM && s_pDevices[iDevice].joyId == pJoystickIds[i]) {
        iFound = 1;
        break;
      }
    }
    if (!iFound)
      return 0;
  }

  return 1;
}

//-------------------------------------------------------------------------------------------------

static int InputSDLDirectInputDeviceListChanged(void)
{
  int iCount = 0;
  SDL_JoystickID *pJoystickIds = SDL_GetJoysticks(&iCount);
  int iChanged = !InputSDLDeviceListMatchesOpenDevices(pJoystickIds, iCount);
  SDL_free(pJoystickIds);
  return iChanged;
}

//-------------------------------------------------------------------------------------------------
#endif

//-------------------------------------------------------------------------------------------------

void InputRefreshDevices(void)
{
  int iCount = 0;

  if (!s_bInitialized)
    return;

  InputCancelPendingDeviceRefresh();
#if defined(_WIN32)
  s_bRefreshingDevices = true;
#endif
  InputFreeCaptureSnapshot();
  s_bCaptureActive = false;
  s_bWaitingForRelease = false;
  InputCloseAllDevices();

#if defined(_WIN32)
  if (InputUsingSDLDirectInput()) {
    SDL_JoystickID *pJoystickIds = SDL_GetJoysticks(&iCount);
    for (int i = 0; pJoystickIds && i < iCount; ++i)
      InputOpenDevice(pJoystickIds[i], i);
    SDL_free(pJoystickIds);
  } else {
    InputOpenWinMMDevices();
  }
#else
  {
    SDL_JoystickID *pJoystickIds = SDL_GetJoysticks(&iCount);
    for (int i = 0; pJoystickIds && i < iCount; ++i)
      InputOpenDevice(pJoystickIds[i], i);
    SDL_free(pJoystickIds);
  }
#endif

  InputResolveAllBindings();
  InputUpdate();
  InputMenuRememberAxisRests();
  InputMenuRememberButtonStates();
  InputMenuResetKeyStates();
#if defined(_WIN32)
  s_bRefreshingDevices = false;
#endif
}

//-------------------------------------------------------------------------------------------------

static int InputMaybeApplyPendingDeviceRefresh(void)
{
  uint64 ullNowMs;

  if (!s_bDeviceRefreshPending)
    return 0;

  ullNowMs = SDL_GetTicks();
  if (ullNowMs < s_ullDeviceRefreshMs)
    return 0;

  InputCancelPendingDeviceRefresh();

#if defined(_WIN32)
  if (InputUsingSDLDirectInput() && !InputSDLDirectInputDeviceListChanged())
    return 0;
#endif

  InputRefreshDevices();
  return 1;
}

//-------------------------------------------------------------------------------------------------

#if defined(_WIN32)
eInputWindowsBackend InputGetWindowsBackend(void)
{
  return s_eWindowsBackend;
}

//-------------------------------------------------------------------------------------------------

void InputSetWindowsBackend(eInputWindowsBackend eBackend)
{
  Uint32 uiInitFlags;

  if (eBackend != INPUT_WINDOWS_BACKEND_SDL_DINPUT)
    eBackend = INPUT_WINDOWS_BACKEND_WINMM;

  if (s_eWindowsBackend == eBackend) {
    InputApplyWindowsBackendHint();
    return;
  }

  s_eWindowsBackend = eBackend;
  InputApplyWindowsBackendHint();

  if (!s_bInitialized)
    return;

  InputFreeCaptureSnapshot();
  s_bCaptureActive = false;
  s_bWaitingForRelease = false;
  InputCancelPendingDeviceRefresh();
  InputCloseAllDevices();
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);

  uiInitFlags = InputGetSDLInitFlags();
  if (uiInitFlags && !SDL_InitSubSystem(uiInitFlags)) {
    SDL_Log("InputSetWindowsBackend: SDL_InitSubSystem failed: %s", SDL_GetError());
    return;
  }

  InputRefreshDevices();
}

//-------------------------------------------------------------------------------------------------
#endif

//-------------------------------------------------------------------------------------------------

void InputInit(void)
{
  Uint32 uiInitFlags;

  if (s_bInitialized)
    return;

  s_bInitialized = true;
  InputResetBindings();

#if defined(_WIN32)
  InputApplyWindowsBackendHint();
  uiInitFlags = InputGetSDLInitFlags();
#else
  uiInitFlags = SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD;
#endif

  if (uiInitFlags && !SDL_InitSubSystem(uiInitFlags)) {
    SDL_Log("InputInit: SDL_InitSubSystem failed: %s", SDL_GetError());
    return;
  }

  InputRefreshDevices();
}

//-------------------------------------------------------------------------------------------------

void InputShutdown(void)
{
  if (!s_bInitialized)
    return;

  InputFreeCaptureSnapshot();
  s_bCaptureActive = false;
  s_bWaitingForRelease = false;
  InputCancelPendingDeviceRefresh();
  InputCloseAllDevices();
#if defined(_WIN32)
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
#else
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif
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
    InputRequestDeviceRefresh();
  }
}

//-------------------------------------------------------------------------------------------------

void InputUpdate(void)
{
  if (!s_bInitialized)
    return;

  if (InputMaybeApplyPendingDeviceRefresh())
    return;

#if defined(_WIN32)
  if (!InputUsingSDLDirectInput() && InputMaybeRefreshWinMMDevices())
    return;
#endif

  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
    tInputDevice *pDevice = &s_pDevices[iDevice];
#if defined(_WIN32)
    if (pDevice->bWinMM) {
      InputUpdateWinMMDevice(pDevice);
      continue;
    }
#endif
    for (int i = 0; i < pDevice->iNumAxes; ++i)
      pDevice->piAxes[i] = SDL_GetJoystickAxis(pDevice->pJoystick, i);
    for (int i = 0; i < pDevice->iNumButtons; ++i)
      pDevice->pbyButtons[i] = SDL_GetJoystickButton(pDevice->pJoystick, i) ? 1 : 0;
    for (int i = 0; i < pDevice->iNumHats; ++i)
      pDevice->pbyHats[i] = SDL_GetJoystickHat(pDevice->pJoystick, i);
  }
}

//-------------------------------------------------------------------------------------------------

static void InputMenuRememberAxisRests(void)
{
  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
    tInputDevice *pDevice = &s_pDevices[iDevice];
    for (int i = 0; i < pDevice->iNumAxes; ++i)
      pDevice->piMenuAxisRest[i] = pDevice->piAxes[i];
  }
}

//-------------------------------------------------------------------------------------------------

static void InputMenuRememberButtonStates(void)
{
  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice) {
    tInputDevice *pDevice = &s_pDevices[iDevice];

    for (int i = 0; i < pDevice->iNumButtons; ++i)
      pDevice->pbyMenuPrevButtons[i] = pDevice->pbyButtons[i];

    if (pDevice->bGamepad) {
      for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        pDevice->byMenuPrevGamepadButtons[i] = (uint8)InputMenuGamepadButtonDown(pDevice, (SDL_GamepadButton)i);
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void InputMenuResetKeyStates(void)
{
  memset(&s_menuUpState, 0, sizeof(s_menuUpState));
  memset(&s_menuDownState, 0, sizeof(s_menuDownState));
  memset(&s_menuLeftState, 0, sizeof(s_menuLeftState));
  memset(&s_menuRightState, 0, sizeof(s_menuRightState));
  memset(&s_menuAcceptState, 0, sizeof(s_menuAcceptState));
  memset(&s_menuBackState, 0, sizeof(s_menuBackState));
  memset(&s_menuAnyButtonState, 0, sizeof(s_menuAnyButtonState));
  memset(&s_menuQuitYesState, 0, sizeof(s_menuQuitYesState));
  memset(&s_menuQuitCancelState, 0, sizeof(s_menuQuitCancelState));
  s_bMenuWaitForRelease = false;
}

//-------------------------------------------------------------------------------------------------

static void InputMenuTapKey(uint8 byScancode)
{
  int iBytesNeeded = byScancode >= 0x48 && byScancode <= 0x50 ? 2 : 1;
  int iUsed = (write_key - read_key) & 0x3F;

  if (iUsed > 63 - iBytesNeeded)
    return;

  if (iBytesNeeded == 2) {
    key_buffer[write_key] = 0;
    write_key = (write_key + 1) & 0x3F;
  }

  key_buffer[write_key] = byScancode;
  write_key = (write_key + 1) & 0x3F;
}

//-------------------------------------------------------------------------------------------------

static void InputMenuUpdateKeyState(tInputMenuKeyState *pState, int iDown, uint8 byScancode, int iRepeat, uint64 ullNowMs)
{
  if (!iDown) {
    pState->bDown = false;
    pState->ullNextRepeatMs = 0;
    return;
  }

  if (!pState->bDown) {
    pState->bDown = true;
    pState->ullNextRepeatMs = ullNowMs + INPUT_MENU_REPEAT_INITIAL_MS;
    InputMenuTapKey(byScancode);
    return;
  }

  if (iRepeat && ullNowMs >= pState->ullNextRepeatMs) {
    pState->ullNextRepeatMs = ullNowMs + INPUT_MENU_REPEAT_MS;
    InputMenuTapKey(byScancode);
  }
}

//-------------------------------------------------------------------------------------------------

static int InputMenuAnyButtonContext(void)
{
  if (intro || winner_mode)
    return 1;

  switch (eFrontendCurrentState) {
    case eFRONTEND_STATE_WINNER_SCREEN:
    case eFRONTEND_STATE_RESULT_ROUNDUP:
    case eFRONTEND_STATE_RACE_RESULT:
    case eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS:
    case eFRONTEND_STATE_TEAM_STANDINGS:
    case eFRONTEND_STATE_LAP_RECORDS:
    case eFRONTEND_STATE_TIME_TRIAL_RESULTS:
    case eFRONTEND_STATE_CHAMPIONSHIP_OVER:
      return 1;
    default:
      break;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static int InputMenuGamepadButtonIsAccept(SDL_GamepadButton eButton)
{
  return eButton == SDL_GAMEPAD_BUTTON_SOUTH;
}

//-------------------------------------------------------------------------------------------------

static int InputMenuGamepadButtonIsBack(SDL_GamepadButton eButton)
{
  return eButton == SDL_GAMEPAD_BUTTON_EAST;
}

//-------------------------------------------------------------------------------------------------

static int InputMenuGamepadButtonIsCupSwitch(SDL_GamepadButton eButton)
{
  return eButton == SDL_GAMEPAD_BUTTON_WEST || eButton == SDL_GAMEPAD_BUTTON_NORTH;
}

//-------------------------------------------------------------------------------------------------

static int InputMenuGamepadButtonDown(tInputDevice *pDevice, SDL_GamepadButton eButton)
{
  if (!pDevice->pGamepad || !SDL_GamepadHasButton(pDevice->pGamepad, eButton))
    return 0;

  return SDL_GetGamepadButton(pDevice->pGamepad, eButton) ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------

static int InputMenuGamepadAxis(tInputDevice *pDevice, SDL_GamepadAxis eAxis)
{
  if (!pDevice->pGamepad || !SDL_GamepadHasAxis(pDevice->pGamepad, eAxis))
    return 0;

  return SDL_GetGamepadAxis(pDevice->pGamepad, eAxis);
}

//-------------------------------------------------------------------------------------------------

static void InputMenuApplyAxis(int iValue, int iRestValue, int iVertical, int *piLeft, int *piRight, int *piUp, int *piDown)
{
  int iDelta = iValue - iRestValue;

  if (iDelta <= -INPUT_MENU_AXIS_DEADZONE) {
    if (iVertical)
      *piUp = 1;
    else
      *piLeft = 1;
  } else if (iDelta >= INPUT_MENU_AXIS_DEADZONE) {
    if (iVertical)
      *piDown = 1;
    else
      *piRight = 1;
  }
}

//-------------------------------------------------------------------------------------------------

static void InputMenuCollectDeviceState(
    tInputDevice *pDevice,
    int iDeviceIdx,
    int iExcludeDeviceRef,
    int iExcludeAxisIndex,
    int *piLeft,
    int *piRight,
    int *piUp,
    int *piDown,
    int *piAccept,
    int *piBack,
    int *piPause,
    int *piAnyButtonPressed,
    int *piCupSwitchPressed,
    int *piButton1Pressed,
    int *piButton2Pressed)
{
  int bExcludeAxis = iDeviceIdx == iExcludeDeviceRef;

  if (pDevice->bGamepad) {
    if (!(bExcludeAxis && iExcludeAxisIndex == SDL_GAMEPAD_AXIS_LEFTX))
      InputMenuApplyAxis(InputMenuGamepadAxis(pDevice, SDL_GAMEPAD_AXIS_LEFTX), 0, 0, piLeft, piRight, piUp, piDown);
    if (!(bExcludeAxis && iExcludeAxisIndex == SDL_GAMEPAD_AXIS_RIGHTX))
      InputMenuApplyAxis(InputMenuGamepadAxis(pDevice, SDL_GAMEPAD_AXIS_RIGHTX), 0, 0, piLeft, piRight, piUp, piDown);
    if (!(bExcludeAxis && iExcludeAxisIndex == SDL_GAMEPAD_AXIS_LEFTY))
      InputMenuApplyAxis(InputMenuGamepadAxis(pDevice, SDL_GAMEPAD_AXIS_LEFTY), 0, 1, piLeft, piRight, piUp, piDown);
    if (!(bExcludeAxis && iExcludeAxisIndex == SDL_GAMEPAD_AXIS_RIGHTY))
      InputMenuApplyAxis(InputMenuGamepadAxis(pDevice, SDL_GAMEPAD_AXIS_RIGHTY), 0, 1, piLeft, piRight, piUp, piDown);

    for (int iButton = 0; iButton < SDL_GAMEPAD_BUTTON_COUNT; ++iButton) {
      int iDown = InputMenuGamepadButtonDown(pDevice, (SDL_GamepadButton)iButton);
      int iPressed = iDown && !pDevice->byMenuPrevGamepadButtons[iButton];
      pDevice->byMenuPrevGamepadButtons[iButton] = (uint8)iDown;

      if (iDown) {
        if (InputMenuGamepadButtonIsAccept((SDL_GamepadButton)iButton))
          *piAccept = 1;
        if (InputMenuGamepadButtonIsBack((SDL_GamepadButton)iButton))
          *piBack = 1;
      }

      if (iPressed) {
        *piAnyButtonPressed = 1;
        if (InputMenuGamepadButtonIsAccept((SDL_GamepadButton)iButton))
          *piButton1Pressed = 1;
        if (InputMenuGamepadButtonIsBack((SDL_GamepadButton)iButton))
          *piButton2Pressed = 1;
        if (InputMenuGamepadButtonIsCupSwitch((SDL_GamepadButton)iButton))
          *piCupSwitchPressed = 1;
      }
    }

    if (InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_DPAD_LEFT))
      *piLeft = 1;
    if (InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
      *piRight = 1;
    if (InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_DPAD_UP))
      *piUp = 1;
    if (InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
      *piDown = 1;

    if (InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_START) ||
        InputMenuGamepadButtonDown(pDevice, SDL_GAMEPAD_BUTTON_BACK))
      *piPause = 1;
  } else {
    // Raw axes on wheels and joysticks often include pedals or throttles, so only hats steer menus here.
    if (pDevice->iNumButtons > 0 && pDevice->pbyButtons[0])
      *piAccept = 1;
    if (pDevice->iNumButtons > 1 && pDevice->pbyButtons[1])
      *piBack = 1;
    for (int i = 0; i < pDevice->iNumButtons; ++i) {
      int iDown = pDevice->pbyButtons[i] != 0;
      int iPressed = iDown && !pDevice->pbyMenuPrevButtons[i];
      pDevice->pbyMenuPrevButtons[i] = (uint8)iDown;
      if (iPressed) {
        *piAnyButtonPressed = 1;
        if (i == 0)
          *piButton1Pressed = 1;
        if (i == 1)
          *piButton2Pressed = 1;
        if (i == 2 || i == 3)
          *piCupSwitchPressed = 1;
      }
    }
  }

  for (int i = 0; i < pDevice->iNumHats; ++i) {
    uint8 byHat = pDevice->pbyHats[i];
    if (byHat & SDL_HAT_LEFT)
      *piLeft = 1;
    if (byHat & SDL_HAT_RIGHT)
      *piRight = 1;
    if (byHat & SDL_HAT_UP)
      *piUp = 1;
    if (byHat & SDL_HAT_DOWN)
      *piDown = 1;
  }
}

//-------------------------------------------------------------------------------------------------

void InputUpdateMenuControls(void)
{
  int iMenuActive = frontend_on || game_req;
  int iAxisTuneActive;
  int iCaptureActive;
  int iExcludeDeviceRef = -1;
  int iExcludeAxisIndex = -1;
  int iLeft = 0;
  int iRight = 0;
  int iUp = 0;
  int iDown = 0;
  int iAccept = 0;
  int iBack = 0;
  int iPause = 0;
  int iAnyButtonPressed = 0;
  int iCupSwitchPressed = 0;
  int iButton1Pressed = 0;
  int iButton2Pressed = 0;
  int iAnyButtonContext;
  int iQuitConfirm;
  int iAnyMenuInput;
  uint64 ullNowMs;

  if (!s_bInitialized)
    return;

  iAxisTuneActive = frontend_config_axis_tune_active() || pause_axis_tune_active();
  iCaptureActive = (define_mode || control_edit >= 0) && !iAxisTuneActive;

  if (iAxisTuneActive && control_edit >= 0) {
    tInputBinding *pTuneBinding = &g_inputBindings[control_edit];
    if (pTuneBinding->eType == INPUT_BINDING_JOYSTICK_AXIS) {
      InputResolveBindingDevice(pTuneBinding);
      iExcludeDeviceRef = pTuneBinding->iDeviceRef;
      iExcludeAxisIndex = pTuneBinding->iInputIndex;
    }
  }

  iAnyButtonContext = InputMenuAnyButtonContext();
  iQuitConfirm = trying_to_exit || frontend_main_menu_quit_confirm_active();

  for (int iDevice = 0; iDevice < s_iNumDevices; ++iDevice)
    InputMenuCollectDeviceState(
        &s_pDevices[iDevice],
        iDevice,
        iExcludeDeviceRef,
        iExcludeAxisIndex,
        &iLeft,
        &iRight,
        &iUp,
        &iDown,
        &iAccept,
        &iBack,
        &iPause,
        &iAnyButtonPressed,
        &iCupSwitchPressed,
        &iButton1Pressed,
        &iButton2Pressed);

  iAnyMenuInput = iLeft || iRight || iUp || iDown || iAccept || iBack || iPause || iCupSwitchPressed;

  if (iCaptureActive)
    s_bMenuWaitForRelease = true;

  if (s_bMenuWaitForRelease) {
    if (iAnyMenuInput)
      iCaptureActive = 1;
    else
      s_bMenuWaitForRelease = false;
  }

  if (!iMenuActive || iCaptureActive || iAnyButtonContext || iQuitConfirm) {
    iLeft = 0;
    iRight = 0;
    iUp = 0;
    iDown = 0;
    iAccept = 0;
    iCupSwitchPressed = 0;
    if (!iQuitConfirm)
      iBack = 0;
  }

  if (iCaptureActive || iAnyButtonContext)
    iPause = 0;
  if (iCaptureActive)
    iButton1Pressed = 0;
  if (iCaptureActive)
    iButton2Pressed = 0;
  if (iCaptureActive || iQuitConfirm)
    iAnyButtonPressed = 0;
  if (iCaptureActive || iAnyButtonContext || iQuitConfirm)
    iCupSwitchPressed = 0;
  if (iQuitConfirm) {
    iBack = 0;
    iPause = 0;
  }

  ullNowMs = SDL_GetTicks();
  InputMenuUpdateKeyState(&s_menuLeftState, iLeft, WHIP_SCANCODE_LEFT, 1, ullNowMs);
  InputMenuUpdateKeyState(&s_menuRightState, iRight, WHIP_SCANCODE_RIGHT, 1, ullNowMs);
  InputMenuUpdateKeyState(&s_menuUpState, iUp, WHIP_SCANCODE_UP, 1, ullNowMs);
  InputMenuUpdateKeyState(&s_menuDownState, iDown, WHIP_SCANCODE_DOWN, 1, ullNowMs);
  InputMenuUpdateKeyState(&s_menuAcceptState, iAccept, WHIP_SCANCODE_RETURN, 0, ullNowMs);
  InputMenuUpdateKeyState(&s_menuBackState, iBack || iPause, WHIP_SCANCODE_ESCAPE, 0, ullNowMs);
  InputMenuUpdateKeyState(&s_menuAnyButtonState, (iAnyButtonContext && iAnyButtonPressed) || iCupSwitchPressed, WHIP_SCANCODE_SPACE, 0, ullNowMs);
  InputMenuUpdateKeyState(&s_menuQuitYesState, iQuitConfirm && iButton1Pressed, WHIP_SCANCODE_Y, 0, ullNowMs);
  InputMenuUpdateKeyState(&s_menuQuitCancelState, iQuitConfirm && iButton2Pressed, WHIP_SCANCODE_ESCAPE, 0, ullNowMs);
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

static void InputSetCapturedButton(tInputBinding *pBindingOut, const tInputDevice *pDevice, int iAction, int iInputIndex, bool bGamepadInput)
{
  InputClearBinding(pBindingOut);
  pBindingOut->eType = INPUT_BINDING_JOYSTICK_BUTTON;
  pBindingOut->iInputIndex = iInputIndex;
  pBindingOut->bGamepadInput = bGamepadInput;
  pBindingOut->iKeyScancode = userkey[iAction];
  InputCopyDeviceIdentity(pBindingOut, pDevice);
}

//-------------------------------------------------------------------------------------------------

static void InputSetCapturedAxis(tInputBinding *pBindingOut, const tInputDevice *pDevice, int iAction, int iInputIndex, int iBaseValue, int iDelta, bool bGamepadInput)
{
  InputClearBinding(pBindingOut);
  pBindingOut->eType = INPUT_BINDING_JOYSTICK_AXIS;
  pBindingOut->iInputIndex = iInputIndex;
  pBindingOut->iDirection = iDelta < 0 ? -1 : 1;
  pBindingOut->eAxisMode = InputIsSteeringAction(iAction) ? INPUT_AXIS_CENTERED : INPUT_AXIS_PEDAL;
  pBindingOut->iDeadzone = pBindingOut->eAxisMode == INPUT_AXIS_PEDAL ? INPUT_PEDAL_DEADZONE : INPUT_DEFAULT_DEADZONE;
  pBindingOut->iThreshold = INPUT_DEFAULT_THRESHOLD;
  pBindingOut->iRestValue = pBindingOut->eAxisMode == INPUT_AXIS_PEDAL ? iBaseValue : 0;
  pBindingOut->bGamepadInput = bGamepadInput;
  pBindingOut->iKeyScancode = userkey[iAction];
  InputCopyDeviceIdentity(pBindingOut, pDevice);
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
  int iReleaseDeadzone;

  if (!s_bWaitingForRelease)
    return 0;

  binding = s_releaseBinding;
  switch (binding.eType) {
    case INPUT_BINDING_JOYSTICK_BUTTON:
      return InputReadButton(&binding);
    case INPUT_BINDING_JOYSTICK_HAT:
      return InputReadHat(&binding);
    case INPUT_BINDING_JOYSTICK_AXIS:
      iReleaseDeadzone = binding.iDeadzone;
      if (iReleaseDeadzone <= 0)
        iReleaseDeadzone = INPUT_DEFAULT_DEADZONE;
      return abs(InputReadAxisRaw(&binding) - binding.iRestValue) > iReleaseDeadzone;
    default:
      break;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static int InputGetGamepadAxisCaptureDelta(SDL_GamepadAxis eAxis)
{
  if (eAxis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
      eAxis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
    return INPUT_TRIGGER_CAPTURE_DELTA;

  return INPUT_AXIS_CAPTURE_DELTA;
}

//-------------------------------------------------------------------------------------------------

static tInputCaptureDevice *InputFindCaptureDevice(SDL_JoystickID joyId)
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
        if (pDevice->pGamepad) {
          for (int iAxis = 0; iAxis < SDL_GAMEPAD_AXIS_COUNT; ++iAxis) {
            if (SDL_GamepadHasAxis(pDevice->pGamepad, (SDL_GamepadAxis)iAxis))
              pCaptureDevice->iGamepadAxes[iAxis] = SDL_GetGamepadAxis(pDevice->pGamepad, (SDL_GamepadAxis)iAxis);
          }
          for (int iButton = 0; iButton < SDL_GAMEPAD_BUTTON_COUNT; ++iButton) {
            if (SDL_GamepadHasButton(pDevice->pGamepad, (SDL_GamepadButton)iButton))
              pCaptureDevice->byGamepadButtons[iButton] = SDL_GetGamepadButton(pDevice->pGamepad, (SDL_GamepadButton)iButton) ? 1 : 0;
          }
        }
      }
    }
  }

  s_bCaptureActive = true;
}

//-------------------------------------------------------------------------------------------------

int InputCapturePoll(int iAction, tInputBinding *pBindingOut)
{
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
    tInputCaptureDevice *pCaptureDevice = InputFindCaptureDevice(pDevice->joyId);

    if (pDevice->pGamepad) {
      for (int iButton = 0; iButton < SDL_GAMEPAD_BUTTON_COUNT; ++iButton) {
        int iWasDown;
        int iDown;

        if (!SDL_GamepadHasButton(pDevice->pGamepad, (SDL_GamepadButton)iButton))
          continue;

        iWasDown = pCaptureDevice ? pCaptureDevice->byGamepadButtons[iButton] != 0 : 0;
        iDown = SDL_GetGamepadButton(pDevice->pGamepad, (SDL_GamepadButton)iButton) ? 1 : 0;
        if (iWasDown) {
          if (!iDown)
            pCaptureDevice->byGamepadButtons[iButton] = 0;
          continue;
        }

        if (iDown) {
          InputSetCapturedButton(pBindingOut, pDevice, iAction, iButton, true);
          InputCaptureWaitForRelease(pBindingOut);
          return 1;
        }
      }

      for (int iAxis = 0; iAxis < SDL_GAMEPAD_AXIS_COUNT; ++iAxis) {
        int iBaseValue;
        int iDelta;

        if (!SDL_GamepadHasAxis(pDevice->pGamepad, (SDL_GamepadAxis)iAxis))
          continue;

        iBaseValue = pCaptureDevice ? pCaptureDevice->iGamepadAxes[iAxis] : 0;
        iDelta = SDL_GetGamepadAxis(pDevice->pGamepad, (SDL_GamepadAxis)iAxis) - iBaseValue;
        if (abs(iDelta) >= InputGetGamepadAxisCaptureDelta((SDL_GamepadAxis)iAxis)) {
          InputSetCapturedAxis(pBindingOut, pDevice, iAction, iAxis, iBaseValue, iDelta, true);
          InputCaptureWaitForRelease(pBindingOut);
          return 1;
        }
      }

      // Keep SDL gamepads in standard button/axis space. Raw fallback is for wheels and other joysticks.
      continue;
    }

    for (int iButton = 0; iButton < pDevice->iNumButtons; ++iButton) {
      int iWasDown = pCaptureDevice && pCaptureDevice->pbyButtons && iButton < pCaptureDevice->iNumButtons
        ? pCaptureDevice->pbyButtons[iButton] != 0
        : 0;
      if (iWasDown) {
        if (!pDevice->pbyButtons[iButton])
          pCaptureDevice->pbyButtons[iButton] = 0;
        continue;
      }

      if (pDevice->pbyButtons[iButton]) {
        InputSetCapturedButton(pBindingOut, pDevice, iAction, iButton, false);
        InputCaptureWaitForRelease(pBindingOut);
        return 1;
      }
    }

    for (int iHat = 0; iHat < pDevice->iNumHats; ++iHat) {
      int iWasHat = pCaptureDevice && pCaptureDevice->pbyHats && iHat < pCaptureDevice->iNumHats
        ? pCaptureDevice->pbyHats[iHat]
        : SDL_HAT_CENTERED;
      int iHatValue = pDevice->pbyHats[iHat];
      if (iWasHat != SDL_HAT_CENTERED) {
        if (iHatValue == SDL_HAT_CENTERED)
          pCaptureDevice->pbyHats[iHat] = SDL_HAT_CENTERED;
        continue;
      }
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
      if (abs(iDelta) >= INPUT_AXIS_CAPTURE_DELTA) {
        InputSetCapturedAxis(pBindingOut, pDevice, iAction, iAxis, iBaseValue, iDelta, false);
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

static int InputParseBoolSetting(const char *szValue, bool *pbOut)
{
  int iValue;

  if (!szValue || !pbOut)
    return 0;

  if (sscanf(szValue, "%i", &iValue) == 1) {
    *pbOut = iValue != 0;
    return 1;
  }

  if (InputStringEqualsNoCase(szValue, "true") ||
      InputStringEqualsNoCase(szValue, "yes") ||
      InputStringEqualsNoCase(szValue, "on")) {
    *pbOut = true;
    return 1;
  }

  if (InputStringEqualsNoCase(szValue, "false") ||
      InputStringEqualsNoCase(szValue, "no") ||
      InputStringEqualsNoCase(szValue, "off")) {
    *pbOut = false;
    return 1;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static void InputApplyMusicSource(int iUseCD)
{
  if (iUseCD) {
    MusicCD = -1;
    MusicCard = 0;
  } else {
    MusicCD = 0;
    MusicCard = -1;
  }
}

//-------------------------------------------------------------------------------------------------

static int InputParseMusicSourceSetting(const char *szValue)
{
  bool bUseCD;

  if (InputStringEqualsNoCase(szValue, "cd") ||
      InputStringEqualsNoCase(szValue, "audio_cd") ||
      InputStringEqualsNoCase(szValue, "audio-cd")) {
    InputApplyMusicSource(1);
    return 1;
  }

  if (InputStringEqualsNoCase(szValue, "midi")) {
    InputApplyMusicSource(0);
    return 1;
  }

  if (InputParseBoolSetting(szValue, &bUseCD)) {
    InputApplyMusicSource(bUseCD ? 1 : 0);
    return 1;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

static int InputParseRendererSetting(const char *szValue)
{
  bool bHardware;
  MenuRenderer *pRenderer;

  if (InputStringEqualsNoCase(szValue, "gpu") ||
      InputStringEqualsNoCase(szValue, "hardware")) {
    bHardware = true;
  } else if (InputStringEqualsNoCase(szValue, "software")) {
    bHardware = false;
  } else if (!InputParseBoolSetting(szValue, &bHardware)) {
    return 0;
  }

  pRenderer = GetMenuRenderer();
  if (pRenderer)
    menu_render_set_mode(pRenderer, bHardware ? MENU_RENDER_GPU : MENU_RENDER_SOFTWARE);

  return 1;
}

//-------------------------------------------------------------------------------------------------

#if defined(_WIN32)
static int InputIsWindowsBackendSettingName(const char *szName)
{
  return InputStringEqualsNoCase(szName, "WindowsInputBackend") ||
         InputStringEqualsNoCase(szName, "ControllerInputBackend") ||
         InputStringEqualsNoCase(szName, "WheelInputBackend") ||
         InputStringEqualsNoCase(szName, "UseSDLDirectInput");
}

//-------------------------------------------------------------------------------------------------

static int InputParseWindowsBackendSetting(const char *szValue)
{
  bool bUseSDLDirectInput;

  if (InputStringEqualsNoCase(szValue, "winmm") ||
      InputStringEqualsNoCase(szValue, "windows_multimedia") ||
      InputStringEqualsNoCase(szValue, "windows-multimedia")) {
    InputSetWindowsBackend(INPUT_WINDOWS_BACKEND_WINMM);
    return 1;
  }

  if (InputStringEqualsNoCase(szValue, "sdl") ||
      InputStringEqualsNoCase(szValue, "sdl_dinput") ||
      InputStringEqualsNoCase(szValue, "sdl-dinput") ||
      InputStringEqualsNoCase(szValue, "sdldirectinput") ||
      InputStringEqualsNoCase(szValue, "sdl_directinput") ||
      InputStringEqualsNoCase(szValue, "sdl-directinput") ||
      InputStringEqualsNoCase(szValue, "directinput") ||
      InputStringEqualsNoCase(szValue, "dinput")) {
    InputSetWindowsBackend(INPUT_WINDOWS_BACKEND_SDL_DINPUT);
    return 1;
  }

  if (InputParseBoolSetting(szValue, &bUseSDLDirectInput)) {
    InputSetWindowsBackend(bUseSDLDirectInput ? INPUT_WINDOWS_BACKEND_SDL_DINPUT : INPUT_WINDOWS_BACKEND_WINMM);
    return 1;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

void InputLoadStartupConfig(void)
{
  FILE *fp = ROLLERfopen("ROLLER.INI", "r");
  char szLine[1024];

  if (!fp)
    return;

  while (fgets(szLine, sizeof(szLine), fp)) {
    char *szText = InputTrim(szLine);
    char *szEquals;

    if (!szText[0] || szText[0] == '#' || szText[0] == ';' || szText[0] == '[')
      continue;

    szEquals = strchr(szText, '=');
    if (!szEquals)
      continue;

    *szEquals = '\0';
    szText = InputTrim(szText);
    if (InputIsWindowsBackendSettingName(szText))
      InputParseWindowsBackendSetting(InputTrim(szEquals + 1));
  }

  fclose(fp);
}

//-------------------------------------------------------------------------------------------------
#endif

//-------------------------------------------------------------------------------------------------

static int InputParseDebugSetting(const char *szName, const char *szValue)
{
  bool bValue;

  if (InputStringEqualsNoCase(szName, "MusicSource") ||
      InputStringEqualsNoCase(szName, "DebugMusicSource")) {
    return InputParseMusicSourceSetting(szValue);
  }

  if (InputStringEqualsNoCase(szName, "InfiniteDrawDistance") ||
      InputStringEqualsNoCase(szName, "ForceMaxDraw")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bForceMaxDraw = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "NoCollisionLimit")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bNoCollisionLimit = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "AirborneCollisions")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bAirborneCollisions = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "AIAutomaticGears") ||
      InputStringEqualsNoCase(szName, "AINoCheatStart")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bAINoCheatStart = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "FixCarMenuBug") ||
      InputStringEqualsNoCase(szName, "FixCarSelectBug")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bFixCarMenuBug = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "ImprovedJumpLanding") ||
      InputStringEqualsNoCase(szName, "FixJumpLanding")) {
    if (!InputParseBoolSetting(szValue, &bValue))
      return 0;
    g_bImprovedJumpLanding = bValue;
    return 1;
  }

  if (InputStringEqualsNoCase(szName, "HardwareRendering") ||
      InputStringEqualsNoCase(szName, "MenuRenderer")) {
    return InputParseRendererSetting(szValue);
  }

#if defined(_WIN32)
  if (InputIsWindowsBackendSettingName(szName)) {
    return InputParseWindowsBackendSetting(szValue);
  }
#endif

  return 0;
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
    szText = InputTrim(szText);
    if (InputParseDebugSetting(szText, InputTrim(szEquals + 1)))
      continue;

    iAction = InputFindActionByName(szText);
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
  fprintf(fp, "[Settings]\n");
  fprintf(fp, "MusicSource=%s\n", MusicCD ? "CD" : "MIDI");
  fprintf(fp, "InfiniteDrawDistance=%d\n", g_bForceMaxDraw ? 1 : 0);
  fprintf(fp, "NoCollisionLimit=%d\n", g_bNoCollisionLimit ? 1 : 0);
  fprintf(fp, "AirborneCollisions=%d\n", g_bAirborneCollisions ? 1 : 0);
  fprintf(fp, "AIAutomaticGears=%d\n", g_bAINoCheatStart ? 1 : 0);
  fprintf(fp, "FixCarMenuBug=%d\n", g_bFixCarMenuBug ? 1 : 0);
  fprintf(fp, "ImprovedJumpLanding=%d\n", g_bImprovedJumpLanding ? 1 : 0);
  fprintf(fp, "HardwareRendering=%d\n",
          menu_render_get_pending_mode(GetMenuRenderer()) == MENU_RENDER_GPU ? 1 : 0);
#if defined(_WIN32)
  fprintf(fp, "WindowsInputBackend=%s\n",
          InputGetWindowsBackend() == INPUT_WINDOWS_BACKEND_SDL_DINPUT ? "SDLDirectInput" : "WinMM");
#endif
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

static const char *InputGetGamepadButtonLabelName(SDL_GamepadButtonLabel eLabel)
{
  switch (eLabel) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
      return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
      return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
      return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
      return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
      return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
      return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
      return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
      return "Triangle";
    default:
      break;
  }

  return NULL;
}

//-------------------------------------------------------------------------------------------------

static const char *InputGetGamepadButtonDisplayName(const tInputBinding *pBinding)
{
  tInputBinding binding;
  tInputDevice *pDevice;
  const char *szLabelName;

  if (!pBinding || !pBinding->bGamepadInput)
    return NULL;

  binding = *pBinding;
  pDevice = InputGetBindingDevice(&binding);
  if (pDevice && pDevice->pGamepad) {
    szLabelName = InputGetGamepadButtonLabelName(
      SDL_GetGamepadButtonLabel(pDevice->pGamepad, (SDL_GamepadButton)pBinding->iInputIndex));
    if (szLabelName)
      return szLabelName;
  }

  return SDL_GetGamepadStringForButton((SDL_GamepadButton)pBinding->iInputIndex);
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
      if (pBinding->bGamepadInput) {
        const char *szButtonName = InputGetGamepadButtonDisplayName(pBinding);
        if (szButtonName && szButtonName[0]) {
          snprintf(szOut, (size_t)iOutLen, "%s", szButtonName);
          break;
        }
      }
      snprintf(szOut, (size_t)iOutLen, "button %d", pBinding->iInputIndex);
      break;
    case INPUT_BINDING_JOYSTICK_AXIS:
      if (pBinding->bGamepadInput) {
        const char *szAxisName = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)pBinding->iInputIndex);
        if (szAxisName && szAxisName[0]) {
          snprintf(szOut, (size_t)iOutLen, "%s %s", szAxisName, pBinding->iDirection < 0 ? "-" : "+");
          break;
        }
      }
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

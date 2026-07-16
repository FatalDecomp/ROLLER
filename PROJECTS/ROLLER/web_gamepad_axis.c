#include "web_gamepad_axis.h"

bool ROLLERWebGamepadAxisAcceptSample(int iValue, int iDeadzone, bool *pbReady)
{
  if (!pbReady || iDeadzone <= 0)
    return false;

  if (!*pbReady) {
    if (iValue <= -iDeadzone || iValue >= iDeadzone)
      return false;
    *pbReady = true;
  }

  return true;
}

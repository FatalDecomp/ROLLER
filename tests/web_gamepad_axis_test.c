#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "web_gamepad_axis.h"

#define TEST_DEADZONE 12000

int main(void)
{
  bool bReady = false;
  bool bCenteredReady = false;

  assert(!ROLLERWebGamepadAxisAcceptSample(32767, TEST_DEADZONE, &bReady));
  assert(!bReady);
  assert(!ROLLERWebGamepadAxisAcceptSample(-32768, TEST_DEADZONE, &bReady));
  assert(!bReady);
  assert(!ROLLERWebGamepadAxisAcceptSample(TEST_DEADZONE, TEST_DEADZONE, &bReady));
  assert(!bReady);

  assert(ROLLERWebGamepadAxisAcceptSample(0, TEST_DEADZONE, &bReady));
  assert(bReady);
  assert(ROLLERWebGamepadAxisAcceptSample(32767, TEST_DEADZONE, &bReady));
  assert(ROLLERWebGamepadAxisAcceptSample(-32768, TEST_DEADZONE, &bReady));

  assert(ROLLERWebGamepadAxisAcceptSample(0, TEST_DEADZONE, &bCenteredReady));
  assert(bCenteredReady);
  assert(!ROLLERWebGamepadAxisAcceptSample(0, 0, &bCenteredReady));
  assert(!ROLLERWebGamepadAxisAcceptSample(0, TEST_DEADZONE, NULL));

  puts("web gamepad axis tests passed");
  return 0;
}

#include "phone_ui.h"

#include <assert.h>

int main(void)
{
#if defined(TEST_PHONE_UI_ANDROID_DEFAULT)
  assert(ROLLERPhoneUIActive() != 0);
#else
  assert(ROLLERPhoneUIActive() == 0);
#endif

  ROLLERSetPhoneUIActive(1);
  assert(ROLLERPhoneUIActive() != 0);
  assert(g_bPhoneUI);

  ROLLERSetPhoneUIActive(-1);
  assert(ROLLERPhoneUIActive() != 0);

  ROLLERSetPhoneUIActive(0);
  assert(ROLLERPhoneUIActive() == 0);
  assert(!g_bPhoneUI);

  return 0;
}

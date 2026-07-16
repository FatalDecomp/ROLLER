#include "phone_ui.h"

#if defined(IS_ANDROID)
bool g_bPhoneUI = true;
#else
bool g_bPhoneUI = false;
#endif

//-------------------------------------------------------------------------------------------------

int ROLLERPhoneUIActive(void)
{
  return g_bPhoneUI ? -1 : 0;
}

//-------------------------------------------------------------------------------------------------

void ROLLERSetPhoneUIActive(int iActive)
{
  g_bPhoneUI = iActive != 0;
}

//-------------------------------------------------------------------------------------------------

#include <assert.h>
#include <stdio.h>

#include "web_default_config.h"

int main(void)
{
  assert(ROLLERWebDefaultConfigChooseAction(false, false) ==
         eROLLER_WEB_DEFAULT_CONFIG_CREATE);
  assert(ROLLERWebDefaultConfigChooseAction(true, true) ==
         eROLLER_WEB_DEFAULT_CONFIG_NONE);
  assert(ROLLERWebDefaultConfigChooseAction(true, false) ==
         eROLLER_WEB_DEFAULT_CONFIG_PRESERVE_PARTIAL);
  assert(ROLLERWebDefaultConfigChooseAction(false, true) ==
         eROLLER_WEB_DEFAULT_CONFIG_PRESERVE_PARTIAL);
  puts("web default config policy tests passed");
  return 0;
}

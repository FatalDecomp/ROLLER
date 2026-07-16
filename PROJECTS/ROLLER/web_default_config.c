#include "web_default_config.h"

eROLLERWebDefaultConfigAction ROLLERWebDefaultConfigChooseAction(bool bFatalExists,
                                                                  bool bRollerExists)
{
  if (bFatalExists && bRollerExists)
    return eROLLER_WEB_DEFAULT_CONFIG_NONE;
  if (!bFatalExists && !bRollerExists)
    return eROLLER_WEB_DEFAULT_CONFIG_CREATE;
  return eROLLER_WEB_DEFAULT_CONFIG_PRESERVE_PARTIAL;
}

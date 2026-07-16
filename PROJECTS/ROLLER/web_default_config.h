#ifndef ROLLER_WEB_DEFAULT_CONFIG_H
#define ROLLER_WEB_DEFAULT_CONFIG_H

#include <stdbool.h>

typedef enum
{
  eROLLER_WEB_DEFAULT_CONFIG_NONE = 0,
  eROLLER_WEB_DEFAULT_CONFIG_CREATE,
  eROLLER_WEB_DEFAULT_CONFIG_PRESERVE_PARTIAL
} eROLLERWebDefaultConfigAction;

eROLLERWebDefaultConfigAction ROLLERWebDefaultConfigChooseAction(bool bFatalExists,
                                                                  bool bRollerExists);

#endif

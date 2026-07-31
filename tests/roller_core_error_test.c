#include "roller_core_error.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    roller_core_error_clear();
    assert(!roller_core_error_pending());
    assert(roller_core_error_message()[0] == '\0');

    ErrorBoxExit("recoverable error %d", 7);
    assert(roller_core_error_pending());
    assert(strcmp(roller_core_error_message(), "recoverable error 7") == 0);

    roller_core_error_clear();
    assert(!roller_core_error_pending());
    return 0;
}

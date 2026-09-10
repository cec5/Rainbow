#include "xbios/xbios_screen.h"
#include "logger.h"

#define ST_RESOLUTION_HIGH 2

// [PARTIAL] Reports the closest real ST mode to the canvas, but the two do not actually match: ST high is monochrome, while the canvas renders a 16-color palette.
unsigned int xbios_getrez(unsigned int args_addr) {
    (void)args_addr;
    log_write(LOG_API, "Getrez() -> %d (ST high)", ST_RESOLUTION_HIGH);
    return ST_RESOLUTION_HIGH;
}
#include "bios/bios_disk.h"
#include "logger.h"

#define FIXED_DRIVE_BIT (1u << 2)

// [FULL] The layer exposes exactly one drive (C:), so the map it reports is fixed rather than probed from the host.
unsigned int bios_drvmap(unsigned int args_addr) {
    (void)args_addr; // Drvmap() takes no arguments
    log_write(LOG_API, "Drvmap() -> 0x%08X (only drive 'C:' reported)", FIXED_DRIVE_BIT);
    return FIXED_DRIVE_BIT;
}
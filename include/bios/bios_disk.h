#ifndef BIOS_DISK_H
#define BIOS_DISK_H

// Drvmap(): bitmap of mounted drives (bit N set = drive N is present, 'A:'=bit0).
unsigned int bios_drvmap(unsigned int args_addr);

#endif
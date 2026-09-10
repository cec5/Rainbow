#ifndef GEMDOS_FIND_H
#define GEMDOS_FIND_H

// Points the process's DTA (Disk Transfer Address) at a caller-supplied buffer, which Fsfirst() then fills in.
unsigned int gemdos_fsetdta(unsigned int args_addr);

unsigned int gemdos_fgetdta(unsigned int args_addr);
unsigned int gemdos_fsfirst(unsigned int args_addr);

#endif
#ifndef GEMDOS_DIR_H
#define GEMDOS_DIR_H

// Dgetdrv(): current GEMDOS drive code (0='A:', 1='B:', 2='C:', ...).
unsigned int gemdos_dgetdrv(unsigned int args_addr);

// Dgetpath(buf, drive): writes the current path (without a drive letter) for the given drive into buf.
unsigned int gemdos_dgetpath(unsigned int args_addr);

// Dcreate(path): creates a directory.
unsigned int gemdos_dcreate(unsigned int args_addr);

#endif
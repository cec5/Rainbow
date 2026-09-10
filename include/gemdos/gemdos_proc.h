#ifndef GEMDOS_PROC_H
#define GEMDOS_PROC_H

// Super(stack): 1 reports the current mode without changing it; anything else enters supervisor mode, taking the caller's value as the new SSP when it is non-zero. This layer never returns to user mode.
unsigned int gemdos_super(unsigned int args_addr);

// Serves both Pterm0() and Pterm(retcode), since there is nothing here for either to return to.
unsigned int gemdos_pterm(unsigned int args_addr);

#endif
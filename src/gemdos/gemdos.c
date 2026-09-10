#include "gemdos/gemdos.h"
#include "gemdos/gemdos_console.h"
#include "gemdos/gemdos_file.h"
#include "gemdos/gemdos_mem.h"
#include "gemdos/gemdos_dir.h"
#include "gemdos/gemdos_find.h"
#include "gemdos/gemdos_proc.h"
#include "trap_table.h"

static const TrapCall s_calls[] = {
    { 0x00,  "Pterm0",   gemdos_pterm     },
    { 0x02,  "Cconout",  gemdos_cconout   },
    { 0x09,  "Cconws",   gemdos_cconws    },
    { 0x19,  "Dgetdrv",  gemdos_dgetdrv   },
    { 0x1A,  "Fsetdta",  gemdos_fsetdta   },
    { 0x20,  "Super",    gemdos_super     },
    { 0x2F,  "Fgetdta",  gemdos_fgetdta   },
    { 0x39,  "Dcreate",  gemdos_dcreate   },
    { 0x3C,  "Fcreate",  gemdos_fcreate   },
    { 0x3D,  "Fopen",    gemdos_fopen     },
    { 0x3E,  "Fclose",   gemdos_fclose    },
    { 0x3F,  "Fread",    gemdos_fread     },
    { 0x40,  "Fwrite",   gemdos_fwrite    },
    { 0x42,  "Fseek",    gemdos_fseek     },
    { 0x47,  "Dgetpath", gemdos_dgetpath  },
    { 0x48,  "Malloc",   gemdos_malloc    },
    { 0x49,  "Mfree",    gemdos_mfree     },
    { 0x4A,  "Mshrink",  gemdos_mshrink   },
    { 0x4C,  "Pterm",    gemdos_pterm     },
    { 0x4E,  "Fsfirst",  gemdos_fsfirst   },
    { 0x104, "Fcntl",    gemdos_fcntl     },
};

static const TrapTable s_table = {
    "GEMDOS", "gemdos", s_calls, sizeof(s_calls) / sizeof(s_calls[0])
};

unsigned int gemdos_dispatch(unsigned int func_num, unsigned int args_addr) {
    return trap_table_dispatch(&s_table, func_num, args_addr);
}
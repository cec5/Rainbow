#ifndef TOS_LAYER_H
#define TOS_LAYER_H

/* Reserved addresses the fake trap handlers live at, right after the
 * exception vector table (0x000-0x3FF). Never fetched or decoded as
 * opcodes: the instruction hook callback intercepts execution the moment
 * PC lands here, before Musashi reads whatever bytes happen to be there. */

#define GEMDOS_TRAP_ADDR 0x00000400u
#define BIOS_TRAP_ADDR   0x00000404u
#define XBIOS_TRAP_ADDR  0x00000408u
#define AES_VDI_TRAP_ADDR 0x00000430u

#define AUTO_TERM_ADDR 0x00000434u

#define SR_S_BIT 0x2000u // the supervisor-mode bit in the 68000 status register

// Changes the S-bit while also performing the active/shadow stack-pointer swap a real mode change triggers.
void tos_set_sr(unsigned int new_sr);

#define FAULT_BUS_ERROR_ADDR      0x0000040Cu
#define FAULT_ADDRESS_ERROR_ADDR  0x00000410u
#define FAULT_ILLEGAL_INSTR_ADDR  0x00000414u
#define FAULT_ZERO_DIVIDE_ADDR    0x00000418u
#define FAULT_CHK_ADDR            0x0000041Cu
#define FAULT_TRAPV_ADDR          0x00000420u
#define FAULT_PRIVILEGE_ADDR      0x00000424u
#define FAULT_LINE_A_ADDR         0x00000428u
#define FAULT_LINE_F_ADDR         0x0000042Cu

void tos_layer_init(void);

void tos_dump_pc_history(void);

void tos_request_halt(void);

int tos_is_halted(void);

#endif
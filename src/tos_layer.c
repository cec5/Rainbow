#include <stdio.h>
#include "m68k.h"
#include "tos_layer.h"
#include "gemdos/gemdos.h"
#include "bios/bios.h"
#include "xbios/xbios.h"
#include "aes/aes.h"
#include "vdi/vdi.h"
#include "logger.h"
#include "memory.h"

// D0 discriminates AES from VDI on TRAP #2: 0xC8 (200) for AES, 0x73 for VDI.
#define AES_D0_MAGIC 0xC8
#define VDI_D0_MAGIC 0x73

static int s_halt_requested = 0; // read directly by trap_instr_hook; set via tos_request_halt()

// FAULT DIAGNOSTICS

#define PC_HISTORY_SIZE 48
static unsigned int s_pc_history[PC_HISTORY_SIZE];
static unsigned int s_pc_history_pos = 0;
static int s_pc_history_filled = 0;

void tos_dump_pc_history(void) {
    int count = s_pc_history_filled ? PC_HISTORY_SIZE : (int)s_pc_history_pos;
    if (count <= 0) {
        return;
    }

    log_write(LOG_ERROR, "--- last %d instructions before the fault (oldest first) ---", count);
    int start = s_pc_history_filled ? (int)s_pc_history_pos : 0;
    for (int i = 0; i < count; i++) {
        unsigned int pc = s_pc_history[(start + i) % PC_HISTORY_SIZE];
        char disasm[256];
        m68k_disassemble(disasm, pc, M68K_CPU_TYPE_68000);
        log_write(LOG_ERROR, "  [%2d] %08X: %s", i - count, pc, disasm);
    }
    log_write(LOG_ERROR, "--- end of instruction history ---");
}

// SUPERVISOR STATE

void tos_set_sr(unsigned int new_sr) {
    unsigned int old_sr = m68k_get_reg(NULL, M68K_REG_SR);
    int old_super = (old_sr & SR_S_BIT) != 0;
    int new_super = (new_sr & SR_S_BIT) != 0;

    if (old_super == new_super) {
        m68k_set_reg(M68K_REG_SR, new_sr);
        return;
    }

    unsigned int outgoing_sp = m68k_get_reg(NULL, M68K_REG_SP);
    unsigned int incoming_sp = new_super
        ? m68k_get_reg(NULL, M68K_REG_ISP)
        : m68k_get_reg(NULL, M68K_REG_USP);

    // Only flips the S/M flags
    m68k_set_reg(M68K_REG_SR, new_sr);

    if (new_super) {
        m68k_set_reg(M68K_REG_USP, outgoing_sp);
    } else {
        m68k_set_reg(M68K_REG_ISP, outgoing_sp);
    }

    m68k_set_reg(M68K_REG_SP, incoming_sp);
}

// EXCEPTION FRAMES

/* On a real 68000, TRAP pushes a 3-word exception frame onto the
 * (supervisor) stack: SR at SP+0, return PC at SP+2..5. Since it's intercepted
 * before that frame's target address is ever executed, we have to pop it
 * back off ourselves to fake an RTE back to the caller. */
static unsigned int pop_exception_frame(void) {
    unsigned int sp = m68k_get_reg(NULL, M68K_REG_SP);
    unsigned int sr = m68k_read_memory_16(sp);
    unsigned int return_pc = m68k_read_memory_32(sp + 2);

    m68k_set_reg(M68K_REG_SP, sp + 6);
    tos_set_sr(sr);
    m68k_set_reg(M68K_REG_PC, return_pc);

    return return_pc;
}

static unsigned int caller_args_addr(unsigned int frame_sp) {
    unsigned int frame_sr = m68k_read_memory_16(frame_sp);
    if (frame_sr & SR_S_BIT) {
        return frame_sp + 6; // args pushed on this same (supervisor) stack, right above our frame
    }
    return m68k_get_reg(NULL, M68K_REG_USP); // args are on the separate user stack; no frame there at all
}

// TRAP HANDLERS

/* GEMDOS and BIOS traps both push a function number + args on the stack
 * (unlike AES/VDI's parameter-block convention), so a single handler covers
 * both, parameterized by the trap's log label and dispatch function. */
static void handle_stack_trap(const char *trap_label, unsigned int (*dispatch)(unsigned int, unsigned int)) {
    unsigned int frame_sp = m68k_get_reg(NULL, M68K_REG_SP);
    unsigned int call_args = caller_args_addr(frame_sp);
    unsigned int func_num = m68k_read_memory_16(call_args);
    unsigned int args_addr = call_args + 2;

    unsigned int return_pc = pop_exception_frame();
    unsigned int call_site = return_pc - 2; // TRAP #n is always a 2-byte opcode
    log_write(LOG_TRAP, "%s at 0x%08X - function=0x%02X, resuming at 0x%08X", trap_label, call_site, func_num, return_pc);

    unsigned int result = dispatch(func_num, args_addr);
    m68k_set_reg(M68K_REG_D0, result);
}

static void handle_gemdos_trap(void) {
    handle_stack_trap("TRAP #1 (GEMDOS)", gemdos_dispatch);
}

static void handle_bios_trap(void) {
    handle_stack_trap("TRAP #13 (BIOS)", bios_dispatch);
}

static void handle_xbios_trap(void) {
    handle_stack_trap("TRAP #14 (XBIOS)", xbios_dispatch);
}

// AES/VDI share TRAP #2; D0 says which, and D1 holds the address of the parameter block.
static void handle_trap2(void) {
    unsigned int d0_full = m68k_get_reg(NULL, M68K_REG_D0);
    unsigned int d0 = d0_full & 0xFFFF;
    unsigned int d1 = m68k_get_reg(NULL, M68K_REG_D1);

    unsigned int return_pc = pop_exception_frame();
    unsigned int call_site = return_pc - 2;

    if (d0 == AES_D0_MAGIC) {
        log_write(LOG_TRAP, "TRAP #2 (AES) at 0x%08X, pb=0x%08X", call_site, d1);
        unsigned int result = aes_dispatch(d1);
        m68k_set_reg(M68K_REG_D0, result);
    } else if (d0 == VDI_D0_MAGIC) {
        log_write(LOG_TRAP, "TRAP #2 (VDI) at 0x%08X, pb=0x%08X", call_site, d1);
        vdi_dispatch(d1);
        // vdi()'s real binding is VOID; callers never read a trap return value, results come back through intout[]/ptsout[] instead.
        m68k_set_reg(M68K_REG_D0, 0);
    } else {
        printf("[tos_layer] Unimplemented TRAP #2 call at PC 0x%08X (D0=%u)\n", call_site, d0_full);
        log_write(LOG_ERROR, "TRAP #2 at 0x%08X with D0=%u is unimplemented", call_site, d0_full);
        m68k_set_reg(M68K_REG_D0, 0);
    }
}

// FAULT HANDLING

// Log the CPU fault (out of our control) and halt.
static void handle_cpu_fault(const char *name) {
    unsigned int fault_pc = pop_exception_frame();

    fprintf(stderr, "[tos_layer] CPU fault: %s at PC=0x%08X - halting\n", name, fault_pc);
    log_write(LOG_ERROR, "CPU FAULT: %s at PC=0x%08X - halting execution", name, fault_pc);
    log_write(LOG_ERROR, "  registers: A6=0x%08X A7=0x%08X USP=0x%08X ISP=0x%08X SR=0x%04X", m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_SP), m68k_get_reg(NULL, M68K_REG_USP), m68k_get_reg(NULL, M68K_REG_ISP), m68k_get_reg(NULL, M68K_REG_SR));
    tos_dump_pc_history();
    tos_request_halt();
}

// DISPATCH

static void trap_instr_hook(unsigned int pc) {
    memory_mark_executed(pc);

    s_pc_history[s_pc_history_pos] = pc;
    s_pc_history_pos = (s_pc_history_pos + 1) % PC_HISTORY_SIZE;
    if (s_pc_history_pos == 0) {
        s_pc_history_filled = 1;
    }

    if (pc == GEMDOS_TRAP_ADDR) {
        handle_gemdos_trap();
    } else if (pc == BIOS_TRAP_ADDR) {
        handle_bios_trap();
    } else if (pc == XBIOS_TRAP_ADDR) {
        handle_xbios_trap();
    } else if (pc == AES_VDI_TRAP_ADDR) {
        handle_trap2();
    } else if (pc == AUTO_TERM_ADDR) {
        log_write(LOG_INFO, "Program returned to its own entry stack frame (implicit Pterm0) - halting");
        tos_request_halt();
    } else if (pc == FAULT_BUS_ERROR_ADDR) {
        handle_cpu_fault("Bus Error");
    } else if (pc == FAULT_ADDRESS_ERROR_ADDR) {
        handle_cpu_fault("Address Error");
    } else if (pc == FAULT_ILLEGAL_INSTR_ADDR) {
        handle_cpu_fault("Illegal Instruction");
    } else if (pc == FAULT_ZERO_DIVIDE_ADDR) {
        handle_cpu_fault("Zero Divide");
    } else if (pc == FAULT_CHK_ADDR) {
        handle_cpu_fault("CHK Instruction");
    } else if (pc == FAULT_TRAPV_ADDR) {
        handle_cpu_fault("TRAPV Instruction");
    } else if (pc == FAULT_PRIVILEGE_ADDR) {
        handle_cpu_fault("Privilege Violation");
    } else if (pc == FAULT_LINE_A_ADDR) {
        handle_cpu_fault("Line 1010 (A-line) Emulator");
    } else if (pc == FAULT_LINE_F_ADDR) {
        handle_cpu_fault("Line 1111 (F-line) Emulator");
    }

    if (s_halt_requested) {
        m68k_end_timeslice();
    }
}

// LIFECYCLE

#define TRAP_VECTOR_ADDR(n) ((32u + (n)) * 4u)

static void install_trap_vector(unsigned int trap_num, unsigned int handler_addr) {
    m68k_write_memory_32(TRAP_VECTOR_ADDR(trap_num), handler_addr);
}

// CPU fault vectors are addressed directly (vector_num * 4), unlike TRAP #n which lives at (32 + n) * 4.
static void install_vector(unsigned int vector_num, unsigned int handler_addr) {
    m68k_write_memory_32(vector_num * 4u, handler_addr);
}

void tos_layer_init(void) {
    install_trap_vector(1, GEMDOS_TRAP_ADDR);
    install_trap_vector(2, AES_VDI_TRAP_ADDR);
    install_trap_vector(13, BIOS_TRAP_ADDR);
    install_trap_vector(14, XBIOS_TRAP_ADDR);

    install_vector(2, FAULT_BUS_ERROR_ADDR);
    install_vector(3, FAULT_ADDRESS_ERROR_ADDR);
    install_vector(4, FAULT_ILLEGAL_INSTR_ADDR);
    install_vector(5, FAULT_ZERO_DIVIDE_ADDR);
    install_vector(6, FAULT_CHK_ADDR);
    install_vector(7, FAULT_TRAPV_ADDR);
    install_vector(8, FAULT_PRIVILEGE_ADDR);
    install_vector(10, FAULT_LINE_A_ADDR);
    install_vector(11, FAULT_LINE_F_ADDR);

    m68k_set_instr_hook_callback(trap_instr_hook);
}

void tos_request_halt(void) {
    s_halt_requested = 1;
}

int tos_is_halted(void) {
    return s_halt_requested;
}
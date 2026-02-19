.syntax unified
.arm

.section .itcm, "ax", %progbits
.align 2

/*
 * ARM assembly 6502 dispatch loop for GameTank NDS emulator.
 *
 * Register allocation:
 *   r4  = A (accumulator, 8-bit value in low byte)
 *   r5  = X (X index register)
 *   r6  = Y (Y index register)
 *   r7  = status (P register)
 *   r8  = PC (16-bit program counter)
 *   r9  = cached_ram_ptr
 *   r10 = sp_6502 (stack pointer, 8-bit)
 *   r11 = cycles_remaining
 *   r0-r3, r12, lr = scratch
 *
 * Stack frame (after push {r4-r11, lr} + sub sp, #24):
 *   [sp, #0]  = AsmCpuState*
 *   [sp, #4]  = cached_rom_lo_ptr
 *   [sp, #8]  = cached_rom_hi_ptr
 *   [sp, #12] = cached_ram_init_ptr (bool*)
 *   [sp, #16] = VIA_regs pointer
 *   [sp, #20] = scratch
 *
 * AsmCpuState layout:
 *   +0: A, +1: X, +2: Y, +3: sp
 *   +4: status, +5: exit_reason, +6: exit_opcode, +7: pad
 *   +8: pc (u16), +10: exit_addr (u16)
 *   +12: cycles_remaining (i32)
 *   +16: exit_value, +17: exit_is_write
 */

.global mos6502_run_asm
.type mos6502_run_asm, %function

/* ========== Macros ========== */

/* Set N and Z flags in r7 from 8-bit value in \reg */
.macro SET_NZ reg
    bic     r7, r7, #0x82
    tst     \reg, #0x80
    orrne   r7, r7, #0x80
    tst     \reg, #0xFF
    orreq   r7, r7, #0x02
.endm

/*
 * Fetch a byte from PC. PC must be >= 0x8000 (ROM).
 * Result in \dst. Clobbers \tmp.
 * If PC < 0x8000, exits to C++.
 */
.macro FETCH_PC dst, tmp
    /* Check PC is in ROM range */
    tst     r8, #0x8000
    beq     .Lexit_unhandled_fetch
    tst     r8, #0x4000
    /* hi bank: $C000-$FFFF */
    ldrne   \tmp, [sp, #8]       /* rom_hi_ptr */
    /* lo bank: $8000-$BFFF */
    ldreq   \tmp, [sp, #4]       /* rom_lo_ptr */
    bic     \dst, r8, #0xC000    /* addr & 0x3FFF */
    ldrb    \dst, [\tmp, \dst]
    add     r8, r8, #1
    bic     r8, r8, #0x10000     /* keep 16-bit */
.endm

/*
 * Read a byte from address in \addr (16-bit).
 * Result in \dst. Clobbers r0, r1, r2, r3, r12.
 * For addresses that can't be resolved inline, exits to C++.
 */
.macro READ_BYTE addr, dst
    /* RAM: $0000-$1FFF */
    cmp     \addr, #0x2000
    ldrltb  \dst, [r9, \addr]
    blt     991f
    /* ROM hi: $C000-$FFFF */
    cmp     \addr, #0xC000
    blt     990f
    ldr     r12, [sp, #8]        /* rom_hi_ptr */
    bic     \dst, \addr, #0xC000
    ldrb    \dst, [r12, \dst]
    b       991f
990:
    /* ROM lo: $8000-$BFFF */
    tst     \addr, #0x8000
    beq     992f
    ldr     r12, [sp, #4]        /* rom_lo_ptr */
    bic     \dst, \addr, #0xC000
    ldrb    \dst, [r12, \dst]
    b       991f
992:
    /* VIA: $2800-$2FFF */
    cmp     \addr, #0x2800
    blt     993f
    cmp     \addr, #0x3000
    bge     993f
    ldr     r12, [sp, #16]       /* VIA_regs pointer */
    and     r0, \addr, #0xF
    ldrb    \dst, [r12, r0]
    b       991f
993:
    /* Unhandled address - exit to C++ for I/O */
    b       .Lexit_io_read
991:
.endm

/* ========== Function Entry ========== */

mos6502_run_asm:
    /* Save callee-saved registers */
    push    {r4-r11, lr}
    sub     sp, sp, #24

    /* Store AsmCpuState pointer */
    str     r0, [sp, #0]

    /* Store ROM pointers */
    str     r2, [sp, #4]         /* rom_lo_ptr */
    str     r3, [sp, #8]         /* rom_hi_ptr */

    /* Load stack arguments (5th and 6th args passed on stack) */
    ldr     r12, [sp, #48]       /* ram_init_ptr */
    str     r12, [sp, #12]
    ldr     r12, [sp, #52]       /* VIA_regs pointer */
    str     r12, [sp, #16]

    /* Load CPU state from AsmCpuState */
    ldrb    r4, [r0, #0]         /* A */
    ldrb    r5, [r0, #1]         /* X */
    ldrb    r6, [r0, #2]         /* Y */
    ldrb    r10, [r0, #3]        /* sp */
    ldrb    r7, [r0, #4]         /* status */
    ldrh    r8, [r0, #8]         /* PC */
    ldr     r11, [r0, #12]       /* cycles_remaining */

    /* r9 = cached_ram_ptr (from arg1) */
    mov     r9, r1

/* ========== Main Dispatch Loop ========== */

.Ldispatch_loop:
    /* Check cycles_remaining */
    cmp     r11, #0
    ble     .Lexit_cycles_done

    /* Fetch opcode */
    FETCH_PC r0, r12             /* r0 = opcode */

    /* Dispatch via jump table - simplified for common opcodes */
    /* This is a placeholder - the actual implementation would have */
    /* a full 256-entry jump table */

    /* For now, exit to C++ for all opcodes */
    b       .Lexit_unhandled

/* ========== Exit Handlers ========== */

.Lexit_cycles_done:
    mov     r0, #0               /* exit_reason = 0 (cycles done) */
    b       .Lwrite_back

.Lexit_unhandled_fetch:
    sub     r8, r8, #1           /* roll back PC */

.Lexit_unhandled:
    mov     r0, #1               /* exit_reason = 1 (unhandled opcode) */
    b       .Lwrite_back

.Lexit_io_read:
    /* Store exit info for I/O read */
    ldr     r12, [sp, #0]        /* AsmCpuState* */
    strh    r2, [r12, #10]       /* exit_addr */
    mov     r3, #0
    strb    r3, [r12, #17]       /* exit_is_write = 0 */
    mov     r0, #2               /* exit_reason = 2 (I/O) */
    b       .Lwrite_back

.Lexit_io_write:
    /* Store exit info for I/O write */
    ldr     r12, [sp, #0]        /* AsmCpuState* */
    strh    r2, [r12, #10]       /* exit_addr */
    strb    r3, [r12, #16]       /* exit_value */
    mov     r3, #1
    strb    r3, [r12, #17]       /* exit_is_write = 1 */
    mov     r0, #2               /* exit_reason = 2 (I/O) */
    b       .Lwrite_back

.Lwrite_back:
    /* Write back CPU state */
    ldr     r12, [sp, #0]        /* AsmCpuState* */
    strb    r4, [r12, #0]        /* A */
    strb    r5, [r12, #1]        /* X */
    strb    r6, [r12, #2]        /* Y */
    strb    r10, [r12, #3]       /* sp */
    strb    r7, [r12, #4]        /* status */
    strh    r8, [r12, #8]        /* PC */
    str     r11, [r12, #12]      /* cycles_remaining */
    strb    r0, [r12, #5]        /* exit_reason */

    /* Restore and return */
    add     sp, sp, #24
    pop     {r4-r11, pc}

.size mos6502_run_asm, .-mos6502_run_asm

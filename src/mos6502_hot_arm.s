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
    mov     r12, #0x2800
    cmp     \addr, r12
    blt     993f
    mov     r12, #0x3000
    cmp     \addr, r12
    bge     993f
    ldr     r12, [sp, #16]       /* via_regs_ptr */
    and     \dst, \addr, #0xF
    ldrb    \dst, [r12, \dst]
    b       991f
993:
    /* Everything else: exit to C++ for I/O read */
    b       .Lexit_io_read_\@
991:
.endm

/*
 * Write a byte \val to address \addr.
 * Clobbers r12.
 */
.macro WRITE_BYTE addr, val
    /* RAM: $0000-$1FFF */
    cmp     \addr, #0x2000
    bge     994f
    strb    \val, [r9, \addr]
    /* set init flag */
    ldr     r12, [sp, #12]       /* ram_init_ptr */
    mov     r0, #1
    strb    r0, [r12, \addr]
    b       995f
994:
    /* VIA: $2800-$2FFF */
    mov     r12, #0x2800
    cmp     \addr, r12
    blt     996f
    mov     r12, #0x3000
    cmp     \addr, r12
    bge     996f
    ldr     r12, [sp, #16]       /* via_regs_ptr */
    and     r0, \addr, #0xF
    strb    \val, [r12, r0]
    b       995f
996:
    /* Everything else: exit to C++ for I/O write */
    b       .Lexit_io_write_\@
995:
.endm

/* Relative branch helper: sign-extend \off (8-bit signed in low byte),
   add to PC, compute cycles (3 or 4 for page cross).
   Result: PC updated, cycles subtracted. */
.macro DO_BRANCH off
    lsl     \off, \off, #24
    asr     \off, \off, #24
    mov     r12, r8
    add     r8, r8, \off
    bic     r8, r8, #0x10000
    /* page cross check */
    eor     r12, r12, r8
    tst     r12, #0xFF00
    subeq   r11, r11, #3
    subne   r11, r11, #4
.endm

/* ========== Entry Point ========== */

mos6502_run_asm:
    push    {r4-r11, lr}
    sub     sp, sp, #24

    /* Save state pointer and ROM pointers */
    str     r0, [sp, #0]         /* AsmCpuState* */
    str     r2, [sp, #4]         /* rom_lo_ptr */
    str     r3, [sp, #8]         /* rom_hi_ptr */

    /* Load stack args: ram_init_ptr and via_regs_ptr */
    /* After push(9 regs=36) + sub(24) = 60 bytes on stack */
    ldr     r12, [sp, #60]       /* ram_init_ptr */
    str     r12, [sp, #12]
    ldr     r12, [sp, #64]       /* via_regs_ptr */
    str     r12, [sp, #16]

    /* Load CPU state from AsmCpuState */
    mov     r9, r1               /* cached_ram_ptr */
    ldrb    r4, [r0, #0]         /* A */
    ldrb    r5, [r0, #1]         /* X */
    ldrb    r6, [r0, #2]         /* Y */
    ldrb    r10, [r0, #3]        /* sp_6502 */
    ldrb    r7, [r0, #4]         /* status */
    ldrh    r8, [r0, #8]         /* pc */
    ldr     r11, [r0, #12]       /* cycles_remaining */

    /* Get jump table address */
    ldr     r12, =.Ljump_table
    str     r12, [sp, #20]       /* cache jump table base in scratch */

    /* Fall into dispatch loop */
    b       .Ldispatch

    .ltorg    /* literal pool for ldr =.Ljump_table */

/* ========== Main Dispatch Loop ========== */
.Ldispatch:
    cmp     r11, #0
    ble     .Lexit_done

    /* Fetch opcode from PC (must be in ROM) */
    tst     r8, #0x8000
    beq     .Lexit_unhandled_fetch
    tst     r8, #0x4000
    ldrne   r1, [sp, #8]        /* rom_hi */
    ldreq   r1, [sp, #4]        /* rom_lo */
    bic     r0, r8, #0xC000
    ldrb    r0, [r1, r0]
    add     r8, r8, #1
    bic     r8, r8, #0x10000

    /* Jump table dispatch */
    ldr     r12, [sp, #20]       /* jump table base */
    ldr     pc, [r12, r0, lsl #2]

/* ========== Exit Handlers ========== */

.Lexit_done:
    ldr     r0, [sp, #0]         /* AsmCpuState* */
    strb    r4, [r0, #0]         /* A */
    strb    r5, [r0, #1]         /* X */
    strb    r6, [r0, #2]         /* Y */
    strb    r10, [r0, #3]        /* sp */
    strb    r7, [r0, #4]         /* status */
    mov     r1, #0
    strb    r1, [r0, #5]         /* exit_reason = 0 (done) */
    strh    r8, [r0, #8]         /* pc */
    str     r11, [r0, #12]       /* cycles_remaining */
    add     sp, sp, #24
    pop     {r4-r11, pc}

.Lexit_unhandled_fetch:
    /* PC points to the unhandled instruction (not yet incremented for non-ROM,
       or already incremented for ROM fetch). For non-ROM fetch we didn't
       increment, so PC is correct. */
    ldr     r0, [sp, #0]
    strb    r4, [r0, #0]
    strb    r5, [r0, #1]
    strb    r6, [r0, #2]
    strb    r10, [r0, #3]
    strb    r7, [r0, #4]
    mov     r1, #1
    strb    r1, [r0, #5]         /* exit_reason = 1 (unhandled) */
    strh    r8, [r0, #8]
    str     r11, [r0, #12]
    add     sp, sp, #24
    pop     {r4-r11, pc}

.Lexit_unhandled:
    /* Opcode already fetched and PC incremented past it.
       Roll back PC to point at the opcode. */
    sub     r8, r8, #1
    bic     r8, r8, #0x10000
    ldr     r0, [sp, #0]
    strb    r4, [r0, #0]
    strb    r5, [r0, #1]
    strb    r6, [r0, #2]
    strb    r10, [r0, #3]
    strb    r7, [r0, #4]
    mov     r1, #1
    strb    r1, [r0, #5]         /* exit_reason = 1 */
    strh    r8, [r0, #8]
    str     r11, [r0, #12]
    add     sp, sp, #24
    pop     {r4-r11, pc}

/* Generic I/O exit for read: address in r2, opcode PC rolled back */
.Lexit_io_read:
    sub     r8, r8, #1           /* roll back past opcode */
    bic     r8, r8, #0x10000
    ldr     r0, [sp, #0]
    strb    r4, [r0, #0]
    strb    r5, [r0, #1]
    strb    r6, [r0, #2]
    strb    r10, [r0, #3]
    strb    r7, [r0, #4]
    mov     r1, #2
    strb    r1, [r0, #5]         /* exit_reason = 2 (io) */
    mov     r1, #0
    strb    r1, [r0, #17]        /* exit_is_write = 0 */
    strh    r2, [r0, #10]        /* exit_addr */
    strh    r8, [r0, #8]
    str     r11, [r0, #12]
    add     sp, sp, #24
    pop     {r4-r11, pc}

/* Generic I/O exit for write: address in r2, value in r3, opcode PC rolled back */
.Lexit_io_write:
    sub     r8, r8, #1
    bic     r8, r8, #0x10000
    ldr     r0, [sp, #0]
    strb    r4, [r0, #0]
    strb    r5, [r0, #1]
    strb    r6, [r0, #2]
    strb    r10, [r0, #3]
    strb    r7, [r0, #4]
    mov     r1, #2
    strb    r1, [r0, #5]         /* exit_reason = 2 (io) */
    mov     r1, #1
    strb    r1, [r0, #17]        /* exit_is_write = 1 */
    strh    r2, [r0, #10]        /* exit_addr */
    strb    r3, [r0, #16]        /* exit_value */
    strh    r8, [r0, #8]
    str     r11, [r0, #12]
    add     sp, sp, #24
    pop     {r4-r11, pc}


/* =====================================================================
 * OPCODE HANDLERS
 * ===================================================================== */

/* ---------- Helper: fetch immediate byte from PC ---------- */
/* After this, r0 = immediate value, PC advanced. */
.macro FETCH_IMM dst
    FETCH_PC \dst, r12
.endm

/* ---------- Helper: fetch zero page address from PC ---------- */
.macro FETCH_ZP dst
    FETCH_PC \dst, r12
.endm

/* ---------- Helper: fetch absolute address from PC into \dst ---------- */
.macro FETCH_ABS dst, tmp
    FETCH_PC \dst, r12
    FETCH_PC \tmp, r12
    orr     \dst, \dst, \tmp, lsl #8
.endm

/* ============ NOP (EA) ============ */
.Lop_EA:
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ LDA immediate (A9) ============ */
.Lop_A9:
    FETCH_IMM r4
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ LDA zero page (A5) ============ */
.Lop_A5:
    FETCH_ZP r0
    ldrb    r4, [r9, r0]
    SET_NZ  r4
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ LDA absolute (AD) ============ */
.Lop_AD:
    FETCH_ABS r2, r3
    /* inline read */
    cmp     r2, #0x2000
    bge     .Lop_AD_not_ram
    ldrb    r4, [r9, r2]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AD_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_AD_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AD_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_AD_not_rom
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AD_not_rom:
    /* VIA: $2800-$2FFF */
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_AD_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_AD_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AD_io:
    /* Exit for I/O read - roll back PC past the 3-byte instruction */
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDA zero page,X (B5) ============ */
.Lop_B5:
    FETCH_ZP r0
    add     r0, r0, r5
    and     r0, r0, #0xFF
    ldrb    r4, [r9, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ LDA absolute,X (BD) ============ */
.Lop_BD:
    FETCH_ABS r2, r3
    add     r2, r2, r5
    bic     r2, r2, #0x10000
    /* page cross: +1 cycle (simplified: always charge 4+1=5 for indexed) */
    /* inline read */
    cmp     r2, #0x2000
    bge     .Lop_BD_not_ram
    ldrb    r4, [r9, r2]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_BD_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_BD_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_BD_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_BD_not_rom
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_BD_not_rom:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_BD_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_BD_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_BD_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDA absolute,Y (B9) ============ */
.Lop_B9:
    FETCH_ABS r2, r3
    add     r2, r2, r6
    bic     r2, r2, #0x10000
    cmp     r2, #0x2000
    bge     .Lop_B9_not_ram
    ldrb    r4, [r9, r2]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_B9_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_B9_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_B9_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_B9_not_rom
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_B9_not_rom:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_B9_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_B9_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_B9_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDA (indirect),Y (B1) ============ */
.Lop_B1:
    FETCH_ZP r0
    /* Read 16-bit pointer from zero page */
    ldrb    r2, [r9, r0]
    add     r1, r0, #1
    and     r1, r1, #0xFF
    ldrb    r3, [r9, r1]
    orr     r2, r2, r3, lsl #8
    add     r2, r2, r6
    bic     r2, r2, #0x10000
    /* Read from effective address */
    cmp     r2, #0x2000
    bge     .Lop_B1_not_ram
    ldrb    r4, [r9, r2]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B1_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_B1_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B1_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_B1_not_rom
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B1_not_rom:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_B1_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_B1_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B1_io:
    sub     r8, r8, #2
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDA (zp) - 65C02 (B2) ============ */
.Lop_B2:
    FETCH_ZP r0
    ldrb    r2, [r9, r0]
    add     r1, r0, #1
    and     r1, r1, #0xFF
    ldrb    r3, [r9, r1]
    orr     r2, r2, r3, lsl #8
    /* Read from effective address */
    cmp     r2, #0x2000
    bge     .Lop_B2_not_ram
    ldrb    r4, [r9, r2]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B2_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_B2_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B2_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_B2_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r4, [r12, r0]
    SET_NZ  r4
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_B2_io:
    sub     r8, r8, #2
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STA zero page (85) ============ */
.Lop_85:
    FETCH_ZP r0
    strb    r4, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ STA absolute (8D) ============ */
.Lop_8D:
    FETCH_ABS r2, r3
    /* RAM write */
    cmp     r2, #0x2000
    bge     .Lop_8D_not_ram
    strb    r4, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_8D_not_ram:
    /* VIA write */
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_8D_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_8D_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    strb    r4, [r12, r0]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_8D_io:
    /* I/O write exit */
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STA zero page,X (95) ============ */
.Lop_95:
    FETCH_ZP r0
    add     r0, r0, r5
    and     r0, r0, #0xFF
    strb    r4, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ STA absolute,X (9D) ============ */
.Lop_9D:
    FETCH_ABS r2, r3
    add     r2, r2, r5
    bic     r2, r2, #0x10000
    cmp     r2, #0x2000
    bge     .Lop_9D_not_ram
    strb    r4, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_9D_not_ram:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_9D_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_9D_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    strb    r4, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_9D_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STA absolute,Y (99) ============ */
.Lop_99:
    FETCH_ABS r2, r3
    add     r2, r2, r6
    bic     r2, r2, #0x10000
    cmp     r2, #0x2000
    bge     .Lop_99_not_ram
    strb    r4, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_99_not_ram:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_99_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_99_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    strb    r4, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_99_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STA (indirect),Y (91) ============ */
.Lop_91:
    FETCH_ZP r0
    ldrb    r2, [r9, r0]
    add     r1, r0, #1
    and     r1, r1, #0xFF
    ldrb    r3, [r9, r1]
    orr     r2, r2, r3, lsl #8
    add     r2, r2, r6
    bic     r2, r2, #0x10000
    cmp     r2, #0x2000
    bge     .Lop_91_not_ram
    strb    r4, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_91_not_ram:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_91_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_91_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    strb    r4, [r12, r0]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_91_io:
    sub     r8, r8, #2
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STA (zp) - 65C02 (92) ============ */
.Lop_92:
    FETCH_ZP r0
    ldrb    r2, [r9, r0]
    add     r1, r0, #1
    and     r1, r1, #0xFF
    ldrb    r3, [r9, r1]
    orr     r2, r2, r3, lsl #8
    cmp     r2, #0x2000
    bge     .Lop_92_not_ram
    strb    r4, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_92_not_ram:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_92_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_92_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    strb    r4, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch
.Lop_92_io:
    sub     r8, r8, #2
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDX immediate (A2) ============ */
.Lop_A2:
    FETCH_IMM r5
    SET_NZ  r5
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ LDX zero page (A6) ============ */
.Lop_A6:
    FETCH_ZP r0
    ldrb    r5, [r9, r0]
    SET_NZ  r5
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ LDX absolute (AE) ============ */
.Lop_AE:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_AE_not_ram
    ldrb    r5, [r9, r2]
    SET_NZ  r5
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AE_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_AE_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r5, [r12, r0]
    SET_NZ  r5
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AE_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_AE_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r5, [r12, r0]
    SET_NZ  r5
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AE_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LDY immediate (A0) ============ */
.Lop_A0:
    FETCH_IMM r6
    SET_NZ  r6
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ LDY zero page (A4) ============ */
.Lop_A4:
    FETCH_ZP r0
    ldrb    r6, [r9, r0]
    SET_NZ  r6
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ LDY absolute (AC) ============ */
.Lop_AC:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_AC_not_ram
    ldrb    r6, [r9, r2]
    SET_NZ  r6
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AC_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_AC_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r6, [r12, r0]
    SET_NZ  r6
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AC_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_AC_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r6, [r12, r0]
    SET_NZ  r6
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_AC_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STZ zero page (64) ============ */
.Lop_64:
    FETCH_ZP r0
    mov     r1, #0
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ STZ absolute (9C) ============ */
.Lop_9C:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_9C_not_ram
    mov     r0, #0
    strb    r0, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_9C_not_ram:
    mov     r0, #0x2800
    cmp     r2, r0
    blt     .Lop_9C_io
    mov     r0, #0x3000
    cmp     r2, r0
    bge     .Lop_9C_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    mov     r1, #0
    strb    r1, [r12, r0]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_9C_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STX zero page (86) ============ */
.Lop_86:
    FETCH_ZP r0
    strb    r5, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ STX absolute (8E) ============ */
.Lop_8E:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_8E_not_ram
    strb    r5, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_8E_not_ram:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ STY zero page (84) ============ */
.Lop_84:
    FETCH_ZP r0
    strb    r6, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ STY absolute (8C) ============ */
.Lop_8C:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_8C_not_ram
    strb    r6, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #4
    b       .Ldispatch
.Lop_8C_not_ram:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ ADC immediate (69) - binary mode only ============ */
.Lop_69:
    tst     r7, #0x08            /* decimal mode? */
    bne     .Lexit_unhandled     /* exit for decimal */
    FETCH_IMM r0
    /* ADC: A + M + C */
    and     r1, r7, #0x01       /* carry in */
    add     r2, r4, r0
    add     r2, r2, r1
    /* Set flags */
    bic     r7, r7, #0xC3       /* clear N,V,Z,C */
    tst     r2, #0x100
    orrne   r7, r7, #0x01       /* carry */
    and     r2, r2, #0xFF
    /* overflow: ~(A^M) & (A^result) & 0x80 */
    eor     r1, r4, r0
    mvn     r1, r1
    eor     r3, r4, r2
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40       /* overflow */
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80       /* negative */
    tst     r4, #0xFF
    orreq   r7, r7, #0x02       /* zero */
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ADC zero page (65) ============ */
.Lop_65:
    tst     r7, #0x08
    bne     .Lexit_unhandled
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    and     r1, r7, #0x01
    add     r2, r4, r0
    add     r2, r2, r1
    bic     r7, r7, #0xC3
    tst     r2, #0x100
    orrne   r7, r7, #0x01
    and     r2, r2, #0xFF
    eor     r1, r4, r0
    mvn     r1, r1
    eor     r3, r4, r2
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80
    tst     r4, #0xFF
    orreq   r7, r7, #0x02
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ ADC absolute (6D) ============ */
.Lop_6D:
    tst     r7, #0x08
    bne     .Lexit_unhandled
    FETCH_ABS r2, r3
    /* Read from address */
    cmp     r2, #0x2000
    bge     .Lop_6D_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_6D_do_adc
.Lop_6D_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_6D_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_6D_do_adc
.Lop_6D_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_6D_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_6D_do_adc
.Lop_6D_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_6D_do_adc:
    and     r1, r7, #0x01
    add     r2, r4, r0
    add     r2, r2, r1
    bic     r7, r7, #0xC3
    tst     r2, #0x100
    orrne   r7, r7, #0x01
    and     r2, r2, #0xFF
    eor     r1, r4, r0
    mvn     r1, r1
    eor     r3, r4, r2
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80
    tst     r4, #0xFF
    orreq   r7, r7, #0x02
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ SBC immediate (E9) - binary mode only ============ */
.Lop_E9:
    tst     r7, #0x08
    bne     .Lexit_unhandled
    FETCH_IMM r0
    /* SBC: A - M - borrow, where borrow = !carry */
    and     r1, r7, #0x01
    eor     r1, r1, #1          /* borrow = !carry */
    sub     r2, r4, r0
    sub     r2, r2, r1
    bic     r7, r7, #0xC3
    /* carry = A >= (M + borrow) */
    add     r3, r0, r1
    cmp     r4, r3
    orrhs   r7, r7, #0x01
    and     r2, r2, #0xFF
    /* overflow: (A^result) & (A^M) & 0x80 */
    eor     r1, r4, r2
    eor     r3, r4, r0
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80
    tst     r4, #0xFF
    orreq   r7, r7, #0x02
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ SBC zero page (E5) ============ */
.Lop_E5:
    tst     r7, #0x08
    bne     .Lexit_unhandled
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    b       .Lop_sbc_common_zp
.Lop_sbc_common_zp:
    and     r1, r7, #0x01
    eor     r1, r1, #1
    sub     r2, r4, r0
    sub     r2, r2, r1
    bic     r7, r7, #0xC3
    /* Set carry: A >= M + borrow */
    add     r3, r0, r1
    cmp     r4, r3
    orrhs   r7, r7, #0x01
    and     r2, r2, #0xFF
    eor     r1, r4, r2
    eor     r3, r4, r0
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80
    tst     r4, #0xFF
    orreq   r7, r7, #0x02
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ SBC absolute (ED) ============ */
.Lop_ED:
    tst     r7, #0x08
    bne     .Lexit_unhandled
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_ED_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_ED_do_sbc
.Lop_ED_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_ED_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_ED_do_sbc
.Lop_ED_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_ED_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_ED_do_sbc
.Lop_ED_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_ED_do_sbc:
    and     r1, r7, #0x01
    eor     r1, r1, #1
    sub     r2, r4, r0
    sub     r2, r2, r1
    bic     r7, r7, #0xC3
    add     r3, r0, r1
    cmp     r4, r3
    orrhs   r7, r7, #0x01
    and     r2, r2, #0xFF
    eor     r1, r4, r2
    eor     r3, r4, r0
    and     r1, r1, r3
    tst     r1, #0x80
    orrne   r7, r7, #0x40
    mov     r4, r2
    tst     r4, #0x80
    orrne   r7, r7, #0x80
    tst     r4, #0xFF
    orreq   r7, r7, #0x02
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ AND immediate (29) ============ */
.Lop_29:
    FETCH_IMM r0
    and     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ AND zero page (25) ============ */
.Lop_25:
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    and     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ AND absolute (2D) ============ */
.Lop_2D:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_2D_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_2D_do
.Lop_2D_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_2D_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_2D_do
.Lop_2D_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_2D_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_2D_do
.Lop_2D_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_2D_do:
    and     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ ORA immediate (09) ============ */
.Lop_09:
    FETCH_IMM r0
    orr     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ORA zero page (05) ============ */
.Lop_05:
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    orr     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ ORA absolute (0D) ============ */
.Lop_0D:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_0D_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_0D_do
.Lop_0D_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_0D_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_0D_do
.Lop_0D_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_0D_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_0D_do
.Lop_0D_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_0D_do:
    orr     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ EOR immediate (49) ============ */
.Lop_49:
    FETCH_IMM r0
    eor     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ EOR zero page (45) ============ */
.Lop_45:
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    eor     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ EOR absolute (4D) ============ */
.Lop_4D:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_4D_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_4D_do
.Lop_4D_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_4D_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_4D_do
.Lop_4D_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_4D_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_4D_do
.Lop_4D_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_4D_do:
    eor     r4, r4, r0
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ CMP immediate (C9) ============ */
.Lop_C9:
    FETCH_IMM r0
    bic     r7, r7, #0x83
    cmp     r4, r0
    orrhs   r7, r7, #0x01
    sub     r2, r4, r0
    and     r2, r2, #0xFF
    tst     r2, #0xFF
    orreq   r7, r7, #0x02
    tst     r2, #0x80
    orrne   r7, r7, #0x80
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ CMP zero page (C5) ============ */
.Lop_C5:
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    bic     r7, r7, #0x83
    cmp     r4, r0
    orrhs   r7, r7, #0x01
    sub     r2, r4, r0
    and     r2, r2, #0xFF
    tst     r2, #0xFF
    orreq   r7, r7, #0x02
    tst     r2, #0x80
    orrne   r7, r7, #0x80
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ CMP absolute (CD) ============ */
.Lop_CD:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_CD_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_CD_do
.Lop_CD_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_CD_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_CD_do
.Lop_CD_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_CD_io
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_CD_do
.Lop_CD_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_CD_do:
    bic     r7, r7, #0x83
    cmp     r4, r0
    orrhs   r7, r7, #0x01
    sub     r2, r4, r0
    and     r2, r2, #0xFF
    tst     r2, #0xFF
    orreq   r7, r7, #0x02
    tst     r2, #0x80
    orrne   r7, r7, #0x80
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ CPX immediate (E0) ============ */
.Lop_E0:
    FETCH_IMM r0
    bic     r7, r7, #0x83
    cmp     r5, r0
    orrhs   r7, r7, #0x01
    sub     r2, r5, r0
    and     r2, r2, #0xFF
    tst     r2, #0xFF
    orreq   r7, r7, #0x02
    tst     r2, #0x80
    orrne   r7, r7, #0x80
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ CPY immediate (C0) ============ */
.Lop_C0:
    FETCH_IMM r0
    bic     r7, r7, #0x83
    cmp     r6, r0
    orrhs   r7, r7, #0x01
    sub     r2, r6, r0
    and     r2, r2, #0xFF
    tst     r2, #0xFF
    orreq   r7, r7, #0x02
    tst     r2, #0x80
    orrne   r7, r7, #0x80
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ASL A (0A) ============ */
.Lop_0A:
    bic     r7, r7, #0x01
    tst     r4, #0x80
    orrne   r7, r7, #0x01
    mov     r4, r4, lsl #1
    and     r4, r4, #0xFF
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ LSR A (4A) ============ */
.Lop_4A:
    bic     r7, r7, #0x01
    tst     r4, #0x01
    orrne   r7, r7, #0x01
    mov     r4, r4, lsr #1
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ROL A (2A) ============ */
.Lop_2A:
    and     r0, r7, #0x01       /* old carry */
    bic     r7, r7, #0x01
    tst     r4, #0x80
    orrne   r7, r7, #0x01       /* new carry from bit 7 */
    mov     r4, r4, lsl #1
    orr     r4, r4, r0           /* old carry into bit 0 */
    and     r4, r4, #0xFF
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ROR A (6A) ============ */
.Lop_6A:
    and     r0, r7, #0x01       /* old carry */
    bic     r7, r7, #0x01
    tst     r4, #0x01
    orrne   r7, r7, #0x01       /* new carry from bit 0 */
    mov     r4, r4, lsr #1
    orr     r4, r4, r0, lsl #7  /* old carry into bit 7 */
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ ASL zero page (06) ============ */
.Lop_06:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    bic     r7, r7, #0x01
    tst     r1, #0x80
    orrne   r7, r7, #0x01
    mov     r1, r1, lsl #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ ASL absolute (0E) ============ */
.Lop_0E:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_0E_io
    ldrb    r1, [r9, r2]
    bic     r7, r7, #0x01
    tst     r1, #0x80
    orrne   r7, r7, #0x01
    mov     r1, r1, lsl #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_0E_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ LSR zero page (46) ============ */
.Lop_46:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    bic     r7, r7, #0x01
    tst     r1, #0x01
    orrne   r7, r7, #0x01
    mov     r1, r1, lsr #1
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ LSR absolute (4E) ============ */
.Lop_4E:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_4E_io
    ldrb    r1, [r9, r2]
    bic     r7, r7, #0x01
    tst     r1, #0x01
    orrne   r7, r7, #0x01
    mov     r1, r1, lsr #1
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_4E_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ ROL zero page (26) ============ */
.Lop_26:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    and     r2, r7, #0x01
    bic     r7, r7, #0x01
    tst     r1, #0x80
    orrne   r7, r7, #0x01
    mov     r1, r1, lsl #1
    orr     r1, r1, r2
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ ROL absolute (2E) ============ */
.Lop_2E:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_2E_io
    ldrb    r1, [r9, r2]
    and     r3, r7, #0x01
    bic     r7, r7, #0x01
    tst     r1, #0x80
    orrne   r7, r7, #0x01
    mov     r1, r1, lsl #1
    orr     r1, r1, r3
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_2E_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ ROR zero page (66) ============ */
.Lop_66:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    and     r2, r7, #0x01
    bic     r7, r7, #0x01
    tst     r1, #0x01
    orrne   r7, r7, #0x01
    mov     r1, r1, lsr #1
    orr     r1, r1, r2, lsl #7
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ ROR absolute (6E) ============ */
.Lop_6E:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_6E_io
    ldrb    r1, [r9, r2]
    and     r3, r7, #0x01
    bic     r7, r7, #0x01
    tst     r1, #0x01
    orrne   r7, r7, #0x01
    mov     r1, r1, lsr #1
    orr     r1, r1, r3, lsl #7
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_6E_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ INX (E8) ============ */
.Lop_E8:
    add     r5, r5, #1
    and     r5, r5, #0xFF
    SET_NZ  r5
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ INY (C8) ============ */
.Lop_C8:
    add     r6, r6, #1
    and     r6, r6, #0xFF
    SET_NZ  r6
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ DEX (CA) ============ */
.Lop_CA:
    sub     r5, r5, #1
    and     r5, r5, #0xFF
    SET_NZ  r5
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ DEY (88) ============ */
.Lop_88:
    sub     r6, r6, #1
    and     r6, r6, #0xFF
    SET_NZ  r6
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ INC A - 65C02 (1A) ============ */
.Lop_1A:
    add     r4, r4, #1
    and     r4, r4, #0xFF
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ DEC A - 65C02 (3A) ============ */
.Lop_3A:
    sub     r4, r4, #1
    and     r4, r4, #0xFF
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ INC zero page (E6) ============ */
.Lop_E6:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    add     r1, r1, #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ DEC zero page (C6) ============ */
.Lop_C6:
    FETCH_ZP r0
    ldrb    r1, [r9, r0]
    sub     r1, r1, #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r2, #1
    strb    r2, [r12, r0]
    sub     r11, r11, #5
    b       .Ldispatch

/* ============ INC absolute (EE) ============ */
.Lop_EE:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_EE_io
    ldrb    r1, [r9, r2]
    add     r1, r1, #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_EE_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ DEC absolute (CE) ============ */
.Lop_CE:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_CE_io
    ldrb    r1, [r9, r2]
    sub     r1, r1, #1
    and     r1, r1, #0xFF
    SET_NZ  r1
    strb    r1, [r9, r2]
    ldr     r12, [sp, #12]
    mov     r0, #1
    strb    r0, [r12, r2]
    sub     r11, r11, #6
    b       .Ldispatch
.Lop_CE_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback

/* ============ Transfers ============ */

/* TAX (AA) */
.Lop_AA:
    mov     r5, r4
    SET_NZ  r5
    sub     r11, r11, #2
    b       .Ldispatch

/* TAY (A8) */
.Lop_A8:
    mov     r6, r4
    SET_NZ  r6
    sub     r11, r11, #2
    b       .Ldispatch

/* TXA (8A) */
.Lop_8A:
    mov     r4, r5
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* TYA (98) */
.Lop_98:
    mov     r4, r6
    SET_NZ  r4
    sub     r11, r11, #2
    b       .Ldispatch

/* TSX (BA) */
.Lop_BA:
    mov     r5, r10
    SET_NZ  r5
    sub     r11, r11, #2
    b       .Ldispatch

/* TXS (9A) */
.Lop_9A:
    mov     r10, r5
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ Stack Operations ============ */

/* PHA (48) */
.Lop_48:
    orr     r0, r10, #0x100
    strb    r4, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    sub     r11, r11, #3
    b       .Ldispatch

/* PLA (68) */
.Lop_68:
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r4, [r9, r0]
    SET_NZ  r4
    sub     r11, r11, #4
    b       .Ldispatch

/* PHP (08) */
.Lop_08:
    orr     r0, r10, #0x100
    orr     r1, r7, #0x30       /* set B and unused flags */
    strb    r1, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    sub     r11, r11, #3
    b       .Ldispatch

/* PLP (28) */
.Lop_28:
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r7, [r9, r0]
    orr     r7, r7, #0x20       /* constant flag always set */
    bic     r7, r7, #0x10       /* clear break */
    sub     r11, r11, #4
    b       .Ldispatch

/* PHX - 65C02 (DA) */
.Lop_DA:
    orr     r0, r10, #0x100
    strb    r5, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    sub     r11, r11, #3
    b       .Ldispatch

/* PLX - 65C02 (FA) */
.Lop_FA:
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r5, [r9, r0]
    SET_NZ  r5
    sub     r11, r11, #4
    b       .Ldispatch

/* PHY - 65C02 (5A) */
.Lop_5A:
    orr     r0, r10, #0x100
    strb    r6, [r9, r0]
    ldr     r12, [sp, #12]
    mov     r1, #1
    strb    r1, [r12, r0]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    sub     r11, r11, #3
    b       .Ldispatch

/* PLY - 65C02 (7A) */
.Lop_7A:
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r6, [r9, r0]
    SET_NZ  r6
    sub     r11, r11, #4
    b       .Ldispatch

/* ============ Branches ============ */

/* BNE (D0) */
.Lop_D0:
    FETCH_IMM r0
    tst     r7, #0x02            /* zero flag set? */
    bne     .Lop_D0_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_D0_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BEQ (F0) */
.Lop_F0:
    FETCH_IMM r0
    tst     r7, #0x02
    beq     .Lop_F0_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_F0_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BCS (B0) */
.Lop_B0:
    FETCH_IMM r0
    tst     r7, #0x01
    beq     .Lop_B0_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_B0_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BCC (90) */
.Lop_90:
    FETCH_IMM r0
    tst     r7, #0x01
    bne     .Lop_90_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_90_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BMI (30) */
.Lop_30:
    FETCH_IMM r0
    tst     r7, #0x80
    beq     .Lop_30_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_30_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BPL (10) */
.Lop_10:
    FETCH_IMM r0
    tst     r7, #0x80
    bne     .Lop_10_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_10_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BVC (50) */
.Lop_50:
    FETCH_IMM r0
    tst     r7, #0x40
    bne     .Lop_50_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_50_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BVS (70) */
.Lop_70:
    FETCH_IMM r0
    tst     r7, #0x40
    beq     .Lop_70_not
    DO_BRANCH r0
    b       .Ldispatch
.Lop_70_not:
    sub     r11, r11, #2
    b       .Ldispatch

/* BRA - 65C02 (80) */
.Lop_80:
    FETCH_IMM r0
    DO_BRANCH r0
    b       .Ldispatch

/* ============ JMP absolute (4C) ============ */
.Lop_4C:
    FETCH_ABS r2, r3
    mov     r8, r2
    sub     r11, r11, #3
    b       .Ldispatch

/* ============ JSR absolute (20) ============ */
.Lop_20:
    FETCH_ABS r2, r3
    /* Push return address - 1 (pc-1, since PC already advanced past operands) */
    sub     r0, r8, #1
    bic     r0, r0, #0x10000
    /* Push high byte */
    orr     r1, r10, #0x100
    mov     r3, r0, lsr #8
    strb    r3, [r9, r1]
    ldr     r12, [sp, #12]
    mov     r3, #1
    strb    r3, [r12, r1]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    /* Push low byte */
    orr     r1, r10, #0x100
    strb    r0, [r9, r1]
    strb    r3, [r12, r1]
    sub     r10, r10, #1
    and     r10, r10, #0xFF
    mov     r8, r2
    sub     r11, r11, #6
    b       .Ldispatch

/* ============ RTS (60) ============ */
.Lop_60:
    /* Pop low byte */
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r2, [r9, r0]
    /* Pop high byte */
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r3, [r9, r0]
    orr     r8, r2, r3, lsl #8
    add     r8, r8, #1
    bic     r8, r8, #0x10000
    sub     r11, r11, #6
    b       .Ldispatch

/* ============ RTI (40) ============ */
.Lop_40:
    /* Pop status */
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r7, [r9, r0]
    orr     r7, r7, #0x20       /* constant always set */
    bic     r7, r7, #0x10       /* clear break */
    /* Pop PC low */
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r2, [r9, r0]
    /* Pop PC high */
    add     r10, r10, #1
    and     r10, r10, #0xFF
    orr     r0, r10, #0x100
    ldrb    r3, [r9, r0]
    orr     r8, r2, r3, lsl #8
    sub     r11, r11, #6
    b       .Ldispatch

/* ============ Flag Operations ============ */

/* CLC (18) */
.Lop_18:
    bic     r7, r7, #0x01
    sub     r11, r11, #2
    b       .Ldispatch

/* SEC (38) */
.Lop_38:
    orr     r7, r7, #0x01
    sub     r11, r11, #2
    b       .Ldispatch

/* CLI (58) */
.Lop_58:
    bic     r7, r7, #0x04
    sub     r11, r11, #2
    b       .Ldispatch

/* SEI (78) */
.Lop_78:
    orr     r7, r7, #0x04
    sub     r11, r11, #2
    b       .Ldispatch

/* CLD (D8) */
.Lop_D8:
    bic     r7, r7, #0x08
    sub     r11, r11, #2
    b       .Ldispatch

/* SED (F8) */
.Lop_F8:
    orr     r7, r7, #0x08
    sub     r11, r11, #2
    b       .Ldispatch

/* CLV (B8) */
.Lop_B8:
    bic     r7, r7, #0x40
    sub     r11, r11, #2
    b       .Ldispatch

/* ============ BIT Operations ============ */

/* BIT absolute (2C) */
.Lop_2C:
    FETCH_ABS r2, r3
    cmp     r2, #0x2000
    bge     .Lop_2C_not_ram
    ldrb    r0, [r9, r2]
    b       .Lop_2C_do
.Lop_2C_not_ram:
    cmp     r2, #0xC000
    blt     .Lop_2C_not_romhi
    ldr     r12, [sp, #8]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_2C_do
.Lop_2C_not_romhi:
    tst     r2, #0x8000
    beq     .Lop_2C_not_rom
    ldr     r12, [sp, #4]
    bic     r0, r2, #0xC000
    ldrb    r0, [r12, r0]
    b       .Lop_2C_do
.Lop_2C_not_rom:
    /* VIA */
    mov     r1, #0x2800
    cmp     r2, r1
    blt     .Lop_2C_io
    mov     r1, #0x3000
    cmp     r2, r1
    bge     .Lop_2C_io
    ldr     r12, [sp, #16]
    and     r0, r2, #0xF
    ldrb    r0, [r12, r0]
    b       .Lop_2C_do
.Lop_2C_io:
    sub     r8, r8, #3
    bic     r8, r8, #0x10000
    b       .Lexit_unhandled_norollback
.Lop_2C_do:
    bic     r7, r7, #0xC2       /* clear N, V, Z */
    and     r1, r0, #0x80
    orr     r7, r7, r1           /* N = bit 7 of memory */
    and     r1, r0, #0x40
    orr     r7, r7, r1           /* V = bit 6 of memory */
    tst     r4, r0               /* A & M */
    orreq   r7, r7, #0x02       /* Z = !(A & M) */
    sub     r11, r11, #4
    b       .Ldispatch

/* BIT zero page (24) */
.Lop_24:
    FETCH_ZP r0
    ldrb    r0, [r9, r0]
    bic     r7, r7, #0xC2
    and     r1, r0, #0x80
    orr     r7, r7, r1
    and     r1, r0, #0x40
    orr     r7, r7, r1
    tst     r4, r0
    orreq   r7, r7, #0x02
    sub     r11, r11, #3
    b       .Ldispatch

/* BIT immediate - 65C02 (89) */
.Lop_89:
    FETCH_IMM r0
    /* BIT immediate only affects Z flag, not N or V */
    bic     r7, r7, #0x02
    tst     r4, r0
    orreq   r7, r7, #0x02
    sub     r11, r11, #2
    b       .Ldispatch


/* ============ Exit for unhandled with PC already rolled back ============ */
.Lexit_unhandled_norollback:
    ldr     r0, [sp, #0]
    strb    r4, [r0, #0]
    strb    r5, [r0, #1]
    strb    r6, [r0, #2]
    strb    r10, [r0, #3]
    strb    r7, [r0, #4]
    mov     r1, #1
    strb    r1, [r0, #5]         /* exit_reason = 1 */
    strh    r8, [r0, #8]
    str     r11, [r0, #12]
    add     sp, sp, #24
    pop     {r4-r11, pc}


/* ============ SBC immediate - simplified carry ============ */
/* Fix the carry logic for SBC E9 using same approach as E5/ED */
/* (The existing E9 handler above has complex branching; let's redirect) */

/* =====================================================================
 * JUMP TABLE - 256 entries, one per opcode
 * ===================================================================== */

.align 2
.Ljump_table:
    .word .Lexit_unhandled  /* 00 BRK - exit to C++ */
    .word .Lexit_unhandled  /* 01 */
    .word .Lexit_unhandled  /* 02 */
    .word .Lexit_unhandled  /* 03 */
    .word .Lexit_unhandled  /* 04 */
    .word .Lop_05           /* 05 ORA zp */
    .word .Lop_06           /* 06 ASL zp */
    .word .Lexit_unhandled  /* 07 RMB0 */
    .word .Lop_08           /* 08 PHP */
    .word .Lop_09           /* 09 ORA imm */
    .word .Lop_0A           /* 0A ASL A */
    .word .Lexit_unhandled  /* 0B */
    .word .Lexit_unhandled  /* 0C */
    .word .Lop_0D           /* 0D ORA abs */
    .word .Lop_0E           /* 0E ASL abs */
    .word .Lexit_unhandled  /* 0F BBR0 */
    .word .Lop_10           /* 10 BPL */
    .word .Lexit_unhandled  /* 11 */
    .word .Lexit_unhandled  /* 12 */
    .word .Lexit_unhandled  /* 13 */
    .word .Lexit_unhandled  /* 14 */
    .word .Lexit_unhandled  /* 15 */
    .word .Lexit_unhandled  /* 16 */
    .word .Lexit_unhandled  /* 17 */
    .word .Lop_18           /* 18 CLC */
    .word .Lexit_unhandled  /* 19 */
    .word .Lop_1A           /* 1A INC A */
    .word .Lexit_unhandled  /* 1B */
    .word .Lexit_unhandled  /* 1C */
    .word .Lexit_unhandled  /* 1D */
    .word .Lexit_unhandled  /* 1E */
    .word .Lexit_unhandled  /* 1F */
    .word .Lop_20           /* 20 JSR */
    .word .Lexit_unhandled  /* 21 */
    .word .Lexit_unhandled  /* 22 */
    .word .Lexit_unhandled  /* 23 */
    .word .Lop_24           /* 24 BIT zp */
    .word .Lop_25           /* 25 AND zp */
    .word .Lop_26           /* 26 ROL zp */
    .word .Lexit_unhandled  /* 27 */
    .word .Lop_28           /* 28 PLP */
    .word .Lop_29           /* 29 AND imm */
    .word .Lop_2A           /* 2A ROL A */
    .word .Lexit_unhandled  /* 2B */
    .word .Lop_2C           /* 2C BIT abs */
    .word .Lop_2D           /* 2D AND abs */
    .word .Lop_2E           /* 2E ROL abs */
    .word .Lexit_unhandled  /* 2F */
    .word .Lop_30           /* 30 BMI */
    .word .Lexit_unhandled  /* 31 */
    .word .Lexit_unhandled  /* 32 */
    .word .Lexit_unhandled  /* 33 */
    .word .Lexit_unhandled  /* 34 */
    .word .Lexit_unhandled  /* 35 */
    .word .Lexit_unhandled  /* 36 */
    .word .Lexit_unhandled  /* 37 */
    .word .Lop_38           /* 38 SEC */
    .word .Lexit_unhandled  /* 39 */
    .word .Lop_3A           /* 3A DEC A */
    .word .Lexit_unhandled  /* 3B */
    .word .Lexit_unhandled  /* 3C */
    .word .Lexit_unhandled  /* 3D */
    .word .Lexit_unhandled  /* 3E */
    .word .Lexit_unhandled  /* 3F */
    .word .Lop_40           /* 40 RTI */
    .word .Lexit_unhandled  /* 41 */
    .word .Lexit_unhandled  /* 42 */
    .word .Lexit_unhandled  /* 43 */
    .word .Lexit_unhandled  /* 44 */
    .word .Lop_45           /* 45 EOR zp */
    .word .Lop_46           /* 46 LSR zp */
    .word .Lexit_unhandled  /* 47 */
    .word .Lop_48           /* 48 PHA */
    .word .Lop_49           /* 49 EOR imm */
    .word .Lop_4A           /* 4A LSR A */
    .word .Lexit_unhandled  /* 4B */
    .word .Lop_4C           /* 4C JMP abs */
    .word .Lop_4D           /* 4D EOR abs */
    .word .Lop_4E           /* 4E LSR abs */
    .word .Lexit_unhandled  /* 4F */
    .word .Lop_50           /* 50 BVC */
    .word .Lexit_unhandled  /* 51 */
    .word .Lexit_unhandled  /* 52 */
    .word .Lexit_unhandled  /* 53 */
    .word .Lexit_unhandled  /* 54 */
    .word .Lexit_unhandled  /* 55 */
    .word .Lexit_unhandled  /* 56 */
    .word .Lexit_unhandled  /* 57 */
    .word .Lop_58           /* 58 CLI */
    .word .Lexit_unhandled  /* 59 */
    .word .Lop_5A           /* 5A PHY */
    .word .Lexit_unhandled  /* 5B */
    .word .Lexit_unhandled  /* 5C */
    .word .Lexit_unhandled  /* 5D */
    .word .Lexit_unhandled  /* 5E */
    .word .Lexit_unhandled  /* 5F */
    .word .Lop_60           /* 60 RTS */
    .word .Lexit_unhandled  /* 61 */
    .word .Lexit_unhandled  /* 62 */
    .word .Lexit_unhandled  /* 63 */
    .word .Lop_64           /* 64 STZ zp */
    .word .Lop_65           /* 65 ADC zp */
    .word .Lop_66           /* 66 ROR zp */
    .word .Lexit_unhandled  /* 67 */
    .word .Lop_68           /* 68 PLA */
    .word .Lop_69           /* 69 ADC imm */
    .word .Lop_6A           /* 6A ROR A */
    .word .Lexit_unhandled  /* 6B */
    .word .Lexit_unhandled  /* 6C JMP (ind) - exit */
    .word .Lop_6D           /* 6D ADC abs */
    .word .Lop_6E           /* 6E ROR abs */
    .word .Lexit_unhandled  /* 6F */
    .word .Lop_70           /* 70 BVS */
    .word .Lexit_unhandled  /* 71 */
    .word .Lexit_unhandled  /* 72 */
    .word .Lexit_unhandled  /* 73 */
    .word .Lexit_unhandled  /* 74 */
    .word .Lexit_unhandled  /* 75 */
    .word .Lexit_unhandled  /* 76 */
    .word .Lexit_unhandled  /* 77 */
    .word .Lop_78           /* 78 SEI */
    .word .Lexit_unhandled  /* 79 */
    .word .Lop_7A           /* 7A PLY */
    .word .Lexit_unhandled  /* 7B */
    .word .Lexit_unhandled  /* 7C */
    .word .Lexit_unhandled  /* 7D */
    .word .Lexit_unhandled  /* 7E */
    .word .Lexit_unhandled  /* 7F */
    .word .Lop_80           /* 80 BRA */
    .word .Lexit_unhandled  /* 81 */
    .word .Lexit_unhandled  /* 82 */
    .word .Lexit_unhandled  /* 83 */
    .word .Lop_84           /* 84 STY zp */
    .word .Lop_85           /* 85 STA zp */
    .word .Lop_86           /* 86 STX zp */
    .word .Lexit_unhandled  /* 87 */
    .word .Lop_88           /* 88 DEY */
    .word .Lop_89           /* 89 BIT imm */
    .word .Lop_8A           /* 8A TXA */
    .word .Lexit_unhandled  /* 8B */
    .word .Lop_8C           /* 8C STY abs */
    .word .Lop_8D           /* 8D STA abs */
    .word .Lop_8E           /* 8E STX abs */
    .word .Lexit_unhandled  /* 8F */
    .word .Lop_90           /* 90 BCC */
    .word .Lop_91           /* 91 STA (ind),Y */
    .word .Lop_92           /* 92 STA (zp) */
    .word .Lexit_unhandled  /* 93 */
    .word .Lexit_unhandled  /* 94 */
    .word .Lop_95           /* 95 STA zp,X */
    .word .Lexit_unhandled  /* 96 */
    .word .Lexit_unhandled  /* 97 */
    .word .Lop_98           /* 98 TYA */
    .word .Lop_99           /* 99 STA abs,Y */
    .word .Lop_9A           /* 9A TXS */
    .word .Lexit_unhandled  /* 9B */
    .word .Lop_9C           /* 9C STZ abs */
    .word .Lop_9D           /* 9D STA abs,X */
    .word .Lexit_unhandled  /* 9E */
    .word .Lexit_unhandled  /* 9F */
    .word .Lop_A0           /* A0 LDY imm */
    .word .Lexit_unhandled  /* A1 */
    .word .Lop_A2           /* A2 LDX imm */
    .word .Lexit_unhandled  /* A3 */
    .word .Lop_A4           /* A4 LDY zp */
    .word .Lop_A5           /* A5 LDA zp */
    .word .Lop_A6           /* A6 LDX zp */
    .word .Lexit_unhandled  /* A7 */
    .word .Lop_A8           /* A8 TAY */
    .word .Lop_A9           /* A9 LDA imm */
    .word .Lop_AA           /* AA TAX */
    .word .Lexit_unhandled  /* AB */
    .word .Lop_AC           /* AC LDY abs */
    .word .Lop_AD           /* AD LDA abs */
    .word .Lop_AE           /* AE LDX abs */
    .word .Lexit_unhandled  /* AF */
    .word .Lop_B0           /* B0 BCS */
    .word .Lop_B1           /* B1 LDA (ind),Y */
    .word .Lop_B2           /* B2 LDA (zp) */
    .word .Lexit_unhandled  /* B3 */
    .word .Lexit_unhandled  /* B4 */
    .word .Lop_B5           /* B5 LDA zp,X */
    .word .Lexit_unhandled  /* B6 */
    .word .Lexit_unhandled  /* B7 */
    .word .Lop_B8           /* B8 CLV */
    .word .Lop_B9           /* B9 LDA abs,Y */
    .word .Lop_BA           /* BA TSX */
    .word .Lexit_unhandled  /* BB */
    .word .Lexit_unhandled  /* BC */
    .word .Lop_BD           /* BD LDA abs,X */
    .word .Lexit_unhandled  /* BE */
    .word .Lexit_unhandled  /* BF */
    .word .Lop_C0           /* C0 CPY imm */
    .word .Lexit_unhandled  /* C1 */
    .word .Lexit_unhandled  /* C2 */
    .word .Lexit_unhandled  /* C3 */
    .word .Lexit_unhandled  /* C4 */
    .word .Lop_C5           /* C5 CMP zp */
    .word .Lop_C6           /* C6 DEC zp */
    .word .Lexit_unhandled  /* C7 */
    .word .Lop_C8           /* C8 INY */
    .word .Lop_C9           /* C9 CMP imm */
    .word .Lop_CA           /* CA DEX */
    .word .Lexit_unhandled  /* CB */
    .word .Lexit_unhandled  /* CC */
    .word .Lop_CD           /* CD CMP abs */
    .word .Lop_CE           /* CE DEC abs */
    .word .Lexit_unhandled  /* CF */
    .word .Lop_D0           /* D0 BNE */
    .word .Lexit_unhandled  /* D1 */
    .word .Lexit_unhandled  /* D2 */
    .word .Lexit_unhandled  /* D3 */
    .word .Lexit_unhandled  /* D4 */
    .word .Lexit_unhandled  /* D5 */
    .word .Lexit_unhandled  /* D6 */
    .word .Lexit_unhandled  /* D7 */
    .word .Lop_D8           /* D8 CLD */
    .word .Lexit_unhandled  /* D9 */
    .word .Lop_DA           /* DA PHX */
    .word .Lexit_unhandled  /* DB */
    .word .Lexit_unhandled  /* DC */
    .word .Lexit_unhandled  /* DD */
    .word .Lexit_unhandled  /* DE */
    .word .Lexit_unhandled  /* DF */
    .word .Lop_E0           /* E0 CPX imm */
    .word .Lexit_unhandled  /* E1 */
    .word .Lexit_unhandled  /* E2 */
    .word .Lexit_unhandled  /* E3 */
    .word .Lexit_unhandled  /* E4 */
    .word .Lop_E5           /* E5 SBC zp */
    .word .Lop_E6           /* E6 INC zp */
    .word .Lexit_unhandled  /* E7 */
    .word .Lop_E8           /* E8 INX */
    .word .Lop_E9           /* E9 SBC imm */
    .word .Lop_EA           /* EA NOP */
    .word .Lexit_unhandled  /* EB */
    .word .Lexit_unhandled  /* EC */
    .word .Lop_ED           /* ED SBC abs */
    .word .Lop_EE           /* EE INC abs */
    .word .Lexit_unhandled  /* EF */
    .word .Lop_F0           /* F0 BEQ */
    .word .Lexit_unhandled  /* F1 */
    .word .Lexit_unhandled  /* F2 */
    .word .Lexit_unhandled  /* F3 */
    .word .Lexit_unhandled  /* F4 */
    .word .Lexit_unhandled  /* F5 */
    .word .Lexit_unhandled  /* F6 */
    .word .Lexit_unhandled  /* F7 */
    .word .Lop_F8           /* F8 SED */
    .word .Lexit_unhandled  /* F9 */
    .word .Lop_FA           /* FA PLX */
    .word .Lexit_unhandled  /* FB */
    .word .Lexit_unhandled  /* FC */
    .word .Lexit_unhandled  /* FD */
    .word .Lexit_unhandled  /* FE */
    .word .Lexit_unhandled  /* FF */

.size mos6502_run_asm, .-mos6502_run_asm

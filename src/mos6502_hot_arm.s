.syntax unified
.arm

.section .itcm, "ax", %progbits
.align 2

.global mos6502_run_hot_arm
.type mos6502_run_hot_arm, %function

/* int32_t mos6502_run_hot_arm(
 *   uint8_t* A, uint8_t* X, uint8_t* Y, uint8_t* status,
 *   uint16_t* pc, uint8_t (*readfn)(uint16_t), void (*writefn)(uint16_t,uint8_t),
 *   int32_t cycles_remaining)
 *
 * Returns cycles consumed. Leaves unhandled opcode at pc (rollback fetch).
 */
mos6502_run_hot_arm:
    push {r4-r11, lr}
    sub  sp, sp, #24

    /* locals */
    str  r0, [sp, #0]   /* A* */
    str  r1, [sp, #4]   /* X* */
    str  r2, [sp, #8]   /* Y* */
    str  r3, [sp, #12]  /* status* */

    /* stack args after push+locals: +60.. */
    ldr  r0, [sp, #60]  /* pc* */
    ldrh r8, [r0]       /* pc value */
    ldr  r9, [sp, #64]  /* readfn */
    ldr  r10,[sp, #68]  /* writefn */
    ldr  r11,[sp, #72]  /* cycles remaining */
    str  r11,[sp, #16]  /* initial cycles */

    ldr  r0, [sp, #0]
    ldrb r4, [r0]       /* A */
    ldr  r0, [sp, #4]
    ldrb r5, [r0]       /* X */
    ldr  r0, [sp, #8]
    ldrb r6, [r0]       /* Y */
    ldr  r0, [sp, #12]
    ldrb r7, [r0]       /* status */

1:  /* loop */
    cmp  r11, #0
    ble  90f

    /* opcode = read(pc++); */
    mov  r0, r8
    blx  r9
    and  r1, r0, #0xFF
    add  r8, r8, #1

    cmp  r1, #0xEA      /* NOP */
    beq  20f
    cmp  r1, #0xA9      /* LDA IMM */
    beq  21f
    cmp  r1, #0xA5      /* LDA ZER */
    beq  22f
    cmp  r1, #0xAD      /* LDA ABS */
    beq  23f
    cmp  r1, #0x85      /* STA ZER */
    beq  24f
    cmp  r1, #0x8D      /* STA ABS */
    beq  25f
    cmp  r1, #0xA2      /* LDX IMM */
    beq  26f
    cmp  r1, #0xA0      /* LDY IMM */
    beq  27f
    cmp  r1, #0xE8      /* INX */
    beq  28f
    cmp  r1, #0xC8      /* INY */
    beq  29f
    cmp  r1, #0xCA      /* DEX */
    beq  30f
    cmp  r1, #0x88      /* DEY */
    beq  31f
    cmp  r1, #0xAA      /* TAX */
    beq  32f
    cmp  r1, #0xA8      /* TAY */
    beq  33f
    cmp  r1, #0x8A      /* TXA */
    beq  34f
    cmp  r1, #0x98      /* TYA */
    beq  35f
    cmp  r1, #0x4C      /* JMP ABS */
    beq  36f
    cmp  r1, #0xD0      /* BNE */
    beq  37f
    cmp  r1, #0xF0      /* BEQ */
    beq  38f
    cmp  r1, #0x10      /* BPL */
    beq  39f
    cmp  r1, #0x30      /* BMI */
    beq  40f
    cmp  r1, #0x09      /* ORA IMM */
    beq  41f
    cmp  r1, #0x29      /* AND IMM */
    beq  42f
    cmp  r1, #0x49      /* EOR IMM */
    beq  43f
    cmp  r1, #0xC9      /* CMP IMM */
    beq  44f
    cmp  r1, #0xC5      /* CMP ZER */
    beq  45f
    cmp  r1, #0xCD      /* CMP ABS */
    beq  46f
    cmp  r1, #0xE0      /* CPX IMM */
    beq  47f
    cmp  r1, #0xC0      /* CPY IMM */
    beq  48f
    cmp  r1, #0xEE      /* INC ABS */
    beq  56f
    cmp  r1, #0x18      /* CLC */
    beq  49f
    cmp  r1, #0x38      /* SEC */
    beq  50f
    cmp  r1, #0x58      /* CLI */
    beq  51f
    cmp  r1, #0x78      /* SEI */
    beq  52f
    cmp  r1, #0xD8      /* CLD */
    beq  53f
    cmp  r1, #0xF8      /* SED */
    beq  54f
    cmp  r1, #0xB8      /* CLV */
    beq  55f

    /* unknown: rollback opcode fetch and exit */
    sub  r8, r8, #1
    b    90f

20: /* NOP */
    sub  r11, r11, #2
    b    1b

21: /* LDA IMM */
    mov  r0, r8
    blx  r9
    and  r4, r0, #0xFF
    add  r8, r8, #1
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

22: /* LDA ZER */
    mov  r0, r8
    blx  r9
    and  r0, r0, #0xFF
    add  r8, r8, #1
    blx  r9
    and  r4, r0, #0xFF
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #3
    b    1b

23: /* LDA ABS */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    mov  r0, r8
    blx  r9
    and  r3, r0, #0xFF
    add  r8, r8, #1
    orr  r0, r2, r3, lsl #8
    blx  r9
    and  r4, r0, #0xFF
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #4
    b    1b

24: /* STA ZER */
    mov  r0, r8
    blx  r9
    and  r0, r0, #0xFF
    add  r8, r8, #1
    mov  r1, r4
    blx  r10
    sub  r11, r11, #3
    b    1b

25: /* STA ABS */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    mov  r0, r8
    blx  r9
    and  r3, r0, #0xFF
    add  r8, r8, #1
    orr  r0, r2, r3, lsl #8
    mov  r1, r4
    blx  r10
    sub  r11, r11, #4
    b    1b

26: /* LDX IMM */
    mov  r0, r8
    blx  r9
    and  r5, r0, #0xFF
    add  r8, r8, #1
    bic  r7, r7, #0x82
    cmp  r5, #0
    orreq r7, r7, #0x02
    tst  r5, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

27: /* LDY IMM */
    mov  r0, r8
    blx  r9
    and  r6, r0, #0xFF
    add  r8, r8, #1
    bic  r7, r7, #0x82
    cmp  r6, #0
    orreq r7, r7, #0x02
    tst  r6, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

28: /* INX */
    add  r5, r5, #1
    and  r5, r5, #0xFF
    bic  r7, r7, #0x82
    cmp  r5, #0
    orreq r7, r7, #0x02
    tst  r5, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

29: /* INY */
    add  r6, r6, #1
    and  r6, r6, #0xFF
    bic  r7, r7, #0x82
    cmp  r6, #0
    orreq r7, r7, #0x02
    tst  r6, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

30: /* DEX */
    sub  r5, r5, #1
    and  r5, r5, #0xFF
    bic  r7, r7, #0x82
    cmp  r5, #0
    orreq r7, r7, #0x02
    tst  r5, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

31: /* DEY */
    sub  r6, r6, #1
    and  r6, r6, #0xFF
    bic  r7, r7, #0x82
    cmp  r6, #0
    orreq r7, r7, #0x02
    tst  r6, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

32: /* TAX */
    mov  r5, r4
    bic  r7, r7, #0x82
    cmp  r5, #0
    orreq r7, r7, #0x02
    tst  r5, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

33: /* TAY */
    mov  r6, r4
    bic  r7, r7, #0x82
    cmp  r6, #0
    orreq r7, r7, #0x02
    tst  r6, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

34: /* TXA */
    mov  r4, r5
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

35: /* TYA */
    mov  r4, r6
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

36: /* JMP ABS */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    mov  r0, r8
    blx  r9
    and  r3, r0, #0xFF
    add  r8, r8, #1
    orr  r8, r2, r3, lsl #8
    sub  r11, r11, #3
    b    1b

37: /* BNE */
    mov  r0, r8
    blx  r9
    lsl  r2, r0, #24
    asr  r2, r2, #24
    add  r8, r8, #1
    tst  r7, #0x02
    bne  .L_bne_not
    mov  r1, r8
    add  r3, r1, r2
    lsr  r0, r1, #8
    lsr  r2, r3, #8
    cmp  r0, r2
    subeq r11, r11, #3
    subne r11, r11, #4
    mov  r8, r3
    b    1b
.L_bne_not:
    sub  r11, r11, #2
    b    1b

38: /* BEQ */
    mov  r0, r8
    blx  r9
    lsl  r2, r0, #24
    asr  r2, r2, #24
    add  r8, r8, #1
    tst  r7, #0x02
    beq  .L_beq_take
    sub  r11, r11, #2
    b    1b
.L_beq_take:
    mov  r1, r8
    add  r3, r1, r2
    lsr  r0, r1, #8
    lsr  r2, r3, #8
    cmp  r0, r2
    subeq r11, r11, #3
    subne r11, r11, #4
    mov  r8, r3
    b    1b

39: /* BPL */
    mov  r0, r8
    blx  r9
    lsl  r2, r0, #24
    asr  r2, r2, #24
    add  r8, r8, #1
    tst  r7, #0x80
    bne  .L_bpl_not
    mov  r1, r8
    add  r3, r1, r2
    lsr  r0, r1, #8
    lsr  r2, r3, #8
    cmp  r0, r2
    subeq r11, r11, #3
    subne r11, r11, #4
    mov  r8, r3
    b    1b
.L_bpl_not:
    sub  r11, r11, #2
    b    1b

40: /* BMI */
    mov  r0, r8
    blx  r9
    lsl  r2, r0, #24
    asr  r2, r2, #24
    add  r8, r8, #1
    tst  r7, #0x80
    beq  .L_bmi_not
    mov  r1, r8
    add  r3, r1, r2
    lsr  r0, r1, #8
    lsr  r2, r3, #8
    cmp  r0, r2
    subeq r11, r11, #3
    subne r11, r11, #4
    mov  r8, r3
    b    1b
.L_bmi_not:
    sub  r11, r11, #2
    b    1b

41: /* ORA IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    orr  r4, r4, r2
    and  r4, r4, #0xFF
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

42: /* AND IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    and  r4, r4, r2
    and  r4, r4, #0xFF
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

43: /* EOR IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    eor  r4, r4, r2
    and  r4, r4, #0xFF
    bic  r7, r7, #0x82
    cmp  r4, #0
    orreq r7, r7, #0x02
    tst  r4, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

44: /* CMP IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    sub  r3, r4, r2
    and  r3, r3, #0xFF
    bic  r7, r7, #0x83
    cmp  r4, r2
    orrhs r7, r7, #0x01
    cmp  r3, #0
    orreq r7, r7, #0x02
    tst  r3, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

45: /* CMP ZER */
    mov  r0, r8
    blx  r9
    and  r0, r0, #0xFF
    add  r8, r8, #1
    blx  r9
    and  r2, r0, #0xFF
    sub  r3, r4, r2
    and  r3, r3, #0xFF
    bic  r7, r7, #0x83
    cmp  r4, r2
    orrhs r7, r7, #0x01
    cmp  r3, #0
    orreq r7, r7, #0x02
    tst  r3, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #3
    b    1b

46: /* CMP ABS */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    mov  r0, r8
    blx  r9
    and  r3, r0, #0xFF
    add  r8, r8, #1
    orr  r0, r2, r3, lsl #8
    blx  r9
    and  r2, r0, #0xFF
    sub  r3, r4, r2
    and  r3, r3, #0xFF
    bic  r7, r7, #0x83
    cmp  r4, r2
    orrhs r7, r7, #0x01
    cmp  r3, #0
    orreq r7, r7, #0x02
    tst  r3, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #4
    b    1b

47: /* CPX IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    sub  r3, r5, r2
    and  r3, r3, #0xFF
    bic  r7, r7, #0x83
    cmp  r5, r2
    orrhs r7, r7, #0x01
    cmp  r3, #0
    orreq r7, r7, #0x02
    tst  r3, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

48: /* CPY IMM */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    sub  r3, r6, r2
    and  r3, r3, #0xFF
    bic  r7, r7, #0x83
    cmp  r6, r2
    orrhs r7, r7, #0x01
    cmp  r3, #0
    orreq r7, r7, #0x02
    tst  r3, #0x80
    orrne r7, r7, #0x80
    sub  r11, r11, #2
    b    1b

49: /* CLC */
    bic  r7, r7, #0x01
    sub  r11, r11, #2
    b    1b

50: /* SEC */
    orr  r7, r7, #0x01
    sub  r11, r11, #2
    b    1b

51: /* CLI */
    bic  r7, r7, #0x04
    sub  r11, r11, #2
    b    1b

52: /* SEI */
    orr  r7, r7, #0x04
    sub  r11, r11, #2
    b    1b

53: /* CLD */
    bic  r7, r7, #0x08
    sub  r11, r11, #2
    b    1b

54: /* SED */
    orr  r7, r7, #0x08
    sub  r11, r11, #2
    b    1b

55: /* CLV */
    bic  r7, r7, #0x40
    sub  r11, r11, #2
    b    1b

56: /* INC ABS */
    mov  r0, r8
    blx  r9
    and  r2, r0, #0xFF
    add  r8, r8, #1
    mov  r0, r8
    blx  r9
    and  r3, r0, #0xFF
    add  r8, r8, #1
    orr  r0, r2, r3, lsl #8
    str  r0, [sp, #20]   /* save absolute addr */
    blx  r9
    and  r2, r0, #0xFF
    add  r2, r2, #1
    and  r2, r2, #0xFF
    bic  r7, r7, #0x82
    cmp  r2, #0
    orreq r7, r7, #0x02
    tst  r2, #0x80
    orrne r7, r7, #0x80
    ldr  r0, [sp, #20]
    mov  r1, r2
    blx  r10
    sub  r11, r11, #6
    b    1b

90: /* flush state */
    ldr  r0, [sp, #0]
    strb r4, [r0]
    ldr  r0, [sp, #4]
    strb r5, [r0]
    ldr  r0, [sp, #8]
    strb r6, [r0]
    ldr  r0, [sp, #12]
    strb r7, [r0]

    ldr  r0, [sp, #60]
    strh r8, [r0]

    ldr  r0, [sp, #16]
    sub  r0, r0, r11

    add  sp, sp, #24
    pop  {r4-r11, pc}

.size mos6502_run_hot_arm, .-mos6502_run_hot_arm

.syntax unified
.arm

.section .text, "ax", %progbits
.align 2

.extern g_nds_blit_palette

.global nds_blit_copy_opaque_arm
.type nds_blit_copy_opaque_arm, %function
nds_blit_copy_opaque_arm:
    cmp r3, #0
    bxeq lr

    push {r4-r6, lr}
    ldr r4, =g_nds_blit_palette
    ldr r4, [r4]

1:
    ldrb r5, [r0], #1
    strb r5, [r1], #1
    add r6, r4, r5, lsl #1
    ldrh r6, [r6]
    strh r6, [r2], #2
    subs r3, r3, #1
    bne 1b

    pop {r4-r6, pc}

.size nds_blit_copy_opaque_arm, .-nds_blit_copy_opaque_arm

.global nds_blit_copy_transparent_arm
.type nds_blit_copy_transparent_arm, %function
nds_blit_copy_transparent_arm:
    cmp r3, #0
    bxeq lr

    push {r4-r6, lr}
    ldr r4, =g_nds_blit_palette
    ldr r4, [r4]

2:
    ldrb r5, [r0], #1
    cmp r5, #0
    beq 3f
    strb r5, [r1]
    add r6, r4, r5, lsl #1
    ldrh r6, [r6]
    strh r6, [r2]
3:
    add r1, r1, #1
    add r2, r2, #2
    subs r3, r3, #1
    bne 2b

    pop {r4-r6, pc}

.size nds_blit_copy_transparent_arm, .-nds_blit_copy_transparent_arm

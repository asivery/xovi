.global untrampolineFunction
.global untrampolineStep2

untrampolineFunction:
    # According to the program, we're in the 'library function'.
    # All argument registers should be considered unsafe to work on. They need to be saved
    # Back up X0-X7
    # In X16 there is our symbol_data structure.
    # (X17 is the address of this exact function)

    # Backup frame pointer and link register:
    stp x29, x30, [sp, -16]!
    stp x0, x1, [sp, -16]!
    stp x2, x3, [sp, -16]!
    stp x4, x5, [sp, -16]!
    stp x6, x7, [sp, -16]!
    stp x8, x9, [sp, -16]!
    # Also backup the info we need to continue
    stp x16, x17, [sp, -16]!
    # All registers backed up! We can now call the function (re)constructor in C
    # It needs the symbol_data struct ptr
    mov x0, x16
    bl untrampolineInit
    # Now it's safe to say all are registers are destroyed
    # Reconstruct...
    ldp x16, x17, [sp]
    ldp x8, x9, [sp, #16]
    ldp x6, x7, [sp, #32]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #64]
    ldp x0, x1, [sp, #80]
    # Now it's safe to jump to the function x17
    ldr x17, [x16]
    blr x17
    # Store return value
    stp x0, x1, [sp, #80]
    stp x8, x9, [sp, #16]
    # This is a call - this function will return.
    # Restore the registers we need to make the function a trampoline again
    ldp x16, x17, [sp]
    mov x0, x16
    bl untrampolineFini
    # We can now restore everything except x0
    ldp x16, x17, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    ldp x29, x30, [sp], #16
    ret
# End

untrampolineStep2:
    # After we enter the function, we need to get rid of the original code
    # as soon as possible.
    # Right now we have just barely started executing the function (4 instrs done)
    # Replace the beginning of the function to the original trampoline.
    # Then replace the second trampoline to original code. Do so WITHOUT USING ANY
    # REGISTERS. NONE OF THEM ARE SAFE AT THIS POINT.
    stp x0, x1, [sp, -16]!
    mrs x0, nzcv
    stp x0, x2, [sp, -16]!
    stp x16, x17, [sp, -16]!
    # I will need at least the 4 registers to implement a copy loop.
    # x0 - destination,
    # x1 - source
    ldr x0, [x16]
    ldr x1, [x16, #24]
    # Copy S1 Trampoline --> Start of function
    mov x2, #5
    .l1:
    ldr w17, [x1], #4
    str w17, [x0], #4
    subs x2, x2, #1
    bne .l1
    # After 5 copies, we're restored
    # Now copy original code (f) into the current address
    ldr x1, [x16, #16]
    add x1, x1, #20
    mov x2, #9
    .l2:
    ldr w17, [x1], #4
    str w17, [x0], #4
    subs x2, x2, #1
    bne .l2
    # Copied. Prepare jump to address[5] (20) - wipe the CPU cache
    ldp x16, x17, [sp], #16
    ldr x17, [x16]
    mov x0, #8
    isb
    dsb sy

    bic x17, x17, #0x3F
    .l3:
    dc cvau, x17
    dsb ish
    ic ivau, x17
    dsb ish
    isb sy
    add x17, x17, #64
    isb
    dsb sy
    subs x0, x0, #1
    bne .l3
    ldp x0, x2, [sp], #16
    msr nzcv, x0
    ldp x0, x1, [sp], #16
    ldr x17, [x16]
    # Done. Return to the function.
    add x17, x17, #20
    br x17

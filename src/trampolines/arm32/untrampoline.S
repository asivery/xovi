.global untrampolineFunction
.type untrampolineFunction, %function
.global untrampolineStackShift
.type untrampolineStackShift, %function
.global untrampolineStep2
.type untrampolineStep2, %function

.global untrampolineInit
.type untrampolineInit, %function
.global untrampolineFini
.type untrampolineFini, %function

untrampolineFunction:
    # According to the program, we're in the 'library function'.
    # All argument registers should be considered unsafe to work on. They need to be saved
    # Back up r0-r3
    # In r12 there is our symbol_data structure.

    # Backup frame pointer and link register:
    stmfd sp!, {r11, lr}
    # Backup arguments and the info we need to continue
    stmfd sp!, {r0-r3, r12}
    # All registers backed up! We can now call the function (re)constructor in C
    # It needs the symbol_data struct ptr
    mov r0, r12
    bl untrampolineInit
    # Now it's safe to say all are registers are destroyed
    # Reconstruct...
    ldmfd sp!, {r0-r3, r12}
    stmfd sp!, {r12}
    # We will not need the arguments after the function will have returned.
    # Now it's safe to jump to the function
    ldr r12, [r12]
    blx r12
    # Store all potential return values and retrieve the pointer to SymbolData
    ldmfd sp!, { r12 }
    stmfd sp!, {r0-r3}
    # This is a call - this function will return.
    # Restore the registers we need to make the function a trampoline again
    mov r0, r12
    bl untrampolineFini
    # We can now restore everything
    ldmfd sp!, {r0-r3}
    ldmfd sp!, {r11, lr}
    bx lr
# End

# This is an alternative implementation of untrampolineFunction that preserves N words of function arguments on the stack
.equ stackShiftN, 8

untrampolineStackShift:
    # Backup argument registers to move the stack
    stmfd sp!, {r0-r3}
    # r12 holds pointer to our symbolData structure
    
    # We are going to shift part of the stack upwards to make room for the pointer to the symbolData structure and a copy of LR
    # r2 holds current address of the two-word "hole" in stack (currently beyond SP)
    sub r2, sp, #8
    # r1 holds the target address of the "hole" in stack (currently within the stack)
    add r1, sp, #(4 + stackShiftN) * 4
    
    .beginShiftLoop:
    cmp r1, r2
    beq endShiftLoop
    
    ldr r3, [r2, #8]
    str r3, [r2]
    add r2, r2, #4
    b beginShiftLoop
    .endShiftLoop:
    
    # now r1 and r2 hold the address of the "hole" in stack
    # store r12 and LR
    stmea r1, {r12, lr}
    # adjust the SP
    sub sp, sp, #8
    
    # Back up one more register and frame pointer
    stmfd sp!, {r4, r11}
    
    # All registers backed up! We can now call the function (re)constructor in C
    # It needs the symbol_data struct ptr
    mov r0, r12
    # We also need it later for retrieving the function address, so store it in callee-preserved register r4
    mov r4, r12
    bl untrampolineInit
    
    # Load function address to r12
    ldr r12, [r4]
    
    # Reconstruct registers
    ldmfd sp!, {r4, r11}
    ldmfd sp!, {r0-r3}
    
    # Now it's safe to jump to the function
    blx r12
    
    # Now the situation is as follow:
    # r0-r3 contain the function's return value
    # r4-r11 are back to what they were before the function was called (callee-preserved)
    # r12 is undefined
    # lr is the address of the next instruction
    # The only useful guarantee we have at this point is that the SP has been preserved
    
    # Retrieve symbolData structure from fixed offset within the stack
    ldr r12, [sp, #stackShiftN * 4]
    
    # Back up argument/return registers
    stmfd sp!, {r0-r3}
    
    # Make the function a trampoline again
    mov r0, r12
    bl untrampolineFini
    
    # Restore LR from from fixed offset within the stack
    ldr lr, [sp, #(stackShiftN + 1 + 4) * 4]
    
    # We do not need the LR and symbolDate structure anymore, so we can fill the hole again
    # r0 - r3 are still backed up and available for implementation of the unshift loop
    
    # r1 holds address of hole
    sub r1, sp, #(4 + stackShiftN) * 4
    
    # shift hole until it has reached SP
    .beginUnshiftLoop:
    cmp r1, sp
    beq .endUnshiftLoop
    
    ldr r2, [r1, #-8]
    str r2, [r1, #8]
    sub r1, r1, #4
    b .beginUnshiftLoop
    .endUnshiftLoop:
    
    # now the SP points to a hole, so we have to adjust it to be full descending again
    add sp, sp, #8
    
    # Restore argument/return registers and return
    ldmfd sp!, {r0-r3}
    bx lr

untrampolineStep2:
    # After we enter the function, we need to get rid of the original code
    # as soon as possible.
    # Right now we have just barely started executing the function (4 instrs done)
    # Replace the beginning of the function to the original trampoline.
    # Then replace the second trampoline to original code. Do so WITHOUT USING ANY
    # REGISTERS. NONE OF THEM ARE SAFE AT THIS POINT.
    stmfd sp!, {r0-r3}
    mrs r0, cpsr
    stmfd sp!, {r0, r12}
    # I will need at least the 4 registers to implement a copy loop.
    # r0 - destination,
    # r1 - source
    ldr r0, [r12]
    ldr r1, [r12, #12]
    # Copy S1 Trampoline --> Start of function
    mov r2, #2
    .l1:
    ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #1
    bne .l1
    # After 2 copies, we're restored
    # Now copy original code (f) into the current address
    ldr r1, [r12, #8]
    add r1, r1, #8
    mov r2, #4
    .l2:
    ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #1
    bne .l2
    # Copied. Prepare jump to address[3] (12) - wipe the CPU cache
    # Since r12 has not been altered, we do not need to restore them from the stack.
    # r12 - start of struct SymbolData*.

    ldmfd sp, {r0, r12}
    ldr r0, [r12]
    add r1, r0, #28
    # Cheat cache cleaning by invoking the gcc primitives
    bl __clear_cache

    ldmfd sp!, {r0, r12}
    msr cpsr, r0
    ldmfd sp!, {r0-r3}
    
    ldr r12, [r12]
    # Done. Return to the function.
    add r12, r12, #8
    bx r12

.section .note.GNU-stack

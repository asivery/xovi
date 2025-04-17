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

# This is an alternative implementation of untrampolineFunction that preserves symbolData->argsize words of function arguments on the stack
.equ argSizeOffset, 24

untrampolineStackShift:
    # Backup argument registers, frame pointer and one more register
    stmfd sp!, {r0-r3, r4, r11}
    # r12 holds pointer to our symbolData structure
    
    # We are going to shift part of the stack upwards to make room for the pointer to the symbolData structure and a copy of LR
    # r2 holds current address of the two-word "hole" in stack (currently beyond SP)
    sub r2, sp, #8
    
    # retrieve the size of function arguments in words
    ldr r4, [r12, #argSizeOffset]
    # reduce by the amount of arguments that go into r0-r3
    subs r4, r4, #4
    movlo r4, #0
    # round up to the nearest even number of words
    # TODO
    # r1 holds the target address of the "hole" in stack (currently within the stack)
    add r1, sp, r4, lsl #2
    # adjust by the number of registers we have pushed onto the stack earlier (6 words) and the size of the hole (2 words)
    add r1, r1, #(4* (6 - 2))
    
    # shifting loop
1:  cmp r2, r1
    bge 2f
    
    ldr r3, [r2, #8]
    str r3, [r2]
    add r2, r2, #4
    b 1b
2:
    
    # now r1 and r2 hold the address of the "hole" in stack
    # store r12 and LR
    stmea r1, {r12, lr}
    # adjust the SP
    sub sp, sp, #8
    
    # All registers backed up! We can now call the function (re)constructor in C
    # It needs the symbol_data struct ptr
    mov r0, r12
    # We also need it later for retrieving the function address, so store it in callee-preserved register r11
    mov r11, r12
    bl untrampolineInit
    
    # Load function address to r12
    ldr r12, [r11]
    
    # Set up return address depending on the amount of stack arguments
    adr lr, .LreturnBase
    # Every other instruction = word contains another jump target, since we rounded to the nearest double-word we can just add
    add lr, lr, r4, lsl #2
    
    # Reconstruct registers
    ldmfd sp!, {r0-r3, r4, r11}
    
    # Now it's safe to jump to the function
    bx r12
    
    # Now the situation is as follow:
    # r0-r3 contain the function's return value
    # r4-r11 are back to what they were before the function was called (callee-preserved)
    # r12 is undefined
    # SP has been preserved
    # LR contains the return address we calculated earlier and execution will resume there
    
    .LreturnBase:
    
    # store the address of the "hole" in r12 depending on the return address
    add r12, sp, #0
    b .LreturnFinal
    add r12, sp, #(2 * 4)
    b .LreturnFinal
    add r12, sp, #(4 * 4)
    b .LreturnFinal
    add r12, sp, #(6 * 4)
    b .LreturnFinal
    add r12, sp, #(8 * 4)
    b .LreturnFinal
    add r12, sp, #(10 * 4)
    b .LreturnFinal
    add r12, sp, #(12 * 4)
    .LreturnFinal:
    
    # now r12 contains the address of where we stored symbolData* and LR
    
    # Back up argument/return registers plus working registers
    stmfd sp!, {r0-r3, r4, r5}
    
    # Back up address of "hole" to a callee-saved register
    mov r4, r12
    
    # Retrieve symbolData address from saved address within the stack
    ldr r0, [r12]
    # Make the function a trampoline again
    bl untrampolineFini
    
    # Restore LR from from saved address within the stack
    ldr lr, [r4, #4]
    
    # We do not need the LR and symbolDate address anymore, so we can fill the hole again
    # r0 - r3 are still backed up and available for implementation of the unshift loop
    # r4 holds address of hole
    
    # shift hole until it has reached SP
1:  cmp r4, sp
    ble 2f
    
    ldr r2, [r4, #-4]
    str r2, [r4, #4]
    sub r4, r4, #4
    b 1b
2:
    
    # now the SP points to a hole, so we have to adjust it to be full descending again
    add sp, sp, #8
    
    # Restore argument/return registers and return
    ldmfd sp!, {r0-r3, r4, r5}
    bx lr

.equ trampoline2OffsetOffset, 28
.equ closuresOffset, 32

untrampolineStep2:
    # After we enter the function, we need to get rid of the original code
    # as soon as possible.
    # Right now we have just barely started executing the function (4 instrs done)
    # Replace the beginning of the function to the original trampoline.
    # Then replace the second trampoline to original code. Do so WITHOUT USING ANY
    # REGISTERS. NONE OF THEM ARE SAFE AT THIS POINT.
    stmfd sp!, {r0-r3, r4, lr}
    mrs r0, cpsr
    stmfd sp!, {r0, r12}
    # I will need at least the 4 registers to implement a copy loop.
    # r0 - destination,
    # r1 - source
    ldr r0, [r12]
    ldr r1, [r12, #12]
    # Zero out lsb of destination in case it is a Thumb function
    bic r0, r0, #1
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
    mov r2, #3
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

    ldr r12, [r12, #closuresOffset]

    ldmfd sp!, {r0-r3, r4, lr}
    # Return to the closure and restore everything.
    bx r12

.section .note.GNU-stack

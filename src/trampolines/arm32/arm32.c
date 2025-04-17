#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../trampolines.h"
#include "arm32.h"
#include "../../debug.h"

// find the beginning of the next instruction after `target` in the stream of mixed 16bit/32bit Thumb instructions beginning at `begin`
thumb_instr_t *nextThumbInstruction(thumb_instr_t *begin, thumb_instr_t *target) {
    thumb_instr_t *current = begin-1;
    while(current <= target) {
        int det = (*current) >> 11;
        if(det >= 0x1D) {
            current += 2;
        } else {
            current += 1;
        }
    }

    return current;
}

void initCall(struct SymbolData *data) {
    void *address = (void *)((ptrint_t)data->address & (~0x1));
    memcpy(address, data->beginningOfOriginalFunction, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(address + ARCHDEP_TRAMPOLINE_LENGTH + data->trampoline2Offset,
           data->step2Trampoline + data->trampoline2Offset,
           ARCHDEP_UNTRAMPOLINE_LENGTH - data->trampoline2Offset);
    __builtin___clear_cache(address, address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}

void finiCall(struct SymbolData *data) {
    void *address = (void *)((ptrint_t)data->address & (~0x1));
    memcpy(address, data->firstTrampoline, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(address + ARCHDEP_TRAMPOLINE_LENGTH, data->beginningOfOriginalFunction + ARCHDEP_TRAMPOLINE_LENGTH, ARCHDEP_UNTRAMPOLINE_LENGTH);
    __builtin___clear_cache(address, address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}
extern void untrampolineStep2(void);

struct SymbolData *pivotSymbol(const char *symbol, void *newaddr, int argSize) {
    static int pagesize = 0;
    if(pagesize == 0) pagesize = getpagesize();
    void *symboladdr = dlsym(RTLD_DEFAULT, symbol);
    if(symboladdr == NULL) {
        symboladdr = dlsym(RTLD_NEXT, symbol);
    }
    if(symboladdr == NULL) {
        printf("!! CANNOT FIND %s !!\n", symbol);
        return NULL;
    }

    int is_thumb_func = (ptrint_t)symboladdr & 1;

    struct SymbolData *s = malloc(sizeof(struct SymbolData));

    instr_t trampoline[2];

    if(!is_thumb_func) {
        memcpy(trampoline, (instr_t[]){
            0xe51ff004,  // ldr pc, [pc, #-4]
	    (instr_t) newaddr
        }, 2 * sizeof(instr_t));
    } else {
        memcpy(trampoline, (instr_t[]){
            0xF000F8DF, // ldr pc, [ pc ]
            (instr_t)newaddr
        }, 2 * sizeof(instr_t));
    }

    instr_t s2trampoline[5];

    if(!is_thumb_func) {
        memcpy(s2trampoline, (instr_t[]){
            0xe59fc000, // ldr r12, [ pc ]
            0xe59ff000, // ldr pc, [ pc ]
            (instr_t) s,
            (instr_t) untrampolineStep2, // address loaded by previous instruction, never executed
        }, 4 * sizeof(instr_t));
    } else {
        memcpy(s2trampoline, (instr_t[]){
            0xBF00BF00, // NOP; NOP; # used to adjust the trampoline beginning to the instruction boundaries in a mixed 16bit/32bit stream of Thumb-2 instructions
            0xC004F8DF, // LDR R12, [PC, #4]
            0xF004F8DF, // LDR PC,  [PC, #4]
            (instr_t)s,
            (instr_t)untrampolineStep2 // addresses loaded by previous instructions, never executed
        }, 5 * sizeof(instr_t));
    }

    // During the restore-call, there will be 2 trampolines at the start of the function.
    uint8_t *funcstart = malloc(ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);
    // Place the beginning of the function into the allocated region
    memcpy(funcstart, (void *)((ptrint_t)symboladdr & (~0x1)), ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);

    s->address = symboladdr;
    s->beginningOfOriginalFunction = funcstart;
    s->page_address = (void*) (((unsigned int) symboladdr) & ~(pagesize - 1));
    s->size = ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH;
    s->firstTrampoline = malloc(ARCHDEP_TRAMPOLINE_LENGTH);
    s->step2Trampoline = malloc(ARCHDEP_UNTRAMPOLINE_LENGTH);

    // untrampolineStackShift supports only even values of argsize, therefore we need to round up odd numbers
    s->argsize = (argSize + 1) & (~1);
    s->trampoline2Offset = 0;
    if(is_thumb_func) {
        void *step_2_address = symboladdr - 1 + ARCHDEP_TRAMPOLINE_LENGTH;
        s->trampoline2Offset = (void*)nextThumbInstruction(symboladdr - 1, step_2_address - 2) - step_2_address;
    }

    pthread_mutex_init (&s->mutex, NULL);
    mprotect(s->page_address, pagesize, PROT_READ | PROT_EXEC | PROT_WRITE);
    memcpy(s->firstTrampoline, trampoline, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(s->step2Trampoline, s2trampoline, ARCHDEP_UNTRAMPOLINE_LENGTH);
    finiCall(s);
    return s;
}
int untrampolineInit(struct SymbolData *symbol) {
    pthread_mutex_lock(&symbol->mutex);
    initCall(symbol);
}
int untrampolineFini(struct SymbolData *symbol) {
    finiCall(symbol);
    pthread_mutex_unlock(&symbol->mutex);
}
extern void untrampolineFunction(void);
extern void untrampolineStackShift(void);
void generateUntrampoline(void *function, struct SymbolData *symbol, int bytesRemaining) {

    void *untrampoline = untrampolineFunction;
    // argsize uses words.
    if(symbol->argsize > 16) {
        LOG_F("[F]: Fatal error - cannot hook function with argument size above 64 bytes (16 words)!\n");
        exit(1);
    }
    else if(symbol->argsize > 4) {
        untrampoline = untrampolineStackShift;
    }

    instr_t trampoline[] = {
        0xe59fc000, // ldr r12, [ pc ] - this will load pc (this address) + 8 (default ARM behavior.
        0xe59ff000, // ldr pc, [ pc ] - load the untrampolineFunction's address into PC (switch instr.set if needed)
        (instr_t) symbol,
        (instr_t) untrampoline // address loaded by previous instruction, never executed
    };

    if(sizeof(trampoline) > bytesRemaining) {
        LOG_F("[F]: Fatal error - too little space to generate a trampoline!\n");
        exit(1);
    }
    memcpy(function, trampoline, sizeof(trampoline));
}

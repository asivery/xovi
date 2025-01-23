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

void initCall(struct SymbolData *data) {
    memcpy(data->address, data->beginning_org, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(data->address + ARCHDEP_TRAMPOLINE_LENGTH, data->step_2_trampoline, ARCHDEP_UNTRAMPOLINE_LENGTH);
    __builtin___clear_cache(data->address, data->address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}

void finiCall(struct SymbolData *data) {
    memcpy(data->address, data->beginning_trampoline, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(data->address + ARCHDEP_TRAMPOLINE_LENGTH, data->beginning_org + ARCHDEP_TRAMPOLINE_LENGTH, ARCHDEP_UNTRAMPOLINE_LENGTH);
    __builtin___clear_cache(data->address, data->address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}
extern void untrampolineStep2(void);

struct SymbolData *pivotSymbol(const char *symbol, void *newaddr) {
    static int pagesize = 0;
    if(pagesize == 0) pagesize = getpagesize();
    void *symboladdr = dlsym(RTLD_NEXT, symbol);
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
            0xf8dff000, // ldr pc, [ pc ]
            (instr_t)newaddr
        }, 2 * sizeof(instr_t));
    }

    instr_t s2trampoline[4];

    if(!is_thumb_func) {
        memcpy(s2trampoline, (instr_t[]){
            0xe59fc000, // ldr r12, [ pc ]
            0xe59ff000, // ldr pc, [ pc ]
            (instr_t) s,
            (instr_t) untrampolineStep2, // address loaded by previous instruction, never executed
        }, 4 * sizeof(instr_t));
    } else {
        memcpy(s2trampoline, (instr_t[]){
            0xf8dfc00c, // LDR R12, [PC, #12]
            0xf8dff00c, // LDR PC,  [PC, #12]
            (instr_t)untrampolineStep2,
            (instr_t)s // addresses loaded by previous instructions, never executed
        }, 4 * sizeof(instr_t));
    }

    // During the restore-call, there will be 2 trampolines at the start of the function.
    uint8_t *funcstart = malloc(ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);
    // Place the beginning of the function into the allocated region
    memcpy(funcstart, symboladdr, ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);

    s->address = symboladdr;
    s->beginning_org = funcstart;
    s->page_address = (void*) (((unsigned int) symboladdr) & ~(pagesize - 1));
    s->size = ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH;
    s->beginning_trampoline = malloc(ARCHDEP_TRAMPOLINE_LENGTH);
    s->step_2_trampoline = malloc(ARCHDEP_UNTRAMPOLINE_LENGTH);
    // FIXME sets the argument size to a hardcoded value of 4 words, resulting in fast codepath
    s->argsize = 4;
    pthread_mutex_init (&s->mutex, NULL);
    mprotect(s->page_address, pagesize, PROT_READ | PROT_EXEC | PROT_WRITE);
    memcpy(s->beginning_trampoline, trampoline, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(s->step_2_trampoline, s2trampoline, ARCHDEP_UNTRAMPOLINE_LENGTH);
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
    if(symbol->argsize > 4) {
        untrampoline = untrampolineStackShift;
    }
    else if(symbol->argsize > 16) {
        printf("[F]: Fatal error - cannot hook function with argument size above 64 byte!\n");
        exit(1);
    }

    instr_t trampoline[] = {
        0xe59fc000, // ldr r12, [ pc ] - this will load pc (this address) + 8 (default ARM behavior.
        0xe59ff000, // ldr pc, [ pc ] - load the untrampolineFunction's address into PC (switch instr.set if needed)
        (instr_t) symbol,
        (instr_t) untrampoline // address loaded by previous instruction, never executed
    };

    if(sizeof(trampoline) > bytesRemaining) {
        printf("[F]: Fatal error - too little space to generate a trampoline!\n");
        exit(1);
    }
    memcpy(function, trampoline, sizeof(trampoline));
}

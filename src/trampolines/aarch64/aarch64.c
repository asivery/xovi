#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include "../trampolines.h"
#include "aarch64.h"

void initCall(struct SymbolData *data) {
    memcpy(data->address, data->beginningOfOriginalFunction, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(data->address + ARCHDEP_TRAMPOLINE_LENGTH, data->step2Trampoline, ARCHDEP_UNTRAMPOLINE_LENGTH);
    __builtin___clear_cache(data->address, data->address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}

void finiCall(struct SymbolData *data) {
    memcpy(data->address, data->firstTrampoline, ARCHDEP_TRAMPOLINE_LENGTH);
    memcpy(data->address + ARCHDEP_TRAMPOLINE_LENGTH, data->beginningOfOriginalFunction + ARCHDEP_TRAMPOLINE_LENGTH, ARCHDEP_UNTRAMPOLINE_LENGTH);
    __builtin___clear_cache(data->address, data->address + ARCHDEP_UNTRAMPOLINE_LENGTH + ARCHDEP_TRAMPOLINE_LENGTH);
}
extern void untrampolineStep2(void);

struct SymbolData *pivotSymbol(const char *symbol, void *newaddr, int argSize) {
    static int pagesize = 0;
    if(argSize != -1) {
        printf("!! TODO: AARCH64 is lacking support for more than 8 arguments / nonstandard cases handled by stack !!\n");
        return NULL;
    }
    if(pagesize == 0) pagesize = getpagesize();
    void *symboladdr = dlsym(RTLD_DEFAULT, symbol);
    if(symboladdr == NULL) {
        symboladdr = dlsym(RTLD_NEXT, symbol);
    }
    if(symboladdr == NULL) {
        printf("!! CANNOT FIND %s !!\n", symbol);
        return NULL;
    }

    struct SymbolData *s = malloc(sizeof(struct SymbolData));

    uint32_t addr4 = (((unsigned long long int) newaddr) >> 48) & 0xFFFF;
    uint32_t addr3 = (((unsigned long long int) newaddr) >> 32) & 0xFFFF;
    uint32_t addr2 = (((unsigned long long int) newaddr) >> 16) & 0xFFFF;
    uint32_t addr1 = (((unsigned long long int) newaddr) >> 0) & 0xFFFF;

    instr_t trampoline[] = {
        0xD2800010 | (addr1 << 5), // First mov x16
        0xF2A00010 | (addr2 << 5), // Second mov x16
        0xF2C00010 | (addr3 << 5), // Third mov x16
        0xF2E00010 | (addr4 << 5), // Forth mov x16
        0xd61f0200 // BR x16
    };
    addr4 = (((unsigned long long int) untrampolineStep2) >> 48) & 0xFFFF;
    addr3 = (((unsigned long long int) untrampolineStep2) >> 32) & 0xFFFF;
    addr2 = (((unsigned long long int) untrampolineStep2) >> 16) & 0xFFFF;
    addr1 = (((unsigned long long int) untrampolineStep2) >> 0) & 0xFFFF;

    uint32_t addr4f = (((unsigned long long int) s) >> 48) & 0xFFFF;
    uint32_t addr3f = (((unsigned long long int) s) >> 32) & 0xFFFF;
    uint32_t addr2f = (((unsigned long long int) s) >> 16) & 0xFFFF;
    uint32_t addr1f = (((unsigned long long int) s) >> 0) & 0xFFFF;

    instr_t s2trampoline[] = {
        0xD2800010 | (addr1f << 5), // First mov x16
        0xF2A00010 | (addr2f << 5), // Second mov x16
        0xF2C00010 | (addr3f << 5), // Third mov x16
        0xF2E00010 | (addr4f << 5), // Forth mov x16

        0xD2800011 | (addr1 << 5), // First mov x17
        0xF2A00011 | (addr2 << 5), // Second mov x17
        0xF2C00011 | (addr3 << 5), // Third mov x17
        0xF2E00011 | (addr4 << 5), // Forth mov x17
        0xd61f0220 // BR x17
    };

    // During the restore-call, there will be 2 trampolines at the start of the function.
    uint8_t *funcstart = malloc(ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);
    // Place the beginning of the function into the allocated region
    memcpy(funcstart, symboladdr, ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH);

    // Setup the structure's values
    s->address = symboladdr;
    s->beginningOfOriginalFunction = funcstart;
    s->page_address = (void*) (((unsigned long long int) symboladdr) & ~(pagesize - 1));
    s->size = ARCHDEP_TRAMPOLINE_LENGTH + ARCHDEP_UNTRAMPOLINE_LENGTH;
    s->firstTrampoline = malloc(ARCHDEP_TRAMPOLINE_LENGTH);
    s->step2Trampoline = malloc(ARCHDEP_UNTRAMPOLINE_LENGTH);

    // Write the correct data to the addresses in the structure object.
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
void generateUntrampoline(void *function, struct SymbolData *symbol, int bytesRemaining) {
    uint32_t addr4 = (((unsigned long long int) symbol) >> 48) & 0xFFFF;
    uint32_t addr3 = (((unsigned long long int) symbol) >> 32) & 0xFFFF;
    uint32_t addr2 = (((unsigned long long int) symbol) >> 16) & 0xFFFF;
    uint32_t addr1 = (((unsigned long long int) symbol) >> 0) & 0xFFFF;
    uint32_t addr4f = (((unsigned long long int) untrampolineFunction) >> 48) & 0xFFFF;
    uint32_t addr3f = (((unsigned long long int) untrampolineFunction) >> 32) & 0xFFFF;
    uint32_t addr2f = (((unsigned long long int) untrampolineFunction) >> 16) & 0xFFFF;
    uint32_t addr1f = (((unsigned long long int) untrampolineFunction) >> 0) & 0xFFFF;

    instr_t trampoline[] = {
        0xD2800010 | (addr1 << 5), // First mov x16
        0xF2A00010 | (addr2 << 5), // Second mov x16
        0xF2C00010 | (addr3 << 5), // Third mov x16
        0xF2E00010 | (addr4 << 5), // Forth mov x16
        0xD2800011 | (addr1f << 5), // First mov x17
        0xF2A00011 | (addr2f << 5), // Second mov x17
        0xF2C00011 | (addr3f << 5), // Third mov x17
        0xF2E00011 | (addr4f << 5), // Forth mov x17

        0xd61f0220 // BR x17
    };

    if(sizeof(trampoline) > bytesRemaining) {
        printf("[F]: Fatal error - too little space to generate a trampoline!\n");
        exit(1);
    }
    memcpy(function, trampoline, sizeof(trampoline));
}

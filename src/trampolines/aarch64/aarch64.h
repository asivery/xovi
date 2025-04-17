#pragma once
#define ARCHDEP_UNTRAMPOLINE_LENGTH (9 * 4)
#define ARCHDEP_S2TRAMPOLINE_LENGTH (9 * 4)
#define ARCHDEP_TRAMPOLINE_LENGTH (5 * 4)
typedef unsigned int instr_t;
typedef unsigned long long int ptrint_t;

struct SymbolData {
    void *address;
    void *page_address;

    void *beginningOfOriginalFunction;
    void *firstTrampoline;
    void *step2Trampoline;
    int size;

    int argsize;
    pthread_mutex_t mutex;
};

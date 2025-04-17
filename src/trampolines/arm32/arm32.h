#pragma once
#define ARCHDEP_UNTRAMPOLINE_LENGTH (5 * 4)
#define ARCHDEP_TRAMPOLINE_LENGTH (2 * 4)
typedef unsigned int instr_t;
typedef unsigned int ptrint_t;
typedef short unsigned int thumb_instr_t;

struct SymbolData {
    void *address;
    void *page_address;

    void *beginningOfOriginalFunction;
    void *firstTrampoline;
    void *step2Trampoline;
    int size;

    int argsize;

    int trampoline2Offset;

    pthread_mutex_t mutex;
};

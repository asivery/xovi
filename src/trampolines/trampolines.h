// ALL FUNCTIONS DEFINED IN THIS FILE ARE ARCHITECTURE-DEPENDENT!!
#pragma once
#include <pthread.h>

struct SymbolData {
    void *address;
    void *page_address;

    void *beginning_org;
    void *beginning_trampoline;
    void *step_2_trampoline;

    int size;

    int argsize;

    pthread_mutex_t mutex;
};

// Returns the size of the function (in bytes)
// Takes in:
// - Function buffer
// - Address of the symbol_data struct
// - Amount of bytes remaining in buffer.
void generateUntrampoline(void *function, struct SymbolData *symbol, int bytesRemaining);
struct SymbolData *pivotSymbol(const char *symbol, void *newaddr);

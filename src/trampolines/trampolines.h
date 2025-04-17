// ALL FUNCTIONS DEFINED IN THIS FILE ARE ARCHITECTURE-DEPENDENT!!
#pragma once
#include <pthread.h>
#include "archdepend.h"

struct SymbolData;

// Returns the size of the function (in bytes)
// Takes in:
// - Function buffer
// - Address of the symbol_data struct
// - Amount of bytes remaining in buffer.
void generateUntrampoline(void *function, struct SymbolData *symbol, int bytesRemaining);
struct SymbolData *pivotSymbol(const char *symbol, void *newaddr, int argSize);

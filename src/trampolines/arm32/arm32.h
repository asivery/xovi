#pragma once
#define ARCHDEP_UNTRAMPOLINE_LENGTH (5 * 4)
#define ARCHDEP_TRAMPOLINE_LENGTH (2 * 4)
#define ARCHDEP_SYMBOLDATA_FIELDS int trampoline_2_offset;
typedef unsigned int instr_t;
typedef unsigned int ptrint_t;
typedef short unsigned int thumb_instr_t;


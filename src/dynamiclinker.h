#pragma once
#include "uthash.h"
#include "hash.h"
#include "system.h"
#include "trampolines/archdepend.h"
#include "trampolines/trampolines.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <sys/mman.h>
#include "debug.h"
#include <string.h>

#define LP1_F_TYPE_EXPORT 1
#define LP1_F_TYPE_IMPORT 2
#define LP1_F_TYPE_OVERRIDE 3
#define LP1_F_TYPE_CONDITION 4

struct LinkingPass1SOFunction {
    hash_t functionNameHash;
    char *functionName;
    int type;
    void *address;

    UT_hash_handle hh;
};

struct SemVer {
    unsigned char major, minor, patch;
};

struct LinkingPass1Result {
    hash_t soFileNameRootHash;
    char loaded;
    char *baseName;
    struct LinkingPass1SOFunction **functions;
    void (*constructor)();
    struct XoViEnvironment **environment;
    unsigned char *untrampolineFunctionCache;
    int populatedUntrampolineFunctions;
    int importsCount;
    void *handle;

    struct SemVer version;

    UT_hash_handle hh;
};

struct OverrideFunctionTrace {
    hash_t overridenFunctionNameHash;
    char *ownerExtensionBaseName;
    struct SymbolData *data;

    UT_hash_handle hh;
};

void loadExtensionPass1(char *extensionSOFile, char *baseName);
void loadAllExtensions(struct XoViEnvironment *env);

#pragma once
#include "uthash.h"
#include "hash.h"
#include "external.h"
#include "trampolines/archdepend.h"
#include "trampolines/trampolines.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <sys/mman.h>
#include "debug.h"
#include <string.h>

#define HASH_ADD_HT(head, keyfield_name, item_ptr) HASH_ADD(hh, head, keyfield_name, sizeof(hash_t), item_ptr)
#define HASH_FIND_HT(head, hash_ptr, out) HASH_FIND(hh, head, hash_ptr, sizeof(hash_t), out)

struct LinkingPass1SOFunction {
    hash_t functionNameHash;
    char *functionName;
    int type;
    void *address;
    struct XoviMetadataEntry **metadataChain;
    int metadataLength;

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
    void (*dependencyConstructor)();
    struct XoViEnvironment **environment;
    unsigned char *untrampolineFunctionCache;
    int populatedUntrampolineFunctions;
    int importsCount;
    void *handle;

    struct SemVer version;

    struct XoviMetadataEntry ***metadataChainRoot;
    int rootMetadataChainLength;
    int metadataChainLength;

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

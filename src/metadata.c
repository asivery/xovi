#include "metadata.h"
#include "dynamiclinker.h"
#include "uthash.h"

extern struct LinkingPass1Result *XOVI_DL_EXTENSIONS;

int getExtensionCount() {
    return HASH_COUNT(XOVI_DL_EXTENSIONS);
}

int getExtensionNames(const char **names, int maxCount) {
    struct LinkingPass1Result *ext;
    int i = 0;
    for(ext = XOVI_DL_EXTENSIONS; ext != NULL && i < maxCount; ext = ext->hh.next, i++) {
        names[i] = ext->baseName;
    }
    return i;
}

int getExtensionFunctionCount(const char *key) {
    hash_t hash = hashString((char *) key);
    struct LinkingPass1Result *ext;
    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &hash, ext);
    if(ext != NULL) {
        return HASH_COUNT(*ext->functions);
    }
    return -1;
}

int getExtensionFunctionNames(const char *key, const char **names, int maxCount) {
    hash_t hash = hashString(key);
    struct LinkingPass1Result *ext;
    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &hash, ext);
    if(ext != NULL) {
        struct LinkingPass1SOFunction *fnc;
        int i = 0;
        for(fnc = *ext->functions; fnc != NULL && i < maxCount; fnc = fnc->hh.next, i++) {
            names[i] = ext->baseName;
        }
        return i;
    }
    return -1;
}

static hash_t getRootHashForFunctionType(int functionType){
    switch(functionType){
        case LP1_F_TYPE_CONDITION: return hashString("C");
        case LP1_F_TYPE_EXPORT: return hashString("E");
        case LP1_F_TYPE_IMPORT: return hashString("I");
        case LP1_F_TYPE_OVERRIDE: return hashString("O");
    }
    return 0;
}

int getMetadataEntriesCountForFunction(const char *extension, const char *function, int functionType) {
    hash_t hash = hashString(extension);
    struct LinkingPass1Result *ext;
    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &hash, ext);
    if(ext != NULL) {
        struct LinkingPass1SOFunction *fnc;
        hash = hashStringS(function, getRootHashForFunctionType(functionType));
        HASH_FIND_HT(*ext->functions, &hash, fnc);
        if(fnc != NULL) {
            return fnc->metadataLength;
        }
    }
    return -1;
}

struct XoviMetadataEntry **getMetadataChainForFunction(const char *extension, const char *function, int functionType) {
    hash_t hash = hashString(extension);
    struct LinkingPass1Result *ext;
    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &hash, ext);
    if(ext != NULL) {
        struct LinkingPass1SOFunction *fnc;
        hash = hashStringS(function, getRootHashForFunctionType(functionType));
        HASH_FIND_HT(*ext->functions, &hash, fnc);
        if(fnc != NULL) {
            return fnc->metadataChain;
        }
    }
    return NULL;
}

struct XoviMetadataEntry *getMetadataEntryFromChain(struct XoviMetadataEntry **chain, const char *name){
    while(*chain) {
        if(strcmp((*chain)->name, name) == 0) {
            return *chain;
        }
        chain++;
    }
    return NULL;
}

struct XoviMetadataEntry *getMetadataEntryForFunction(const char *extension, const char *function, int functionType, const char *metadataEntryName) {
    struct XoviMetadataEntry **chain = getMetadataChainForFunction(extension, function, functionType);
    if(chain != NULL) {
        return getMetadataEntryFromChain(chain, metadataEntryName);
    }
    return NULL;
}

void createMetadataSearchingIterator(struct _ExtensionMetadataIterator *iterator, const char *metadataEntryName) {
    iterator->query = metadataEntryName;
    iterator->extensionRoot = XOVI_DL_EXTENSIONS;
    iterator->extensionName = XOVI_DL_EXTENSIONS->baseName;
    iterator->functionAddress = NULL;
    iterator->inExtensionFunctionRoot = *XOVI_DL_EXTENSIONS->functions;
    iterator->currentMetadataRoot = iterator->inExtensionFunctionRoot->metadataChain;
    iterator->functionName = NULL;
    iterator->terminated = false;
}

static bool goToNextRootIfNeeded(struct _ExtensionMetadataIterator *iterator) {
    if(iterator->terminated) return false;
    while(iterator->inExtensionFunctionRoot == NULL) {
        iterator->extensionRoot = iterator->extensionRoot->hh.next;
        if(iterator->extensionRoot == NULL) {
            iterator->terminated = true;
            return false;
        }
        iterator->inExtensionFunctionRoot = *iterator->extensionRoot->functions;
    }
    iterator->currentMetadataRoot = NULL;
    return true;
}

static struct XoviMetadataEntry *getFromCurrentChainThenAdvance(struct _ExtensionMetadataIterator *iterator) {
    if(iterator->currentMetadataRoot != NULL) {
        while(*iterator->currentMetadataRoot) {
            if(strcmp((*iterator->currentMetadataRoot)->name, iterator->query) == 0) {
                iterator->functionName = iterator->inExtensionFunctionRoot->functionName;
                iterator->extensionName = iterator->extensionRoot->baseName;
                iterator->functionAddress = iterator->inExtensionFunctionRoot->address;
                // Since there can only be one metadata attribute named in one way, we can safely
                // advance to the next function.
                iterator->inExtensionFunctionRoot = iterator->inExtensionFunctionRoot->hh.next;
                struct XoviMetadataEntry *ret = *iterator->currentMetadataRoot;
                iterator->currentMetadataRoot++;
                return ret;
            } else {
                iterator->currentMetadataRoot++;
            }
        }
        iterator->currentMetadataRoot = NULL;
    }
    return NULL;
}

struct XoviMetadataEntry *nextFunctionMetadataEntry(struct _ExtensionMetadataIterator *iterator) {
    // Check for any remaining metadata in the current chain
    struct XoviMetadataEntry *entry = getFromCurrentChainThenAdvance(iterator);
    if (entry != NULL) {
        return entry;
    }

    // Move to the next available function if needed
    while (goToNextRootIfNeeded(iterator)) {
        while (iterator->inExtensionFunctionRoot != NULL) {
            iterator->currentMetadataRoot = iterator->inExtensionFunctionRoot->metadataChain;
            entry = getFromCurrentChainThenAdvance(iterator);
            if (entry != NULL) {
                return entry;
            }
            iterator->inExtensionFunctionRoot = iterator->inExtensionFunctionRoot->hh.next;
        }
    }

    return NULL; // No more metadata entries found
}


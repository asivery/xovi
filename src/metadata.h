#pragma once
#include "dynamiclinker.h"

#define NEXT_EXTENSION 1
#define NEXT_ENTRY 2

struct _ExtensionMetadataIterator {
    const char *extensionName;
    const char *functionName;

    // Opaque starting from here....
    struct LinkingPass1Result *extensionRoot;
    struct LinkingPass1SOFunction *inExtensionFunctionRoot;
    struct XoviMetadataEntry **currentMetadataRoot;

    const char *query;
    bool terminated;
};

int getExtensionCount();
int getExtensionNames(const char **table, int maxCount);
int getExtensionFunctionCount(const char *key);
int getExtensionFunctionNames(const char *extension, const char **table, int maxCount);

int getMetadataEntriesCountForFunction(const char *extension, const char *function, int functionType);
struct XoviMetadataEntry **getMetadataChainForFunction(const char *extension, const char *function, int functionType);
struct XoviMetadataEntry *getMetadataEntryForFunction(const char *extension, const char *function, int functionType, const char *metadataEntryName);

void createMetadataSearchingIterator(struct _ExtensionMetadataIterator *iterator, const char *metadataEntryName);
struct XoviMetadataEntry *nextFunctionMetadataEntry(struct _ExtensionMetadataIterator *iterator);
void *getFunctionFromIterator(struct _ExtensionMetadataIterator *iterator);

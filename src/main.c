#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include "dynamiclinker.h"
#include "external.h"
#include "metadata.h"
#define XOVI_ROOT "/home/root/xovi/"
#define EXT_ROOT (XOVI_ROOT "extensions.d/")
#define FILES_ROOT (XOVI_ROOT "exthome/")

#ifdef DEBUGFUNC
#define CONSTRUCTOR

void _ext_init();

int testFunc() {
    printf("In playground\n");
}

int main(){
    _ext_init();
}
#else
#define CONSTRUCTOR __attribute__((constructor))
#endif

char *findBaseName(const char *fileName) {
    const char *extStart = strchr(fileName, '.');
    if(extStart == NULL) return strdup(fileName); // No '.'? The whole name is baseName
    char *allocd = malloc(extStart - fileName + 1);
    allocd[extStart - fileName] = 0; // NULL-term
    memcpy(allocd, fileName, extStart - fileName);
    return allocd;
}

char *mstrcat(const char *a, const char *b, int additionalSpace, char **end) {
    int aLength = strlen(a), bLength = strlen(b);
    char *c = calloc(1, aLength + bLength + 1);
    memcpy(c, a, aLength);
    memcpy(c + aLength, b, bLength);
    c[aLength + bLength] = 0;
    if(end) {
        *end = &c[aLength + bLength];
    }
    return c;
}

char *findFullName(const char *fileName) {
    return mstrcat(EXT_ROOT, fileName, 0, NULL);
}

char *getExtensionDirectory(const char *family){
    char *slash, *ret = mstrcat(FILES_ROOT, family, 1, &slash);
    slash[0] = '/';
    slash[1] = 0;
    return ret;
}

void CONSTRUCTOR _ext_init() {
    struct XoViEnvironment *environment = malloc(sizeof(struct XoViEnvironment));
    environment->getExtensionDirectory = getExtensionDirectory;

    // 0.2.0 API:
    environment->getExtensionCount = getExtensionCount;
    environment->getExtensionNames = getExtensionNames;
    environment->getExtensionFunctionCount = getExtensionFunctionCount;
    environment->getExtensionFunctionNames = getExtensionFunctionNames;

    environment->getMetadataEntriesCountForFunction = getMetadataEntriesCountForFunction;
    environment->getMetadataChainForFunction = getMetadataChainForFunction;
    environment->getMetadataEntryForFunction = getMetadataEntryForFunction;

    // Cast required due to differences between the public and private Iterator structure
    environment->createMetadataSearchingIterator = (void (*)(struct ExtensionMetadataIterator *, const char *)) createMetadataSearchingIterator;
    environment->nextFunctionMetadataEntry = (struct XoviMetadataEntry *(*)(struct ExtensionMetadataIterator *)) nextFunctionMetadataEntry;

    // At this point none of the functions could have been hooked.
    // It's safe to use stdlib.
    DIR *rootDirOfExtensions;
    struct dirent *entry;

    if((rootDirOfExtensions = opendir(EXT_ROOT)) != NULL){
        // Try to load every extension
        while((entry = readdir(rootDirOfExtensions)) != NULL) {
            if(entry->d_type == DT_REG){
                // LOG("Loading: %s\n", entry->d_name);
                char *fullName = findFullName(entry->d_name);
                loadExtensionPass1(fullName, findBaseName(entry->d_name));
                free(fullName);
            }
        }
        closedir(rootDirOfExtensions);
        loadAllExtensions(environment);

        #ifdef DEBUGFUNC
        // Only when we're in debug (non-shared) do we actually clean up in the env. Otherwise this object should stay resident for the whole life of the
        // attached process.
        testFunc();
        free(environment);
        #endif
    } else {
        printf("Cannot find extensions dir! Bailing!\n");
    }
}

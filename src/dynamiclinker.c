// Load all extensions the software depends on, but DO NOT LINK yet
#include "dynamiclinker.h"
#include "metadata.h"

struct LinkingPass1Result *XOVI_DL_EXTENSIONS = NULL;
static struct OverrideFunctionTrace *OVERRIDEN_FUNCTIONS = NULL;

int getTerminatedChainLength(void **data, void *terminator){
    ptrint_t *asInt = (ptrint_t *) data;
    int i = 0;
    while(*asInt != (ptrint_t) terminator) {
        i++;
        asInt++;
    }
    return i;
}

// baseName needs to be preallocated!
void loadExtensionPass1(char *extensionSOFile, char *baseName){
    LOG("[W]: Pass 1: Begin loading extension %s from file %s\n", baseName, extensionSOFile);
    void *extension = dlopen(extensionSOFile, RTLD_NOW);
    if(!extension) {
#ifdef LOAD_FAIL_ABORT
        LOG_F("[F]: Pass 1: Couldn't load extension:\n%s\n", dlerror());
        exit(1);
#else
        LOG("[I]: Pass 1: Failed to load extension:\n%s\n", dlerror());
        LOG("[I]: Pass 1: Skipping extension %s.\n", baseName);
        return;
#endif
    }
    char (*shouldLoad)() = dlsym(extension, "_xovi_shouldLoad");
    if(shouldLoad){
        if(!shouldLoad()){
            LOG("[I]: Pass 1: The extension refused being loaded. Skipping.");
            return;
        }
    }
    // Traverse the link tables, load all functions and dependencies into XOVI_DL_EXTENSIONS
    struct LinkingPass1Result *thisExtension = malloc(sizeof(struct LinkingPass1Result));
    thisExtension->soFileNameRootHash = hashString(baseName);
    thisExtension->functions = malloc(sizeof(void *));
    *thisExtension->functions = NULL;
    thisExtension->baseName = baseName;
    thisExtension->loaded = 0;
    thisExtension->handle = extension;

    char **LINKTABLENAMESptr = dlsym(extension, "LINKTABLENAMES");
    void **LINKTABLEVALUES = dlsym(extension, "LINKTABLEVALUES");
    unsigned int *EXTENSIONVERSION = dlsym(extension, "EXTENSIONVERSION");

    if(EXTENSIONVERSION == NULL) {
        LOG("[W]: Pass 1: Extension doesn't define its version! Defaulting to 0.1.0.\n");
        thisExtension->version.major = 0;
        thisExtension->version.minor = 1;
        thisExtension->version.patch = 0;
    } else {
        int version = *EXTENSIONVERSION;
        thisExtension->version.major = (version >> 16) & 0xFF;
        thisExtension->version.minor = (version >> 8) & 0xFF;
        thisExtension->version.patch = version & 0xFF;
        LOG(
            "[I]: Pass 1: Extension version %d.%d.%d\n",
            thisExtension->version.major,
            thisExtension->version.minor,
            thisExtension->version.patch
        );
    }

    thisExtension->metadataChainRoot = dlsym(extension, "METADATAVALUES");
    if(!thisExtension->metadataChainRoot) {
        LOG("[W]: Pass 1: Extension %s does not export any metadata!\n", baseName);
        thisExtension->metadataChainLength = -1;
    } else {
        struct XoviMetadataEntry ***entry = thisExtension->metadataChainRoot;
        thisExtension->metadataChainLength = getTerminatedChainLength((void *) entry, (void *) 1);
        thisExtension->rootMetadataChainLength = 0;
        if(thisExtension->metadataChainRoot[0]) {
            thisExtension->rootMetadataChainLength = getTerminatedChainLength((void *) thisExtension->metadataChainRoot[0], NULL);
        }
        LOG("[I]: Pass 1: Metadata chain length is %d\n", thisExtension->metadataChainLength);
    }

    if(LINKTABLENAMESptr && LINKTABLEVALUES){
        // Unprotect the memory to prepare it for modifying
        unsigned int elementsCount = *((unsigned int *) LINKTABLEVALUES);
        mprotect((void *) (((ptrint_t) LINKTABLEVALUES) & ~0xFFF), sizeof(ptrint_t) * (elementsCount + 1) + (((ptrint_t) LINKTABLEVALUES) & 0xFFF), PROT_READ | PROT_WRITE);

        // The first uint32_t of LINKTABLEVALUES is the length.
        LOG("[I]: Pass 1: Found link table at %p\n", LINKTABLENAMESptr);
        char *LINKTABLENAMES = *LINKTABLENAMESptr;

        // Iterate over the functions to work on and declare them in the RESULTS list

        int entryLength, entryNumber = 1; // We're starting with entry 1 since entry 0 is length.
        while(elementsCount && (entryLength = strlen(LINKTABLENAMES)) != 0) {
            char symbolType = LINKTABLENAMES[0];
            struct LinkingPass1SOFunction *definition = malloc(sizeof(struct LinkingPass1SOFunction));
            definition->functionNameHash = hashString(LINKTABLENAMES);
            definition->functionName = strdup(LINKTABLENAMES + 1);
            definition->metadataLength = 0;
            definition->metadataChain = NULL;
            if(entryNumber < thisExtension->metadataChainLength) {
                definition->metadataChain = thisExtension->metadataChainRoot[entryNumber];
                if(definition->metadataChain) {
                    definition->metadataLength = getTerminatedChainLength((void *) definition->metadataChain, NULL);
                } else {
                    definition->metadataLength = 0;
                }
            }

            struct LinkingPass1SOFunction *check;
            HASH_FIND_HT(*thisExtension->functions, &definition->functionNameHash, check);
            if(check != NULL){
                LOG_F("[F]: Pass 1: Extension %s works on the same function %s more than once!\n", baseName, LINKTABLENAMES + 1);
                exit(1);
            }

            if(symbolType == 'I') {
                // Import
                definition->type = LP1_F_TYPE_IMPORT;
                definition->address = &LINKTABLEVALUES[entryNumber]; // Pointer to this value - it's the address of the function according to the extension
                HASH_ADD_HT(*thisExtension->functions, functionNameHash, definition);
            } else if(symbolType == 'C') {
                // Import
                definition->type = LP1_F_TYPE_CONDITION;
                definition->address = NULL;
                HASH_ADD_HT(*thisExtension->functions, functionNameHash, definition);
            } else if (symbolType == 'E') {
                // Export
                definition->type = LP1_F_TYPE_EXPORT;
                definition->address = LINKTABLEVALUES[entryNumber]; // No pointer - we take the address from the table
                HASH_ADD_HT(*thisExtension->functions, functionNameHash, definition);
            } else if (symbolType == 'O') {
                // Override the symbol in the global scope
                definition->type = LP1_F_TYPE_OVERRIDE;
                definition->address = LINKTABLEVALUES[entryNumber]; // No pointer - we take the address from the table (like before)
                HASH_ADD_HT(*thisExtension->functions, functionNameHash, definition);
            } else {
                LOG("[E]: Pass 1: Illegal symbol type %c for function %s! Symbol skipped.\n", *LINKTABLENAMES, LINKTABLENAMES + 1);
                // Destroy the memory.
                free(definition->functionName);
                free(definition);
            }
            LINKTABLENAMES += entryLength + 1;
            entryNumber++;
            --elementsCount;
        }
    }
    thisExtension->constructor = dlsym(extension, "_xovi_construct");
    thisExtension->dependencyConstructor = dlsym(extension, "_xovi_depconstruct");
    if(!thisExtension->constructor) {
        LOG("[W]: Pass 1: Extension %s does not export a constructor!\n", baseName);
    }
    thisExtension->environment = dlsym(extension, "Environment");
    if(!thisExtension->constructor) {
        LOG("[W]: Pass 1: Extension %s does not expect to receive the Environment handle!\n", baseName);
    }

    struct LinkingPass1Result *check;
    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &thisExtension->soFileNameRootHash, check);
    if(check != NULL){
        LOG_F("[F]: Pass 1: Extension %s has been processed more than once!\n", baseName);
        exit(1);
    }

    HASH_ADD_HT(XOVI_DL_EXTENSIONS, soFileNameRootHash, thisExtension);
    LOG("[I]: Pass 1: Loaded extension %s from file %s\n", baseName, extensionSOFile);
}

static void *dlsymStandardOrder(const char *sym){
    void *address = dlsym(RTLD_DEFAULT, sym);
    if(address == NULL) {
        address = dlsym(RTLD_NEXT, sym);
    }
    return address;
}

static void *resolveImport(struct LinkingPass1Result *extension, char *importName, bool onlyCheck) {
    // An import can be defined in one of two ways:
    // 1. Standard library, but safe-to-use, never hooked version. (f.ex. $strlen - will be written as 'strlen' (stripped '$'))
    // 2. Different extension (f.ex. fileman$addFileHook ($ remains))
    // To differentiate between the variant, it's enough to look for a '$' sign.
    char *extensionFunctionName = strchr(importName, '$');
    LOG("[I]: Pass 2b: Importing function %s - ", importName);
    if(extensionFunctionName == NULL) {
        LOG("standard function - ");
        // It's a standard library function.
        // Has it ever been hooked?
        if(onlyCheck) {
            LOG("check only.\n");
            return dlsymStandardOrder(importName);
        }
        hash_t hash = hashString(importName);
        struct OverrideFunctionTrace *function;
        HASH_FIND_HT(OVERRIDEN_FUNCTIONS, &hash, function);
        if(function != NULL) {
            // Yes - here be dragons
            // Allocate the untrampoline function
            LOG("HOOKED - Allocating untrampoline\n");
            void *untrampolineBase = &extension->untrampolineFunctionCache[
                ARCHDEP_UNTRAMPOLINE_LENGTH *
                extension->populatedUntrampolineFunctions++
            ];
            generateUntrampoline(
                untrampolineBase,
                function->data,
                (extension->importsCount - extension->populatedUntrampolineFunctions + 1) * ARCHDEP_UNTRAMPOLINE_LENGTH
            );
            return untrampolineBase;
        } else {
            // No - it is not hooked.
            // Use the system linker
            LOG("untouched.\n");
            return dlsymStandardOrder(importName);
        }
    } else {
        LOG("Submodule\n");
        // No - this is a submodule function.
        hash_t extensionBaseNameHash = hashStringL(importName, extensionFunctionName - importName);
        extensionFunctionName++; // Skip the '$'.
        struct LinkingPass1Result *dependency;
        HASH_FIND_HT(XOVI_DL_EXTENSIONS, &extensionBaseNameHash, dependency);
        if(dependency == NULL) {
            if(onlyCheck){
                LOG("[I]: Pass 2b: Extension %s wanted to load function %s - couldn't find extension!\n", extension->baseName, importName);
            } else {
                LOG_F("[F]: Pass 2b: Extension %s wanted to load function %s - couldn't find extension!\n", extension->baseName, importName);
            }
            return NULL;
        }
        hash_t functionHash = hashStringS(extensionFunctionName, hashString("E"));
        struct LinkingPass1SOFunction *function;
        HASH_FIND_HT(*dependency->functions, &functionHash, function);
        if(function == NULL) {
            if(onlyCheck){
                LOG("[I]: Pass 2b: Extension %s wanted to load function %s - couldn't find function!\n", extension->baseName, importName);
            } else {
                LOG_F("[F]: Pass 2b: Extension %s wanted to load function %s - couldn't find function!\n", extension->baseName, importName);
            }
            return NULL;
        }
        LOG("[I]: Pass 2b: Extension %s loaded %s!\n", extension->baseName, importName);
        return function->address;
    }
}

static void defineOverride(char *extensionBaseName, char *symbolName, void *newAddress) {
    // Have we already hooked this function?
    hash_t hash = hashString(symbolName);
    struct OverrideFunctionTrace *function;
    HASH_FIND_HT(OVERRIDEN_FUNCTIONS, &hash, function);
    if(function != NULL) {
        // Bad
        LOG_F("[F]: Pass 2a: Function %s has been hooked more than once! (Hooking by %s. Already hooked by %s)\n", symbolName, extensionBaseName, function->ownerExtensionBaseName);
        exit(1);
    }
    // Good.
    function = malloc(sizeof(struct OverrideFunctionTrace));
    function->ownerExtensionBaseName = extensionBaseName;
    function->overridenFunctionNameHash = hash;
    LOG("[I]: Pass 2a: Hooking function %s on behalf of extension %s\n", symbolName, extensionBaseName);
    int argSize = -1;
    struct XoviMetadataEntry *argSizeEntry = getMetadataEntryForFunction(extensionBaseName, symbolName, LP1_F_TYPE_OVERRIDE, "$argsize");
    if(argSizeEntry != NULL) {
        if(argSizeEntry->type == METADATA_TYPE_INT) {
            argSize = argSizeEntry->value.i;
        } else {
            LOG("[W]: Pass 2a: While hooking function %s: Invalid type for $argsize metadata entry: %d\n", symbolName, extensionBaseName);
        }
    }
    function->data = pivotSymbol(symbolName, newAddress, argSize);
    if(function->data == NULL) {
        LOG_F("[F]: Pass 2a: Failed to hook function!");
        exit(1);
    }
    HASH_ADD_HT(OVERRIDEN_FUNCTIONS, overridenFunctionNameHash, function);
}

void requireExtension(hash_t hash, const char *nameFallback, unsigned char major, unsigned char minor, unsigned char patch) {
    struct LinkingPass1Result *soFile;
    const char *thisExtName = nameFallback ? nameFallback : "<Not provided>";

    LOG("[I]: Init: Initializing extension %s (%llx)...\n", thisExtName, hash);

    HASH_FIND_HT(XOVI_DL_EXTENSIONS, &hash, soFile);
    if(soFile == NULL) {
        // There is no such extension
        LOG_F("[F]: Init: Cannot load extension %s(%llx) - not found!\n", thisExtName, hash);
        exit(1);
    }
    if(!(major == 255 && minor == 255 && patch == 255)) {
        // Needs to check version
        if(
            (major != soFile->version.major) ||
            (major == soFile->version.major && minor > soFile->version.minor) ||
            (major == soFile->version.major && minor == soFile->version.minor && patch > soFile->version.patch)
        ) {
            LOG_F(
                "[F]: Init: Extension %s requires extension %s at version %d.%d.%d (or compatible). Version %d.%d.%d installed currently is not compatible!\n",
                thisExtName,
                soFile->baseName,
                major, minor, patch,
                soFile->version.major, soFile->version.minor, soFile->version.patch
            );
            exit(1);
        }
    }
    if(soFile->loaded){
        LOG("[I]: Init: Loading %s(%llx) - skipped. Already loaded.\n", thisExtName, hash);
        return;
    }

    if(soFile->dependencyConstructor) soFile->dependencyConstructor();
    if(soFile->constructor) soFile->constructor();
    soFile->loaded = 1;
}

void requireExtensionByName(const char *name, unsigned char major, unsigned char minor, unsigned char patch) {
    requireExtension(hashString((char *) name), NULL, major, minor, patch);
}

void unloadPass1Result(struct LinkingPass1Result *currentExtension) {
    HASH_DEL(XOVI_DL_EXTENSIONS, currentExtension);
    struct LinkingPass1SOFunction *func, *tmp;
    HASH_ITER(hh, *currentExtension->functions, func, tmp) {
        HASH_DEL(*currentExtension->functions, func);
        free(func->functionName);
        free(func);
    }
    dlclose(currentExtension->handle);
    free(currentExtension->baseName);
    free(currentExtension);
}

// PASS 2:
void loadAllExtensions(struct XoViEnvironment *env){
    struct LinkingPass1Result *currentExtension;
    LOG("[I]: Pass 2: Starting pass 2a (conditional loading)...\n");
    bool changesDone = false;
    do {
        LOG("[I]: Pass 2a: Iterating over modules...\n");
        changesDone = false;
        struct LinkingPass1Result *tmp;
        HASH_ITER(hh, XOVI_DL_EXTENSIONS, currentExtension, tmp) {
            struct LinkingPass1SOFunction *currentFunction;
            for(
                currentFunction = *currentExtension->functions;
                currentFunction != NULL;
                currentFunction = currentFunction->hh.next
            ) {
                if(currentFunction->type == LP1_F_TYPE_CONDITION){
                    if(resolveImport(currentExtension, currentFunction->functionName, true) == NULL) {
                        // If the function does not exist, delete unload this extension.
                        LOG("[I]: Pass 2a: Condition not met for %s. Unloading...\n", currentExtension->baseName);
                        unloadPass1Result(currentExtension);
                        // Continue iteration - unload all items that depended on this
                        changesDone = true;
                        break;
                    }
                }
            }
        }
    } while(changesDone);
    LOG("[I]: Pass 2: Starting pass 2b (override hooking)...\n");

    for(
        currentExtension = XOVI_DL_EXTENSIONS;
        currentExtension != NULL;
        currentExtension = currentExtension->hh.next
    ) {
        // Count the IMPORTs as well. We'll need their count later.
        currentExtension->importsCount = 0;
        if(currentExtension->environment) {
            *currentExtension->environment = env;
        }
        LOG("[I]: Pass 2b: Linking extension %s...\n", currentExtension->baseName);
        struct LinkingPass1SOFunction *currentFunction;
        for(
            currentFunction = *currentExtension->functions;
            currentFunction != NULL;
            currentFunction = currentFunction->hh.next
        ) {
            LOG("[I]: Pass 2b: Processing function %s... ", currentFunction->functionName);
            void *resolved;
            switch(currentFunction->type){
                case LP1_F_TYPE_EXPORT:
                    LOG("Export - Skipping\n");
                    break;
                case LP1_F_TYPE_IMPORT:
                    LOG("Import - Skipping.\n");
                    currentExtension->importsCount++;
                    break;
                case LP1_F_TYPE_OVERRIDE:
                    LOG("Override - defining.\n");
                    defineOverride(currentExtension->baseName, currentFunction->functionName, currentFunction->address);
                    break;
            }
        }
        // Allocate the space for all the untrampoline jumps
        currentExtension->untrampolineFunctionCache = mmap(NULL, currentExtension->importsCount * ARCHDEP_UNTRAMPOLINE_LENGTH, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
        currentExtension->populatedUntrampolineFunctions = 0;
    }
    LOG("[I]: Pass 2: Starting pass 2c (import / export linking)...\n");
    int count = 0;
    for(
        currentExtension = XOVI_DL_EXTENSIONS;
        currentExtension != NULL;
        currentExtension = currentExtension->hh.next
    ) {
        ++count;
        LOG("[I]: Pass 2c: Linking extension %s...\n", currentExtension->baseName);
        struct LinkingPass1SOFunction *currentFunction;
        for(
            currentFunction = *currentExtension->functions;
            currentFunction != NULL;
            currentFunction = currentFunction->hh.next
        ) {
            LOG("[I]: Pass 2c: Processing function %s... ", currentFunction->functionName);
            void *resolved;
            switch(currentFunction->type){
                case LP1_F_TYPE_EXPORT:
                    LOG("Export - Skipping\n");
                    break;
                case LP1_F_TYPE_IMPORT:
                    LOG("Import - Trying to resolve import.\n");
                    resolved = resolveImport(currentExtension, currentFunction->functionName, false);
                    if(!resolved) {
                        LOG_F("[F]: Pass 2c: Failed to resolve import %s! Halting.\n", currentFunction->functionName);
                        exit(-1);
                    }
                    *((void **) currentFunction->address) = resolved;
                    break;
                case LP1_F_TYPE_OVERRIDE:
                    LOG("Override - Skipping\n");
                    break;
            }
        }
        mprotect(currentExtension->untrampolineFunctionCache, currentExtension->importsCount * ARCHDEP_UNTRAMPOLINE_LENGTH, PROT_EXEC | PROT_READ);
    }
    LOG("[I]: Pass 2: Pass 2 complete. Linking is done.\n");
    LOG("[I]: Starting initialization...\n");
    env->requireExtension = requireExtensionByName;

    for(
        currentExtension = XOVI_DL_EXTENSIONS;
        currentExtension != NULL;
        currentExtension = currentExtension->hh.next
    ) {
        requireExtension(currentExtension->soFileNameRootHash, currentExtension->baseName, 255, 255, 255);
    }
    env->requireExtension = NULL;
    LOG("[I]: Init complete. There are %d extensions loaded.\n", count);
}

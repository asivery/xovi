#include "header.h"
#include "xovi.h"

bool isDuck(char *string){ // Exports do not need to be marked in any way
    return strcmp(string, "duck") == 0;
}

char *override$strdup(char *string) { // Override functions have to be preffixed with 'override$'
    if(isDuck(string)){
        string = "pigeon";
    }
    return (char *) $strdup(string); // Imports are preffixed with a '$' character, if the function comes from the global scope, and use the format `extension$export`, if they come from other extension.;
}


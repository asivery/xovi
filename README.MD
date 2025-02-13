# XOVI - The universal LD_PRELOAD extension framework

## What does it do?

XOVI lets you write extensions for applications which do not support it natively. It lets you hook arbitrary functions from the global symbol scope through the use of `dlsym`.
After a global function gets redirected to your function, you have full control over its parameters, return value and behavior. You can invoke or suppress the invocation of the original function - it's your call!
Note that XOVI does not allow multiple extensions to hook the same function.


Extensions can also import and export symbols from other extensions - XOVI doubles as a dynamic linker. (If you're importing a symbol from the global scope, XOVI will make sure to give you the unhooked version of the function, even if other extensions hook it.)

Right now XOVI expects to see the following directory structure:

```
/home/root/xovi
|
|-> extensions.d
|   |
|   \-> all the extension files (ending in .so)
|
\-> exthome
    |
    \-> one directory for every extension in extensions.d
        (e.g. fileman.so (in extensions.d) uses the fileman directory here)
```


## How to build it?

Right now, XOVI supports the aarch64 and arm32 architectures. Arm32, however, is still being tested.

To build, run:
```sh
mkdir build
cd build
cmake ..
make -j$(nproc)
```

For your convenience, prebuilts for the reMarkable Paper Pro are provided in [GitHub Releases](https://github.com/asivery/xovi/releases).

## How does it work?

XOVI iterates over the extensions 4 times, in order to:

- Find all extensions' exports, imports and overrides (and store them internally) [Pass 1]
- Verify loading conditions [Pass 2a]
- Define all overrides (hooks) [Pass 2b]
- Build the links between imports and exports [Pass 2c]
- Invoke all constructors and resolve the dependency map [Init]

## Hooks

In order to hook functions, XOVI inserts trampolines at the start of each hooked function.
For AARCH64, the hook consists of the following assembly code:

```aarch64
movx x8, <bits 0-15>
movk x8, <bits 16-31>
movk x8, <bits 32-47>
movk x8, <bits 48-63>
br x8
```
This code first copies the address of the hook destination function (extension function) into register x8, then jumps to it.

If an extension wants to jump to the unhooked version of a function, XOVI first replaces the trampoline with the function's original code. After that, it replaces the code that follows that with yet another (internal) trampoline and jumps to the function. The second trampoline's job is to remove itself by restoring the original code of the function, and replace the start of the function with the original trampoline, so as to prevent the function from ever being invoked untrampolined. (I am aware of there being a small race condition here, but I haven't managed to come up with a way to prevent it. If you know how to fix this, please open an issue)
For more information, see [untrampoline.S](src/trampolines/aarch64/untrampoline.S), [aarch64.c](src/trampolines/aarch64/aarch64.c).

## Writing extensions

To write an extension, you first need to start with writing a description file for `xovigen`. This file describes all the imports, exports and overrides your extension needs.

Example:

`example.xovi`:

```
version 0.1.0       ; Self explanatory
import strdup       ; The file requires an unmodified version of 'strdup'
export isDuck       ; The file exports a function called 'isDuck'
override strdup     ; The extension will override (hook) strdup
```

This code could correspond to the following extension project:

`main.c`:

```c
#include <stdbool.h>
#include <string.h>
#include "xovi.h"

bool isDuck(char *string){ // Exports do not need to be marked in any way
    return strcmp(string, "duck") == 0;
}

char *override$strdup(char *string) { // Override functions have to be prefixed with 'override$'
    if(isDuck(string)){
        string = "pigeon";
    }
    return $strdup(string); // Imports are prefixed with a '$' character, if the function comes from the global scope, and use the format `extension$export`, if they come from another extension.
}

```

To build the extension, run xovigen to generate xovi.c and xovi.h files:

`python3 util/xovigen.py -o xovi.c -H xovi.h example.xovi`

Note: The xovi.h file is not strictly necessary.
If your application does not import any symbols, you can skip generating it by omitting the `-H xovi.h` parameter.

Then compile as usual:

`$CC -shared -fPIC example.c xovi.c -o xovi-example.so`

Other statements available in `xovigen` files are:

```
resource test:a.txt ; Load the contents of 'a.txt' as the variable r$test

condition globalf   ; Only load this extension if there exists a symbol 'globalf'
import? ext$globalf ; Equal to condition ext$globalf..., import ext$globalf...
```

## Current issues

### aarch64

Right now, apart from the race condition mentioned in [Hooks](#hooks), xovi should be somewhat stable.

### arm32

Since the ARM32 code is still quite new, and hasn't been tested as thoroughly as aarch64, it might be less stable.

As of right now, only methods which take **up to 4 arguments** can be hooked in ARM32. This is due to the calling convention of 32-bit ARM, according to which in order to transfer more than 4 arguments, they should be passed onto the stack. Xovi's current implementation uses the stack internally to process the trampoline code, therefore arguments 5 and above will become corrupted while executing the original code of any hooked method. This is a temporary issue, and at some point will be fixed.

## Happy hacking!
#

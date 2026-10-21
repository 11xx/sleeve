/* The whole library and nothing else, for `make freestanding`: built for wasm32 with no C
   library and every function exported, it links only if nothing it needs is missing. */
#define SLEEVE_NO_STDIO
#define SLEEVE_IMPLEMENTATION
#include "sleeve.h"

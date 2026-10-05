/* Force malloc() to zero-initialize, matching the STM32 firmware convention
 * (components/furi/core/memmgr.h does the same for firmware code).
 *
 * The original Flipper firmware runs on STM32 where the heap starts zeroed, so
 * many upstream apps implicitly rely on calloc-like behavior (struct pointer
 * fields left unset are NULL, then guarded by `if(p != NULL) free(p)`). The
 * ESP32 heap is NOT zeroed, so those reads return garbage -> free(garbage) ->
 * heap corruption. Force-included into every FAP source so ported apps behave
 * like they do on STM32. C only (C++ TUs use new/STL). */
#pragma once
#include <stdlib.h>
/* Also pre-include malloc.h: it redeclares malloc/calloc and would otherwise
 * expand through the macro below in TUs that include it (e.g. jblanked
 * apps). Included here, before the macro is defined. May not exist on all
 * libcs, hence __has_include. */
#if __has_include(<malloc.h>)
#include <malloc.h>
#endif
#ifndef __cplusplus
#define malloc(size) calloc(1, (size))
#endif

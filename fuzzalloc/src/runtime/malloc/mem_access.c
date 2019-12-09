//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <stdio.h>

#include "debug.h"
#include "malloc_internal.h"

#if defined(AFL_INSTRUMENT)
#include "types.h" // from afl
#endif

#if defined(AFL_INSTRUMENT)
extern u8 *__afl_area_ptr;
#endif

void __mem_access(tag_t def_site, int64_t offset) {
  DEBUG_MSG("accessing def site %#x from %p (at offset %ld)\n", def_site,
            __builtin_return_address(0), offset);

#if defined(AFL_INSTRUMENT)
  // Update the AFL bitmap based on the previous location (i.e., the mspace
  // identifier) and the current location (i.e., the address of the memory
  // access)
  u16 use_site = (u16)(__builtin_return_address(0) + offset);
  u16 map_idx =
      ((3 * (def_site - FUZZALLOC_DEFAULT_TAG)) ^ use_site) - use_site;

  DEBUG_MSG("updating AFL bitmap at index %u\n", map_idx);

  __afl_area_ptr[map_idx]++;
#endif
}

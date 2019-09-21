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

void __mem_access(tag_t mspace_id) {
  DEBUG_MSG("accessing mspace %#x from %p\n", mspace_id,
            __builtin_return_address(0));

#if defined(AFL_INSTRUMENT)
  // Update the AFL bitmap based on the previous location (i.e., the mspace
  // identifier) and the current location (i.e., the address of the memory
  // access)
  u32 map_idx = mspace_id ^ (u16)__builtin_return_address(0);

  DEBUG_MSG("updating AFL bitmap at index %u\n", map_idx);

  __afl_area_ptr[map_idx]++;
#endif
}

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

// Defined in malloc.c
extern tag_t __mspace_to_def_site_map[TAG_MAX + 1];

#if defined(AFL_INSTRUMENT)
extern u8 *__afl_area_ptr;
#endif

void __mem_access(tag_t mspace_id) {
  tag_t def_site = __mspace_to_def_site_map[mspace_id];
  (void)def_site;

  DEBUG_MSG("accessing mspace 0x%x (def site 0x%x) from %p\n", mspace_id,
            def_site, __builtin_return_address(0));

#if defined(AFL_INSTRUMENT)
  // If the default tag is used, then we have no idea where the def site is.
  // Don't bother updating anything in the AFL bitmap, because we cannot
  // accurately track it anyway
  if (def_site == DEFAULT_TAG) {
    return;
  }

  // Update the AFL bitmap based on the previous location (i.e., the def site)
  // and the current location (i.e., the address of the memory access)
  u32 use_site = (u16)__builtin_return_address(0);
  u32 map_idx = def_site ^ use_site;

  DEBUG_MSG("updating AFL bitmap at %u\n", map_idx);

  __afl_area_ptr[map_idx]++;
#endif
}

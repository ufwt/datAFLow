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
extern tag_t __pool_to_alloc_site_map[TAG_MAX + 1];

#if defined(AFL_INSTRUMENT)
extern u8 *__afl_area_ptr;
#endif

void __ptr_deref(tag_t pool_tag) {
  tag_t alloc_site_tag = __pool_to_alloc_site_map[pool_tag];
  (void)alloc_site_tag;

  DEBUG_MSG("accessing pool 0x%x (allocation site 0x%x) from %p\n", pool_tag,
            alloc_site_tag, __builtin_return_address(0));

#if defined(AFL_INSTRUMENT)
  // If the default tag is used, then we have no idea where the allocation site
  // is. Don't bother updating anything in the AFL bitmap, because we cannot
  // accurately track it anyway
  if (alloc_site_tag == DEFAULT_TAG) {
    return;
  }

  // Update the AFL bitmap based on the previous location (i.e., the allocation
  // call site) and the current location (i.e., the address of the memory
  // access)
  u32 prev_loc = alloc_site_tag;
  u32 cur_loc = (u16)__builtin_return_address(0);
  u32 map_idx = prev_loc ^ cur_loc;

  DEBUG_MSG("updating AFL bitmap at %u\n", map_idx);

  __afl_area_ptr[map_idx]++;
#endif
}

//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <stdio.h>

#include "malloc_internal.h"

/// Maps allocation pool tags (created during malloc/calloc/reallocs) to
/// allocation call site tags (inserted during compilation)
tag_t __pool_to_alloc_site_map[TAG_MAX + 1];

void __ptr_deref(tag_t pool_tag) {
  tag_t alloc_site_tag = __pool_to_alloc_site_map[pool_tag];

  DEBUG_MSG("accessing pool 0x%x\n", pool_tag);
  DEBUG_MSG("  allocation site -> 0x%x\n", alloc_site_tag);
}

//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <stdio.h>

#include "malloc_internal.h"

void __ptr_deref(tag_t pool_id) { printf("accessing pool 0x%x\n", pool_id); }

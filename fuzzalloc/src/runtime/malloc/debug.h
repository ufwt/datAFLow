//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#ifndef _MALLOC_DEBUG_H_
#define _MALLOC_DEBUG_H_

#if !defined(NDEBUG)
#include <assert.h>
#include <stdio.h>

#define DEBUG_MSG(format, args...)                                             \
  fprintf(stderr, "[%s:%d] %s: " format, __FILE__, __LINE__, __func__, ##args)
#else
#define DEBUG_MSG(format, ...)
#define assert(x)
#endif

#endif // _MALLOC_DEBUG_H_

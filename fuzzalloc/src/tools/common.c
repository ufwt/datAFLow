//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <string.h>

#include "common.h"

u8 check_if_assembler(u32 argc, const char **argv) {
  while (--argc) {
    u8 *cur = *(++argv);

    const u8 *ext = strrchr(cur, '.');
    if (ext && !strcmp(ext + 1, "s")) {
      return 1;
    }
  }

  return 0;
}

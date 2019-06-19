// AFL include files
#include "alloc-inl.h"
#include "config.h"
#include "debug.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static u8 **cc_params;     /* Parameters passed to the opt */
static u32 cc_par_cnt = 1; /* Param count, including argv0 */

static void edit_params(u32 argc, char **argv) {
  u8 *name;

  cc_params = ck_alloc((argc + 512) * sizeof(u8 *));

  name = strrchr(argv[0], '/');
  if (!name) {
    name = argv[0];
  } else {
    name++;
  }

  if (!strcmp(name, "dataflow-collect-tags++")) {
    u8 *alt_cxx = getenv("AFL_CXX");
    cc_params[0] = alt_cxx ? alt_cxx : (u8 *)"clang++";
  } else {
    u8 *alt_cc = getenv("AFL_CC");
    cc_params[0] = alt_cc ? alt_cc : (u8 *)"clang";
  }

  /* Collect values to tag */

  cc_params[cc_par_cnt++] =
      "-fplugin=" FUZZALLOC_LLVM_DIR
      "/Analysis/CollectTags/fuzzalloc-collect-tags.so";

  char *fuzzalloc_tag_log = getenv("FUZZALLOC_TAG_LOG");
  if (fuzzalloc_tag_log) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] =
        alloc_printf("-fuzzalloc-tag-log=%s", fuzzalloc_tag_log);
  }

  char *fuzzalloc_whitelist = getenv("FUZZALLOC_WHITELIST");
  if (fuzzalloc_whitelist) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] =
        alloc_printf("-fuzzalloc-whitelist=%s", fuzzalloc_whitelist);
  }

  if (getenv("FUZZALLOC_DEBUG")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-debug";
  }

  if (getenv("FUZZALLOC_STATS")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-stats";
  }

  cc_params[cc_par_cnt++] = "-Qunused-arguments";

  while (--argc) {
    u8 *cur = *(++argv);

    cc_params[cc_par_cnt++] = cur;
  }

  if (!getenv("AFL_DONT_OPTIMIZE")) {
    cc_params[cc_par_cnt++] = "-g";
    cc_params[cc_par_cnt++] = "-O3";
    cc_params[cc_par_cnt++] = "-funroll-loops";
  }

  cc_params[cc_par_cnt] = NULL;
}

int main(int argc, char **argv) {
  if (isatty(2) && !getenv("AFL_QUIET")) {
    SAYF(cCYA "dataflow-collect-tags " cBRI VERSION cRST
              " by <adrian.herrera@anu.edu.au>\n");
  }

  if (argc < 2) {
    SAYF("\n"
         "This is a helper application for working out which values (i.e., "
         "function, global variables/aliases, struct elements, etc.) are "
         "required to be tagged by dataflow-clang-fast. A typical usage would "
         "be:\n\n"

         "  dataflow-collect-tags /path/to/bc/file\n\n");

    exit(1);
  }

  edit_params(argc, argv);

  execvp(cc_params[0], (char **)cc_params);

  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;
}

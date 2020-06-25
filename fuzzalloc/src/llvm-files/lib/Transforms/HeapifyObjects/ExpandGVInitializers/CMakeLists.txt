add_llvm_library(fuzzalloc-expand-gv-initializers
                 MODULE ${SOURCE} ExpandGVInitializers.cpp
                 PLUGIN_TOOL opt)

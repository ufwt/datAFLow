add_llvm_library(fuzzalloc-tag-dyn-allocs
                 MODULE TagDynamicAllocs.cpp
                 LINK_LIBS fuzzalloc-utils
                 PLUGIN_TOOL opt)

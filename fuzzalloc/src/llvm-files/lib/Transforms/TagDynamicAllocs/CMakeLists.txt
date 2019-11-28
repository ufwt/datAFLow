add_llvm_loadable_module(fuzzalloc-tag-dyn-allocs
                         TagDynamicAllocs.cpp
                         LINK_LIBS fuzzalloc-utils
                         PLUGIN_TOOL opt)

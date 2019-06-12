add_llvm_loadable_module(fuzzalloc-tag-dyn-allocs ${SOURCE}
                                                  TagDynamicAllocs.cpp
                         PLUGIN_TOOL opt)

add_llvm_loadable_module(fuzzalloc-prom-arrays ${SOURCE}
                                               PromoteStaticArrays.cpp
                         PLUGIN_TOOL opt)

add_llvm_loadable_module(fuzzalloc-prom-arrays ../PromoteCommon.cpp
                                               PromoteStaticArrays.cpp
                         PLUGIN_TOOL opt)

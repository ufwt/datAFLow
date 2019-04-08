add_llvm_loadable_module(fuzzalloc-prom-structs ../PromoteCommon.cpp
                                                PromoteStaticStructs.cpp
                         PLUGIN_TOOL opt)

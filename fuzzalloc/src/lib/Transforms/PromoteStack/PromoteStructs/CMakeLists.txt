add_llvm_loadable_module(fuzzalloc-prom-structs ${SOURCE}
                                                PromoteStaticStructs.cpp
                         PLUGIN_TOOL opt)

add_llvm_loadable_module(fuzzalloc-llvm TagDynamicAllocs.cpp
                                        PromoteCommon.cpp
                                        PromoteStaticArrays.cpp
                                        PromoteStaticStructs.cpp
                                        InstrumentDereferences.cpp
                         PLUGIN_TOOL opt)

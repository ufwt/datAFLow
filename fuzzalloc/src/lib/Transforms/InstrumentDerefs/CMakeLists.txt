add_llvm_loadable_module(fuzzalloc-inst-derefs ${SOURCE}
                                               InstrumentDereferences.cpp
                         PLUGIN_TOOL opt)

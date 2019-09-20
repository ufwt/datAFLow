add_llvm_loadable_module(fuzzalloc-inst-mem-accesses
                         ${SOURCE} InstrumentMemAccesses.cpp
                         PLUGIN_TOOL opt)

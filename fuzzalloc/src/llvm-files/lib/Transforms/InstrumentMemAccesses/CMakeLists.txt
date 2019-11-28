add_llvm_loadable_module(fuzzalloc-inst-mem-accesses
                         InstrumentMemAccesses.cpp
                         LINK_LIBS fuzzalloc-utils
                         PLUGIN_TOOL opt)

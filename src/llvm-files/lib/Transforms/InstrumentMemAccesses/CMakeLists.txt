add_llvm_library(fuzzalloc-inst-mem-accesses
                 MODULE InstrumentMemAccesses.cpp
                 LINK_LIBS fuzzalloc-utils
                 PLUGIN_TOOL opt)

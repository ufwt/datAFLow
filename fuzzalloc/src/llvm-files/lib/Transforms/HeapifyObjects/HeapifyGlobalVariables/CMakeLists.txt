add_llvm_library(fuzzalloc-heapify-global-vars
                 MODULE ${SOURCE} HeapifyGlobalVariables.cpp
                 PLUGIN_TOOL opt)

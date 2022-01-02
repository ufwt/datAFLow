add_llvm_library(fuzzalloc-heapify-allocas
                 MODULE ${SOURCE} HeapifyAllocas.cpp
                 PLUGIN_TOOL opt)

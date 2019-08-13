add_llvm_loadable_module(fuzzalloc-heapify-allocas
                         ${SOURCE} HeapifyAllocas.cpp
                         PLUGIN_TOOL opt)

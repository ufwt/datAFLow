add_llvm_loadable_module(fuzzalloc-heapify-global-vars
                         ${SOURCE} HeapifyGlobalVariables.cpp
                         PLUGIN_TOOL opt)

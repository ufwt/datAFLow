add_llvm_loadable_module(fuzzalloc-count-objects
                         ${SOURCE} CountObjects.cpp
                         PLUGIN_TOOL opt)

add_llvm_loadable_module(fuzzalloc-collect-stats
                         ${SOURCE} CollectStats.cpp
                         PLUGIN_TOOL opt)

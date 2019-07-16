add_llvm_loadable_module(fuzzalloc-prom-allocas
                         ${SOURCE} PromoteAllocas.cpp
                         PLUGIN_TOOL opt)

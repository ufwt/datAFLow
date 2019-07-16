add_llvm_loadable_module(fuzzalloc-prom-global-vars
                         ${SOURCE} PromoteGlobalVariables.cpp
                         PLUGIN_TOOL opt)

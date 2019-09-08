add_llvm_loadable_module(fuzzalloc-expand-gv-initializers
                         ${SOURCE} ExpandGVInitializers.cpp
                         PLUGIN_TOOL opt)

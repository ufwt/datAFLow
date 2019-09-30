add_llvm_loadable_module(fuzzalloc-collect-tag-sites
                         ${SOURCE} CollectTagSites.cpp
                         PLUGIN_TOOL opt)
